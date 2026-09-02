#include "pip_link/backend/ground_station_backend.hpp"

#include "media_pipeline.hpp"
#include "pip_link/backend/protocol_codec.hpp"
#include "pip_link/core/build_info.hpp"
#include "pip_link/core/media_output_path.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windns.h>
#include <d3d11.h>
#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <charconv>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <utility>

namespace pip_link::backend {
namespace {

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

constexpr std::uint16_t default_control_port = 6000;
constexpr std::uint16_t default_video_port = 5000;
constexpr std::size_t max_audit_entries = 10000;
constexpr std::array<int, 4> jpeg_quality_levels{55, 70, 85, 95};
constexpr int minimum_video_bitrate_kbps = 100;
constexpr int maximum_video_bitrate_kbps = 80000;
constexpr float minimum_fec_redundancy = 0.0F;
constexpr float maximum_fec_redundancy = 1.0F;
constexpr auto video_sequence_reset_timeout = 750ms;

template <typename T>
void release(T*& pointer) noexcept {
    if (pointer != nullptr) {
        pointer->Release();
        pointer = nullptr;
    }
}

double monotonic_seconds() noexcept {
    return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}

std::string local_time(const char* format) {
    const std::time_t value = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &value);
    std::ostringstream stream;
    stream << std::put_time(&local, format);
    return stream.str();
}

std::wstring utf16(std::string_view value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), size);
    return result;
}

media::DecodedFrame rgba_to_bgra_for_png(const media::DecodedFrame& rgba) {
    media::DecodedFrame bgra = rgba;
    for (std::size_t offset = 0; offset + 3 < bgra.bgra.size(); offset += 4) {
        std::swap(bgra.bgra[offset], bgra.bgra[offset + 2]);
    }
    return bgra;
}

std::string utf8(std::wstring_view value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0,
                                         nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), size,
                        nullptr, nullptr);
    return result;
}

std::string com_error(const char* action, HRESULT result) {
    std::ostringstream stream;
    stream << action << " (HRESULT=0x" << std::hex << std::uppercase
           << static_cast<unsigned long>(result) << ')';
    return stream.str();
}

std::filesystem::path application_data_directory() {
    wchar_t buffer[MAX_PATH]{};
    const DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    std::filesystem::path result = size > 0 && size < MAX_PATH
                                       ? std::filesystem::path(buffer)
                                       : std::filesystem::current_path();
    result /= L"PIP-Link";
    std::error_code error;
    std::filesystem::create_directories(result, error);
    return result;
}

std::filesystem::path media_output_directory(std::string_view configured_directory) {
    PWSTR known_directory = nullptr;
    std::filesystem::path videos_directory;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Videos, KF_FLAG_CREATE, nullptr,
                                       &known_directory))) {
        videos_directory = known_directory;
        CoTaskMemFree(known_directory);
    } else {
        videos_directory = application_data_directory() / L"media";
    }
    return core::resolve_media_output_directory(
        std::filesystem::path{utf16(configured_directory)}, videos_directory);
}

std::string json_escape(std::string_view value) {
    std::string result;
    result.reserve(value.size() + 8);
    for (const char character : value) {
        switch (character) {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += character; break;
        }
    }
    return result;
}

std::optional<std::string> json_string_field(std::string_view line,
                                             std::string_view field) {
    const std::string marker = "\"" + std::string(field) + "\":\"";
    const std::size_t start = line.find(marker);
    if (start == std::string_view::npos) return {};
    std::string result;
    bool escaped = false;
    for (std::size_t index = start + marker.size(); index < line.size(); ++index) {
        const char character = line[index];
        if (escaped) {
            switch (character) {
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                default: result += character; break;
            }
            escaped = false;
        } else if (character == '\\') {
            escaped = true;
        } else if (character == '"') {
            return result;
        } else {
            result += character;
        }
    }
    return {};
}

std::optional<std::string_view> json_scalar_field(std::string_view json,
                                                  std::string_view field) {
    const std::string marker = "\"" + std::string(field) + "\"";
    std::size_t position = json.find(marker);
    if (position == std::string_view::npos) return {};
    position = json.find(':', position + marker.size());
    if (position == std::string_view::npos) return {};
    position = json.find_first_not_of(" \t\r\n", position + 1);
    if (position == std::string_view::npos) return {};
    if (json[position] == '"') {
        const std::size_t end = json.find('"', position + 1);
        if (end == std::string_view::npos) return {};
        return json.substr(position + 1, end - position - 1);
    }
    const std::size_t end = json.find_first_of(",}", position);
    if (end == std::string_view::npos) return {};
    std::size_t trimmed_end = end;
    while (trimmed_end > position && std::isspace(
               static_cast<unsigned char>(json[trimmed_end - 1]))) {
        --trimmed_end;
    }
    return json.substr(position, trimmed_end - position);
}

template <typename T>
std::optional<T> json_number_field(std::string_view json, std::string_view field) {
    const auto token = json_scalar_field(json, field);
    if (!token) return {};
    T value{};
    const auto result = std::from_chars(token->data(), token->data() + token->size(), value);
    if (result.ec != std::errc{} || result.ptr != token->data() + token->size()) return {};
    return value;
}

std::optional<bool> json_boolean_field(std::string_view json, std::string_view field) {
    const auto token = json_scalar_field(json, field);
    if (token == "true") return true;
    if (token == "false") return false;
    return {};
}

std::string csv_escape(std::string_view value) {
    std::string result{"\""};
    for (const char character : value) {
        if (character == '"') result += '"';
        result += character;
    }
    result += '"';
    return result;
}

