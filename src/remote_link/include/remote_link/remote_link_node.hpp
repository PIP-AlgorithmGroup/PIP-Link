#pragma once

#include <memory>
#include <string>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <std_msgs/msg/string.hpp>
#include <pip_vision_interfaces/msg/remote_command.hpp>
#include <rcl_interfaces/msg/set_parameters_result.hpp>

namespace remote_link {

class ControlReceiver;
class VideoSender;
class MdnsService;

class RemoteLinkNode : public rclcpp::Node {
public:
    explicit RemoteLinkNode(const rclcpp::NodeOptions& options = rclcpp::NodeOptions());
    ~RemoteLinkNode();

private:
    void on_frame(sensor_msgs::msg::Image::ConstSharedPtr msg);

    void on_command(const std::string& client_ip,
                    uint32_t seq, double t1,
                    const uint8_t kb[10],
                    int16_t mouse_dx, int16_t mouse_dy,
                    uint8_t mouse_buttons, int8_t scroll_delta);

    void on_param_update_from_udp(uint32_t seq, const std::string& json);

    rcl_interfaces::msg::SetParametersResult
    on_parameter_change(const std::vector<rclcpp::Parameter>& params);

    void watchdog_tick();
    void diagnostic_tick();
    std::string params_to_json() const;
    void apply_video_config();

    rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr frame_sub_;
    rclcpp::Publisher<pip_vision_interfaces::msg::RemoteCommand>::SharedPtr cmd_pub_;
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr stats_pub_;
    rclcpp::TimerBase::SharedPtr watchdog_timer_;
    rclcpp::TimerBase::SharedPtr diagnostic_timer_;
    rclcpp::node_interfaces::OnSetParametersCallbackHandle::SharedPtr param_cb_handle_;

    std::unique_ptr<ControlReceiver> control_rx_;
    std::unique_ptr<VideoSender>     video_tx_;
    std::unique_ptr<MdnsService>     mdns_;

    double prev_t1_{-1.0};
    double client_timeout_s_{5.0};
    bool   frame_received_{false};
    bool   debug_verbose_{false};
};

}  // namespace remote_link
