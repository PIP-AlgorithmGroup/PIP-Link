#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <chrono>

#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/simple-watch.h>

namespace remote_link {

class MdnsService {
public:
    struct Config {
        std::string service_name;
        uint16_t    control_port;
        uint16_t    video_port;
    };

    explicit MdnsService(const Config& cfg);
    ~MdnsService();

    bool start(std::chrono::seconds timeout = std::chrono::seconds(5));
    void stop();

private:
    void avahi_thread_loop();

    static void client_callback(AvahiClient* c, AvahiClientState state, void* userdata);
    static void entry_group_callback(AvahiEntryGroup* g, AvahiEntryGroupState state, void* userdata);
    void create_services(AvahiClient* c);

    Config             cfg_;
    AvahiClient*       avahi_client_{nullptr};
    AvahiEntryGroup*   avahi_group_{nullptr};
    AvahiSimplePoll*   avahi_poll_{nullptr};
    std::thread        avahi_thread_;
    std::atomic<bool>  running_{false};
    std::atomic<bool>  registered_{false};
};

}  // namespace remote_link