std::string trim(std::string value) {
    const auto not_space = [](unsigned char character) { return !std::isspace(character); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

struct Endpoint final {
    sockaddr_in control{};
    sockaddr_in video{};
    std::string display;
};

std::optional<std::pair<std::string, std::uint16_t>> split_endpoint(
    std::string_view text) {
    const std::size_t colon = text.rfind(':');
    if (colon == std::string_view::npos || colon == 0 || colon + 1 >= text.size()) return {};
    const std::string host{text.substr(0, colon)};
    try {
        const unsigned long port = std::stoul(std::string{text.substr(colon + 1)});
        if (port == 0 || port > 65535) return {};
        return std::pair{host, static_cast<std::uint16_t>(port)};
    } catch (...) {
        return {};
    }
}

std::optional<Endpoint> resolve_endpoint(const DeviceInfo& device,
                                         std::uint16_t video_port_override,
                                         std::string& error) {
    const auto split = split_endpoint(device.address);
    if (!split) {
        error = "地址格式无效，请使用 IP:端口";
        return {};
    }
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* results = nullptr;
    const std::string port = std::to_string(split->second);
    const int status = getaddrinfo(split->first.c_str(), port.c_str(), &hints, &results);
    if (status != 0 || results == nullptr) {
        error = "无法解析机器人地址: " + std::to_string(status);
        return {};
    }
    Endpoint endpoint{};
    endpoint.control = *reinterpret_cast<sockaddr_in*>(results->ai_addr);
    endpoint.video = endpoint.control;
    const std::uint16_t video_port = video_port_override != 0
        ? video_port_override
        : (split->second >= 1000 ? static_cast<std::uint16_t>(split->second - 1000)
                                 : default_video_port);
    endpoint.video.sin_port = htons(video_port);
    endpoint.display = device.address;
    freeaddrinfo(results);
    return endpoint;
}

void set_nonblocking(SOCKET socket) {
    u_long enabled = 1;
    ioctlsocket(socket, FIONBIO, &enabled);
}

struct VideoAssembly final {
    std::uint16_t total_chunks{};
    std::uint16_t original_chunks{};
    std::uint8_t codec{};
    float encode_ms{};
    std::vector<std::vector<std::uint8_t>> chunks;
    std::vector<bool> received;
    std::size_t received_bytes{};
    Clock::time_point first_seen{Clock::now()};
    Clock::time_point last_nack{};
    int nack_count{};
};

struct PendingPacket final {
    std::vector<std::uint8_t> bytes;
    Clock::time_point sent_at{};
    int retries{};
};

struct DisplaySnapshot final {
    int x{};
    int y{};
    int width{};
    int height{};
    bool fullscreen{};
    bool valid{};
};

using RuntimeConfig = BackendPreferences;

}  // namespace

class GroundStationBackendRuntime::Impl final {
public:
    Impl(SDL_Window* window, ID3D11Device* device, ID3D11DeviceContext* context)
        : window_(window), device_(device), context_(context),
          data_directory_(application_data_directory()),
          settings_path_(data_directory_ / "settings.ini"),
          audit_path_(data_directory_ / "audit.jsonl") {
        const int winsock_result = WSAStartup(MAKEWORD(2, 2), &winsock_);
        winsock_ready_ = winsock_result == 0;
        load_config();
        sanitize_config();
        low_latency_ = config_.low_latency;
        load_audit();
        decoder_.set_h264_preference(config_.decoder_index);
        append_audit("INFO", winsock_ready_ ? "地面端后端已启动" : "Winsock 初始化失败");
        network_thread_ = std::thread([this] { network_loop(); });
        decoder_thread_ = std::thread([this] { decoder_loop(); });
    }

    ~Impl() {
        running_ = false;
        request_cv_.notify_all();
        decode_cv_.notify_all();
        if (decoder_thread_.joinable()) CancelSynchronousIo(decoder_thread_.native_handle());
        stop_discovery();
        if (network_thread_.joinable()) network_thread_.join();
        if (decoder_thread_.joinable()) decoder_thread_.join();
        recorder_.stop();
        close_session();
        release(video_view_);
        release(video_texture_);
        if (winsock_ready_) WSACleanup();
    }

    void append_audit(std::string level, std::string message) {
        const AuditEntry entry{local_time("%Y-%m-%d %H:%M:%S"), std::move(level),
                               std::move(message)};
        std::lock_guard lock(audit_mutex_);
        audit_entries_.push_back(entry);
        if (audit_entries_.size() > max_audit_entries) audit_entries_.pop_front();
        std::ofstream output(audit_path_, std::ios::app | std::ios::binary);
        if (output) {
            output << "{\"time\":\"" << json_escape(entry.time)
                   << "\",\"level\":\"" << json_escape(entry.level)
                   << "\",\"message\":\"" << json_escape(entry.message) << "\"}\n";
        }
    }

    void load_audit() {
        std::ifstream input(audit_path_, std::ios::binary);
        std::string line;
        while (std::getline(input, line)) {
            auto time = json_string_field(line, "time");
            auto level = json_string_field(line, "level");
            auto message = json_string_field(line, "message");
            if (!time || !level || !message) continue;
            audit_entries_.push_back({std::move(*time), std::move(*level),
                                      std::move(*message)});
            if (audit_entries_.size() > max_audit_entries) audit_entries_.pop_front();
        }
    }

    void load_config() {
        std::ifstream input(settings_path_, std::ios::binary);
        std::string line;
        while (std::getline(input, line)) {
            const auto separator = line.find('=');
            if (separator == std::string::npos) continue;
            const std::string key = trim(line.substr(0, separator));
            const std::string value = trim(line.substr(separator + 1));
            try {
                if (key == "heartbeat_ms") config_.heartbeat_ms = std::stoi(value);
                else if (key == "reconnect_seconds") config_.reconnect_seconds = std::stoi(value);
                else if (key == "mtu") config_.mtu = std::stoi(value);
                else if (key == "auto_reconnect") config_.auto_reconnect = std::stoi(value) != 0;
                else if (key == "encoder_index") config_.encoder_index = std::stoi(value);
                else if (key == "decoder_index") config_.decoder_index = std::stoi(value);
                else if (key == "frame_rate") config_.frame_rate = std::stoi(value);
                else if (key == "bitrate_kbps") config_.bitrate_kbps = std::stoi(value);
                else if (key == "fec_enabled") config_.fec_enabled = std::stoi(value) != 0;
                else if (key == "fec_redundancy") config_.fec_redundancy = std::stof(value);
                else if (key == "resolution_index") config_.resolution_index = std::stoi(value);
                else if (key == "window_mode") config_.window_mode = std::stoi(value);
                else if (key == "display_index") config_.display_index = std::stoi(value);
                else if (key == "display_configured") {
                    config_.display_configured = std::stoi(value) != 0;
                }
                else if (key == "mouse_sensitivity") config_.mouse_sensitivity = std::stof(value);
                else if (key == "field_of_view") config_.field_of_view = std::stof(value);
                else if (key == "invert_pitch") config_.invert_pitch = std::stoi(value) != 0;
                else if (key == "quality_index") {
                    config_.quality_index = std::clamp(std::stoi(value), 0, 3);
                    config_.jpeg_quality = jpeg_quality_levels[
                        static_cast<std::size_t>(config_.quality_index)];
                }
                else if (key == "jpeg_quality") config_.jpeg_quality = std::stoi(value);
                else if (key == "brightness") config_.brightness = std::stoi(value);
                else if (key == "contrast") config_.contrast = std::stoi(value);
                else if (key == "sharpness") config_.sharpness = std::stoi(value);
                else if (key == "denoise") config_.denoise = std::stoi(value);
                else if (key == "low_latency") config_.low_latency = std::stoi(value) != 0;
                else if (key == "vertical_sync") config_.vertical_sync = std::stoi(value) != 0;
                else if (key == "invert_y") config_.invert_y = std::stoi(value) != 0;
                else if (key == "capture_mouse") config_.capture_mouse = std::stoi(value) != 0;
                else if (key == "send_keyboard") config_.send_keyboard = std::stoi(value) != 0;
                else if (key == "hud_scale") config_.hud_scale = std::stof(value);
                else if (key == "hud_opacity") config_.hud_opacity = std::stof(value);
                else if (key == "show_input") config_.show_input = std::stoi(value) != 0;
                else if (key == "show_statistics") config_.show_statistics = std::stoi(value) != 0;
                else if (key == "show_ready") config_.show_ready = std::stoi(value) != 0;
                else if (key == "language_index") config_.language_index = std::stoi(value);
                else if (key == "show_performance_graph") {
                    config_.show_performance_graph = std::stoi(value) != 0;
                } else if (key == "show_debug_info") {
                    config_.show_debug_info = std::stoi(value) != 0;
                } else if (key == "verbose_log") {
                    config_.verbose_log = std::stoi(value) != 0;
                } else if (key == "gamepad_deadzone") config_.gamepad_deadzone = std::stof(value);
                else if (key == "gamepad_vibration") {
                    config_.gamepad_vibration = std::stoi(value) != 0;
                } else if (key == "recording_format") {
                    config_.recording_format = std::stoi(value);
                } else if (key == "recording_quality") {
                    config_.recording_quality = std::stoi(value);
                } else if (key == "split_minutes") {
                    config_.split_minutes = std::stoi(value);
                } else if (key == "service_name") {
                    config_.service_name = value;
                } else if (key == "last_endpoint") {
                    config_.last_endpoint = value;
                } else if (key == "last_video_port") {
                    config_.last_video_port = std::stoi(value);
                } else if (key == "recording_directory") {
                    config_.recording_directory = value;
                } else if (key.starts_with("key_")) {
                    const std::size_t index = std::stoul(key.substr(4));
                    if (config_.key_bindings.size() <= index) {
                        config_.key_bindings.resize(index + 1);
                    }
                    config_.key_bindings[index] = std::stoi(value);
                }
            } catch (...) {
                // Ignore one malformed property and preserve all valid properties.
            }
        }
    }

    void sanitize_config() {
        const RuntimeConfig defaults{};
        config_.heartbeat_ms = std::clamp(config_.heartbeat_ms, 250, 5000);
        config_.reconnect_seconds = std::clamp(config_.reconnect_seconds, 1, 30);
        config_.mtu = std::clamp(config_.mtu, 576, 1500);
        config_.mouse_sensitivity = std::isfinite(config_.mouse_sensitivity)
            ? std::clamp(config_.mouse_sensitivity, 0.05F, 5.0F) : defaults.mouse_sensitivity;
        config_.field_of_view = std::isfinite(config_.field_of_view)
            ? std::clamp(config_.field_of_view, 50.0F, 130.0F) : defaults.field_of_view;
        config_.quality_index = std::clamp(config_.quality_index, 0, 3);
        config_.jpeg_quality = std::clamp(config_.jpeg_quality, 1, 100);
        config_.resolution_index = std::clamp(config_.resolution_index, 0, 5);
        config_.window_mode = std::clamp(config_.window_mode, 0, 1);
        config_.display_index = std::max(config_.display_index, 0);
        config_.encoder_index = std::clamp(config_.encoder_index, 0, 1);
        config_.decoder_index = std::clamp(config_.decoder_index, 0, 2);
        config_.frame_rate = std::clamp(config_.frame_rate, 24, 240);
        config_.bitrate_kbps = std::clamp(
            config_.bitrate_kbps, minimum_video_bitrate_kbps, maximum_video_bitrate_kbps);
        config_.fec_redundancy = std::isfinite(config_.fec_redundancy)
            ? std::clamp(config_.fec_redundancy, 0.0F, 1.0F) : defaults.fec_redundancy;
        config_.brightness = std::clamp(config_.brightness, -100, 100);
        config_.contrast = std::clamp(config_.contrast, -100, 100);
        config_.sharpness = std::clamp(config_.sharpness, 0, 100);
        config_.denoise = std::clamp(config_.denoise, 0, 100);
        config_.hud_scale = std::isfinite(config_.hud_scale)
            ? std::clamp(config_.hud_scale, 0.75F, 1.5F) : defaults.hud_scale;
        config_.hud_opacity = std::isfinite(config_.hud_opacity)
            ? std::clamp(config_.hud_opacity, 0.35F, 1.0F) : defaults.hud_opacity;
        config_.language_index = std::max(config_.language_index, 0);
        config_.gamepad_deadzone = std::isfinite(config_.gamepad_deadzone)
            ? std::clamp(config_.gamepad_deadzone, 0.0F, 0.5F) : defaults.gamepad_deadzone;
        config_.recording_format = std::clamp(config_.recording_format, 0, 2);
        config_.recording_quality = std::clamp(config_.recording_quality, 1, 100);
        config_.split_minutes = std::clamp(config_.split_minutes, 0, 120);
        config_.last_video_port = std::clamp(config_.last_video_port, 1, 65535);
        if (config_.service_name.empty()) config_.service_name = defaults.service_name;
        if (config_.last_endpoint.empty()) config_.last_endpoint = defaults.last_endpoint;
        if (config_.recording_directory.empty()) {
            config_.recording_directory = defaults.recording_directory;
        }
        if (config_.key_bindings.size() == 11) {
            config_.key_bindings.resize(15, ImGuiKey_None);
            config_.key_bindings[11] = ImGuiKey_F9;
            config_.key_bindings[12] = ImGuiKey_F10;
        }
        if (config_.key_bindings.size() != 15 ||
            std::any_of(config_.key_bindings.begin(), config_.key_bindings.end(), [](int key) {
                return key != ImGuiKey_None &&
                       (key < ImGuiKey_NamedKey_BEGIN || key >= ImGuiKey_COUNT);
            })) {
            config_.key_bindings.clear();
        }
    }

    void save_config() {
        RuntimeConfig copy;
        {
            std::lock_guard lock(config_mutex_);
            copy = config_;
        }
        const std::filesystem::path temporary = settings_path_.string() + ".tmp";
        std::ofstream output(temporary, std::ios::trunc | std::ios::binary);
        if (!output) {
            append_audit("ERROR", "无法写入设置文件");
            return;
        }
        output << "heartbeat_ms=" << copy.heartbeat_ms << '\n'
               << "reconnect_seconds=" << copy.reconnect_seconds << '\n'
               << "mtu=" << copy.mtu << '\n'
               << "auto_reconnect=" << copy.auto_reconnect << '\n'
               << "mouse_sensitivity=" << copy.mouse_sensitivity << '\n'
               << "field_of_view=" << copy.field_of_view << '\n'
               << "invert_pitch=" << copy.invert_pitch << '\n'
               << "quality_index=" << copy.quality_index << '\n'
               << "jpeg_quality=" << copy.jpeg_quality << '\n'
               << "resolution_index=" << copy.resolution_index << '\n'
               << "window_mode=" << copy.window_mode << '\n'
               << "display_index=" << copy.display_index << '\n'
               << "display_configured=" << copy.display_configured << '\n'
               << "encoder_index=" << copy.encoder_index << '\n'
               << "decoder_index=" << copy.decoder_index << '\n'
               << "frame_rate=" << copy.frame_rate << '\n'
               << "bitrate_kbps=" << copy.bitrate_kbps << '\n'
               << "fec_enabled=" << copy.fec_enabled << '\n'
               << "fec_redundancy=" << copy.fec_redundancy << '\n'
               << "brightness=" << copy.brightness << '\n'
               << "contrast=" << copy.contrast << '\n'
               << "sharpness=" << copy.sharpness << '\n'
               << "denoise=" << copy.denoise << '\n'
               << "low_latency=" << copy.low_latency << '\n'
               << "vertical_sync=" << copy.vertical_sync << '\n'
               << "invert_y=" << copy.invert_y << '\n'
               << "capture_mouse=" << copy.capture_mouse << '\n'
               << "send_keyboard=" << copy.send_keyboard << '\n'
               << "hud_scale=" << copy.hud_scale << '\n'
               << "hud_opacity=" << copy.hud_opacity << '\n'
               << "show_input=" << copy.show_input << '\n'
               << "show_statistics=" << copy.show_statistics << '\n'
               << "show_ready=" << copy.show_ready << '\n'
               << "language_index=" << copy.language_index << '\n'
               << "show_performance_graph=" << copy.show_performance_graph << '\n'
               << "show_debug_info=" << copy.show_debug_info << '\n'
               << "verbose_log=" << copy.verbose_log << '\n'
               << "gamepad_deadzone=" << copy.gamepad_deadzone << '\n'
               << "gamepad_vibration=" << copy.gamepad_vibration << '\n'
               << "recording_format=" << copy.recording_format << '\n'
               << "recording_quality=" << copy.recording_quality << '\n'
               << "split_minutes=" << copy.split_minutes << '\n'
               << "service_name=" << copy.service_name << '\n'
               << "last_endpoint=" << copy.last_endpoint << '\n'
               << "last_video_port=" << copy.last_video_port << '\n'
               << "recording_directory=" << copy.recording_directory << '\n';
        for (std::size_t index = 0; index < copy.key_bindings.size(); ++index) {
            output << "key_" << index << '=' << copy.key_bindings[index] << '\n';
        }
        output.close();
        std::error_code error;
        std::filesystem::remove(settings_path_, error);
        error.clear();
        std::filesystem::rename(temporary, settings_path_, error);
        if (error) append_audit("ERROR", "设置文件原子替换失败: " + error.message());
    }

    void request_connection(const DeviceInfo& device) {
        std::lock_guard lock(request_mutex_);
        requested_device_ = device;
        connect_requested_ = true;
        disconnect_requested_ = false;
        manual_disconnect_ = false;
        request_cv_.notify_all();
    }

    void request_disconnect() {
        std::lock_guard lock(request_mutex_);
        disconnect_requested_ = true;
        connect_requested_ = false;
        manual_disconnect_ = true;
        request_cv_.notify_all();
    }

    void set_connection_state(ConnectionState state) {
        std::lock_guard lock(state_mutex_);
        state_.connection = state;
        if (state != ConnectionState::connected) state_.ready = false;
        if (state == ConnectionState::disconnected || state == ConnectionState::failed) {
            telemetry_.fps = 0;
            telemetry_.bandwidth_mbps = 0;
            telemetry_.packet_loss_percent = 0;
        }
    }

    void reset_video_stream() {
        if (decoder_thread_.joinable()) CancelSynchronousIo(decoder_thread_.native_handle());
        assemblies_.clear();
        last_completed_frame_ = 0;
        last_completed_frame_at_ = {};
        {
            std::lock_guard lock(decode_mutex_);
            ++media_session_generation_;
            decode_queue_.clear();
            decoder_reset_requested_ = true;
        }
        decode_cv_.notify_one();
        {
            std::lock_guard lock(frame_mutex_);
            latest_frame_ = {};
            last_decoded_frame_at_ = {};
            ++frame_generation_;
        }
        {
            std::lock_guard lock(state_mutex_);
            state_.video_available = false;
            state_.ready = false;
            telemetry_.fps = 0;
        }
    }

    void close_session() {
        if (control_socket_ != INVALID_SOCKET) {
            closesocket(control_socket_);
            control_socket_ = INVALID_SOCKET;
        }
        if (video_socket_ != INVALID_SOCKET) {
            closesocket(video_socket_);
            video_socket_ = INVALID_SOCKET;
        }
        pending_packets_.clear();
        reset_video_stream();
    }

    bool open_session(const DeviceInfo& device) {
        std::uint16_t video_port = device.video_port;
        {
            std::lock_guard lock(discovery_mutex_);
            const auto iterator = discovered_video_ports_.find(device.address);
            if (video_port == 0 && iterator != discovered_video_ports_.end()) {
                video_port = iterator->second;
            }
        }
        std::string error;
        const auto endpoint = resolve_endpoint(device, video_port, error);
        if (!endpoint) {
            append_audit("ERROR", error);
            set_connection_state(ConnectionState::failed);
            return false;
        }
        close_session();
        control_socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        video_socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (control_socket_ == INVALID_SOCKET || video_socket_ == INVALID_SOCKET) {
            append_audit("ERROR", "创建 UDP 套接字失败: " + std::to_string(WSAGetLastError()));
            close_session();
            set_connection_state(ConnectionState::failed);
            return false;
        }
        int receive_buffer = 4 * 1024 * 1024;
        setsockopt(video_socket_, SOL_SOCKET, SO_RCVBUF,
                   reinterpret_cast<const char*>(&receive_buffer), sizeof(receive_buffer));
        sockaddr_in local{};
        local.sin_family = AF_INET;
        local.sin_addr.s_addr = htonl(INADDR_ANY);
        local.sin_port = 0;
        if (bind(video_socket_, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) ==
            SOCKET_ERROR) {
            append_audit("ERROR", "绑定视频接收端口失败: " + std::to_string(WSAGetLastError()));
            close_session();
            set_connection_state(ConnectionState::failed);
            return false;
        }
        connect(control_socket_, reinterpret_cast<const sockaddr*>(&endpoint->control),
                sizeof(endpoint->control));
        connect(video_socket_, reinterpret_cast<const sockaddr*>(&endpoint->video),
                sizeof(endpoint->video));
        set_nonblocking(control_socket_);
        set_nonblocking(video_socket_);
        {
            std::lock_guard lock(session_mutex_);
            current_endpoint_ = *endpoint;
            current_device_ = device;
        }
        connect_started_ = Clock::now();
        last_ack_ = connect_started_;
        last_heartbeat_ = Clock::time_point{};
        last_control_ = Clock::time_point{};
        last_register_ = Clock::time_point{};
        reconnect_at_ = Clock::time_point{};
        set_connection_state(ConnectionState::connecting);
        append_audit("INFO", "正在连接 " + device.name + " (" + device.address + ")");
        send_heartbeat();
        send_register();
        return true;
    }

    void send_control_packet(std::vector<std::uint8_t> packet, std::uint32_t sequence) {
        if (control_socket_ == INVALID_SOCKET) return;
        const int sent = send(control_socket_, reinterpret_cast<const char*>(packet.data()),
                              static_cast<int>(packet.size()), 0);
        if (sent == static_cast<int>(packet.size())) {
            pending_packets_[sequence] = {std::move(packet), Clock::now(), 0};
            ++control_sent_window_;
        }
    }

    std::uint32_t next_sequence() noexcept { return sequence_++; }

    void send_heartbeat() {
        const std::uint32_t sequence = next_sequence();
        send_control_packet(protocol::heartbeat(sequence, monotonic_seconds()), sequence);
        last_heartbeat_ = Clock::now();
    }

    void send_latest_control() {
        ControlInput input{};
        bool enabled = false;
        {
            std::lock_guard lock(input_mutex_);
            input = latest_input_;
            latest_input_.mouse_delta_x = 0;
            latest_input_.mouse_delta_y = 0;
            latest_input_.mouse_wheel = 0;
        }
        {
            std::lock_guard lock(state_mutex_);
            enabled = state_.ready;
        }
        const std::uint32_t sequence = next_sequence();
        send_control_packet(protocol::control(sequence, monotonic_seconds(), input, enabled),
                            sequence);
        last_control_ = Clock::now();
    }

    void send_parameters() {
        std::deque<std::string> pending;
        {
            std::lock_guard lock(parameter_mutex_);
            pending.swap(parameter_updates_);
        }
        for (const std::string& json : pending) {
            const std::uint32_t sequence = next_sequence();
            send_control_packet(
                protocol::parameter_update(sequence, monotonic_seconds(), json), sequence);
        }
    }

    void send_parameter_query() {
        const std::uint32_t sequence = next_sequence();
        send_control_packet(
            protocol::parameter_query(sequence, monotonic_seconds()), sequence);
    }

    bool apply_remote_video_parameters(std::string_view json) {
        const auto bitrate = json_number_field<int>(json, "bitrate");
        const auto frame_rate = json_number_field<int>(json, "target_fps");
        const auto jpeg_quality = json_number_field<int>(json, "jpeg_quality");
        const auto encoder = json_scalar_field(json, "encoder");
        const auto fec_enabled = json_boolean_field(json, "fec_enabled");
        const auto fec_redundancy = json_number_field<float>(json, "fec_redundancy");
        const auto brightness = json_number_field<int>(json, "brightness");
        const auto contrast = json_number_field<int>(json, "contrast");
        const auto sharpness = json_number_field<int>(json, "sharpness");
        const auto denoise = json_number_field<int>(json, "denoise");
        const auto udp_mtu = json_number_field<int>(json, "udp_mtu");
        if (!bitrate || !frame_rate || !jpeg_quality || !encoder || !fec_enabled ||
            !fec_redundancy || !brightness || !contrast || !sharpness || !denoise ||
            (*encoder != "jpeg" && *encoder != "h264") ||
            *bitrate < minimum_video_bitrate_kbps ||
            *bitrate > maximum_video_bitrate_kbps ||
            *frame_rate < 24 || *frame_rate > 240 ||
            *jpeg_quality < 1 || *jpeg_quality > 100 ||
            *fec_redundancy < minimum_fec_redundancy ||
            *fec_redundancy > maximum_fec_redundancy ||
            *brightness < -100 || *brightness > 100 ||
            *contrast < -100 || *contrast > 100 ||
            *sharpness < 0 || *sharpness > 100 || *denoise < 0 || *denoise > 100) {
            return false;
        }
        if (udp_mtu && (*udp_mtu < 576 || *udp_mtu > 1500)) return false;
        const auto quality = std::min_element(
            jpeg_quality_levels.begin(), jpeg_quality_levels.end(),
            [jpeg_quality](int left, int right) {
                return std::abs(left - *jpeg_quality) < std::abs(right - *jpeg_quality);
            });
        {
            std::lock_guard lock(config_mutex_);
            config_.quality_index = static_cast<int>(
                std::distance(jpeg_quality_levels.begin(), quality));
            config_.jpeg_quality = *jpeg_quality;
            config_.encoder_index = *encoder == "h264" ? 1 : 0;
            config_.frame_rate = *frame_rate;
            config_.bitrate_kbps = *bitrate;
            config_.fec_enabled = *fec_enabled;
            config_.fec_redundancy = *fec_redundancy;
            config_.brightness = *brightness;
            config_.contrast = *contrast;
            config_.sharpness = *sharpness;
            config_.denoise = *denoise;
            if (udp_mtu) config_.mtu = *udp_mtu;
        }
        last_codec_ = static_cast<std::uint8_t>(*encoder == "h264" ? 1 : 0);
        save_config();
        {
            std::lock_guard lock(state_mutex_);
            ++state_.remote_parameters_revision;
        }
        append_audit("INFO", "已读取机器人图传参数");
        return true;
    }

    void send_register() {
        if (video_socket_ == INVALID_SOCKET) return;
        constexpr char command[] = "REGISTER";
        send(video_socket_, command, 8, 0);
        last_register_ = Clock::now();
    }

    void handle_control_receive() {
        std::array<std::uint8_t, 2048> buffer{};
        for (;;) {
            const int received = recv(control_socket_, reinterpret_cast<char*>(buffer.data()),
                                      static_cast<int>(buffer.size()), 0);
            if (received == SOCKET_ERROR) {
                const int error = WSAGetLastError();
                if (error != WSAEWOULDBLOCK) {
                    fail_session("控制链路接收失败: " + std::to_string(error));
                }
                break;
            }
            const std::span<const std::uint8_t> packet{
                buffer.data(), static_cast<std::size_t>(received)};
            std::uint32_t parameter_sequence = 0;
            std::string parameter_json;
            if (protocol::parse_parameter_update(
                    packet, parameter_sequence, parameter_json)) {
                const auto pending = pending_packets_.find(parameter_sequence);
                if (pending == pending_packets_.end() || pending->second.bytes.size() <= 3 ||
                    pending->second.bytes[3] != static_cast<std::uint8_t>(
                        protocol::MessageType::parameter_query)) {
                    ++invalid_packets_;
                    continue;
                }
                pending_packets_.erase(pending);
                ++control_acked_window_;
                last_ack_ = Clock::now();
                if (!apply_remote_video_parameters(parameter_json)) {
                    ++invalid_packets_;
                    append_audit("WARN", "机器人图传参数响应无效");
                }
                continue;
            }
            protocol::Acknowledgment acknowledgment{};
            if (!protocol::parse_ack(packet, acknowledgment)) {
                ++invalid_packets_;
                continue;
            }
            const auto now = Clock::now();
            const auto pending = pending_packets_.find(acknowledgment.sequence);
            if (pending == pending_packets_.end()) {
                ++invalid_packets_;
                continue;
            }
            const float round_trip = std::chrono::duration<float, std::milli>(
                                         now - pending->second.sent_at).count();
            const float remote_processing = static_cast<float>(std::max(
                0.0, acknowledgment.remote_sent_at -
                         acknowledgment.remote_received_at) * 1000.0);
            const float latency = std::max(0.0F, round_trip - remote_processing);
            {
                std::lock_guard lock(state_mutex_);
                telemetry_.latency_ms = latency;
            }
            pending_packets_.erase(pending);
            ++control_acked_window_;
            last_ack_ = now;
            bool became_connected = false;
            {
                std::lock_guard lock(state_mutex_);
                if (state_.connection == ConnectionState::connecting) {
                    state_.connection = ConnectionState::connected;
                    became_connected = true;
                }
            }
            if (became_connected) {
                append_audit("INFO", "机器人会话已建立，正在读取图传参数");
                send_parameter_query();
            }
        }
    }

    void enqueue_encoded_frame(std::uint32_t frame_id, std::uint8_t codec,
                               float encode_ms, std::vector<std::uint8_t> encoded) {
        last_completed_frame_ = frame_id;
        last_completed_frame_at_ = Clock::now();
        for (auto iterator = assemblies_.begin(); iterator != assemblies_.end() &&
             iterator->first < frame_id;) {
            iterator = assemblies_.erase(iterator);
            ++dropped_frames_;
        }
        last_codec_ = codec;
        const bool low_latency = low_latency_.load();
        {
            std::lock_guard lock(decode_mutex_);
            if (low_latency && !decode_queue_.empty()) {
                dropped_frames_ += decode_queue_.size();
                decode_queue_.clear();
            } else if (decode_queue_.size() >= 4) {
                decode_queue_.pop_front();
                ++dropped_frames_;
            }
            decode_queue_.push_back(
                {media_session_generation_, codec, encode_ms, std::move(encoded)});
        }
        decode_cv_.notify_one();
        const auto ack = protocol::video_ack(frame_id);
        send(video_socket_, reinterpret_cast<const char*>(ack.data()),
             static_cast<int>(ack.size()), 0);
    }

    void handle_video_receive() {
        std::array<std::uint8_t, 65536> buffer{};
        for (;;) {
            const int received = recv(video_socket_, reinterpret_cast<char*>(buffer.data()),
                                      static_cast<int>(buffer.size()), 0);
            if (received == SOCKET_ERROR) {
                const int error = WSAGetLastError();
                if (error != WSAEWOULDBLOCK) {
                    fail_session("视频链路接收失败: " + std::to_string(error));
                }
                break;
            }
            protocol::VideoChunkHeader header{};
            std::span<const std::uint8_t> payload;
            if (!protocol::parse_video_chunk(
                    std::span<const std::uint8_t>{buffer.data(),
                                                  static_cast<std::size_t>(received)},
                    header, payload)) {
                ++invalid_packets_;
                continue;
            }
            video_bytes_window_ += static_cast<std::uint64_t>(received);
            ++video_packets_window_;
            if (header.frame_id <= last_completed_frame_) {
                const auto now = Clock::now();
                if (last_completed_frame_at_ == Clock::time_point{} ||
                    now - last_completed_frame_at_ < video_sequence_reset_timeout) {
                    continue;
                }
                reset_video_stream();
                append_audit("INFO", "检测到机器人视频会话重启，正在重新同步");
                send_parameter_query();
            }
            auto [iterator, inserted] = assemblies_.try_emplace(header.frame_id);
            VideoAssembly& frame = iterator->second;
            if (inserted) {
                frame.total_chunks = header.total_chunks;
                frame.original_chunks = header.original_chunks;
                frame.codec = header.codec;
                frame.encode_ms = header.encode_ms;
                frame.chunks.resize(header.total_chunks);
                frame.received.resize(header.total_chunks);
            }
            if (frame.total_chunks != header.total_chunks ||
                frame.original_chunks != header.original_chunks ||
                frame.codec != header.codec) {
                assemblies_.erase(iterator);
                ++invalid_packets_;
                continue;
            }
            if (!frame.received[header.chunk_index]) {
                if (frame.received_bytes + payload.size() > protocol::max_encoded_frame_size) {
                    assemblies_.erase(iterator);
                    ++invalid_packets_;
                    continue;
                }
                frame.chunks[header.chunk_index].assign(payload.begin(), payload.end());
                frame.received[header.chunk_index] = true;
                frame.received_bytes += payload.size();
            }
            try_complete_frame(header.frame_id, frame);
        }
    }

    void try_complete_frame(std::uint32_t frame_id, VideoAssembly& frame) {
        std::vector<std::uint16_t> missing;
        for (std::uint16_t index = 0; index < frame.original_chunks; ++index) {
            if (!frame.received[index]) missing.push_back(index);
        }
        const std::uint16_t parity_count = frame.total_chunks - frame.original_chunks;
        for (std::uint16_t group = 0; group < parity_count && !missing.empty(); ++group) {
            const std::uint16_t parity_index = frame.original_chunks + group;
            if (!frame.received[parity_index] || frame.chunks[parity_index].size() < 5) continue;
            std::vector<std::uint16_t> group_missing;
            for (std::uint16_t index = group; index < frame.original_chunks;
                 index = static_cast<std::uint16_t>(index + parity_count)) {
                if (!frame.received[index]) group_missing.push_back(index);
            }
            if (group_missing.size() != 1) continue;
            const auto& parity = frame.chunks[parity_index];
            const std::uint32_t frame_size = static_cast<std::uint32_t>(parity[0]) |
                                             static_cast<std::uint32_t>(parity[1]) << 8U |
                                             static_cast<std::uint32_t>(parity[2]) << 16U |
                                             static_cast<std::uint32_t>(parity[3]) << 24U;
            const std::size_t chunk_size = parity.size() - 4;
            if (frame_size == 0 || frame_size > protocol::max_encoded_frame_size ||
                chunk_size == 0 ||
                (frame_size + chunk_size - 1) / chunk_size != frame.original_chunks) {
                continue;
            }
            const std::uint16_t missing_index = group_missing.front();
            std::vector<std::uint8_t> recovered(parity.begin() + 4, parity.end());
            for (std::uint16_t index = group; index < frame.original_chunks;
                 index = static_cast<std::uint16_t>(index + parity_count)) {
                if (index == missing_index) continue;
                const auto& chunk = frame.chunks[index];
                for (std::size_t byte = 0; byte < chunk.size(); ++byte) {
                    recovered[byte] ^= chunk[byte];
                }
            }
            const std::size_t recovered_size = missing_index + 1 == frame.original_chunks
                ? frame_size - static_cast<std::size_t>(frame.original_chunks - 1) * chunk_size
                : chunk_size;
            recovered.resize(recovered_size);
            frame.received_bytes += recovered.size();
            frame.chunks[missing_index] = std::move(recovered);
            frame.received[missing_index] = true;
            missing.erase(std::remove(missing.begin(), missing.end(), missing_index), missing.end());
            ++recovered_frames_;
        }
        if (!missing.empty()) return;
        std::size_t size = 0;
        for (std::uint16_t index = 0; index < frame.original_chunks; ++index) {
            size += frame.chunks[index].size();
        }
        if (size > protocol::max_encoded_frame_size) {
            assemblies_.erase(frame_id);
            ++invalid_packets_;
            return;
        }
        std::vector<std::uint8_t> encoded;
        encoded.reserve(size);
        for (std::uint16_t index = 0; index < frame.original_chunks; ++index) {
            encoded.insert(encoded.end(), frame.chunks[index].begin(), frame.chunks[index].end());
        }
        const std::uint8_t codec = frame.codec;
        const float encode_ms = frame.encode_ms;
        assemblies_.erase(frame_id);
        enqueue_encoded_frame(frame_id, codec, encode_ms, std::move(encoded));
    }

    void service_incomplete_frames() {
        const auto now = Clock::now();
        for (auto iterator = assemblies_.begin(); iterator != assemblies_.end();) {
            VideoAssembly& frame = iterator->second;
            if (now - frame.first_seen > 1s || frame.nack_count >= 2) {
                ++dropped_frames_;
                iterator = assemblies_.erase(iterator);
                continue;
            }
            if (now - frame.first_seen >= 50ms &&
                (frame.last_nack == Clock::time_point{} || now - frame.last_nack >= 50ms)) {
                std::vector<std::uint16_t> missing;
                for (std::uint16_t index = 0; index < frame.original_chunks; ++index) {
                    if (!frame.received[index]) missing.push_back(index);
                }
                if (!missing.empty()) {
                    const auto nack = protocol::video_nack(iterator->first, missing);
                    send(video_socket_, reinterpret_cast<const char*>(nack.data()),
                         static_cast<int>(nack.size()), 0);
                    frame.last_nack = now;
                    ++frame.nack_count;
                }
            }
            ++iterator;
        }
        while (assemblies_.size() > 32) {
            assemblies_.erase(assemblies_.begin());
            ++dropped_frames_;
        }
    }

    void service_pending_packets() {
        const auto now = Clock::now();
        for (auto iterator = pending_packets_.begin(); iterator != pending_packets_.end();) {
            PendingPacket& pending = iterator->second;
            if (now - pending.sent_at < 100ms) {
                ++iterator;
                continue;
            }
            const std::uint8_t type = pending.bytes.size() > 3 ? pending.bytes[3] : 0;
            const bool retryable = type == static_cast<std::uint8_t>(
                                       protocol::MessageType::heartbeat) ||
                                   type == static_cast<std::uint8_t>(
                                       protocol::MessageType::parameter_update) ||
                                   type == static_cast<std::uint8_t>(
                                       protocol::MessageType::parameter_query);
            if (retryable && pending.retries < 3 && control_socket_ != INVALID_SOCKET) {
                const int sent = send(control_socket_,
                                      reinterpret_cast<const char*>(pending.bytes.data()),
                                      static_cast<int>(pending.bytes.size()), 0);
                if (sent == static_cast<int>(pending.bytes.size())) {
                    pending.sent_at = now;
                    ++pending.retries;
                    ++control_sent_window_;
                    ++iterator;
                    continue;
                }
            }
            if (type == static_cast<std::uint8_t>(protocol::MessageType::parameter_query)) {
                append_audit("WARN", "读取机器人图传参数超时，保留本地设置");
            }
            iterator = pending_packets_.erase(iterator);
        }
    }

    void fail_session(std::string reason) {
        append_audit("ERROR", std::move(reason));
        close_session();
        set_connection_state(ConnectionState::failed);
        std::lock_guard lock(request_mutex_);
        RuntimeConfig config;
        {
            std::lock_guard config_lock(config_mutex_);
            config = config_;
        }
        if (!manual_disconnect_ && config.auto_reconnect && requested_device_) {
            reconnect_at_ = Clock::now() + std::chrono::seconds(config.reconnect_seconds);
        }
    }

    void update_statistics() {
        const auto now = Clock::now();
        if (statistics_at_ == Clock::time_point{}) statistics_at_ = now;
        const double elapsed = std::chrono::duration<double>(now - statistics_at_).count();
        if (elapsed < 1.0) return;
        TelemetrySnapshot snapshot;
        {
            std::lock_guard lock(state_mutex_);
            snapshot = telemetry_;
            if (state_.connection != ConnectionState::connected) snapshot.fps = 0;
            snapshot.bandwidth_mbps = static_cast<float>(
                static_cast<double>(video_bytes_window_) * 8.0 / elapsed / 1'000'000.0);
            snapshot.packet_loss_percent = control_sent_window_ == 0
                                               ? 0.0F
                                               : 100.0F * static_cast<float>(
                                                     control_sent_window_ -
                                                     std::min(control_sent_window_,
                                                              control_acked_window_)) /
                                                     static_cast<float>(control_sent_window_);
            snapshot.decoded_frames = static_cast<int>(decoded_frames_);
            telemetry_ = snapshot;
        }
        video_bytes_window_ = 0;
        video_packets_window_ = 0;
        control_sent_window_ = 0;
        control_acked_window_ = 0;
        statistics_at_ = now;
    }

    void network_loop() {
        while (running_) {
            DeviceInfo connect_device;
            bool should_connect = false;
            bool should_disconnect = false;
            {
                std::unique_lock lock(request_mutex_);
                request_cv_.wait_for(lock, 5ms, [this] {
                    return !running_ || connect_requested_ || disconnect_requested_;
                });
                if (!running_) break;
                if (disconnect_requested_) {
                    disconnect_requested_ = false;
                    should_disconnect = true;
                }
                if (connect_requested_ && requested_device_) {
                    connect_requested_ = false;
                    connect_device = *requested_device_;
                    should_connect = true;
                }
            }
            if (should_disconnect) {
                set_connection_state(ConnectionState::disconnecting);
                close_session();
                set_connection_state(ConnectionState::disconnected);
                append_audit("INFO", "机器人会话已断开");
            }
            if (should_connect) open_session(connect_device);

            if (control_socket_ != INVALID_SOCKET) {
                handle_control_receive();
                handle_video_receive();
                const auto now = Clock::now();
                RuntimeConfig config;
                {
                    std::lock_guard lock(config_mutex_);
                    config = config_;
                }
                RuntimeState state;
                {
                    std::lock_guard lock(state_mutex_);
                    state = state_;
                }
                if (now - last_heartbeat_ >= std::chrono::milliseconds(config.heartbeat_ms)) {
                    send_heartbeat();
                }
                if (state.connection == ConnectionState::connected && now - last_control_ >= 20ms) {
                    send_latest_control();
                    send_parameters();
                }
                if (now - last_register_ >= 2s) send_register();
                service_incomplete_frames();
                service_pending_packets();
                const auto timeout = std::max(1500ms,
                    std::chrono::milliseconds(config.heartbeat_ms * 3));
                if ((state.connection == ConnectionState::connecting &&
                     now - connect_started_ > 10s) ||
                    (state.connection == ConnectionState::connected && now - last_ack_ > timeout)) {
                    fail_session("机器人会话超时");
                }
                bool video_timed_out = false;
                {
                    std::lock_guard lock(frame_mutex_);
                    if (last_decoded_frame_at_ != Clock::time_point{} &&
                        now - last_decoded_frame_at_ > 1500ms) {
                        latest_frame_ = {};
                        last_decoded_frame_at_ = {};
                        ++frame_generation_;
                        video_timed_out = true;
                    }
                }
                if (video_timed_out) {
                    {
                        std::lock_guard lock(state_mutex_);
                        state_.video_available = false;
                        state_.ready = false;
                        telemetry_.fps = 0;
                    }
                    append_audit("WARN", "视频信号超时，已退出 READY");
                }
            } else {
                RuntimeConfig config;
                std::optional<DeviceInfo> reconnect_device;
                {
                    std::lock_guard lock(config_mutex_);
                    config = config_;
                }
                {
                    std::lock_guard lock(request_mutex_);
                    if (config.auto_reconnect && !manual_disconnect_ && requested_device_ &&
                        reconnect_at_ != Clock::time_point{} && Clock::now() >= reconnect_at_) {
                        reconnect_device = requested_device_;
                    }
                }
                if (reconnect_device) {
                    append_audit("INFO", "正在自动重连机器人");
                    open_session(*reconnect_device);
                }
            }
            if (recorder_.active() && !recorder_.healthy()) {
                const std::string error = recorder_.last_error();
                recorder_.stop();
                {
                    std::lock_guard lock(state_mutex_);
                    state_.recording = RecordingState::failed;
                }
                append_audit("ERROR", error);
            }
            update_statistics();
        }
    }

    struct EncodedFrame final {
        std::uint64_t session_generation{};
        std::uint8_t codec{};
        float encode_ms{};
        std::vector<std::uint8_t> bytes;
    };

    void decoder_loop() {
        const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        std::deque<Clock::time_point> decoded_times;
        while (running_) {
            EncodedFrame encoded;
            bool reset_decoder = false;
            {
                std::unique_lock lock(decode_mutex_);
                decode_cv_.wait(lock, [this] {
                    return !running_ || decoder_reset_requested_ || !decode_queue_.empty();
                });
                if (!running_ && decode_queue_.empty()) break;
                reset_decoder = decoder_reset_requested_;
                decoder_reset_requested_ = false;
                if (!decode_queue_.empty()) {
                    const bool low_latency = low_latency_.load();
                    if (low_latency) {
                        encoded = std::move(decode_queue_.back());
                        dropped_frames_ += decode_queue_.size() - 1;
                        decode_queue_.clear();
                    } else {
                        encoded = std::move(decode_queue_.front());
                        decode_queue_.pop_front();
                    }
                }
            }
            if (reset_decoder) {
                decoder_.reset();
                decoded_times.clear();
            }
            if (encoded.bytes.empty()) continue;
            media::DecodedFrame decoded;
            std::string error;
            const auto started = Clock::now();
            if (!decoder_.decode(encoded.codec, encoded.bytes, decoded, error)) {
                ++decode_errors_;
                if (decode_errors_ <= 3 || decode_errors_ % 120 == 0) {
                    append_audit("ERROR", error);
                }
                continue;
            }
            if (decoded.bgra.empty()) continue;
            {
                std::lock_guard lock(decode_mutex_);
                if (encoded.session_generation != media_session_generation_) continue;
            }
            const auto now = Clock::now();
            decoded_times.push_back(now);
            while (!decoded_times.empty() && now - decoded_times.front() > 1s) {
                decoded_times.pop_front();
            }
            {
                std::lock_guard lock(frame_mutex_);
                latest_frame_ = std::move(decoded);
                last_decoded_frame_at_ = now;
                ++frame_generation_;
            }
            ++decoded_frames_;
            {
                std::lock_guard lock(state_mutex_);
                telemetry_.fps = static_cast<float>(decoded_times.size());
                state_.video_available = true;
            }
            last_decode_ms_ = std::chrono::duration<float, std::milli>(now - started).count();
            last_encode_ms_ = encoded.encode_ms;
        }
        if (SUCCEEDED(com_result)) CoUninitialize();
    }

    VideoSurface upload_video_surface() const {
        if (device_ == nullptr || context_ == nullptr) return {};
        std::lock_guard lock(frame_mutex_);
        if (latest_frame_.bgra.empty()) return {};
        if (uploaded_generation_ != frame_generation_) {
            if (video_texture_ == nullptr || texture_width_ != latest_frame_.width ||
                texture_height_ != latest_frame_.height) {
                release(video_view_);
                release(video_texture_);
                D3D11_TEXTURE2D_DESC description{};
                description.Width = static_cast<UINT>(latest_frame_.width);
                description.Height = static_cast<UINT>(latest_frame_.height);
                description.MipLevels = 1;
                description.ArraySize = 1;
                description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                description.SampleDesc.Count = 1;
                description.Usage = D3D11_USAGE_DEFAULT;
                description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                D3D11_SUBRESOURCE_DATA initial{};
                initial.pSysMem = latest_frame_.bgra.data();
                initial.SysMemPitch = static_cast<UINT>(latest_frame_.width * 4);
                if (FAILED(device_->CreateTexture2D(&description, &initial, &video_texture_)) ||
                    FAILED(device_->CreateShaderResourceView(video_texture_, nullptr,
                                                             &video_view_))) {
                    release(video_view_);
                    release(video_texture_);
                    return {};
                }
                texture_width_ = latest_frame_.width;
                texture_height_ = latest_frame_.height;
            } else {
                context_->UpdateSubresource(video_texture_, 0, nullptr,
                                            latest_frame_.bgra.data(),
                                            static_cast<UINT>(latest_frame_.width * 4), 0);
            }
            uploaded_generation_ = frame_generation_;
        }
        return {video_view_, texture_width_, texture_height_};
    }

    void enqueue_parameters(std::string json) {
        std::lock_guard lock(parameter_mutex_);
        parameter_updates_.push_back(std::move(json));
    }

    void stop_discovery() {
        if (discovery_thread_.joinable()) {
            discovery_stop_ = true;
            discovery_thread_.join();
        }
    }

    void start_discovery(std::string service_name) {
        stop_discovery();
        discovery_stop_ = false;
        {
            std::lock_guard lock(discovery_mutex_);
            discovered_devices_.clear();
            discovered_video_ports_.clear();
        }
        discovery_thread_ = std::thread([this, service_name = std::move(service_name)] {
            raw_mdns_scan(service_name);
        });
    }

    struct DnsReader final {
        std::span<const std::uint8_t> packet;

        bool name(std::size_t& offset, std::string& output, int depth = 0) const {
            if (depth > 16 || offset >= packet.size()) return false;
            std::size_t cursor = offset;
            bool jumped = false;
            output.clear();
            for (int labels = 0; labels < 128; ++labels) {
                if (cursor >= packet.size()) return false;
                const std::uint8_t length = packet[cursor++];
                if (length == 0) {
                    if (!jumped) offset = cursor;
                    return true;
                }
                if ((length & 0xC0U) == 0xC0U) {
                    if (cursor >= packet.size()) return false;
                    const std::size_t pointer =
                        (static_cast<std::size_t>(length & 0x3FU) << 8U) | packet[cursor++];
                    if (!jumped) offset = cursor;
                    jumped = true;
                    std::size_t target = pointer;
                    std::string suffix;
                    if (!name(target, suffix, depth + 1)) return false;
                    if (!output.empty() && !suffix.empty()) output += '.';
                    output += suffix;
                    return true;
                }
                if (length > 63 || cursor + length > packet.size()) return false;
                if (!output.empty()) output += '.';
                output.append(reinterpret_cast<const char*>(packet.data() + cursor), length);
                cursor += length;
                if (!jumped) offset = cursor;
            }
            return false;
        }
    };

    static void append_dns_name(std::vector<std::uint8_t>& packet, std::string name) {
        if (!name.empty() && name.back() == '.') name.pop_back();
        std::size_t start = 0;
        while (start < name.size()) {
            const std::size_t dot = name.find('.', start);
            const std::size_t end = dot == std::string::npos ? name.size() : dot;
            const std::size_t length = std::min<std::size_t>(63, end - start);
            packet.push_back(static_cast<std::uint8_t>(length));
            packet.insert(packet.end(), name.begin() + static_cast<std::ptrdiff_t>(start),
                          name.begin() + static_cast<std::ptrdiff_t>(start + length));
            if (dot == std::string::npos) break;
            start = dot + 1;
        }
        packet.push_back(0);
    }

    void raw_mdns_scan(std::string service_name) {
        if (!winsock_ready_) return;
        if (service_name.empty()) service_name = "_pip-link._udp.local";
        if (service_name.ends_with('.')) service_name.pop_back();
        SOCKET socket_handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socket_handle == INVALID_SOCKET) return;
        int reuse = 1;
        setsockopt(socket_handle, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&reuse), sizeof(reuse));
        sockaddr_in local{};
        local.sin_family = AF_INET;
        local.sin_addr.s_addr = htonl(INADDR_ANY);
        local.sin_port = htons(5353);
        const bool bound_to_mdns =
            bind(socket_handle, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) !=
            SOCKET_ERROR;
        if (!bound_to_mdns) {
            local.sin_port = 0;
            if (bind(socket_handle, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) ==
                SOCKET_ERROR) {
                closesocket(socket_handle);
                return;
            }
        }
        set_nonblocking(socket_handle);
        std::vector<std::uint8_t> query(12, 0);
        query[5] = 1;
        append_dns_name(query, service_name);
        query.push_back(0); query.push_back(DNS_TYPE_PTR);
        query.push_back(0x80); query.push_back(1);
        sockaddr_in multicast{};
        multicast.sin_family = AF_INET;
        multicast.sin_port = htons(5353);
        inet_pton(AF_INET, "224.0.0.251", &multicast.sin_addr);
        std::array<INTERFACE_INFO, 64> interfaces{};
        DWORD interface_bytes = 0;
        bool query_sent = false;
        std::set<u_long> queried_addresses;
        if (WSAIoctl(socket_handle, SIO_GET_INTERFACE_LIST, nullptr, 0,
                     interfaces.data(), static_cast<DWORD>(sizeof(interfaces)),
                     &interface_bytes, nullptr, nullptr) == 0) {
            const std::size_t interface_count =
                interface_bytes / sizeof(INTERFACE_INFO);
            for (std::size_t index = 0; index < interface_count; ++index) {
                const auto& interface_info = interfaces[index];
                if ((interface_info.iiFlags & IFF_UP) == 0 ||
                    (interface_info.iiFlags & IFF_LOOPBACK) != 0 ||
                    (interface_info.iiFlags & IFF_MULTICAST) == 0) {
                    continue;
                }
                const auto* interface_address =
                    reinterpret_cast<const sockaddr_in*>(&interface_info.iiAddress);
                const in_addr address = interface_address->sin_addr;
                if (address.s_addr == htonl(INADDR_ANY) ||
                    !queried_addresses.insert(address.s_addr).second) {
                    continue;
                }
                if (bound_to_mdns) {
                    ip_mreq membership{};
                    membership.imr_multiaddr = multicast.sin_addr;
                    membership.imr_interface = address;
                    setsockopt(socket_handle, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                               reinterpret_cast<const char*>(&membership),
                               sizeof(membership));
                }
                if (setsockopt(socket_handle, IPPROTO_IP, IP_MULTICAST_IF,
                               reinterpret_cast<const char*>(&address), sizeof(address)) ==
                    SOCKET_ERROR) {
                    continue;
                }
                query_sent |= sendto(socket_handle,
                                     reinterpret_cast<const char*>(query.data()),
                                     static_cast<int>(query.size()), 0,
                                     reinterpret_cast<const sockaddr*>(&multicast),
                                     sizeof(multicast)) != SOCKET_ERROR;
            }
        }
        if (!query_sent) {
            if (bound_to_mdns) {
                ip_mreq membership{};
                membership.imr_multiaddr = multicast.sin_addr;
                membership.imr_interface.s_addr = htonl(INADDR_ANY);
                setsockopt(socket_handle, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                           reinterpret_cast<const char*>(&membership), sizeof(membership));
            }
            sendto(socket_handle, reinterpret_cast<const char*>(query.data()),
                   static_cast<int>(query.size()), 0,
                   reinterpret_cast<const sockaddr*>(&multicast), sizeof(multicast));
        }

        struct ServiceRecord final {
            std::string instance;
            std::string host;
            std::uint16_t control_port{};
            std::uint16_t video_port{};
            in_addr address{};
            bool has_address{};
        };
        std::unordered_map<std::string, ServiceRecord> services;
        std::unordered_map<std::string, in_addr> addresses;
        const auto deadline = Clock::now() + 4s;
        std::array<std::uint8_t, 65536> buffer{};
        while (!discovery_stop_ && Clock::now() < deadline) {
            sockaddr_in from{};
            int from_size = sizeof(from);
            const int count = recvfrom(socket_handle, reinterpret_cast<char*>(buffer.data()),
                                       static_cast<int>(buffer.size()), 0,
                                       reinterpret_cast<sockaddr*>(&from), &from_size);
            if (count == SOCKET_ERROR) {
                if (WSAGetLastError() == WSAEWOULDBLOCK) {
                    std::this_thread::sleep_for(10ms);
                    continue;
                }
                break;
            }
            parse_mdns_packet(std::span<const std::uint8_t>{buffer.data(),
                                                            static_cast<std::size_t>(count)},
                              service_name, services, addresses);
            for (auto& [instance, service] : services) {
                const auto address = addresses.find(service.host);
                if (address != addresses.end()) {
                    service.address = address->second;
                    service.has_address = true;
                }
                if (!service.has_address || service.control_port == 0) continue;
                char ip[INET_ADDRSTRLEN]{};
                inet_ntop(AF_INET, &service.address, ip, sizeof(ip));
                const std::string endpoint = std::string(ip) + ':' +
                                             std::to_string(service.control_port);
                std::string display_name = instance;
                const std::string suffix = "." + service_name;
                if (display_name.ends_with(suffix)) {
                    display_name.resize(display_name.size() - suffix.size());
                }
                std::lock_guard lock(discovery_mutex_);
                const auto found = std::find_if(
                    discovered_devices_.begin(), discovered_devices_.end(),
                    [&](const DeviceInfo& value) { return value.address == endpoint; });
                if (found == discovered_devices_.end()) {
                    discovered_devices_.push_back({display_name, endpoint, 100});
                    discovered_video_ports_[endpoint] = service.video_port != 0
                                                            ? service.video_port
                                                            : static_cast<std::uint16_t>(
                                                                  service.control_port - 1000);
                    append_audit("INFO", "发现机器人 " + display_name + " (" + endpoint + ")");
                }
            }
        }
        closesocket(socket_handle);
    }

    template <typename Services, typename Addresses>
    static void parse_mdns_packet(std::span<const std::uint8_t> packet,
                                  const std::string& service_name,
                                  Services& services, Addresses& addresses) {
        if (packet.size() < 12) return;
        const auto read_u16 = [&](std::size_t offset) {
            return static_cast<std::uint16_t>(packet[offset] << 8U | packet[offset + 1]);
        };
        const auto read_u32 = [&](std::size_t offset) {
            return static_cast<std::uint32_t>(packet[offset]) << 24U |
                   static_cast<std::uint32_t>(packet[offset + 1]) << 16U |
                   static_cast<std::uint32_t>(packet[offset + 2]) << 8U |
                   packet[offset + 3];
        };
        const std::uint16_t questions = read_u16(4);
        const std::uint32_t records = static_cast<std::uint32_t>(read_u16(6)) +
                                      read_u16(8) + read_u16(10);
        DnsReader reader{packet};
        std::size_t offset = 12;
        std::string name;
        for (std::uint16_t index = 0; index < questions; ++index) {
            if (!reader.name(offset, name) || offset + 4 > packet.size()) return;
            offset += 4;
        }
        std::unordered_map<std::string, std::pair<std::string, std::uint16_t>> srv;
        std::unordered_map<std::string, std::pair<std::uint16_t, std::uint16_t>> txt;
        std::set<std::string> ptr_instances;
        for (std::uint32_t index = 0; index < records; ++index) {
            if (!reader.name(offset, name) || offset + 10 > packet.size()) return;
            const std::uint16_t type = read_u16(offset);
            const std::uint16_t length = read_u16(offset + 8);
            offset += 10;
            if (offset + length > packet.size()) return;
            const std::size_t data_start = offset;
            if (type == DNS_TYPE_PTR) {
                std::string instance;
                std::size_t cursor = offset;
                if (reader.name(cursor, instance) && name == service_name) {
                    ptr_instances.insert(instance);
                }
            } else if (type == DNS_TYPE_SRV && length >= 6) {
                std::string host;
                std::size_t cursor = offset + 6;
                if (reader.name(cursor, host)) srv[name] = {host, read_u16(offset + 4)};
            } else if (type == DNS_TYPE_TEXT) {
                std::uint16_t control_port = 0;
                std::uint16_t video_port = 0;
                std::size_t cursor = offset;
                while (cursor < data_start + length) {
                    const std::size_t item_length = packet[cursor++];
                    if (cursor + item_length > data_start + length) break;
                    const std::string item(
                        reinterpret_cast<const char*>(packet.data() + cursor), item_length);
                    const auto separator = item.find('=');
                    if (separator != std::string::npos) {
                        try {
                            if (item.substr(0, separator) == "control_port") {
                                control_port = static_cast<std::uint16_t>(
                                    std::stoul(item.substr(separator + 1)));
                            } else if (item.substr(0, separator) == "video_port") {
                                video_port = static_cast<std::uint16_t>(
                                    std::stoul(item.substr(separator + 1)));
                            }
                        } catch (...) {
                        }
                    }
                    cursor += item_length;
                }
                txt[name] = {control_port, video_port};
                auto& record = services[name];
                record.instance = name;
                if (control_port != 0) record.control_port = control_port;
                if (video_port != 0) record.video_port = video_port;
            } else if (type == DNS_TYPE_A && length == 4) {
                in_addr address{};
                const std::uint32_t network_value = read_u32(offset);
                address.s_addr = htonl(network_value);
                addresses[name] = address;
            }
            offset = data_start + length;
        }
        for (const auto& [instance, service] : srv) {
            auto& record = services[instance];
            record.instance = instance;
            record.host = service.first;
            record.control_port = service.second;
            if (const auto iterator = txt.find(instance); iterator != txt.end()) {
                if (iterator->second.first != 0) record.control_port = iterator->second.first;
                record.video_port = iterator->second.second;
            }
        }
        for (const std::string& instance : ptr_instances) {
            auto& record = services[instance];
            record.instance = instance;
        }
    }

    SDL_Window* window_{};
    ID3D11Device* device_{};
    ID3D11DeviceContext* context_{};
    std::filesystem::path data_directory_;
    std::filesystem::path settings_path_;
    std::filesystem::path audit_path_;
    WSADATA winsock_{};
    bool winsock_ready_{};
    std::atomic_bool running_{true};
    std::atomic_bool low_latency_{true};
    std::thread network_thread_;
    std::thread decoder_thread_;
    mutable std::mutex state_mutex_;
    RuntimeState state_{};
    TelemetrySnapshot telemetry_{};
    mutable std::mutex config_mutex_;
    RuntimeConfig config_{};
    mutable std::mutex audit_mutex_;
    std::deque<AuditEntry> audit_entries_;
    mutable std::mutex request_mutex_;
    std::condition_variable request_cv_;
    std::optional<DeviceInfo> requested_device_;
    DeviceInfo current_device_{};
    bool connect_requested_{};
    bool disconnect_requested_{};
    bool manual_disconnect_{true};
    Endpoint current_endpoint_{};
    mutable std::mutex session_mutex_;
    SOCKET control_socket_{INVALID_SOCKET};
    SOCKET video_socket_{INVALID_SOCKET};
    std::uint32_t sequence_{1};
    std::unordered_map<std::uint32_t, PendingPacket> pending_packets_;
    Clock::time_point connect_started_{};
    Clock::time_point last_ack_{};
    Clock::time_point last_heartbeat_{};
    Clock::time_point last_control_{};
    Clock::time_point last_register_{};
    Clock::time_point reconnect_at_{};
    Clock::time_point statistics_at_{};
    mutable std::mutex input_mutex_;
    ControlInput latest_input_{};
    mutable std::mutex parameter_mutex_;
    std::deque<std::string> parameter_updates_;
    std::map<std::uint32_t, VideoAssembly> assemblies_;
    std::uint32_t last_completed_frame_{};
    Clock::time_point last_completed_frame_at_{};
    std::atomic_uint8_t last_codec_{1};
    mutable std::mutex decode_mutex_;
    std::condition_variable decode_cv_;
    std::deque<EncodedFrame> decode_queue_;
    std::uint64_t media_session_generation_{};
    bool decoder_reset_requested_{};
    media::FrameDecoder decoder_;
    mutable std::mutex frame_mutex_;
    mutable media::DecodedFrame latest_frame_;
    Clock::time_point last_decoded_frame_at_{};
    mutable std::uint64_t frame_generation_{};
    mutable std::uint64_t uploaded_generation_{};
    mutable ID3D11Texture2D* video_texture_{};
    mutable ID3D11ShaderResourceView* video_view_{};
    mutable int texture_width_{};
    mutable int texture_height_{};
    media::CompositedRecorder recorder_;
    mutable std::mutex composite_mutex_;
    std::optional<std::filesystem::path> pending_screenshot_;
    std::filesystem::path pending_recording_directory_;
    int pending_recording_format_{};
    int pending_recording_quality_{85};
    int pending_recording_split_minutes_{};
    DisplaySnapshot display_snapshot_{};
    int pending_resolution_index_{3};
    int pending_window_mode_{};
    int pending_display_index_{};
    mutable std::mutex discovery_mutex_;
    std::vector<DeviceInfo> discovered_devices_;
    std::unordered_map<std::string, std::uint16_t> discovered_video_ports_;
    std::thread discovery_thread_;
    std::atomic_bool discovery_stop_{};
    std::uint64_t video_bytes_window_{};
    std::uint64_t video_packets_window_{};
    std::uint64_t control_sent_window_{};
    std::uint64_t control_acked_window_{};
    std::atomic_uint64_t decoded_frames_{};
    std::atomic_uint64_t dropped_frames_{};
    std::atomic_uint64_t recovered_frames_{};
    std::atomic_uint64_t decode_errors_{};
    std::atomic_uint64_t invalid_packets_{};
    std::atomic<float> last_decode_ms_{};
    std::atomic<float> last_encode_ms_{};
};

