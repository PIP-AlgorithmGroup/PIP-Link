#pragma once

#include "frame_encoder.hpp"

#include <cstdint>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <map>
#include <vector>
#include <memory>
#include <chrono>
#include <netinet/in.h>
#include <opencv2/core.hpp>
#include <rclcpp/logger.hpp>
#include <rclcpp/logging.hpp>

namespace remote_link {

class VideoSender {
public:
    struct Config {
        uint16_t    port          = 5000;
        bool        fec_enabled   = false;
        float       fec_redundancy = 0.2f;
        EncoderConfig encoder_cfg;
    };

    VideoSender(const Config& cfg, rclcpp::Logger logger);
    ~VideoSender();

    void start();
    void stop();

    // Thread-safe, latest-frame-wins delivery
    void push_frame(const cv::Mat& frame);

    void set_client_addr(const std::string& ip, uint16_t port);
    void clear_client_addr();
    bool has_client() const;

    void update_config(const Config& cfg);

    struct Stats {
        uint64_t frames_sent{0};
        uint64_t frames_acked{0};
        uint64_t bytes_sent{0};
        float    actual_bitrate_kbps{0.0f};
        int      current_jpeg_quality{80};
    };
    Stats get_stats() const;

private:
    void sender_loop();
    void drain_recv_queue();
    void send_frame(const cv::Mat& frame);
    void retransmit_nack(uint32_t frame_id,
                          const std::vector<uint16_t>& missing);

    std::mutex              frame_mutex_;
    cv::Mat                 latest_frame_;
    bool                    has_new_frame_{false};
    std::condition_variable frame_cv_;

    mutable std::mutex      client_mutex_;
    struct sockaddr_in      client_addr_{};
    bool                    has_client_{false};

    // NACK frame cache: frame_id -> chunk_idx -> raw packet bytes
    std::mutex              cache_mutex_;
    std::map<uint32_t, std::map<uint16_t, std::vector<uint8_t>>> frame_cache_;
    static constexpr int    FRAME_CACHE_MAX = 10;

    int                     video_fd_{-1};
    uint32_t                frame_id_{0};
    std::atomic<bool>       running_{false};
    std::thread             sender_thread_;

    rclcpp::Logger          logger_;
    Config                      cfg_;
    std::unique_ptr<FrameEncoder> encoder_;
    mutable std::mutex          cfg_mutex_;

    // Stats
    mutable std::mutex          stats_mutex_;
    Stats                       stats_;
    std::atomic<uint64_t>       bytes_window_{0};
    std::chrono::steady_clock::time_point window_start_;
};

}  // namespace remote_link
