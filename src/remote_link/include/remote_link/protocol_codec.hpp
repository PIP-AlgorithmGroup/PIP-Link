#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace remote_link {

class ProtocolCodec {
public:
    static constexpr uint16_t MAGIC          = 0xABCD;
    static constexpr uint8_t  VERSION        = 0x01;
    static constexpr size_t   HEADER_SIZE    = 9;
    static constexpr size_t   VIDEO_HDR_SIZE = 20;
    static constexpr size_t   CHUNK_SIZE     = 60000;

    // Returns msg_type (0x01~0x07); returns -1 on magic/CRC failure
    static int  parse_type(const uint8_t* data, size_t len);
    static bool verify_crc(const uint8_t* data, size_t len);

    static bool parse_control_command(
        const uint8_t* data, size_t len,
        uint32_t& seq, double& t1,
        uint8_t  keyboard_state[10],
        int16_t& mouse_dx, int16_t& mouse_dy,
        uint8_t& mouse_buttons, int8_t& scroll_delta);

    static bool parse_param_update(
        const uint8_t* data, size_t len,
        uint32_t& seq, std::string& json_payload);

    // heartbeat: just need seq for ACK
    static bool parse_heartbeat(
        const uint8_t* data, size_t len,
        uint32_t& seq);

    // buf must be >= 29 bytes; returns bytes written
    static size_t build_ack(uint8_t* buf, size_t buf_size,
                             uint32_t seq, double t2, double t3);

    // buf must be >= VIDEO_HDR_SIZE bytes; returns bytes written
    static size_t build_video_chunk_header(
        uint8_t* buf,
        uint32_t frame_id, uint16_t total_chunks, uint16_t chunk_idx,
        uint32_t chunk_size, uint8_t fec_flag, uint16_t orig_chunks,
        uint8_t codec_flag, float encode_ms);

    // buf must be large enough; returns bytes written
    static size_t build_param_response(
        uint8_t* buf, size_t buf_size,
        uint32_t seq, const std::string& json_params);

private:
    static uint32_t calc_crc(const uint8_t* data, size_t len);
};

}  // namespace remote_link