GroundStationBackendRuntime::GroundStationBackendRuntime(
    SDL_Window* window, ID3D11Device* device, ID3D11DeviceContext* context)
    : impl_(std::make_unique<Impl>(window, device, context)) {}

GroundStationBackendRuntime::~GroundStationBackendRuntime() = default;

std::vector<DeviceInfo> GroundStationBackendRuntime::discovered_devices() const {
    std::lock_guard lock(impl_->discovery_mutex_);
    return impl_->discovered_devices_;
}

TelemetrySnapshot GroundStationBackendRuntime::telemetry() const {
    std::lock_guard lock(impl_->state_mutex_);
    return impl_->telemetry_;
}

std::vector<AuditEntry> GroundStationBackendRuntime::audit_entries() const {
    std::lock_guard lock(impl_->audit_mutex_);
    return {impl_->audit_entries_.begin(), impl_->audit_entries_.end()};
}

BackendPreferences GroundStationBackendRuntime::preferences() const {
    std::lock_guard lock(impl_->config_mutex_);
    return impl_->config_;
}

VideoSurface GroundStationBackendRuntime::latest_video_surface() const {
    return impl_->upload_video_surface();
}

RuntimeState GroundStationBackendRuntime::runtime_state() const {
    std::lock_guard lock(impl_->state_mutex_);
    return impl_->state_;
}

