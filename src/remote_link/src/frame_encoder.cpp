#include "remote_link/frame_encoder.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <chrono>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/photo.hpp>

namespace remote_link {

FrameEncoder::FrameEncoder(const EncoderConfig& cfg)
: cfg_(cfg)
, last_encode_ms_(0.0f)
, quality_(cfg.quality)
, ema_size_(0.0f)
, target_frame_bytes_(0.0f)
{
    target_frame_bytes_ = static_cast<float>(cfg.target_bitrate * 1000) / 8.0f
                        / static_cast<float>(cfg.fps);
    ema_size_ = target_frame_bytes_;
    if (cfg_.use_h264) open_h264();
}

FrameEncoder::~FrameEncoder() {
    close_h264();
}

void FrameEncoder::open_h264() {
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        fprintf(stderr, "[FrameEncoder] H.264 codec not found\n");
        return;
    }

    codec_ctx_ = avcodec_alloc_context3(codec);
    codec_ctx_->codec_id    = AV_CODEC_ID_H264;
    codec_ctx_->width       = cfg_.width;
    codec_ctx_->height      = cfg_.height;
    codec_ctx_->time_base   = {1, cfg_.fps > 0 ? cfg_.fps : 30};
    codec_ctx_->framerate   = {cfg_.fps > 0 ? cfg_.fps : 30, 1};
    codec_ctx_->pix_fmt     = AV_PIX_FMT_YUV420P;
    codec_ctx_->bit_rate    = cfg_.target_bitrate * 1000;
    codec_ctx_->gop_size    = 30;
    codec_ctx_->max_b_frames = 0;
    av_opt_set(codec_ctx_->priv_data, "preset",  "ultrafast",   0);
    av_opt_set(codec_ctx_->priv_data, "tune",    "zerolatency", 0);
    av_opt_set(codec_ctx_->priv_data, "profile", "baseline",    0);

    fprintf(stderr, "[FrameEncoder] open_h264: %dx%d fps=%d bitrate=%dkbps\n",
            cfg_.width, cfg_.height, cfg_.fps, cfg_.target_bitrate);

    int ret = avcodec_open2(codec_ctx_, codec, nullptr);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        fprintf(stderr, "[FrameEncoder] avcodec_open2 failed: %s\n", errbuf);
        avcodec_free_context(&codec_ctx_);
        return;
    }
    fprintf(stderr, "[FrameEncoder] H.264 encoder opened OK\n");

    av_frame_ = av_frame_alloc();
    av_frame_->format = AV_PIX_FMT_YUV420P;
    av_frame_->width  = codec_ctx_->width;
    av_frame_->height = codec_ctx_->height;
    av_frame_get_buffer(av_frame_, 0);
    pts_ = 0;
}

void FrameEncoder::close_h264() {
    if (sws_ctx_)  { sws_freeContext(sws_ctx_); sws_ctx_ = nullptr; }
    if (av_frame_) { av_frame_free(&av_frame_); av_frame_ = nullptr; }
    if (codec_ctx_) { avcodec_free_context(&codec_ctx_); codec_ctx_ = nullptr; }
    pts_ = 0;
    sws_src_w_ = 0;
    sws_src_h_ = 0;
}

std::vector<uint8_t> FrameEncoder::encode_h264(const cv::Mat& frame, bool force_keyframe) {
    if (!codec_ctx_ || !av_frame_) return {};

    // 按需重建 sws_ctx（源分辨率变化时）
    if (!sws_ctx_ || frame.cols != sws_src_w_ || frame.rows != sws_src_h_) {
        if (sws_ctx_) { sws_freeContext(sws_ctx_); sws_ctx_ = nullptr; }
        sws_src_w_ = frame.cols;
        sws_src_h_ = frame.rows;
        sws_ctx_ = sws_getContext(
            frame.cols, frame.rows, AV_PIX_FMT_BGR24,
            codec_ctx_->width, codec_ctx_->height, AV_PIX_FMT_YUV420P,
            SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!sws_ctx_) return {};
    }

    av_frame_make_writable(av_frame_);

    const uint8_t* src_data[1]  = {frame.data};
    int            src_stride[1] = {static_cast<int>(frame.step)};
    sws_scale(sws_ctx_, src_data, src_stride, 0, frame.rows,
              av_frame_->data, av_frame_->linesize);

    av_frame_->pts = pts_++;
    if (force_keyframe) {
        av_frame_->pict_type = AV_PICTURE_TYPE_I;
        av_frame_->key_frame = 1;
    } else {
        av_frame_->pict_type = AV_PICTURE_TYPE_NONE;
        av_frame_->key_frame = 0;
    }

    std::vector<uint8_t> result;
    if (avcodec_send_frame(codec_ctx_, av_frame_) == 0) {
        AVPacket* pkt = av_packet_alloc();
        while (avcodec_receive_packet(codec_ctx_, pkt) == 0) {
            result.insert(result.end(), pkt->data, pkt->data + pkt->size);
            av_packet_unref(pkt);
        }
        av_packet_free(&pkt);
    }
    return result;
}

