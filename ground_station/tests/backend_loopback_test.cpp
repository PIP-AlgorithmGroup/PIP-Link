#include "pip_link/backend/ground_station_backend.hpp"
#include "pip_link/backend/protocol_codec.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <span>
#include <thread>
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

std::uint32_t read_u32(const std::uint8_t* input) {
    return input[0] | static_cast<std::uint32_t>(input[1]) << 8U |
           static_cast<std::uint32_t>(input[2]) << 16U |
           static_cast<std::uint32_t>(input[3]) << 24U;
}

std::vector<std::uint8_t> acknowledgment(std::uint32_t sequence) {
    using namespace pip_link::backend::protocol;
    std::vector<std::uint8_t> packet(29);
    write_u16(packet.data(), magic);
    packet[2] = version;
    packet[3] = static_cast<std::uint8_t>(MessageType::acknowledgment);
    write_u32(packet.data() + 5, sequence);
    const double now = 1.0;
    std::memcpy(packet.data() + 9, &now, sizeof(now));
    std::memcpy(packet.data() + 17, &now, sizeof(now));
    write_u32(packet.data() + 25,
              crc32(std::span<const std::uint8_t>{packet.data(), 25}));
    return packet;
}

std::vector<std::uint8_t> video_chunk(std::uint32_t frame_id,
                                      std::uint16_t total_chunks,
                                      std::uint16_t chunk_index,
                                      std::span<const std::uint8_t> payload) {
    std::vector<std::uint8_t> packet(20 + payload.size());
    write_u32(packet.data(), frame_id);
    write_u16(packet.data() + 4, total_chunks);
    write_u16(packet.data() + 6, chunk_index);
    write_u32(packet.data() + 8, static_cast<std::uint32_t>(payload.size()));
    packet[12] = 0;
    write_u16(packet.data() + 13, total_chunks);
    packet[15] = 0;
    const float encode_ms = 1.5F;
    std::memcpy(packet.data() + 16, &encode_ms, sizeof(encode_ms));
    std::copy(payload.begin(), payload.end(), packet.begin() + 20);
    return packet;
}

bool wait_for(const std::function<bool()>& predicate,
              std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return predicate();
}

}  // namespace

