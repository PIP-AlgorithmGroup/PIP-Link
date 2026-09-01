#include "pip_link/backend/protocol_codec.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace pip_link::backend::protocol {
namespace {

void write_u16(std::uint8_t* output, std::uint16_t value) noexcept {
    output[0] = static_cast<std::uint8_t>(value);
    output[1] = static_cast<std::uint8_t>(value >> 8U);
}

void write_u32(std::uint8_t* output, std::uint32_t value) noexcept {
    output[0] = static_cast<std::uint8_t>(value);
    output[1] = static_cast<std::uint8_t>(value >> 8U);
    output[2] = static_cast<std::uint8_t>(value >> 16U);
    output[3] = static_cast<std::uint8_t>(value >> 24U);
}

void write_f64(std::uint8_t* output, double value) noexcept {
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
    write_u32(output, static_cast<std::uint32_t>(bits));
    write_u32(output + 4, static_cast<std::uint32_t>(bits >> 32U));
}

std::uint16_t read_u16(const std::uint8_t* input) noexcept {
    return static_cast<std::uint16_t>(input[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(input[1]) << 8U);
}

std::uint32_t read_u32(const std::uint8_t* input) noexcept {
    return static_cast<std::uint32_t>(input[0]) |
           (static_cast<std::uint32_t>(input[1]) << 8U) |
           (static_cast<std::uint32_t>(input[2]) << 16U) |
           (static_cast<std::uint32_t>(input[3]) << 24U);
}

double read_f64(const std::uint8_t* input) noexcept {
    const std::uint64_t bits = read_u32(input) |
                               (static_cast<std::uint64_t>(read_u32(input + 4)) << 32U);
    return std::bit_cast<double>(bits);
}

float read_f32(const std::uint8_t* input) noexcept {
    return std::bit_cast<float>(read_u32(input));
}

void write_header(std::uint8_t* output, MessageType type, std::uint32_t sequence) noexcept {
    write_u16(output, magic);
    output[2] = version;
    output[3] = static_cast<std::uint8_t>(type);
    output[4] = 0;
    write_u32(output + 5, sequence);
}

void seal(std::vector<std::uint8_t>& packet) noexcept {
    write_u32(packet.data() + packet.size() - 4,
              crc32(std::span<const std::uint8_t>{packet.data(), packet.size() - 4}));
}

template <typename T>
T clamp_integral(float value) noexcept {
    const float rounded = std::round(value);
    return static_cast<T>(std::clamp(
        rounded, static_cast<float>(std::numeric_limits<T>::min()),
        static_cast<float>(std::numeric_limits<T>::max())));
}

}  // namespace

std::uint32_t crc32(std::span<const std::uint8_t> bytes) noexcept {
    std::uint32_t value = 0xFFFFFFFFU;
    for (const std::uint8_t byte : bytes) {
        value ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = 0U - (value & 1U);
            value = (value >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return value ^ 0xFFFFFFFFU;
}

bool verify_crc(std::span<const std::uint8_t> packet) noexcept {
    if (packet.size() < common_header_size + 4 || read_u16(packet.data()) != magic ||
        packet[2] != version) {
        return false;
    }
    return read_u32(packet.data() + packet.size() - 4) ==
           crc32(packet.first(packet.size() - 4));
}

std::vector<std::uint8_t> heartbeat(std::uint32_t sequence, double timestamp) {
    std::vector<std::uint8_t> packet(21);
    write_header(packet.data(), MessageType::heartbeat, sequence);
    write_f64(packet.data() + 9, timestamp);
    seal(packet);
    return packet;
}

std::vector<std::uint8_t> control(std::uint32_t sequence, double timestamp,
                                  const ControlInput& input, bool enabled) {
    std::vector<std::uint8_t> packet(37);
    write_header(packet.data(), MessageType::control, sequence);
    if (enabled) packet[4] |= control_ready_flag;
    write_f64(packet.data() + 9, timestamp);
    if (enabled) {
        std::copy(input.keyboard.begin(), input.keyboard.end(), packet.begin() + 17);
        write_u16(packet.data() + 27,
                  static_cast<std::uint16_t>(clamp_integral<std::int16_t>(input.mouse_delta_x)));
        write_u16(packet.data() + 29,
                  static_cast<std::uint16_t>(clamp_integral<std::int16_t>(input.mouse_delta_y)));
        packet[31] = static_cast<std::uint8_t>(input.mouse_buttons & 0xFFU);
        packet[32] = static_cast<std::uint8_t>(std::clamp(
            input.mouse_wheel, static_cast<int>(std::numeric_limits<std::int8_t>::min()),
            static_cast<int>(std::numeric_limits<std::int8_t>::max())));
    }
    seal(packet);
    return packet;
}

std::vector<std::uint8_t> parameter_update(std::uint32_t sequence, double timestamp,
                                           std::string_view json) {
    std::vector<std::uint8_t> packet(common_header_size + 8 + json.size() + 4);
    write_header(packet.data(), MessageType::parameter_update, sequence);
    write_f64(packet.data() + 9, timestamp);
    std::memcpy(packet.data() + 17, json.data(), json.size());
    seal(packet);
    return packet;
}

std::vector<std::uint8_t> parameter_query(std::uint32_t sequence, double timestamp) {
    std::vector<std::uint8_t> packet(common_header_size + 8 + 4);
    write_header(packet.data(), MessageType::parameter_query, sequence);
    write_f64(packet.data() + 9, timestamp);
    seal(packet);
    return packet;
}

std::vector<std::uint8_t> video_ack(std::uint32_t frame_id) {
    std::vector<std::uint8_t> packet(common_header_size + 4);
    write_header(packet.data(), MessageType::video_acknowledgment, frame_id);
    seal(packet);
    return packet;
}

std::vector<std::uint8_t> video_nack(
    std::uint32_t frame_id, std::span<const std::uint16_t> missing_chunks) {
    const std::size_t count = std::min<std::size_t>(missing_chunks.size(), 0xFFFFU);
    std::vector<std::uint8_t> packet(common_header_size + 2 + count * 2 + 4);
    write_header(packet.data(), MessageType::video_negative_acknowledgment, frame_id);
    write_u16(packet.data() + 9, static_cast<std::uint16_t>(count));
    for (std::size_t index = 0; index < count; ++index) {
        write_u16(packet.data() + 11 + index * 2, missing_chunks[index]);
    }
    seal(packet);
    return packet;
}

bool parse_ack(std::span<const std::uint8_t> packet,
               Acknowledgment& acknowledgment) noexcept {
    if (packet.size() != 29 || packet[3] != static_cast<std::uint8_t>(MessageType::acknowledgment) ||
        !verify_crc(packet)) {
        return false;
    }
    acknowledgment.sequence = read_u32(packet.data() + 5);
    acknowledgment.remote_received_at = read_f64(packet.data() + 9);
    acknowledgment.remote_sent_at = read_f64(packet.data() + 17);
    return true;
}

bool parse_parameter_update(std::span<const std::uint8_t> packet,
                            std::uint32_t& sequence,
                            std::string& json) {
    constexpr std::size_t payload_offset = common_header_size + 8;
    if (packet.size() <= payload_offset + 4 ||
        packet[3] != static_cast<std::uint8_t>(MessageType::parameter_update) ||
        !verify_crc(packet)) {
        return false;
    }
    sequence = read_u32(packet.data() + 5);
    json.assign(reinterpret_cast<const char*>(packet.data() + payload_offset),
                packet.size() - payload_offset - 4);
    return true;
}

bool parse_video_chunk(std::span<const std::uint8_t> packet,
                       VideoChunkHeader& header,
                       std::span<const std::uint8_t>& payload) noexcept {
    if (packet.size() < video_header_size) return false;
    header.frame_id = read_u32(packet.data());
    header.total_chunks = read_u16(packet.data() + 4);
    header.chunk_index = read_u16(packet.data() + 6);
    header.chunk_size = read_u32(packet.data() + 8);
    header.parity = packet[12] != 0;
    header.original_chunks = read_u16(packet.data() + 13);
    header.codec = packet[15];
    header.encode_ms = read_f32(packet.data() + 16);
    if (header.frame_id == 0 || header.total_chunks == 0 || header.original_chunks == 0 ||
        header.original_chunks > header.total_chunks || header.chunk_index >= header.total_chunks ||
        header.chunk_size != packet.size() - video_header_size ||
        header.chunk_size > 60000U) {
        return false;
    }
    payload = packet.subspan(video_header_size);
    return true;
}

}  // namespace pip_link::backend::protocol
