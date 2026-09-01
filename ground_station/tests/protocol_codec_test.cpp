#include "pip_link/backend/protocol_codec.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>
#include <vector>

namespace {

void write_u16(std::uint8_t* output, std::uint16_t value) {
    output[0] = static_cast<std::uint8_t>(value);
    output[1] = static_cast<std::uint8_t>(value >> 8U);
}

void write_u32(std::uint8_t* output, std::uint32_t value) {
    output[0] = static_cast<std::uint8_t>(value);
    output[1] = static_cast<std::uint8_t>(value >> 8U);
    output[2] = static_cast<std::uint8_t>(value >> 16U);
    output[3] = static_cast<std::uint8_t>(value >> 24U);
}

bool expect(bool condition, const char* message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
}

}  // namespace

int main() {
    using namespace pip_link::backend;
    using namespace pip_link::backend::protocol;

    const std::array<std::uint8_t, 9> crc_vector{'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    if (!expect(crc32(crc_vector) == 0xCBF43926U, "CRC-32 fixed vector mismatch")) return 1;

    ControlInput input{};
    input.keyboard = {1, 0, 1, 0, 1, 0, 1, 0, 1, 0};
    input.mouse_delta_x = 40000.0F;
    input.mouse_delta_y = -40000.0F;
    input.mouse_buttons = 0x15;
    input.mouse_wheel = 500;
    auto control_packet = control(0x12345678U, 42.5, input, true);
    if (!expect(control_packet.size() == 37 && verify_crc(control_packet),
                "control packet size or CRC mismatch")) return 1;
    if (!expect((control_packet[4] & control_ready_flag) != 0 &&
                    control_packet[5] == 0x78 && control_packet[8] == 0x12 &&
                    control_packet[27] == 0xFF && control_packet[28] == 0x7F &&
                    control_packet[29] == 0x00 && control_packet[30] == 0x80 &&
                    control_packet[31] == 0x15 && control_packet[32] == 0x7F,
                "control packet fields mismatch")) return 1;

    auto disabled_packet = control(1, 1.0, input, false);
    if (!expect((disabled_packet[4] & control_ready_flag) == 0 &&
                    std::all_of(disabled_packet.begin() + 17, disabled_packet.begin() + 33,
                            [](std::uint8_t value) { return value == 0; }),
                "disabled control packet must clear READY and be neutral")) return 1;

    auto json_packet = parameter_update(9, 3.25, R"({"target_fps":60})");
    if (!expect(json_packet.size() == 38 && verify_crc(json_packet),
                "parameter update mismatch")) return 1;

    auto query_packet = parameter_query(10, 3.5);
    if (!expect(query_packet.size() == 21 && verify_crc(query_packet) &&
                    query_packet[3] == static_cast<std::uint8_t>(MessageType::parameter_query),
                "parameter query mismatch")) return 1;

    std::uint32_t response_sequence = 0;
    std::string response_json;
    if (!expect(parse_parameter_update(json_packet, response_sequence, response_json) &&
                    response_sequence == 9 && response_json == R"({"target_fps":60})",
                "parameter response parser mismatch")) return 1;
    json_packet.back() ^= 1;
    if (!expect(!parse_parameter_update(json_packet, response_sequence, response_json),
                "corrupt parameter response accepted")) return 1;

    std::vector<std::uint8_t> ack(29);
    write_u16(ack.data(), magic);
    ack[2] = version;
    ack[3] = static_cast<std::uint8_t>(MessageType::acknowledgment);
    write_u32(ack.data() + 5, 77);
    const double t2 = 12.5;
    const double t3 = 12.75;
    std::memcpy(ack.data() + 9, &t2, sizeof(t2));
    std::memcpy(ack.data() + 17, &t3, sizeof(t3));
    write_u32(ack.data() + 25, crc32(std::span<const std::uint8_t>{ack.data(), 25}));
    Acknowledgment parsed{};
    if (!expect(parse_ack(ack, parsed) && parsed.sequence == 77 &&
                    parsed.remote_received_at == t2 && parsed.remote_sent_at == t3,
                "ACK parser mismatch")) return 1;
    ack.back() ^= 1;
    if (!expect(!parse_ack(ack, parsed), "corrupt ACK accepted")) return 1;

    const std::array<std::uint16_t, 3> missing{1, 4, 7};
    auto nack = video_nack(99, missing);
    if (!expect(nack.size() == 21 && verify_crc(nack), "video NACK mismatch")) return 1;

    std::vector<std::uint8_t> chunk(24);
    write_u32(chunk.data(), 8);
    write_u16(chunk.data() + 4, 2);
    write_u16(chunk.data() + 6, 1);
    write_u32(chunk.data() + 8, 4);
    chunk[12] = 1;
    write_u16(chunk.data() + 13, 1);
    chunk[15] = 0;
    const float encode_ms = 2.5F;
    std::memcpy(chunk.data() + 16, &encode_ms, sizeof(encode_ms));
    chunk[20] = 1; chunk[21] = 2; chunk[22] = 3; chunk[23] = 4;
    VideoChunkHeader header{};
    std::span<const std::uint8_t> payload;
    if (!expect(parse_video_chunk(chunk, header, payload) && header.frame_id == 8 &&
                    header.parity && payload.size() == 4 && payload[3] == 4,
                "video chunk parser mismatch")) return 1;
    chunk[8] = 5;
    if (!expect(!parse_video_chunk(chunk, header, payload), "invalid chunk accepted")) return 1;
    return 0;
}