void GroundStationBackendRuntime::scan_devices(const std::string& service_name) {
    {
        std::lock_guard lock(impl_->config_mutex_);
        impl_->config_.service_name = service_name;
    }
    impl_->save_config();
    impl_->append_audit("INFO", "开始扫描服务 " + service_name);
    impl_->start_discovery(service_name);
}

void GroundStationBackendRuntime::connect_device(const DeviceInfo& device) {
    {
        std::lock_guard lock(impl_->config_mutex_);
        impl_->config_.last_endpoint = device.address;
        if (device.video_port != 0) impl_->config_.last_video_port = device.video_port;
    }
    impl_->save_config();
    impl_->request_connection(device);
}

void GroundStationBackendRuntime::disconnect_device() { impl_->request_disconnect(); }

void GroundStationBackendRuntime::apply_connection_settings(
    int heartbeat_ms, int reconnect_seconds, int mtu, bool auto_reconnect) {
    const int udp_mtu = std::clamp(mtu, 576, 1500);
    {
        std::lock_guard lock(impl_->config_mutex_);
        impl_->config_.heartbeat_ms = std::clamp(heartbeat_ms, 250, 5000);
        impl_->config_.reconnect_seconds = std::clamp(reconnect_seconds, 1, 30);
        impl_->config_.mtu = udp_mtu;
        impl_->config_.auto_reconnect = auto_reconnect;
    }
    impl_->save_config();
    impl_->enqueue_parameters("{\"udp_mtu\":" + std::to_string(udp_mtu) + "}");
    impl_->append_audit("INFO", "连接策略已更新");
}

