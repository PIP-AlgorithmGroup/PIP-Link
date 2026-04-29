#include "remote_link/protocol_codec.hpp"

#include <cstring>
#include <zlib.h>

namespace remote_link {

// little-endian read helpers
static inline uint16_t read_u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}
static inline uint32_t read_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16)
         | (static_cast<uint32_t>(p[3]) << 24);
}
static inline int16_t read_i16(const uint8_t* p) {
    return static_cast<int16_t>(read_u16(p));
}
static inline double read_f64(const uint8_t* p) {
    double v; std::memcpy(&v, p, 8); return v;
}
static inline float read_f32(const uint8_t* p) {
    float v; std::memcpy(&v, p, 4); return v;
}

static inline void write_u16(uint8_t* p, uint16_t v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF;
}
static inline void write_u32(uint8_t* p, uint32_t v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
}
static inline void write_f64(uint8_t* p, double v) { std::memcpy(p, &v, 8); }
static inline void write_f32(uint8_t* p, float v)  { std::memcpy(p, &v, 4); }

uint32_t ProtocolCodec::calc_crc(const uint8_t* data, size_t len) {
    return crc32(0, data, static_cast<uInt>(len)) & 0xFFFFFFFF;
}

bool ProtocolCodec::verify_crc(const uint8_t* data, size_t len) {
    if (len < HEADER_SIZE + 4) return false;
    uint32_t expected = calc_crc(data, len - 4);
    uint32_t received = read_u32(data + len - 4);
    return expected == received;
}

int ProtocolCodec::parse_type(const uint8_t* data, size_t len) {
    if (len < HEADER_SIZE + 4) return -1;
    if (read_u16(data) != MAGIC) return -1;
    if (!verify_crc(data, len)) return -1;
    return data[3];  // MsgType byte
}

bool ProtocolCodec::parse_control_command(
    const uint8_t* data, size_t len,
    uint32_t& seq, double& t1,
    uint8_t keyboard_state[10],
    int16_t& mouse_dx, int16_t& mouse_dy,
    uint8_t& mouse_buttons, int8_t& scroll_delta)
{
    // CONTROL_COMMAND: Header(9) + t1(8) + kb(10) + dx(2) + dy(2) + btn(1) + scroll(1) + CRC(4) = 37B
    if (len < 37) return false;
    if (read_u16(data) != MAGIC || data[3] != 0x01) return false;
    if (!verify_crc(data, len)) return false;

    seq = read_u32(data + 5);
    t1  = read_f64(data + 9);
    std::memcpy(keyboard_state, data + 17, 10);
    mouse_dx      = read_i16(data + 27);
    mouse_dy      = read_i16(data + 29);
    mouse_buttons = data[31];
    scroll_delta  = static_cast<int8_t>(data[32]);
    return true;
}

bool ProtocolCodec::parse_param_update(
    const uint8_t* data, size_t len,
    uint32_t& seq, std::string& json_payload)
{
    // PARAM_UPDATE: Header(9) + t1(8) + JSON + CRC(4); min = 9+8+4 = 21B
    if (len < 21) return false;
    if (read_u16(data) != MAGIC || data[3] != 0x02) return false;
    if (!verify_crc(data, len)) return false;

    seq = read_u32(data + 5);
    // JSON payload: bytes [17, len-4)
    const size_t json_start = 17;
    const size_t json_len   = len - 4 - json_start;
    if (json_len == 0) return false;
    json_payload.assign(reinterpret_cast<const char*>(data + json_start), json_len);
    return true;
}

bool ProtocolCodec::parse_heartbeat(
    const uint8_t* data, size_t len, uint32_t& seq)
{
    // HEARTBEAT: Header(9) + t1(8) + CRC(4) = 21B
    if (len < 21) return false;
    if (read_u16(data) != MAGIC || data[3] != 0x04) return false;
    if (!verify_crc(data, len)) return false;
    seq = read_u32(data + 5);
    return true;
}

size_t ProtocolCodec::build_ack(
    uint8_t* buf, size_t buf_size,
    uint32_t seq, double t2, double t3)
{
    // ACK: Header(9) + t2(8) + t3(8) + CRC(4) = 29B
    constexpr size_t MSG_SIZE = 29;
    if (buf_size < MSG_SIZE) return 0;

    write_u16(buf,     MAGIC);
    buf[2] = VERSION;
    buf[3] = 0x05;   // ACK
    buf[4] = 0x00;   // reserved
    write_u32(buf + 5, seq);
    write_f64(buf + 9,  t2);
    write_f64(buf + 17, t3);
    uint32_t crc = calc_crc(buf, MSG_SIZE - 4);
    write_u32(buf + 25, crc);
    return MSG_SIZE;
}

size_t ProtocolCodec::build_video_chunk_header(
    uint8_t* buf,
    uint32_t frame_id, uint16_t total_chunks, uint16_t chunk_idx,
    uint32_t chunk_size, uint8_t fec_flag, uint16_t orig_chunks,
    uint8_t codec_flag, float encode_ms)
{
    // [frame_id:4][total_chunks:2][chunk_idx:2][chunk_size:4]
    // [fec_flag:1][orig_chunks:2][codec_flag:1][encode_ms:4]  = 20B
    write_u32(buf + 0,  frame_id);
    write_u16(buf + 4,  total_chunks);
    write_u16(buf + 6,  chunk_idx);
    write_u32(buf + 8,  chunk_size);
    buf[12] = fec_flag;
    write_u16(buf + 13, orig_chunks);
    buf[15] = codec_flag;
    write_f32(buf + 16, encode_ms);
    return VIDEO_HDR_SIZE;
}

size_t ProtocolCodec::build_param_response(
    uint8_t* buf, size_t buf_size,
    uint32_t seq, const std::string& json_params)
{
    // PARAM_UPDATE response: Header(9) + t1(8) + JSON + CRC(4)
    const size_t total = 9 + 8 + json_params.size() + 4;
    if (buf_size < total) return 0;

    double t1_now = 0.0;  // caller can set; 0 is acceptable
    write_u16(buf,     MAGIC);
    buf[2] = VERSION;
    buf[3] = 0x02;   // PARAM_UPDATE
    buf[4] = 0x00;
    write_u32(buf + 5, seq);
    write_f64(buf + 9, t1_now);
    std::memcpy(buf + 17, json_params.data(), json_params.size());
    uint32_t crc = calc_crc(buf, total - 4);
    write_u32(buf + total - 4, crc);
    return total;
}

}  // namespace remote_link