void FrameEncoder::update_config(const EncoderConfig& cfg) {
    bool need_reinit = (cfg.use_h264 != cfg_.use_h264) ||
                       (cfg.use_h264 && (cfg.width  != cfg_.width  ||
                                          cfg.height != cfg_.height ||
                                          cfg.fps    != cfg_.fps    ||
                                          cfg.target_bitrate != cfg_.target_bitrate));
    cfg_ = cfg;
    target_frame_bytes_ = static_cast<float>(cfg.target_bitrate * 1000) / 8.0f
                        / static_cast<float>(cfg.fps);
    if (need_reinit) {
        close_h264();
        if (cfg_.use_h264) open_h264();
    }
}

std::vector<uint8_t> FrameEncoder::encode(
    const cv::Mat& frame, bool force_keyframe, uint8_t& codec_flag)
{
    auto t0 = std::chrono::steady_clock::now();
    std::vector<uint8_t> buf;

    if (cfg_.use_h264 && codec_ctx_) {
        codec_flag = 1;
        cv::Mat h264_input = apply_enhancements(frame);
        buf = encode_h264(h264_input, force_keyframe);
    } else {
        codec_flag = 0;
        cv::Mat enhanced = apply_enhancements(frame);
        std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, quality_};
        cv::imencode(".jpg", enhanced, buf, params);
        adjust_quality(buf.size());
    }

    auto t1 = std::chrono::steady_clock::now();
    last_encode_ms_ = std::chrono::duration<float, std::milli>(t1 - t0).count();
    return buf;
}

void FrameEncoder::adjust_quality(size_t encoded_size) {
    ema_size_ = EMA_ALPHA * static_cast<float>(encoded_size)
              + (1.0f - EMA_ALPHA) * ema_size_;
    float ratio = ema_size_ / target_frame_bytes_;
    if      (ratio > 1.1f) quality_ = std::max(QUALITY_MIN, quality_ - 2);
    else if (ratio < 0.8f) quality_ = std::min(QUALITY_MAX, quality_ + 1);
}

cv::Mat FrameEncoder::apply_enhancements(const cv::Mat& frame) const {
    cv::Mat result = frame;
    if (cfg_.brightness != 0 || cfg_.contrast != 0) {
        double alpha = 1.0 + cfg_.contrast / 100.0;
        double beta  = static_cast<double>(cfg_.brightness);
        cv::convertScaleAbs(frame, result, alpha, beta);
    }
    if (cfg_.sharpness > 0) {
        float  strength = cfg_.sharpness / 100.0f;
        cv::Mat blurred;
        cv::GaussianBlur(result, blurred, {0, 0}, 3.0);
        cv::addWeighted(result, 1.0f + strength, blurred, -strength, 0.0, result);
    }
    if (cfg_.denoise > 0) {
        // 轻量高斯降噪：denoise 1-100 映射到 sigma 0.5-3.0
        // 避免使用 fastNlMeansDenoisingColored（耗时 300-500ms，不适合实时）
        double sigma = 0.5 + cfg_.denoise / 100.0 * 2.5;
        int ksize = static_cast<int>(sigma * 3) * 2 + 1;  // 奇数核，至少 1
        cv::GaussianBlur(result, result, cv::Size(ksize, ksize), sigma);
    }
    return result;
}

}  // namespace remote_link