void GroundStationBackendRuntime::apply_input_settings(
    float mouse_sensitivity, float field_of_view, bool invert_pitch) {
    {
        std::lock_guard lock(impl_->config_mutex_);
        impl_->config_.mouse_sensitivity = std::clamp(mouse_sensitivity, 0.05F, 5.0F);
        impl_->config_.field_of_view = std::clamp(field_of_view, 50.0F, 130.0F);
        impl_->config_.invert_pitch = invert_pitch;
    }
    impl_->save_config();
}

void GroundStationBackendRuntime::apply_video_settings(
    int quality_index, int resolution_index, int window_mode, int encoder_index,
    int decoder_index, int frame_rate, int bitrate_kbps, bool fec_enabled,
    float fec_redundancy, int brightness, int contrast, int sharpness, int denoise,
    bool low_latency, bool vertical_sync) {
    int jpeg_quality = 85;
    int udp_mtu = 1400;
    {
        std::lock_guard lock(impl_->config_mutex_);
        auto& config = impl_->config_;
        const int selected_quality = std::clamp(quality_index, 0, 3);
        if (config.quality_index != selected_quality) {
            config.jpeg_quality = jpeg_quality_levels[
                static_cast<std::size_t>(selected_quality)];
        }
        config.quality_index = selected_quality;
        jpeg_quality = config.jpeg_quality;
        udp_mtu = config.mtu;
        config.resolution_index = std::clamp(resolution_index, 0, 5);
        config.window_mode = std::clamp(window_mode, 0, 1);
        config.encoder_index = std::clamp(encoder_index, 0, 1);
        config.decoder_index = std::clamp(decoder_index, 0, 2);
        config.frame_rate = std::clamp(frame_rate, 24, 240);
        config.bitrate_kbps = std::clamp(
            bitrate_kbps, minimum_video_bitrate_kbps, maximum_video_bitrate_kbps);
        config.fec_enabled = fec_enabled;
        config.fec_redundancy = std::clamp(
            fec_redundancy, minimum_fec_redundancy, maximum_fec_redundancy);
        config.brightness = std::clamp(brightness, -100, 100);
        config.contrast = std::clamp(contrast, -100, 100);
        config.sharpness = std::clamp(sharpness, 0, 100);
        config.denoise = std::clamp(denoise, 0, 100);
        config.low_latency = low_latency;
        config.vertical_sync = vertical_sync;
    }
    impl_->low_latency_ = low_latency;
    std::ostringstream json;
    json << "{\"target_fps\":" << std::clamp(frame_rate, 24, 240)
         << ",\"bitrate\":" << std::clamp(
                bitrate_kbps, minimum_video_bitrate_kbps, maximum_video_bitrate_kbps)
         << ",\"jpeg_quality\":" << jpeg_quality
         << ",\"encoder\":\"" << (encoder_index == 0 ? "jpeg" : "h264")
         << "\",\"fec_enabled\":" << (fec_enabled ? "true" : "false")
         << ",\"fec_redundancy\":" << std::clamp(
                fec_redundancy, minimum_fec_redundancy, maximum_fec_redundancy)
         << ",\"brightness\":" << std::clamp(brightness, -100, 100)
         << ",\"contrast\":" << std::clamp(contrast, -100, 100)
         << ",\"sharpness\":" << std::clamp(sharpness, 0, 100)
         << ",\"denoise\":" << std::clamp(denoise, 0, 100) << '}';
    std::string parameters = json.str();
    parameters.insert(parameters.size() - 1,
                      ",\"udp_mtu\":" + std::to_string(udp_mtu));
    impl_->enqueue_parameters(std::move(parameters));
    impl_->last_codec_ = static_cast<std::uint8_t>(encoder_index == 0 ? 0 : 1);
    impl_->decoder_.set_h264_preference(std::clamp(decoder_index, 0, 2));
    impl_->save_config();
    impl_->append_audit("INFO", "图传参数已提交");
}

