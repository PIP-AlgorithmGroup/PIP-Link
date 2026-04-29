#pragma once

#include "test_frame_generator.hpp"

#include <memory>
#include <chrono>
#include <limits>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>

namespace test_frame_publisher {

class TestFramePublisherNode : public rclcpp::Node {
public:
    explicit TestFramePublisherNode(const rclcpp::NodeOptions& options = rclcpp::NodeOptions());

private:
    void timer_callback();
    void report_stats();

    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr pub_;
    rclcpp::TimerBase::SharedPtr                         timer_;
    std::unique_ptr<TestFrameGenerator>                  generator_;
    uint64_t frame_id_{0};
    int      width_, height_;
    int      stats_interval_{100};  // print every N frames

    using Clock = std::chrono::steady_clock;
    using Ms    = std::chrono::duration<double, std::milli>;

    struct StageStat {
        double sum{0}, min{std::numeric_limits<double>::max()}, max{0};
        void record(double v) {
            sum += v; if (v < min) min = v; if (v > max) max = v;
        }
        double avg(int n) const { return n > 0 ? sum / n : 0; }
        void reset() { sum = 0; min = std::numeric_limits<double>::max(); max = 0; }
    };

    struct Stats {
        StageStat alloc, generate, publish, total, interval;
        int count{0};
        void reset() {
            alloc.reset(); generate.reset(); publish.reset();
            total.reset(); interval.reset(); count = 0;
        }
    } stats_;

    Clock::time_point last_callback_tp_{Clock::now()};
    bool first_callback_{true};
};

}  // namespace test_frame_publisher
