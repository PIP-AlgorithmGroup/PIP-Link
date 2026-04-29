#include "remote_link/video_sender.hpp"
#include "remote_link/protocol_codec.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <algorithm>

namespace remote_link {

VideoSender::VideoSender(const Config& cfg, rclcpp::Logger logger)
: logger_(logger)
, cfg_(cfg)
, encoder_(std::make_unique<FrameEncoder>(cfg.encoder_cfg))
, window_start_(std::chrono::steady_clock::now())
{}

VideoSender::~VideoSender() { stop(); }

void VideoSender::start() {
    video_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (video_fd_ < 0) {
        RCLCPP_ERROR(logger_, "VideoSender: socket() failed: %s", strerror(errno));
        return;
    }

    int opt = 1;
    setsockopt(video_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Increase send buffer
    int sndbuf = 4 * 1024 * 1024;
    setsockopt(video_fd_, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(cfg_.port);

    if (bind(video_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        RCLCPP_ERROR(logger_, "VideoSender: bind port %u failed: %s", cfg_.port, strerror(errno));
        close(video_fd_); video_fd_ = -1; return;
    }
    RCLCPP_INFO(logger_, "VideoSender: listening on UDP port %u", cfg_.port);

    running_ = true;
    sender_thread_ = std::thread(&VideoSender::sender_loop, this);
}

void VideoSender::stop() {
    if (!running_.exchange(false)) return;
    frame_cv_.notify_all();
    if (sender_thread_.joinable()) sender_thread_.join();
    if (video_fd_ >= 0) { close(video_fd_); video_fd_ = -1; }
}

void VideoSender::push_frame(const cv::Mat& frame) {
    {
        std::lock_guard<std::mutex> lk(frame_mutex_);
        latest_frame_ = frame.clone();  // must clone: source may be freed after callback returns
        has_new_frame_ = true;
    }
    frame_cv_.notify_one();
}

void VideoSender::set_client_addr(const std::string& ip, uint16_t port) {
    std::lock_guard<std::mutex> lk(client_mutex_);
    std::memset(&client_addr_, 0, sizeof(client_addr_));
    client_addr_.sin_family = AF_INET;
    client_addr_.sin_port   = htons(port);
    inet_pton(AF_INET, ip.c_str(), &client_addr_.sin_addr);
    has_client_ = true;
    RCLCPP_INFO(logger_, "VideoSender: client REGISTERED %s:%u", ip.c_str(), port);
}

void VideoSender::clear_client_addr() {
    std::lock_guard<std::mutex> lk(client_mutex_);
    if (has_client_) {
        RCLCPP_WARN(logger_, "VideoSender: client timeout, clearing address");
    }
    has_client_ = false;
}

bool VideoSender::has_client() const {
    std::lock_guard<std::mutex> lk(client_mutex_);
    return has_client_;
}

void VideoSender::update_config(const Config& cfg) {
    std::lock_guard<std::mutex> lk(cfg_mutex_);
    cfg_ = cfg;
    encoder_->update_config(cfg.encoder_cfg);
}

VideoSender::Stats VideoSender::get_stats() const {
    std::lock_guard<std::mutex> lk(stats_mutex_);
    Stats s = stats_;
    s.current_jpeg_quality = encoder_->current_quality();
    return s;
}

void VideoSender::sender_loop() {
    auto next_frame_time = std::chrono::steady_clock::now();

    while (running_) {
        // 帧率限制：等到下一帧应发送的时刻
        auto now = std::chrono::steady_clock::now();
        if (now < next_frame_time) {
            std::unique_lock<std::mutex> lk(frame_mutex_);
            frame_cv_.wait_until(lk, next_frame_time,
                                 [this] { return !running_; });
            if (!running_) break;
        }

        cv::Mat frame;
        {
            std::unique_lock<std::mutex> lk(frame_mutex_);
            frame_cv_.wait_for(lk, std::chrono::milliseconds(100),
                               [this] { return has_new_frame_ || !running_; });
            if (!running_) break;
            if (!has_new_frame_) {
                lk.unlock();
                drain_recv_queue();
                continue;
            }
            frame = latest_frame_;  // cv::Mat is reference-counted; safe
            has_new_frame_ = false;
        }

        drain_recv_queue();

        {
            std::lock_guard<std::mutex> lk(client_mutex_);
            if (!has_client_) continue;
        }

        // 计算下一帧时间（基于当前 fps 参数）
        int fps;
        {
            std::lock_guard<std::mutex> lk(cfg_mutex_);
            fps = cfg_.encoder_cfg.fps > 0 ? cfg_.encoder_cfg.fps : 30;
        }
        next_frame_time = std::chrono::steady_clock::now()
                        + std::chrono::microseconds(1000000 / fps);

        send_frame(frame);
    }
}

void VideoSender::drain_recv_queue() {
    if (video_fd_ < 0) return;
    uint8_t buf[2048];
    struct sockaddr_in from{};
    socklen_t from_len = sizeof(from);

    while (true) {
        ssize_t n = recvfrom(video_fd_, buf, sizeof(buf), MSG_DONTWAIT,
                             reinterpret_cast<sockaddr*>(&from), &from_len);
        if (n <= 0) break;

        if (n == 8 && std::memcmp(buf, "REGISTER", 8) == 0) {
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
            set_client_addr(ip, ntohs(from.sin_port));
            continue;
        }

        if (static_cast<size_t>(n) < ProtocolCodec::HEADER_SIZE + 4) continue;
        int msg_type = ProtocolCodec::parse_type(buf, static_cast<size_t>(n));

        if (msg_type == 0x06) {  // VIDEO_ACK
            std::lock_guard<std::mutex> lk(stats_mutex_);
            stats_.frames_acked++;
        } else if (msg_type == 0x07) {  // VIDEO_NACK
            // frame_id is in Seq field (bytes 5-8)
            uint32_t frame_id;
            std::memcpy(&frame_id, buf + 5, 4);
            if (static_cast<size_t>(n) < 11) continue;
            uint16_t num_chunks;
            std::memcpy(&num_chunks, buf + 9, 2);
            std::vector<uint16_t> missing;
            missing.reserve(num_chunks);
            for (int i = 0; i < num_chunks; ++i) {
                size_t off = 11 + static_cast<size_t>(i) * 2;
                if (static_cast<size_t>(n) < off + 2) break;
                uint16_t idx;
                std::memcpy(&idx, buf + off, 2);
                missing.push_back(idx);
            }
            retransmit_nack(frame_id, missing);
        }
    }
}

void VideoSender::send_frame(const cv::Mat& frame) {
    if (video_fd_ < 0) return;

    uint8_t codec_flag;
    std::vector<uint8_t> encoded;
    bool fec_enabled;
    float fec_redundancy;
    {
        std::lock_guard<std::mutex> lk(cfg_mutex_);
        encoded = encoder_->encode(frame, (frame_id_ % 30 == 0), codec_flag);
        fec_enabled   = cfg_.fec_enabled;
        fec_redundancy = cfg_.fec_redundancy;
    }

    float encode_ms = encoder_->last_encode_ms();
    frame_id_++;

    const size_t CHUNK = ProtocolCodec::CHUNK_SIZE;
    const size_t total_data = encoded.size();
    const uint16_t n_data = static_cast<uint16_t>(
        (total_data + CHUNK - 1) / CHUNK);

    // FEC K=1 XOR: 对所有 data chunk 零填充到 CHUNK 后逐字节 XOR
    uint16_t k_parity = 0;
    std::vector<uint8_t> parity_chunk;
    if (fec_enabled && n_data > 0) {
        k_parity = static_cast<uint16_t>(
            std::max(1, static_cast<int>(std::ceil(n_data * fec_redundancy))));
        k_parity = 1;  // Phase 2: 只实现 K=1
        // parity 大小 = 最后一个 data chunk 的实际大小（不强制零填充到 CHUNK）
        size_t last_chunk_len = total_data - static_cast<size_t>(n_data - 1) * CHUNK;
        parity_chunk.assign(last_chunk_len, 0);
        for (uint16_t i = 0; i < n_data; ++i) {
            size_t offset = static_cast<size_t>(i) * CHUNK;
            size_t len    = std::min(CHUNK, total_data - offset);
            size_t parity_len = std::min(len, last_chunk_len);
            for (size_t b = 0; b < parity_len; ++b) {
                parity_chunk[b] ^= encoded[offset + b];
            }
        }
    }

    const uint16_t total_chunks = n_data + k_parity;

    struct sockaddr_in dest;
    {
        std::lock_guard<std::mutex> lk(client_mutex_);
        if (!has_client_) return;
        dest = client_addr_;
    }

    std::map<uint16_t, std::vector<uint8_t>> this_frame_cache;

    // 发送 data chunks
    for (uint16_t i = 0; i < n_data; ++i) {
        size_t offset    = static_cast<size_t>(i) * CHUNK;
        size_t chunk_len = std::min(CHUNK, total_data - offset);

        uint8_t hdr[ProtocolCodec::VIDEO_HDR_SIZE];
        ProtocolCodec::build_video_chunk_header(
            hdr, frame_id_, total_chunks, i,
            static_cast<uint32_t>(chunk_len),
            0,      // fec_flag: data
            n_data, // orig_chunks
            codec_flag, encode_ms);

        std::vector<uint8_t> pkt(ProtocolCodec::VIDEO_HDR_SIZE + chunk_len);
        std::memcpy(pkt.data(), hdr, ProtocolCodec::VIDEO_HDR_SIZE);
        std::memcpy(pkt.data() + ProtocolCodec::VIDEO_HDR_SIZE,
                    encoded.data() + offset, chunk_len);

        sendto(video_fd_, pkt.data(), pkt.size(), 0,
               reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));

        {
            std::lock_guard<std::mutex> lk(stats_mutex_);
            stats_.bytes_sent += pkt.size();
        }
        bytes_window_ += pkt.size();
        this_frame_cache[i] = std::move(pkt);
    }

    // 发送 parity chunk（K=1 XOR）
    if (k_parity > 0) {
        uint16_t parity_idx = n_data;
        uint8_t hdr[ProtocolCodec::VIDEO_HDR_SIZE];
        ProtocolCodec::build_video_chunk_header(
            hdr, frame_id_, total_chunks, parity_idx,
            static_cast<uint32_t>(parity_chunk.size()),
            1,      // fec_flag: parity
            n_data, // orig_chunks
            codec_flag, encode_ms);

        std::vector<uint8_t> pkt(ProtocolCodec::VIDEO_HDR_SIZE + parity_chunk.size());
        std::memcpy(pkt.data(), hdr, ProtocolCodec::VIDEO_HDR_SIZE);
        std::memcpy(pkt.data() + ProtocolCodec::VIDEO_HDR_SIZE,
                    parity_chunk.data(), parity_chunk.size());

        sendto(video_fd_, pkt.data(), pkt.size(), 0,
               reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));

        bytes_window_ += pkt.size();
        this_frame_cache[parity_idx] = std::move(pkt);
    }

    // 帧级别统计（在所有 chunk 发完后计一次）
    {
        std::lock_guard<std::mutex> lk(stats_mutex_);
        stats_.frames_sent++;
    }

    // Update frame cache for NACK retransmit
    {
        std::lock_guard<std::mutex> lk(cache_mutex_);
        frame_cache_[frame_id_] = std::move(this_frame_cache);
        while (static_cast<int>(frame_cache_.size()) > FRAME_CACHE_MAX) {
            frame_cache_.erase(frame_cache_.begin());
        }
    }

    // Bitrate window stats (every 5s)
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - window_start_).count();
    if (elapsed >= 5.0) {
        float kbps = static_cast<float>(bytes_window_.exchange(0)) * 8.0f
                   / static_cast<float>(elapsed) / 1000.0f;
        {
            std::lock_guard<std::mutex> lk(stats_mutex_);
            stats_.actual_bitrate_kbps = kbps;
        }
        RCLCPP_INFO(logger_, "VideoSender: %.0f kbps  sent=%lu  acked=%lu  quality=%d",
                    kbps, stats_.frames_sent, stats_.frames_acked,
                    encoder_->current_quality());
        window_start_ = now;
    }
}

void VideoSender::retransmit_nack(uint32_t frame_id,
                                   const std::vector<uint16_t>& missing)
{
    std::lock_guard<std::mutex> cache_lk(cache_mutex_);
    auto fit = frame_cache_.find(frame_id);
    if (fit == frame_cache_.end()) return;

    struct sockaddr_in dest;
    {
        std::lock_guard<std::mutex> lk(client_mutex_);
        if (!has_client_) return;
        dest = client_addr_;
    }

    for (uint16_t idx : missing) {
        auto cit = fit->second.find(idx);
        if (cit != fit->second.end()) {
            sendto(video_fd_,
                   cit->second.data(), cit->second.size(), 0,
                   reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
        }
    }
}

}  // namespace remote_link
