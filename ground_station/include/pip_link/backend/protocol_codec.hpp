#pragma once

#include "pip_link/backend/ground_station_backend.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pip_link::backend::protocol {

constexpr std::uint16_t magic = 0xABCD;
constexpr std::uint8_t version = 1;
constexpr std::uint8_t control_ready_flag = 0x01U;
constexpr std::size_t common_header_size = 9;
constexpr std::size_t video_header_size = 20;
constexpr std::size_t max_video_chunks = 4096;
constexpr std::size_t max_encoded_frame_size = 16U * 1024U * 1024U;

enum class MessageType : std::uint8_t {
    control = 0x01,
    parameter_update = 0x02,
    parameter_query = 0x03,
    heartbeat = 0x04,
    acknowledgment = 0x05,
    video_acknowledgment = 0x06,
    video_negative_acknowledgment = 0x07,
};

struct Acknowledgment final {
    std::uint32_t sequence{};
    double remote_received_at{};
    double remote_sent_at{};
};

struct VideoChunkHeader final {
    std::uint32_t frame_id{};
    std::uint16_t total_chunks{};
    std::uint16_t chunk_index{};
    std::uint32_t chunk_size{};
    bool parity{};
    std::uint16_t original_chunks{};
    std::uint8_t codec{};
    float encode_ms{};
};

[[nodiscard]] std::uint32_t crc32(std::span<const std::uint8_t> bytes) noexcept;
[[nodiscard]] bool verify_crc(std::span<const std::uint8_t> packet) noexcept;
[[nodiscard]] std::vector<std::uint8_t> heartbeat(std::uint32_t sequence, double timestamp);
[[nodiscard]] std::vector<std::uint8_t> control(std::uint32_t sequence, double timestamp,
                                                const ControlInput& input, bool enabled);
[[nodiscard]] std::vector<std::uint8_t> parameter_update(std::uint32_t sequence,
                                                         double timestamp,
                                                         std::string_view json);
[[nodiscard]] std::vector<std::uint8_t> parameter_query(std::uint32_t sequence,
                                                        double timestamp);
[[nodiscard]] std::vector<std::uint8_t> video_ack(std::uint32_t frame_id);
[[nodiscard]] std::vector<std::uint8_t> video_nack(
    std::uint32_t frame_id, std::span<const std::uint16_t> missing_chunks);
[[nodiscard]] bool parse_ack(std::span<const std::uint8_t> packet,
                             Acknowledgment& acknowledgment) noexcept;
[[nodiscard]] bool parse_parameter_update(std::span<const std::uint8_t> packet,
                                          std::uint32_t& sequence,
                                          std::string& json);
[[nodiscard]] bool parse_video_chunk(std::span<const std::uint8_t> packet,
                                     VideoChunkHeader& header,
                                     std::span<const std::uint8_t>& payload) noexcept;

}  // namespace pip_link::backend::protocol
