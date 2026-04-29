#include "remote_link/remote_link_node.hpp"
#include "remote_link/control_receiver.hpp"
#include "remote_link/video_sender.hpp"
#include "remote_link/mdns_service.hpp"

#include <std_msgs/msg/string.hpp>
#include <nlohmann/json.hpp>
#include <rclcpp_components/register_node_macro.hpp>
#include <chrono>
#include <algorithm>
#include <cstdint>

namespace remote_link {

static double now_sec() {
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

RemoteLinkNode::RemoteLinkNode(const rclcpp::NodeOptions& options)
: rclcpp::Node("remote_link", options)
{
    // --- Declare parameters ---
    declare_parameter("air_unit_name",       "air_unit_01");
    declare_parameter("control_port",        6000);
    declare_parameter("video_port",          5000);
    declare_parameter("target_fps",          30);
    declare_parameter("target_bitrate_kbps", 2000);
    declare_parameter("jpeg_quality",        80);
    declare_parameter("encoder",             std::string("h264"));
    declare_parameter("fec_enabled",         false);
    declare_parameter("fec_redundancy",      0.2);
    declare_parameter("client_timeout_s",    5.0);
    declare_parameter("debug.verbose",       false);
    declare_parameter("brightness",          0);
    declare_parameter("contrast",            0);
    declare_parameter("sharpness",           0);
    declare_parameter("denoise",             0);

    client_timeout_s_ = get_parameter("client_timeout_s").as_double();
    debug_verbose_    = get_parameter("debug.verbose").as_bool();

    // --- QoS: depth=1, best_effort, volatile ---
    auto qos = rclcpp::QoS(1).best_effort().durability_volatile();

    // --- Publisher ---
    cmd_pub_ = create_publisher<pip_vision_interfaces::msg::RemoteCommand>(
        "/remote_command", rclcpp::QoS(10));

    // --- Subscriber ---
    frame_sub_ = create_subscription<sensor_msgs::msg::Image>(
        "/sending_frame", qos,
        [this](sensor_msgs::msg::Image::ConstSharedPtr msg) {
            on_frame(msg);
        });

    // --- VideoSender ---
    VideoSender::Config vs_cfg;
    vs_cfg.port = static_cast<uint16_t>(get_parameter("video_port").as_int());
    vs_cfg.encoder_cfg.quality        = get_parameter("jpeg_quality").as_int();
    vs_cfg.encoder_cfg.target_bitrate = get_parameter("target_bitrate_kbps").as_int();
    vs_cfg.encoder_cfg.fps            = get_parameter("target_fps").as_int();
    vs_cfg.encoder_cfg.use_h264       = (get_parameter("encoder").as_string() == "h264");
    vs_cfg.encoder_cfg.brightness     = get_parameter("brightness").as_int();
    vs_cfg.encoder_cfg.contrast       = get_parameter("contrast").as_int();
    vs_cfg.encoder_cfg.sharpness      = get_parameter("sharpness").as_int();
    vs_cfg.encoder_cfg.denoise        = get_parameter("denoise").as_int();
    vs_cfg.fec_enabled   = get_parameter("fec_enabled").as_bool();
    vs_cfg.fec_redundancy = static_cast<float>(get_parameter("fec_redundancy").as_double());

    video_tx_ = std::make_unique<VideoSender>(vs_cfg, get_logger());
    video_tx_->start();

    // --- ControlReceiver ---
    uint16_t ctrl_port = static_cast<uint16_t>(get_parameter("control_port").as_int());
    control_rx_ = std::make_unique<ControlReceiver>(ctrl_port);

    control_rx_->set_command_callback(
        [this](const std::string& ip, uint32_t seq, double t1,
               const uint8_t kb[10], int16_t dx, int16_t dy,
               uint8_t btn, int8_t scroll) {
            on_command(ip, seq, t1, kb, dx, dy, btn, scroll);
        });

    control_rx_->set_param_update_callback(
        [this](uint32_t seq, const std::string& json) {
            on_param_update_from_udp(seq, json);
        });

    control_rx_->set_verbose(debug_verbose_);
    control_rx_->start();

    // --- MdnsService ---
    MdnsService::Config mdns_cfg;
    mdns_cfg.service_name = get_parameter("air_unit_name").as_string();
    mdns_cfg.control_port = ctrl_port;
    mdns_cfg.video_port   = vs_cfg.port;
    mdns_ = std::make_unique<MdnsService>(mdns_cfg);
    if (!mdns_->start()) {
        RCLCPP_WARN(get_logger(), "mDNS registration timed out");
    }

    // --- Parameter callback ---
    param_cb_handle_ = add_on_set_parameters_callback(
        [this](const std::vector<rclcpp::Parameter>& params) {
            return on_parameter_change(params);
        });

    // --- Stats publisher ---
    stats_pub_ = create_publisher<std_msgs::msg::String>(
        "/remote_link/stats", rclcpp::QoS(10));

    // --- Watchdog timer (2s) ---
    watchdog_timer_ = create_wall_timer(
        std::chrono::seconds(2),
        [this] { watchdog_tick(); });

    // --- Diagnostic timer (5s) ---
    diagnostic_timer_ = create_wall_timer(
        std::chrono::seconds(5),
        [this] { diagnostic_tick(); });

    RCLCPP_INFO(get_logger(), "RemoteLinkNode started (ctrl:%u video:%u)",
                ctrl_port, vs_cfg.port);
}

RemoteLinkNode::~RemoteLinkNode() {
    if (mdns_)       mdns_->stop();
    if (control_rx_) control_rx_->stop();
    if (video_tx_)   video_tx_->stop();
}

void RemoteLinkNode::on_frame(sensor_msgs::msg::Image::ConstSharedPtr msg) {
    if (!frame_received_) {
        frame_received_ = true;
        RCLCPP_INFO(get_logger(), "First frame received: %ux%u encoding=%s",
                    msg->width, msg->height, msg->encoding.c_str());
    }
    cv::Mat frame(static_cast<int>(msg->height), static_cast<int>(msg->width),
                  CV_8UC3, const_cast<uint8_t*>(msg->data.data()));
    video_tx_->push_frame(frame);
}

void RemoteLinkNode::on_command(
    const std::string& client_ip,
    uint32_t seq, double t1,
    const uint8_t kb[10],
    int16_t mouse_dx, int16_t mouse_dy,
    uint8_t mouse_buttons, int8_t scroll_delta)
{
    auto msg = std::make_unique<pip_vision_interfaces::msg::RemoteCommand>();
    msg->header.stamp = now();
    msg->header.frame_id = "";
    msg->client_ip = client_ip;
    msg->seq = seq;
    msg->t1  = t1;
    msg->is_ready = true;

    // mouse velocity
    constexpr double DT_MIN = 0.001;
    constexpr double DT_MAX = 0.200;
    if (prev_t1_ < 0.0) {
        msg->mouse_vx = 0.0f;
        msg->mouse_vy = 0.0f;
    } else {
        double dt = t1 - prev_t1_;
        if (dt > DT_MAX) {
            msg->mouse_vx = 0.0f;
            msg->mouse_vy = 0.0f;
        } else {
            dt = std::max(dt, DT_MIN);
            msg->mouse_vx = static_cast<float>(mouse_dx) / static_cast<float>(dt);
            msg->mouse_vy = static_cast<float>(mouse_dy) / static_cast<float>(dt);
        }
    }
    prev_t1_ = t1;

    // mouse buttons: bit0=L, bit1=R, bit2=M, bit3=M4, bit4=M5
    msg->mouse_left   = (mouse_buttons & 0x01) != 0;
    msg->mouse_right  = (mouse_buttons & 0x02) != 0;
    msg->mouse_middle = (mouse_buttons & 0x04) != 0;
    msg->mouse4       = (mouse_buttons & 0x08) != 0;
    msg->mouse5       = (mouse_buttons & 0x10) != 0;
    msg->scroll_up    = (scroll_delta > 0);
    msg->scroll_down  = (scroll_delta < 0);

    for (int i = 0; i < 10; ++i) msg->keyboard_state[i] = kb[i];

    uint8_t count = 0;
    for (int i = 0; i < 10; ++i) count += static_cast<uint8_t>(__builtin_popcount(kb[i]));
    msg->pressed_keys_count = count;

    cmd_pub_->publish(std::move(msg));
}

void RemoteLinkNode::on_param_update_from_udp(
    uint32_t /*seq*/, const std::string& json)
{
    try {
        auto j = nlohmann::json::parse(json);
        std::vector<rclcpp::Parameter> params;

        if (j.contains("bitrate"))
            params.emplace_back("target_bitrate_kbps", j["bitrate"].get<int>());
        if (j.contains("target_fps"))
            params.emplace_back("target_fps", j["target_fps"].get<int>());
        if (j.contains("encoder"))
            params.emplace_back("encoder", j["encoder"].get<std::string>());
        if (j.contains("fec_enabled"))
            params.emplace_back("fec_enabled", j["fec_enabled"].get<bool>());
        if (j.contains("fec_redundancy"))
            params.emplace_back("fec_redundancy", j["fec_redundancy"].get<double>());
        if (j.contains("brightness"))
            params.emplace_back("brightness", j["brightness"].get<int>());
        if (j.contains("contrast"))
            params.emplace_back("contrast", j["contrast"].get<int>());
        if (j.contains("sharpness"))
            params.emplace_back("sharpness", j["sharpness"].get<int>());
        if (j.contains("denoise"))
            params.emplace_back("denoise", j["denoise"].get<int>());

        if (!params.empty()) set_parameters(params);
    } catch (const std::exception& e) {
        RCLCPP_WARN(get_logger(), "PARAM_UPDATE parse error: %s", e.what());
    }
}

rcl_interfaces::msg::SetParametersResult
RemoteLinkNode::on_parameter_change(const std::vector<rclcpp::Parameter>& params)
{
    for (const auto& p : params) {
        if (p.get_name() == "debug.verbose") {
            debug_verbose_ = p.as_bool();
        } else if (p.get_name() == "client_timeout_s") {
            client_timeout_s_ = p.as_double();
        }
    }
    apply_video_config();
    rcl_interfaces::msg::SetParametersResult result;
    result.successful = true;
    return result;
}

void RemoteLinkNode::apply_video_config() {
    if (!video_tx_) return;
    VideoSender::Config cfg;
    cfg.port          = static_cast<uint16_t>(get_parameter("video_port").as_int());
    cfg.fec_enabled   = get_parameter("fec_enabled").as_bool();
    cfg.fec_redundancy = static_cast<float>(get_parameter("fec_redundancy").as_double());
    cfg.encoder_cfg.quality        = get_parameter("jpeg_quality").as_int();
    cfg.encoder_cfg.target_bitrate = get_parameter("target_bitrate_kbps").as_int();
    cfg.encoder_cfg.fps            = get_parameter("target_fps").as_int();
    cfg.encoder_cfg.use_h264       = (get_parameter("encoder").as_string() == "h264");
    cfg.encoder_cfg.brightness     = get_parameter("brightness").as_int();
    cfg.encoder_cfg.contrast       = get_parameter("contrast").as_int();
    cfg.encoder_cfg.sharpness      = get_parameter("sharpness").as_int();
    cfg.encoder_cfg.denoise        = get_parameter("denoise").as_int();
    video_tx_->update_config(cfg);
}

void RemoteLinkNode::watchdog_tick() {
    double last = control_rx_ ? control_rx_->last_client_time() : 0.0;
    if (last > 0.0 && (now_sec() - last) > client_timeout_s_) {
        RCLCPP_WARN(get_logger(), "Client timeout, stopping video stream");
        video_tx_->clear_client_addr();
    }
}

void RemoteLinkNode::diagnostic_tick() {
    if (!video_tx_) return;
    auto s = video_tx_->get_stats();

    nlohmann::json j;
    j["frames_sent"]          = s.frames_sent;
    j["frames_acked"]         = s.frames_acked;
    j["bytes_sent"]           = s.bytes_sent;
    j["actual_bitrate_kbps"]  = s.actual_bitrate_kbps;
    j["current_jpeg_quality"] = s.current_jpeg_quality;
    j["has_client"]           = video_tx_->has_client();

    std::string payload = j.dump();
    if (debug_verbose_) {
        RCLCPP_INFO(get_logger(), "stats: %s", payload.c_str());
    }

    auto msg = std::make_unique<std_msgs::msg::String>();
    msg->data = std::move(payload);
    stats_pub_->publish(std::move(msg));
}

std::string RemoteLinkNode::params_to_json() const {    nlohmann::json j;
    j["bitrate"]       = get_parameter("target_bitrate_kbps").as_int();
    j["target_fps"]    = get_parameter("target_fps").as_int();
    j["encoder"]       = get_parameter("encoder").as_string();
    j["fec_enabled"]   = get_parameter("fec_enabled").as_bool();
    j["fec_redundancy"]= get_parameter("fec_redundancy").as_double();
    return j.dump();
}

}  // namespace remote_link

RCLCPP_COMPONENTS_REGISTER_NODE(remote_link::RemoteLinkNode)
