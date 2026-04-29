#include "remote_link/control_receiver.hpp"
#include "remote_link/protocol_codec.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <chrono>
#include <cstdio>

namespace remote_link {

static double now_sec() {
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

ControlReceiver::ControlReceiver(uint16_t port) : port_(port) {}

ControlReceiver::~ControlReceiver() { stop(); }

void ControlReceiver::set_command_callback(CommandCallback cb) { cmd_cb_ = std::move(cb); }
void ControlReceiver::set_param_update_callback(ParamUpdateCallback cb) { param_cb_ = std::move(cb); }

void ControlReceiver::start() {
    control_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (control_fd_ < 0) { perror("control socket"); return; }

    int opt = 1;
    setsockopt(control_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct timeval tv{1, 0};
    setsockopt(control_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port_);

    if (bind(control_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("control bind"); close(control_fd_); control_fd_ = -1; return;
    }

    running_ = true;
    recv_thread_ = std::thread(&ControlReceiver::recv_loop, this);
}

void ControlReceiver::stop() {
    if (!running_.exchange(false)) return;
    if (control_fd_ >= 0) { shutdown(control_fd_, SHUT_RDWR); }
    if (recv_thread_.joinable()) recv_thread_.join();
    if (control_fd_ >= 0) { close(control_fd_); control_fd_ = -1; }
}

void ControlReceiver::recv_loop() {
    uint8_t buf[4096];
    while (running_) {
        struct sockaddr_in from{};
        socklen_t from_len = sizeof(from);
        ssize_t n = recvfrom(control_fd_, buf, sizeof(buf), 0,
                             reinterpret_cast<sockaddr*>(&from), &from_len);
        if (n <= 0) continue;

        if (static_cast<size_t>(n) < ProtocolCodec::HEADER_SIZE + 4) continue;

        int msg_type = ProtocolCodec::parse_type(buf, static_cast<size_t>(n));
        if (msg_type < 0) continue;

        // Update last-seen client IP
        {
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
            std::lock_guard<std::mutex> lk(client_ip_mutex_);
            client_ip_ = ip;
        }
        last_client_time_.store(now_sec());

        if (verbose_) {
            fprintf(stderr, "[CtrlRX] PKT len=%zd type=0x%02x\n", n, (unsigned)msg_type);
        }

        switch (msg_type) {
            case 0x01: handle_control_command(buf, static_cast<size_t>(n), from); break;
            case 0x04: handle_heartbeat(buf, static_cast<size_t>(n), from);       break;
            case 0x02: handle_param_update(buf, static_cast<size_t>(n), from);    break;
            case 0x03: handle_param_query(buf, static_cast<size_t>(n), from);     break;
        }
    }
}

void ControlReceiver::send_ack(int fd, const struct sockaddr_in& to,
                                uint32_t seq, double t2, double t3)
{
    uint8_t buf[32];
    size_t n = ProtocolCodec::build_ack(buf, sizeof(buf), seq, t2, t3);
    sendto(fd, buf, n, 0,
           reinterpret_cast<const sockaddr*>(&to), sizeof(to));
}

void ControlReceiver::handle_control_command(
    const uint8_t* data, size_t len, const struct sockaddr_in& from)
{
    uint32_t seq;
    double   t1;
    uint8_t  kb[10];
    int16_t  mouse_dx, mouse_dy;
    uint8_t  mouse_buttons;
    int8_t   scroll_delta;

    if (!ProtocolCodec::parse_control_command(
            data, len, seq, t1, kb, mouse_dx, mouse_dy, mouse_buttons, scroll_delta))
        return;

    double t2 = now_sec();

    if (cmd_cb_) {
        std::string ip;
        { std::lock_guard<std::mutex> lk(client_ip_mutex_); ip = client_ip_; }
        cmd_cb_(ip, seq, t1, kb, mouse_dx, mouse_dy, mouse_buttons, scroll_delta);
    }

    double t3 = now_sec();
    send_ack(control_fd_, from, seq, t2, t3);
}

void ControlReceiver::handle_heartbeat(
    const uint8_t* data, size_t len, const struct sockaddr_in& from)
{
    uint32_t seq;
    if (!ProtocolCodec::parse_heartbeat(data, len, seq)) return;
    double t2 = now_sec();
    double t3 = now_sec();
    send_ack(control_fd_, from, seq, t2, t3);
}

void ControlReceiver::handle_param_update(
    const uint8_t* data, size_t len, const struct sockaddr_in& from)
{
    uint32_t    seq;
    std::string json;
    if (!ProtocolCodec::parse_param_update(data, len, seq, json)) {
        // 打印原始字节帮助诊断
        fprintf(stderr, "[CtrlRX] PARAM_UPDATE parse FAILED len=%zu magic=%04X type=%02X\n",
                len,
                static_cast<unsigned>(data[0]) | (static_cast<unsigned>(data[1]) << 8),
                static_cast<unsigned>(data[3]));
        // 打印 bytes 17..len-4 作为 JSON 候选内容
        if (len > 21) {
            size_t json_len = len - 4 - 17;
            fprintf(stderr, "[CtrlRX] raw json bytes (%zu): %.*s\n",
                    json_len, static_cast<int>(json_len),
                    reinterpret_cast<const char*>(data + 17));
        }
        return;
    }

    fprintf(stderr, "[CtrlRX] PARAM_UPDATE OK seq=%u json=%s\n", seq, json.c_str());
    double t2 = now_sec();
    if (param_cb_) param_cb_(seq, json);
    double t3 = now_sec();
    send_ack(control_fd_, from, seq, t2, t3);
}

void ControlReceiver::handle_param_query(
    const uint8_t* data, size_t len, const struct sockaddr_in& from)
{
    // Respond with empty ACK; full PARAM_QUERY response handled by RemoteLinkNode via param_cb_
    if (len < ProtocolCodec::HEADER_SIZE + 4) return;
    uint32_t seq = 0;
    std::memcpy(&seq, data + 5, 4);  // Seq field
    double t2 = now_sec();
    double t3 = now_sec();
    send_ack(control_fd_, from, seq, t2, t3);
}

}  // namespace remote_link