void GroundStationBackendRuntime::preview_display_settings(
    int resolution_index, int window_mode, int display_index) {
    if (impl_->window_ == nullptr) return;
    constexpr std::array<std::pair<int, int>, 6> resolutions{{
        {960, 540}, {1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160}}};
    if (!impl_->display_snapshot_.valid) {
        SDL_GetWindowPosition(impl_->window_, &impl_->display_snapshot_.x,
                              &impl_->display_snapshot_.y);
        SDL_GetWindowSize(impl_->window_, &impl_->display_snapshot_.width,
                          &impl_->display_snapshot_.height);
        impl_->display_snapshot_.fullscreen =
            (SDL_GetWindowFlags(impl_->window_) & SDL_WINDOW_FULLSCREEN) != 0;
        impl_->display_snapshot_.valid = true;
    }
    const auto resolution = resolutions[static_cast<std::size_t>(
        std::clamp(resolution_index, 0, static_cast<int>(resolutions.size() - 1)))];
    int display_count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&display_count);
    const int selected_display = std::clamp(display_index, 0, std::max(0, display_count - 1));
    SDL_Rect bounds{};
    if (displays != nullptr && display_count > 0) {
        SDL_GetDisplayBounds(displays[selected_display], &bounds);
        SDL_free(displays);
    }
    SDL_SetWindowFullscreen(impl_->window_, false);
    SDL_SetWindowPosition(impl_->window_, bounds.x + (bounds.w - resolution.first) / 2,
                          bounds.y + (bounds.h - resolution.second) / 2);
    SDL_SetWindowSize(impl_->window_, resolution.first, resolution.second);
    if (window_mode == 1) SDL_SetWindowFullscreen(impl_->window_, true);
    impl_->pending_resolution_index_ = std::clamp(resolution_index, 0, 5);
    impl_->pending_window_mode_ = std::clamp(window_mode, 0, 1);
    impl_->pending_display_index_ = std::max(0, display_index);
    impl_->append_audit("INFO", "正在预览显示设置");
}

