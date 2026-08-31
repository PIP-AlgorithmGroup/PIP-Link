#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace pip_link::backend {

struct DeviceInfo final {
    std::string name;
    std::string address;
    int signal_percent{};
};

struct TelemetrySnapshot final {
    float fps{};
    float latency_ms{};
    float packet_loss_percent{};
    float bandwidth_mbps{};
    int decoded_frames{};
};

struct AuditEntry final {
    std::string time;
    std::string level;
    std::string message;
};

struct VideoSurface final {
    void* native_texture{};
    int width{};
    int height{};
};

struct ControlInput final {
    float mouse_delta_x{};
    float mouse_delta_y{};
    std::uint32_t mouse_buttons{};
    int mouse_wheel{};
    std::array<std::uint8_t, 10> keyboard{};
};

class GroundStationBackend {
public:
    virtual ~GroundStationBackend() = default;

    [[nodiscard]] virtual std::vector<DeviceInfo> discovered_devices() const = 0;
    [[nodiscard]] virtual TelemetrySnapshot telemetry() const = 0;
    [[nodiscard]] virtual std::vector<AuditEntry> audit_entries() const = 0;
    [[nodiscard]] virtual VideoSurface latest_video_surface() const = 0;

    virtual void scan_devices(const std::string& service_name) = 0;
    virtual void connect_device(const DeviceInfo& device) = 0;
    virtual void disconnect_device() = 0;
    virtual void apply_connection_settings(int heartbeat_ms, int reconnect_seconds,
                                           int mtu, bool auto_reconnect) = 0;
    virtual void apply_input_settings(float mouse_sensitivity, float field_of_view,
                                      bool invert_pitch) = 0;
    virtual void apply_video_settings(int quality_index, int resolution_index,
                                      int window_mode, int encoder_index,
                                      int decoder_index, int frame_rate,
                                      int bitrate_kbps, bool fec_enabled,
                                      float fec_redundancy, int brightness,
                                      int contrast, int sharpness, int denoise,
                                      bool low_latency, bool vertical_sync) = 0;
    virtual void apply_control_settings(float mouse_sensitivity, bool invert_y,
                                        bool capture_mouse, bool send_keyboard) = 0;
    virtual void set_ready(bool ready) = 0;
    virtual void start_recording(const std::string& directory, int format_index,
                                 int quality, int split_minutes) = 0;
    virtual void stop_recording() = 0;
    virtual void take_screenshot(const std::string& directory) = 0;
    virtual void open_recordings_folder(const std::string& directory) = 0;
    virtual void save_key_bindings(const std::vector<int>& bindings) = 0;
    virtual void apply_gamepad_settings(float deadzone, bool vibration) = 0;
    virtual void export_diagnostics() = 0;
    virtual void export_audit_log() = 0;
    virtual void clear_audit_log() = 0;
    virtual void submit_control_input(const ControlInput& input) = 0;
    [[nodiscard]] virtual std::string execute_console_command(const std::string& command) = 0;
};

class GroundStationBackendStub final : public GroundStationBackend {
public:
    [[nodiscard]] std::vector<DeviceInfo> discovered_devices() const override;
    [[nodiscard]] TelemetrySnapshot telemetry() const override;
    [[nodiscard]] std::vector<AuditEntry> audit_entries() const override;
    [[nodiscard]] VideoSurface latest_video_surface() const override;

    void scan_devices(const std::string& service_name) override;
    void connect_device(const DeviceInfo& device) override;
    void disconnect_device() override;
    void apply_connection_settings(int heartbeat_ms, int reconnect_seconds,
                                   int mtu, bool auto_reconnect) override;
    void apply_input_settings(float mouse_sensitivity, float field_of_view,
                              bool invert_pitch) override;
    void apply_video_settings(int quality_index, int resolution_index,
                              int window_mode, int encoder_index,
                              int decoder_index, int frame_rate,
                              int bitrate_kbps, bool fec_enabled,
                              float fec_redundancy, int brightness,
                              int contrast, int sharpness, int denoise,
                              bool low_latency, bool vertical_sync) override;
    void apply_control_settings(float mouse_sensitivity, bool invert_y,
                                bool capture_mouse, bool send_keyboard) override;
    void set_ready(bool ready) override;
    void start_recording(const std::string& directory, int format_index,
                         int quality, int split_minutes) override;
    void stop_recording() override;
    void take_screenshot(const std::string& directory) override;
    void open_recordings_folder(const std::string& directory) override;
    void save_key_bindings(const std::vector<int>& bindings) override;
    void apply_gamepad_settings(float deadzone, bool vibration) override;
    void export_diagnostics() override;
    void export_audit_log() override;
    void clear_audit_log() override;
    void submit_control_input(const ControlInput& input) override;
    [[nodiscard]] std::string execute_console_command(const std::string& command) override;
};

}  // namespace pip_link::backend