int main() {
    bool failed = false;
    const std::filesystem::path output_directory = PIP_LINK_TEST_OUTPUT;
    std::error_code filesystem_error;
    std::filesystem::remove_all(output_directory, filesystem_error);
    WSADATA winsock{};
    if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) return 1;
    SOCKET server = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    SOCKET video_server = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(26000);
    sockaddr_in video_address = address;
    video_address.sin_port = htons(25000);
    if (server == INVALID_SOCKET || video_server == INVALID_SOCKET ||
        bind(server, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) ==
            SOCKET_ERROR ||
        bind(video_server, reinterpret_cast<const sockaddr*>(&video_address),
             sizeof(video_address)) == SOCKET_ERROR) {
        std::cerr << "unable to bind loopback fake air unit\n";
        return 1;
    }
    DWORD timeout = 100;
    setsockopt(server, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    setsockopt(video_server, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    std::ifstream image_file(PIP_LINK_TEST_IMAGE, std::ios::binary);
    std::vector<std::uint8_t> image{
        std::istreambuf_iterator<char>(image_file), std::istreambuf_iterator<char>()};
    if (image.empty()) {
        std::cerr << "test image is unavailable\n";
        return 1;
    }
    std::atomic_bool running{true};
    std::atomic_bool saw_enabled_control{false};
    std::atomic_bool sent_video{false};
    std::atomic_bool allow_video{false};
    std::mutex video_client_mutex;
    sockaddr_in registered_video_client{};
    int registered_video_client_length{};
    std::atomic_bool has_video_client{false};
    std::thread fake_air_unit([&] {
        std::array<std::uint8_t, 2048> buffer{};
        while (running) {
            sockaddr_in from{};
            int from_length = sizeof(from);
            const int count = recvfrom(server, reinterpret_cast<char*>(buffer.data()),
                                       static_cast<int>(buffer.size()), 0,
                                       reinterpret_cast<sockaddr*>(&from), &from_length);
            if (count == SOCKET_ERROR) continue;
            if (count < 13 || buffer[0] != 0xCD || buffer[1] != 0xAB) continue;
            const std::uint32_t sequence = read_u32(buffer.data() + 5);
            const auto ack = acknowledgment(sequence);
            sendto(server, reinterpret_cast<const char*>(ack.data()),
                   static_cast<int>(ack.size()), 0,
                   reinterpret_cast<const sockaddr*>(&from), from_length);
            if (buffer[3] == 0x01 && count == 37 && buffer[17] == 1 &&
                buffer[27] == 12 && buffer[29] == 0xF9 && buffer[30] == 0xFF) {
                saw_enabled_control = true;
            }
            sockaddr_in video_client{};
            int video_client_length = sizeof(video_client);
            const int video_count = recvfrom(
                video_server, reinterpret_cast<char*>(buffer.data()),
                static_cast<int>(buffer.size()), 0,
                reinterpret_cast<sockaddr*>(&video_client), &video_client_length);
            if (video_count == 8 && std::memcmp(buffer.data(), "REGISTER", 8) == 0) {
                std::lock_guard lock(video_client_mutex);
                registered_video_client = video_client;
                registered_video_client_length = video_client_length;
                has_video_client = true;
            }
            if (allow_video && has_video_client && !sent_video) {
                {
                    std::lock_guard lock(video_client_mutex);
                    video_client = registered_video_client;
                    video_client_length = registered_video_client_length;
                }
                constexpr std::size_t chunk_size = 60000;
                const std::uint16_t chunks = static_cast<std::uint16_t>(
                    (image.size() + chunk_size - 1) / chunk_size);
                std::vector<std::uint16_t> order;
                for (std::uint16_t index = 0; index < chunks; ++index) order.push_back(index);
                if (order.size() >= 2) std::swap(order[0], order[1]);
                for (const std::uint16_t index : order) {
                    const std::size_t offset = static_cast<std::size_t>(index) * chunk_size;
                    const auto packet = video_chunk(
                        1, chunks, index,
                        std::span<const std::uint8_t>{image}.subspan(
                            offset, std::min(chunk_size, image.size() - offset)));
                    sendto(video_server, reinterpret_cast<const char*>(packet.data()),
                           static_cast<int>(packet.size()), 0,
                           reinterpret_cast<const sockaddr*>(&video_client),
                           video_client_length);
                }
                sent_video = true;
            }
        }
    });

    {
        pip_link::backend::GroundStationBackendRuntime backend(nullptr, nullptr, nullptr);
        backend.apply_connection_settings(250, 1, 1400, false);
        backend.apply_video_settings(2, 3, 0, 0, 0, 60, 12000, false,
                                     0.2F, 0, 0, 0, 0, true, true);
        backend.connect_device({"Loopback", "127.0.0.1:26000", 100});
        if (!wait_for([&] {
                return backend.runtime_state().connection ==
                       pip_link::backend::ConnectionState::connected;
            }, std::chrono::seconds(2))) {
            std::cerr << "loopback handshake did not connect\n";
            running = false;
            fake_air_unit.join();
            closesocket(server);
            WSACleanup();
            return 1;
        }
        backend.start_recording(output_directory.string(), 2, 85, 0);
        if (backend.runtime_state().recording !=
            pip_link::backend::RecordingState::recording) {
            std::cerr << "raw stream recording did not start\n";
            failed = true;
        }
        allow_video = true;
        pip_link::backend::ControlInput input{};
        input.keyboard[0] = 1;
        input.mouse_delta_x = 12;
        input.mouse_delta_y = -7;
        backend.set_ready(true);
        backend.submit_control_input(input);
        if (!wait_for([&] { return saw_enabled_control.load(); },
                      std::chrono::seconds(1))) {
            std::cerr << "enabled control packet was not observed\n";
            failed = true;
        }
        backend.take_screenshot(output_directory.string());
        backend.stop_recording();
        bool has_screenshot = false;
        bool has_recording = false;
        for (const auto& item : std::filesystem::directory_iterator(output_directory)) {
            std::cerr << "output: " << item.path().string() << " size="
                      << item.file_size() << '\n';
            if (item.path().extension() == ".png" && item.file_size() > 0) {
                has_screenshot = true;
            }
            if (item.path().extension() == ".mjpeg" && item.file_size() > 0) {
                has_recording = true;
            }
        }
        if (!has_screenshot || !has_recording) {
            std::cerr << "screenshot or raw recording output is missing\n";
            failed = true;
        }
        if (!wait_for([&] { return backend.telemetry().decoded_frames > 0; },
                      std::chrono::seconds(3))) {
            std::cerr << "video frame was not reassembled and decoded\n";
            failed = true;
        }
        backend.disconnect_device();
        if (!wait_for([&] {
                return backend.runtime_state().connection ==
                       pip_link::backend::ConnectionState::disconnected;
            }, std::chrono::seconds(1))) {
            std::cerr << "disconnect did not complete\n";
            failed = true;
        }
    }
    running = false;
    fake_air_unit.join();
    closesocket(server);
    closesocket(video_server);
    WSACleanup();
    if (!failed) std::filesystem::remove_all(output_directory, filesystem_error);
    return failed ? 1 : 0;
}