void GroundStationBackendRuntime::confirm_display_settings() {
    impl_->display_snapshot_.valid = false;
    {
        std::lock_guard lock(impl_->config_mutex_);
        impl_->config_.resolution_index = impl_->pending_resolution_index_;
        impl_->config_.window_mode = impl_->pending_window_mode_;
        impl_->config_.display_index = impl_->pending_display_index_;
        impl_->config_.display_configured = true;
    }
    impl_->save_config();
    impl_->append_audit("INFO", "显示设置已确认");
}

void GroundStationBackendRuntime::revert_display_settings() {
    if (impl_->window_ == nullptr || !impl_->display_snapshot_.valid) return;
    SDL_SetWindowFullscreen(impl_->window_, false);
    SDL_SetWindowPosition(impl_->window_, impl_->display_snapshot_.x,
                          impl_->display_snapshot_.y);
    SDL_SetWindowSize(impl_->window_, impl_->display_snapshot_.width,
                      impl_->display_snapshot_.height);
    if (impl_->display_snapshot_.fullscreen) SDL_SetWindowFullscreen(impl_->window_, true);
    impl_->display_snapshot_.valid = false;
    impl_->append_audit("WARN", "显示设置已回滚");
}

void GroundStationBackendRuntime::apply_control_settings(
    float mouse_sensitivity, bool invert_y, bool capture_mouse, bool send_keyboard) {
    {
        std::lock_guard lock(impl_->config_mutex_);
        impl_->config_.mouse_sensitivity = std::clamp(mouse_sensitivity, 0.05F, 5.0F);
        impl_->config_.invert_y = invert_y;
        impl_->config_.capture_mouse = capture_mouse;
        impl_->config_.send_keyboard = send_keyboard;
    }
    impl_->save_config();
}

void GroundStationBackendRuntime::apply_interface_settings(
    float hud_scale, float hud_opacity, bool show_input, bool show_statistics,
    bool show_ready, int language_index) {
    {
        std::lock_guard lock(impl_->config_mutex_);
        impl_->config_.hud_scale = std::clamp(hud_scale, 0.5F, 2.0F);
        impl_->config_.hud_opacity = std::clamp(hud_opacity, 0.2F, 1.0F);
        impl_->config_.show_input = show_input;
        impl_->config_.show_statistics = show_statistics;
        impl_->config_.show_ready = show_ready;
        impl_->config_.language_index = std::max(0, language_index);
    }
    impl_->save_config();
}

void GroundStationBackendRuntime::apply_diagnostics_settings(
    bool show_performance_graph, bool show_debug_info, bool verbose_log) {
    {
        std::lock_guard lock(impl_->config_mutex_);
        impl_->config_.show_performance_graph = show_performance_graph;
        impl_->config_.show_debug_info = show_debug_info;
        impl_->config_.verbose_log = verbose_log;
    }
    impl_->save_config();
}

void GroundStationBackendRuntime::set_ready(bool ready) {
    bool accepted = false;
    {
        std::lock_guard lock(impl_->state_mutex_);
        accepted = !ready || (impl_->state_.connection == ConnectionState::connected &&
                              impl_->state_.video_available);
        impl_->state_.ready = accepted && ready;
    }
    if (!accepted) impl_->append_audit("WARN", "控制链路或视频未就绪，拒绝进入 READY");
    else impl_->append_audit("INFO", ready ? "已进入 READY" : "已退出 READY");
}

MediaActionResult GroundStationBackendRuntime::start_recording(
    const std::string& directory, int format_index, int quality, int split_minutes) {
    {
        std::lock_guard lock(impl_->state_mutex_);
        if (impl_->state_.recording == RecordingState::recording ||
            impl_->state_.recording == RecordingState::paused ||
            impl_->state_.recording == RecordingState::starting) {
            return {false, "录像已经在运行"};
        }
        impl_->state_.recording = RecordingState::starting;
    }
    {
        std::lock_guard lock(impl_->config_mutex_);
        impl_->config_.recording_directory = directory;
        impl_->config_.recording_format = std::clamp(format_index, 0, 2);
        impl_->config_.recording_quality = std::clamp(quality, 1, 100);
        impl_->config_.split_minutes = std::clamp(split_minutes, 0, 120);
    }
    impl_->save_config();
    {
        std::lock_guard lock(impl_->composite_mutex_);
        impl_->pending_recording_directory_ = media_output_directory(directory);
        impl_->pending_recording_format_ = std::clamp(format_index, 0, 2);
        impl_->pending_recording_quality_ = std::clamp(quality, 1, 100);
        impl_->pending_recording_split_minutes_ = std::clamp(split_minutes, 0, 120);
    }
    const std::string message = "正在启动完整窗口录像";
    impl_->append_audit("INFO", message);
    return {true, message};
}

MediaActionResult GroundStationBackendRuntime::set_recording_paused(bool paused) {
    {
        std::lock_guard lock(impl_->state_mutex_);
        const RecordingState expected = paused ? RecordingState::recording
                                               : RecordingState::paused;
        if (impl_->state_.recording != expected) {
            return {false, paused ? "当前录像无法暂停" : "当前录像无法继续"};
        }
        impl_->state_.recording = paused ? RecordingState::paused
                                         : RecordingState::recording;
    }
    const std::string message = paused ? "录像已暂停" : "录像已继续";
    impl_->append_audit("INFO", message);
    return {true, message};
}

void GroundStationBackendRuntime::stop_recording() {
    {
        std::lock_guard lock(impl_->state_mutex_);
        if (impl_->state_.recording != RecordingState::recording &&
            impl_->state_.recording != RecordingState::paused &&
            impl_->state_.recording != RecordingState::starting &&
            impl_->state_.recording != RecordingState::failed) return;
        impl_->state_.recording = RecordingState::stopping;
    }
    const bool recorder_active = impl_->recorder_.active();
    const auto path = impl_->recorder_.output_path();
    impl_->recorder_.stop();
    {
        std::lock_guard lock(impl_->state_mutex_);
        impl_->state_.recording = RecordingState::idle;
    }
    impl_->append_audit("INFO", recorder_active ? "录像已保存: " + path.string()
                                                 : "录像启动已取消");
}

