#pragma once

#include <vector>
#include <cstdint>
#include <opencv2/core.hpp>

// Forward declarations for FFmpeg types (Phase 3)
struct AVCodecContext;
struct AVFrame;
struct SwsContext;

namespace remote_link {

struct EncoderConfig {
    int  quality        = 80;    // JPEG quality [15, 85]
    int  target_bitrate = 2000;  // kbps (H.264)
    int  fps            = 30;
    bool use_h264       = false;
    int  width          = 1280;
    int  height         = 720;
    int  brightness     = 0;     // -100~100
    int  contrast       = 0;     // -100~100
    int  sharpness      = 0;     // 0~100
    int  denoise        = 0;     // 0~100
};

class FrameEncoder {
public:
    explicit FrameEncoder(const EncoderConfig& cfg);
    ~FrameEncoder();

    // Encodes BGR Mat; codec_flag: 0=JPEG, 1=H.264
    std::vector<uint8_t> encode(const cv::Mat& frame,
                                 bool force_keyframe,
                                 uint8_t& codec_flag);

    void update_config(const EncoderConfig& cfg);

    float last_encode_ms() const { return last_encode_ms_; }
    int   current_quality() const { return quality_; }

private:
    void  adjust_quality(size_t encoded_size);
    cv::Mat apply_enhancements(const cv::Mat& frame) const;
    void  open_h264();
    void  close_h264();
    std::vector<uint8_t> encode_h264(const cv::Mat& frame, bool force_keyframe);

    EncoderConfig cfg_;
    float         last_encode_ms_{0.0f};

    int   quality_;
    float ema_size_;
    float target_frame_bytes_;

    // H.264 (Phase 3)
    AVCodecContext* codec_ctx_{nullptr};
    AVFrame*        av_frame_{nullptr};
    SwsContext*     sws_ctx_{nullptr};
    int64_t         pts_{0};
    int             sws_src_w_{0};
    int             sws_src_h_{0};

    static constexpr float EMA_ALPHA   = 0.3f;
    static constexpr int   QUALITY_MIN = 15;
    static constexpr int   QUALITY_MAX = 85;
};

}  // namespace remote_link
