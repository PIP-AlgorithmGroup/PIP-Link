#include "test_frame_publisher/test_frame_publisher_node.hpp"

#include <rclcpp_components/register_node_macro.hpp>

namespace test_frame_publisher {

TestFramePublisherNode::TestFramePublisherNode(const rclcpp::NodeOptions& options)
: rclcpp::Node("test_frame_publisher", options)
{
    declare_parameter("width",          1280);
    declare_parameter("height",         720);
    declare_parameter("fps",            30);
    declare_parameter("frame_topic",    std::string("/sending_frame"));
    declare_parameter("stats_interval", 100);

    width_          = get_parameter("width").as_int();
    height_         = get_parameter("height").as_int();
    stats_interval_ = get_parameter("stats_interval").as_int();
    int fps         = get_parameter("fps").as_int();
    auto topic      = get_parameter("frame_topic").as_string();

    generator_ = std::make_unique<TestFrameGenerator>(width_, height_);

    auto qos = rclcpp::QoS(1).best_effort().durability_volatile();
    pub_ = create_publisher<sensor_msgs::msg::Image>(topic, qos);

    auto period = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<double>(1.0 / fps));
    timer_ = create_wall_timer(period, [this] { timer_callback(); });

    RCLCPP_INFO(get_logger(),
                "TestFramePublisherNode: %dx%d @ %d fps -> %s  (stats every %d frames)",
                width_, height_, fps, topic.c_str(), stats_interval_);
}

void TestFramePublisherNode::timer_callback() {
    auto t_start = Clock::now();

    if (!first_callback_) {
        stats_.interval.record(Ms(t_start - last_callback_tp_).count());
    }
    first_callback_   = false;
    last_callback_tp_ = t_start;

    // ── alloc ──────────────────────────────────────────────────────────────
    auto t0  = Clock::now();
    auto msg = std::make_unique<sensor_msgs::msg::Image>();
    msg->header.stamp    = now();
    msg->header.frame_id = "camera";
    msg->width    = static_cast<uint32_t>(width_);
    msg->height   = static_cast<uint32_t>(height_);
    msg->encoding = "bgr8";
    msg->step     = static_cast<uint32_t>(width_ * 3);
    msg->data.resize(static_cast<size_t>(width_ * height_ * 3));
    auto t1 = Clock::now();
    stats_.alloc.record(Ms(t1 - t0).count());

    // ── generate ───────────────────────────────────────────────────────────
    cv::Mat view(height_, width_, CV_8UC3, msg->data.data());
    generator_->generate_into(frame_id_++, view);
    auto t2 = Clock::now();
    stats_.generate.record(Ms(t2 - t1).count());

    // ── publish ────────────────────────────────────────────────────────────
    pub_->publish(std::move(msg));
    auto t3 = Clock::now();
    stats_.publish.record(Ms(t3 - t2).count());

    // ── total ──────────────────────────────────────────────────────────────
    stats_.total.record(Ms(t3 - t_start).count());
    stats_.count++;

    if (stats_.count >= stats_interval_) {
        report_stats();
        stats_.reset();
    }
}

void TestFramePublisherNode::report_stats() {
    int n  = stats_.count;
    int ni = n > 1 ? n - 1 : 1;
    double avg_interval = stats_.interval.avg(ni);
    double actual_fps   = avg_interval > 0.0 ? 1000.0 / avg_interval : 0.0;

    RCLCPP_INFO(get_logger(),
        "[stats %d frames]  fps=%.1f  "
        "alloc avg/min/max=%.2f/%.2f/%.2f ms  "
        "gen=%.2f/%.2f/%.2f ms  "
        "pub=%.2f/%.2f/%.2f ms  "
        "total=%.2f/%.2f/%.2f ms  "
        "interval=%.2f/%.2f/%.2f ms",
        n, actual_fps,
        stats_.alloc.avg(n),    stats_.alloc.min,    stats_.alloc.max,
        stats_.generate.avg(n), stats_.generate.min, stats_.generate.max,
        stats_.publish.avg(n),  stats_.publish.min,  stats_.publish.max,
        stats_.total.avg(n),    stats_.total.min,    stats_.total.max,
        stats_.interval.avg(ni), stats_.interval.min, stats_.interval.max);
}

}  // namespace test_frame_publisher

RCLCPP_COMPONENTS_REGISTER_NODE(test_frame_publisher::TestFramePublisherNode)
