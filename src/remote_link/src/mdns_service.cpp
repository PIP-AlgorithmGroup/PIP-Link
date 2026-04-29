#include "remote_link/mdns_service.hpp"

#include <avahi-common/malloc.h>
#include <avahi-common/error.h>
#include <avahi-common/alternative.h>

#include <cstdio>
#include <cstring>

namespace remote_link {

MdnsService::MdnsService(const Config& cfg) : cfg_(cfg) {}

MdnsService::~MdnsService() { stop(); }

void MdnsService::create_services(AvahiClient* c) {
    if (!avahi_group_) {
        avahi_group_ = avahi_entry_group_new(c, entry_group_callback, this);
        if (!avahi_group_) return;
    }

    if (avahi_entry_group_is_empty(avahi_group_)) {
        char video_port_str[16];
        char ctrl_port_str[16];
        std::snprintf(video_port_str, sizeof(video_port_str), "%u", cfg_.video_port);
        std::snprintf(ctrl_port_str,  sizeof(ctrl_port_str),  "%u", cfg_.control_port);

        int ret = avahi_entry_group_add_service(
            avahi_group_,
            AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, static_cast<AvahiPublishFlags>(0),
            cfg_.service_name.c_str(),
            "_pip-link._udp",
            nullptr, nullptr,
            cfg_.control_port,
            ("video_port=" + std::string(video_port_str)).c_str(),
            ("control_port=" + std::string(ctrl_port_str)).c_str(),
            "version=1.0",
            "device_type=air_unit",
            nullptr);

        if (ret < 0) {
            fprintf(stderr, "[MdnsService] avahi_entry_group_add_service failed: %s\n",
                    avahi_strerror(ret));
            return;
        }
        avahi_entry_group_commit(avahi_group_);
    }
}

void MdnsService::entry_group_callback(AvahiEntryGroup* /*g*/, AvahiEntryGroupState state, void* userdata) {
    auto* self = static_cast<MdnsService*>(userdata);
    if (state == AVAHI_ENTRY_GROUP_ESTABLISHED) {
        self->registered_ = true;
    } else if (state == AVAHI_ENTRY_GROUP_COLLISION) {
        char* new_name = avahi_alternative_service_name(self->cfg_.service_name.c_str());
        self->cfg_.service_name = new_name;
        avahi_free(new_name);
        // avahi_client_ is valid for the lifetime of the poll loop
        if (self->avahi_client_)
            self->create_services(self->avahi_client_);
    } else if (state == AVAHI_ENTRY_GROUP_FAILURE) {
        avahi_simple_poll_quit(self->avahi_poll_);
    }
}

void MdnsService::client_callback(AvahiClient* c, AvahiClientState state, void* userdata) {
    auto* self = static_cast<MdnsService*>(userdata);
    if (state == AVAHI_CLIENT_S_RUNNING) {
        self->create_services(c);
    } else if (state == AVAHI_CLIENT_FAILURE) {
        avahi_simple_poll_quit(self->avahi_poll_);
    } else if (state == AVAHI_CLIENT_S_COLLISION ||
               state == AVAHI_CLIENT_S_REGISTERING) {
        if (self->avahi_group_)
            avahi_entry_group_reset(self->avahi_group_);
        self->registered_ = false;
    }
}

void MdnsService::avahi_thread_loop() {
    avahi_poll_ = avahi_simple_poll_new();
    if (!avahi_poll_) return;

    int error;
    avahi_client_ = avahi_client_new(
        avahi_simple_poll_get(avahi_poll_),
        AVAHI_CLIENT_NO_FAIL,
        client_callback,
        this,
        &error);

    if (!avahi_client_) {
        fprintf(stderr, "[MdnsService] avahi_client_new failed: %s\n",
                avahi_strerror(error));
        avahi_simple_poll_free(avahi_poll_);
        avahi_poll_ = nullptr;
        return;
    }

    avahi_simple_poll_loop(avahi_poll_);

    if (avahi_client_) avahi_client_free(avahi_client_);
    avahi_client_ = nullptr;
    avahi_simple_poll_free(avahi_poll_);
    avahi_poll_ = nullptr;
}

bool MdnsService::start(std::chrono::seconds timeout) {
    running_ = true;
    avahi_thread_ = std::thread(&MdnsService::avahi_thread_loop, this);

    // Wait for registration
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!registered_ && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return registered_.load();
}

void MdnsService::stop() {
    if (!running_.exchange(false)) return;
    if (avahi_poll_) avahi_simple_poll_quit(avahi_poll_);
    if (avahi_thread_.joinable()) avahi_thread_.join();
}

}  // namespace remote_link
