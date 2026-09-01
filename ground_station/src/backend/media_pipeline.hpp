#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace pip_link::backend::media {

struct DecodedFrame final {
    int width{};
    int height{};
    std::vector<std::uint8_t> bgra;
};

class FrameDecoder final {
public:
    FrameDecoder();
    ~FrameDecoder();
    FrameDecoder(const FrameDecoder&) = delete;
    FrameDecoder& operator=(const FrameDecoder&) = delete;

    [[nodiscard]] bool decode(std::uint8_t codec, std::span<const std::uint8_t> encoded,
                              DecodedFrame& output, std::string& error);
    void set_h264_preference(int preference) noexcept;
    void reset();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

class StreamRecorder final {
public:
    StreamRecorder();
    ~StreamRecorder();
    StreamRecorder(const StreamRecorder&) = delete;
    StreamRecorder& operator=(const StreamRecorder&) = delete;

    [[nodiscard]] bool start(const std::filesystem::path& directory, int format_index,
                             int quality, int split_minutes, std::uint8_t codec,
                             int frame_rate,
                             std::string& error);
    void write(std::uint8_t codec, std::span<const std::uint8_t> frame);
    void stop();
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] bool healthy() const;
    [[nodiscard]] std::string last_error() const;
    [[nodiscard]] std::filesystem::path output_path() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] bool save_png(const std::filesystem::path& path, const DecodedFrame& frame,
                            std::string& error);

}  // namespace pip_link::backend::media
