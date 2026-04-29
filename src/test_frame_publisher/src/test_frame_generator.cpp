#include "test_frame_publisher/test_frame_generator.hpp"

#include <opencv2/imgproc.hpp>
#include <cmath>
#include <ctime>
#include <cstdio>

namespace test_frame_publisher {

TestFrameGenerator::TestFrameGenerator(int width, int height)
: width_(width), height_(height)
{
    build_base_frame();
}

void TestFrameGenerator::build_base_frame() {
    base_frame_ = cv::Mat::zeros(height_, width_, CV_8UC3);

    const int bar_h = static_cast<int>(height_ * 0.70);

    // Colour bars (7 bars, top 70%)
    const cv::Scalar colors[] = {
        {255, 255, 255}, {0, 255, 255}, {255, 255, 0}, {0, 255, 0},
        {255, 0, 255},   {0, 0, 255},   {255, 0, 0},
    };
    const int n_colors = 7;
    const int bar_w = width_ / n_colors;
    for (int i = 0; i < n_colors; ++i) {
        int x0 = i * bar_w;
        int x1 = (i < n_colors - 1) ? (i + 1) * bar_w : width_;
        base_frame_(cv::Rect(x0, 0, x1 - x0, bar_h)).setTo(colors[i]);
    }

    // Greyscale ramp (next 8%)
    const int gray_y0 = bar_h;
    const int gray_h  = static_cast<int>(height_ * 0.08);
    for (int x = 0; x < width_; ++x) {
        uint8_t v = static_cast<uint8_t>(255 * x / width_);
        base_frame_(cv::Rect(x, gray_y0, 1, gray_h)).setTo(cv::Scalar(v, v, v));
    }

    // Grid lines
    const int grid = 64;
    for (int x = 0; x < width_; x += grid)
        base_frame_(cv::Rect(x, 0, 1, bar_h)).setTo(cv::Scalar(80, 80, 80));
    for (int y = 0; y < bar_h; y += grid)
        base_frame_(cv::Rect(0, y, width_, 1)).setTo(cv::Scalar(80, 80, 80));

    // Centre crosshair + circles
    const int cx = width_ / 2;
    const int cy = bar_h / 2;
    cv::circle(base_frame_, {cx, cy}, 80, {200, 200, 200}, 2);
    cv::circle(base_frame_, {cx, cy}, 40, {200, 200, 200}, 1);
    cv::line(base_frame_, {cx - 100, cy}, {cx + 100, cy}, {200, 200, 200}, 1);
    cv::line(base_frame_, {cx, cy - 100}, {cx, cy + 100}, {200, 200, 200}, 1);

    // Corner markers
    const int m = 30;
    for (auto [mx, my] : std::initializer_list<std::pair<int,int>>{
            {0, 0}, {width_ - m, 0}, {0, bar_h - m}, {width_ - m, bar_h - m}}) {
        base_frame_(cv::Rect(mx, my, m, m)).setTo(cv::Scalar(255, 255, 255));
    }

    // Resolution text
    char txt[32];
    std::snprintf(txt, sizeof(txt), "%dx%d", width_, height_);
    cv::putText(base_frame_, txt, {cx - 80, cy + 130},
                cv::FONT_HERSHEY_SIMPLEX, 0.7, {200, 200, 200}, 1, cv::LINE_AA);
}

void TestFrameGenerator::generate_into(uint64_t frame_id, cv::Mat& dst) {
    base_frame_.copyTo(dst);

    const int dyn_y = static_cast<int>(height_ * 0.78);

    // Timestamp + frame counter
    char txt[64];
    std::time_t t = std::time(nullptr);
    struct tm tm_info;
    localtime_r(&t, &tm_info);
    std::snprintf(txt, sizeof(txt), "#%06llu  %02d:%02d:%02d",
                  static_cast<unsigned long long>(frame_id),
                  tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec);
    cv::putText(dst, txt, {20, dyn_y + 30},
                cv::FONT_HERSHEY_SIMPLEX, 0.7, {0, 200, 255}, 1, cv::LINE_AA);

    // Rotating pointer
    const int ptr_cx = width_ - 80;
    const int ptr_cy = dyn_y + 50;
    const double angle = static_cast<double>((frame_id * 12) % 360);
    const double rad   = angle * M_PI / 180.0;
    const int px = ptr_cx + static_cast<int>(35 * std::cos(rad));
    const int py = ptr_cy + static_cast<int>(35 * std::sin(rad));
    cv::circle(dst, {ptr_cx, ptr_cy}, 38, {100, 100, 100}, 1);
    cv::line(dst, {ptr_cx, ptr_cy}, {px, py}, {0, 255, 0}, 2);

    // Scrolling checkerboard block
    const int checker_sz = 8;
    const int block_w = 120;
    const int block_h = 80;
    const int block_x = static_cast<int>(
        (width_ / 2.0 - block_w / 2.0) + 60.0 * std::sin(frame_id * 0.05));
    const int block_y = dyn_y + 10;
    for (int by = 0; by < block_h; ++by) {
        for (int bx = 0; bx < block_w; ++bx) {
            if (((bx / checker_sz) + (by / checker_sz)) % 2 == 0) {
                int px2 = block_x + bx;
                int py2 = block_y + by;
                if (px2 >= 0 && px2 < width_ && py2 >= 0 && py2 < height_)
                    dst.at<cv::Vec3b>(py2, px2) = {255, 255, 255};
            }
        }
    }

    // Scrolling cyan bar at bottom
    const int scroll_y  = height_ - 6;
    const int bar_start = static_cast<int>((frame_id * 6) % static_cast<uint64_t>(width_));
    const int bar_end   = std::min(bar_start + 160, width_);
    if (scroll_y >= 0 && scroll_y < height_)
        dst(cv::Rect(bar_start, scroll_y, bar_end - bar_start, height_ - scroll_y))
            .setTo(cv::Scalar(255, 200, 0));
}

}  // namespace test_frame_publisher