MediaActionResult GroundStationBackendRuntime::take_screenshot(
    const std::string& directory) {
    std::filesystem::path target = media_output_directory(directory);
    std::error_code filesystem_error;
    std::filesystem::create_directories(target, filesystem_error);
    if (filesystem_error) {
        const std::string error = "无法创建截图目录: " + filesystem_error.message();
        impl_->append_audit("ERROR", error);
        return {false, error};
    }
    target /= utf16("pip_link_" + local_time("%Y%m%d_%H%M%S") + "_" +
                    std::to_string(GetTickCount64() % 1000) + ".png");
    {
        std::lock_guard lock(impl_->composite_mutex_);
        if (impl_->pending_screenshot_) return {false, "已有截图请求正在处理"};
        impl_->pending_screenshot_ = target;
    }
    return {true, "正在截取完整窗口画面"};
}

bool GroundStationBackendRuntime::needs_composited_frame() const {
    {
        std::lock_guard lock(impl_->composite_mutex_);
        if (impl_->pending_screenshot_) return true;
    }
    const RecordingState state = runtime_state().recording;
    return state == RecordingState::starting || state == RecordingState::recording;
}

void GroundStationBackendRuntime::submit_composited_frame(CompositedFrame frame) {
    media::DecodedFrame media_frame{frame.width, frame.height, std::move(frame.rgba)};
    std::optional<std::filesystem::path> screenshot;
    {
        std::lock_guard lock(impl_->composite_mutex_);
        screenshot = std::exchange(impl_->pending_screenshot_, std::nullopt);
    }
    if (screenshot) {
        std::string error;
        const media::DecodedFrame screenshot_frame = rgba_to_bgra_for_png(media_frame);
        if (media::save_png(*screenshot, screenshot_frame, error)) {
            {
                std::lock_guard lock(impl_->state_mutex_);
                ++impl_->state_.screenshot_revision;
            }
            impl_->append_audit("INFO", "截图已保存: " + screenshot->string());
        } else {
            impl_->append_audit("ERROR", error);
        }
    }

    RecordingState state;
    {
        std::lock_guard lock(impl_->state_mutex_);
        state = impl_->state_.recording;
    }
    if (state == RecordingState::starting) {
        std::filesystem::path directory;
        int format = 0;
        int quality = 85;
        int split_minutes = 0;
        {
            std::lock_guard lock(impl_->composite_mutex_);
            directory = impl_->pending_recording_directory_;
            format = impl_->pending_recording_format_;
            quality = impl_->pending_recording_quality_;
            split_minutes = impl_->pending_recording_split_minutes_;
        }
        std::string error;
        if (!impl_->recorder_.start(directory, format, quality, split_minutes,
                                    frame.width, frame.height, 30, error)) {
            {
                std::lock_guard lock(impl_->state_mutex_);
                impl_->state_.recording = RecordingState::failed;
            }
            impl_->append_audit("ERROR", error);
            return;
        }
        {
            std::lock_guard lock(impl_->state_mutex_);
            impl_->state_.recording = RecordingState::recording;
        }
        impl_->append_audit("INFO", "完整窗口录像已开始: " +
                                     impl_->recorder_.output_path().string());
        state = RecordingState::recording;
    }
    if (state == RecordingState::recording) impl_->recorder_.write(std::move(media_frame));
}

MediaActionResult GroundStationBackendRuntime::open_recordings_folder(
    const std::string& directory) {
    const std::filesystem::path path = media_output_directory(directory);
    std::error_code error;
    std::filesystem::create_directories(path, error);
    if (error) {
        impl_->append_audit("ERROR", "无法创建录像目录: " + error.message());
        return {false, "无法创建录像目录: " + error.message()};
    }
    const auto result = reinterpret_cast<std::intptr_t>(
        ShellExecuteW(nullptr, L"open", path.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) {
        impl_->append_audit("ERROR", "无法打开录像目录");
        return {false, "无法打开录像目录"};
    }
    return {true, "保存目录已打开: " + path.string()};
}

DirectorySelectionResult GroundStationBackendRuntime::choose_recording_directory(
    const std::string& current_directory) {
    const HRESULT com_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IFileOpenDialog* dialog = nullptr;
    IShellItem* initial_item = nullptr;
    IShellItem* selected_item = nullptr;
    PWSTR selected_path = nullptr;
    DirectorySelectionResult response;

    HRESULT result = CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                      CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (SUCCEEDED(result)) {
        FILEOPENDIALOGOPTIONS options{};
        result = dialog->GetOptions(&options);
        if (SUCCEEDED(result)) {
            result = dialog->SetOptions(options | FOS_PICKFOLDERS |
                                        FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        }
    }
    if (SUCCEEDED(result)) {
        dialog->SetTitle(L"选择录像与截图保存目录");
        const std::filesystem::path initial_path =
            media_output_directory(current_directory);
        std::error_code filesystem_error;
        std::filesystem::create_directories(initial_path, filesystem_error);
        if (!filesystem_error &&
            SUCCEEDED(SHCreateItemFromParsingName(initial_path.c_str(), nullptr,
                                                  IID_PPV_ARGS(&initial_item)))) {
            dialog->SetFolder(initial_item);
        }
        HWND owner = nullptr;
        if (impl_->window_ != nullptr) {
            const SDL_PropertiesID properties = SDL_GetWindowProperties(impl_->window_);
            owner = static_cast<HWND>(SDL_GetPointerProperty(
                properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        }
        result = dialog->Show(owner);
    }
    if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        result = S_OK;
    } else if (SUCCEEDED(result)) {
        result = dialog->GetResult(&selected_item);
        if (SUCCEEDED(result)) {
            result = selected_item->GetDisplayName(SIGDN_FILESYSPATH, &selected_path);
        }
        if (SUCCEEDED(result) && selected_path != nullptr) {
            response.directory = utf8(selected_path);
            response.selected = !response.directory.empty();
            if (response.selected) {
                response.message = "保存目录已更新: " + response.directory;
            } else {
                result = E_UNEXPECTED;
            }
        }
    }
    if (FAILED(result)) response.message = com_error("无法选择保存目录", result);

    if (selected_path != nullptr) CoTaskMemFree(selected_path);
    release(selected_item);
    release(initial_item);
    release(dialog);
    if (SUCCEEDED(com_result)) CoUninitialize();

    if (response.selected) {
        {
            std::lock_guard lock(impl_->config_mutex_);
            impl_->config_.recording_directory = response.directory;
        }
        impl_->save_config();
        impl_->append_audit("INFO", response.message);
    } else if (!response.message.empty()) {
        impl_->append_audit("ERROR", response.message);
    }
    return response;
}

void GroundStationBackendRuntime::save_key_bindings(const std::vector<int>& bindings) {
    {
        std::lock_guard lock(impl_->config_mutex_);
        impl_->config_.key_bindings = bindings;
    }
    impl_->save_config();
    impl_->append_audit("INFO", "键位绑定已保存");
}

void GroundStationBackendRuntime::apply_gamepad_settings(float deadzone, bool vibration) {
    {
        std::lock_guard lock(impl_->config_mutex_);
        impl_->config_.gamepad_deadzone = std::clamp(deadzone, 0.0F, 0.5F);
        impl_->config_.gamepad_vibration = vibration;
    }
    impl_->save_config();
}

void GroundStationBackendRuntime::export_diagnostics() {
    const std::filesystem::path directory = impl_->data_directory_ / "diagnostics";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    const auto path = directory / ("diagnostics_" + local_time("%Y%m%d_%H%M%S") + ".txt");
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    const auto telemetry = this->telemetry();
    const auto state = this->runtime_state();
    output << "PIP-Link diagnostics\n"
           << "version=" << core::BuildInfo::version() << '\n'
           << "connection=" << static_cast<int>(state.connection) << '\n'
           << "recording=" << static_cast<int>(state.recording) << '\n'
           << "fps=" << telemetry.fps << '\n'
           << "latency_ms=" << telemetry.latency_ms << '\n'
           << "packet_loss_percent=" << telemetry.packet_loss_percent << '\n'
           << "bandwidth_mbps=" << telemetry.bandwidth_mbps << '\n'
           << "decoded_frames=" << telemetry.decoded_frames << '\n'
           << "dropped_frames=" << impl_->dropped_frames_ << '\n'
           << "recovered_frames=" << impl_->recovered_frames_ << '\n'
           << "decode_errors=" << impl_->decode_errors_ << '\n'
           << "invalid_packets=" << impl_->invalid_packets_ << '\n'
           << "decode_ms=" << impl_->last_decode_ms_ << '\n'
           << "remote_encode_ms=" << impl_->last_encode_ms_ << '\n';
    if (output) impl_->append_audit("INFO", "诊断报告已导出: " + path.string());
    else impl_->append_audit("ERROR", "诊断报告导出失败");
}

void GroundStationBackendRuntime::export_audit_log() {
    const auto path = impl_->data_directory_ /
                      ("audit_export_" + local_time("%Y%m%d_%H%M%S") + ".csv");
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << "time,level,message\n";
    for (const auto& entry : audit_entries()) {
        output << csv_escape(entry.time) << ',' << csv_escape(entry.level) << ','
               << csv_escape(entry.message) << '\n';
    }
    if (output) impl_->append_audit("INFO", "审计日志已导出: " + path.string());
    else impl_->append_audit("ERROR", "审计日志导出失败");
}

void GroundStationBackendRuntime::clear_audit_log() {
    {
        std::lock_guard lock(impl_->audit_mutex_);
        impl_->audit_entries_.clear();
        std::ofstream output(impl_->audit_path_, std::ios::trunc);
    }
    impl_->append_audit("INFO", "审计日志已清空");
}

void GroundStationBackendRuntime::submit_control_input(const ControlInput& input) {
    std::lock_guard lock(impl_->input_mutex_);
    impl_->latest_input_.mouse_delta_x += input.mouse_delta_x;
    impl_->latest_input_.mouse_delta_y += input.mouse_delta_y;
    impl_->latest_input_.mouse_wheel = std::clamp(
        impl_->latest_input_.mouse_wheel + input.mouse_wheel, -127, 127);
    impl_->latest_input_.mouse_buttons = input.mouse_buttons;
    impl_->latest_input_.keyboard = input.keyboard;
}

std::string GroundStationBackendRuntime::execute_console_command(const std::string& command) {
    const std::string normalized = trim(command);
    if (normalized == "help") {
        return "命令: help, status, stats, reconnect, disconnect, export diagnostics, "
               "export log, clear log, version";
    }
    if (normalized == "status") {
        const auto state = runtime_state();
        std::string endpoint;
        {
            std::lock_guard lock(impl_->session_mutex_);
            endpoint = impl_->current_endpoint_.display;
        }
        std::ostringstream output;
        output << "connection=" << static_cast<int>(state.connection)
               << " recording=" << static_cast<int>(state.recording)
               << " ready=" << (state.ready ? "true" : "false")
               << " endpoint=" << endpoint;
        return output.str();
    }
    if (normalized == "stats") {
        const auto value = telemetry();
        std::ostringstream output;
        output << std::fixed << std::setprecision(2)
               << "fps=" << value.fps << " latency_ms=" << value.latency_ms
               << " loss=" << value.packet_loss_percent << "% bandwidth="
               << value.bandwidth_mbps << "Mbps decoded=" << value.decoded_frames;
        return output.str();
    }
    if (normalized == "reconnect") {
        std::optional<DeviceInfo> device;
        {
            std::lock_guard lock(impl_->request_mutex_);
            device = impl_->requested_device_;
        }
        if (!device) return "没有可重连的机器人";
        impl_->request_connection(*device);
        return "已请求重连";
    }
    if (normalized == "disconnect") {
        disconnect_device();
        return "已请求断开";
    }
    if (normalized == "export diagnostics") {
        export_diagnostics();
        return "诊断报告已导出";
    }
    if (normalized == "export log") {
        export_audit_log();
        return "审计日志已导出";
    }
    if (normalized == "clear log") {
        clear_audit_log();
        return "审计日志已清空";
    }
    if (normalized == "version") return std::string(core::BuildInfo::version());
    if (normalized.empty()) return {};
    return "未知命令: " + normalized + "；输入 help 查看可用命令";
}

bool GroundStationBackendRuntime::vertical_sync_enabled() const {
    std::lock_guard lock(impl_->config_mutex_);
    return impl_->config_.vertical_sync;
}

}  // namespace pip_link::backend
