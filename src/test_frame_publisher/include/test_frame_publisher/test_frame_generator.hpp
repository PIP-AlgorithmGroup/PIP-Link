#pragma once

#include <opencv2/core.hpp>
#include <cstdint>

namespace test_frame_publisher {

class TestFrameGenerator {
public:
    TestFrameGenerator(int width, int height);

    // Draw frame directly into dst (zero-copy for Loaned Message)
    void generate_into(uint64_t frame_id, cv::Mat& dst);

private:
    void build_base_frame();

    cv::Mat base_frame_;
    int     width_, height_;
};

}  // namespace test_frame_publisher
