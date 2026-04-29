#pragma once

#include <cstdint>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <netinet/in.h>

namespace remote_link {

class ControlReceiver {
public:
    using CommandCallback = std::function<void(
        const std::string& client_ip,
        uint32_t seq, double t1,
        const uint8_t kb[10],
        int16_t mouse_dx, int16_t mouse_dy,
        uint8_t mouse_buttons, int8_t scroll_delta)>;

    using ParamUpdateCallback = std::function<void(
        uint32_t seq, const std::string& json)>;

    explicit ControlReceiver(uint16_t port);
    ~ControlReceiver();

    void set_command_callback(CommandCallback cb);
    void set_param_update_callback(ParamUpdateCallback cb);
    void set_verbose(bool v) { verbose_.store(v); }

    void start();
    void stop();

    double last_client_time() const { return last_client_time_.load(); }

private:
    void recv_loop();
    void handle_control_command(const uint8_t* data, size_t len,
                                 const struct sockaddr_in& from);
    void handle_heartbeat(const uint8_t* data, size_t len,
                          const struct sockaddr_in& from);
    void handle_param_update(const uint8_t* data, size_t len,
                              const struct sockaddr_in& from);
    void handle_param_query(const uint8_t* data, size_t len,
                             const struct sockaddr_in& from);
    void send_ack(int fd, const struct sockaddr_in& to,
                  uint32_t seq, double t2, double t3);

    uint16_t            port_;
    int                 control_fd_{-1};
    std::atomic<bool>   running_{false};
    std::thread         recv_thread_;

    CommandCallback     cmd_cb_;
    ParamUpdateCallback param_cb_;

    std::atomic<double> last_client_time_{0.0};
    std::string         client_ip_;
    std::mutex          client_ip_mutex_;
    std::atomic<bool>   verbose_{false};
};

}  // namespace remote_link
