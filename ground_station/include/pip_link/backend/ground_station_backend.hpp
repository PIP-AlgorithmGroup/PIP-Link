#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct SDL_Window;
struct ID3D11Device;
struct ID3D11DeviceContext;

namespace pip_link::backend {

struct DeviceInfo final {
    std::string name;
    std::string address;
    int signal_percent{};
    std::uint16_t video_port{};
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

struct CompositedFrame final {
    int width{};
    int height{};
    std::vector<std::uint8_t> rgba;
};

struct ControlInput final {
    float mouse_delta_x{};
    float mouse_delta_y{};
    std::uint32_t mouse_buttons{};
    int mouse_wheel{};
    std::array<std::uint8_t, 10> keyboard{};
};

enum class ConnectionState {
    disconnected,
    connecting,
    connected,
    disconnecting,
    failed,
};

enum class RecordingState {
    idle,
    starting,
    recording,
    stopping,
    failed,
};

struct RuntimeState final {
    ConnectionState connection{ConnectionState::disconnected};
    RecordingState recording{RecordingState::idle};
    bool ready{};
    bool video_available{};
    std::uint64_t remote_parameters_revision{};
};

struct BackendPreferences final {
    int heartbeat_ms{1000};
    int reconnect_seconds{3};
    int mtu{1400};
    bool auto_reconnect{true};
    float mouse_sensitivity{1.0F};
    float field_of_view{90.0F};
    bool invert_pitch{};
    int quality_index{2};
    int jpeg_quality{85};
    int resolution_index{3};
    int window_mode{};
    int display_index{};
    bool display_configured{};
    int encoder_index{1};
    int decoder_index{};
    int frame_rate{60};
    int bitrate_kbps{12000};
    bool fec_enabled{};
    float fec_redundancy{0.2F};
    int brightness{};
    int contrast{};
    int sharpness{};
    int denoise{};
    bool low_latency{true};
    bool vertical_sync{true};
    bool invert_y{};
    bool capture_mouse{true};
    bool send_keyboard{true};
    float hud_scale{1.0F};
    float hud_opacity{0.86F};
    bool show_input{true};
    bool show_statistics{true};
    bool show_ready{true};
    int language_index{};
    bool show_performance_graph{true};
    bool show_debug_info{};
    bool verbose_log{};
    float gamepad_deadzone{0.15F};
    bool gamepad_vibration{true};
    int recording_format{};
    int recording_quality{85};
    int split_minutes{30};
    std::string service_name{"_pip-link._udp.local"};
    std::string last_endpoint{"192.168.1.10:6000"};
    int last_video_port{5000};
    std::string recording_directory{"recordings"};
    std::vector<int> key_bindings;
};

struct MediaActionResult final {
    bool succeeded{};
    std::string message;
};

struct DirectorySelectionResult final {
    bool selected{};
    std::string directory;
    std::string message;
};

class GroundStationBackend {
public:
    virtual ~GroundStationBackend() = default;

    [[nodiscard]] virtual std::vector<DeviceInfo> discovered_devices() const = 0;
    [[nodiscard]] virtual TelemetrySnapshot telemetry() const = 0;
    [[nodiscard]] virtual std::vector<AuditEntry> audit_entries() const = 0;
    [[nodiscard]] virtual VideoSurface latest_video_surface() const = 0;
    [[nodiscard]] virtual RuntimeState runtime_state() const = 0;
    [[nodiscard]] virtual BackendPreferences preferences() const = 0;

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
    virtual void preview_display_settings(int resolution_index, int window_mode,
                                          int display_index) = 0;
    virtual void confirm_display_settings() = 0;
    virtual void revert_display_settings() = 0;
    virtual void apply_control_settings(float mouse_sensitivity, bool invert_y,
                                        bool capture_mouse, bool send_keyboard) = 0;
    virtual void apply_interface_settings(float hud_scale, float hud_opacity,
                                          bool show_input, bool show_statistics,
                                          bool show_ready, int language_index) = 0;
    virtual void apply_diagnostics_settings(bool show_performance_graph,
                                            bool show_debug_info,
                                            bool verbose_log) = 0;
    virtual void set_ready(bool ready) = 0;
    [[nodiscard]] virtual MediaActionResult start_recording(
        const std::string& directory, int format_index, int quality,
        int split_minutes) = 0;
    virtual void stop_recording() = 0;
    [[nodiscard]] virtual MediaActionResult take_screenshot(
        const std::string& directory) = 0;
    [[nodiscard]] virtual bool needs_composited_frame() const = 0;
    virtual void submit_composited_frame(CompositedFrame frame) = 0;
    [[nodiscard]] virtual MediaActionResult open_recordings_folder(
        const std::string& directory) = 0;
    [[nodiscard]] virtual DirectorySelectionResult choose_recording_directory(
        const std::string& current_directory) = 0;
    virtual void save_key_bindings(const std::vector<int>& bindings) = 0;
    virtual void apply_gamepad_settings(float deadzone, bool vibration) = 0;
    virtual void export_diagnostics() = 0;
    virtual void export_audit_log() = 0;
    virtual void clear_audit_log() = 0;
    virtual void submit_control_input(const ControlInput& input) = 0;
    [[nodiscard]] virtual std::string execute_console_command(const std::string& command) = 0;
};

class GroundStationBackendStub : public GroundStationBackend {
public:
    [[nodiscard]] std::vector<DeviceInfo> discovered_devices() const override;
    [[nodiscard]] TelemetrySnapshot telemetry() const override;
    [[nodiscard]] std::vector<AuditEntry> audit_entries() const override;
    [[nodiscard]] VideoSurface latest_video_surface() const override;
    [[nodiscard]] RuntimeState runtime_state() const override;
    [[nodiscard]] BackendPreferences preferences() const override;

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
    void preview_display_settings(int resolution_index, int window_mode,
                                  int display_index) override;
    void confirm_display_settings() override;
    void revert_display_settings() override;
    void apply_control_settings(float mouse_sensitivity, bool invert_y,
                                bool capture_mouse, bool send_keyboard) override;
    void apply_interface_settings(float hud_scale, float hud_opacity,
                                  bool show_input, bool show_statistics,
                                  bool show_ready, int language_index) override;
    void apply_diagnostics_settings(bool show_performance_graph,
                                    bool show_debug_info,
                                    bool verbose_log) override;
    void set_ready(bool ready) override;
    [[nodiscard]] MediaActionResult start_recording(
        const std::string& directory, int format_index, int quality,
        int split_minutes) override;
    void stop_recording() override;
    [[nodiscard]] MediaActionResult take_screenshot(
        const std::string& directory) override;
    [[nodiscard]] bool needs_composited_frame() const override;
    void submit_composited_frame(CompositedFrame frame) override;
    [[nodiscard]] MediaActionResult open_recordings_folder(
        const std::string& directory) override;
    [[nodiscard]] DirectorySelectionResult choose_recording_directory(
        const std::string& current_directory) override;
    void save_key_bindings(const std::vector<int>& bindings) override;
    void apply_gamepad_settings(float deadzone, bool vibration) override;
    void export_diagnostics() override;
    void export_audit_log() override;
    void clear_audit_log() override;
    void submit_control_input(const ControlInput& input) override;
    [[nodiscard]] std::string execute_console_command(const std::string& command) override;
};

class GroundStationBackendRuntime final : public GroundStationBackend {
public:
    GroundStationBackendRuntime(SDL_Window* window, ID3D11Device* device,
                                ID3D11DeviceContext* context);
    ~GroundStationBackendRuntime() override;
    GroundStationBackendRuntime(const GroundStationBackendRuntime&) = delete;
    GroundStationBackendRuntime& operator=(const GroundStationBackendRuntime&) = delete;

    [[nodiscard]] std::vector<DeviceInfo> discovered_devices() const override;
    [[nodiscard]] TelemetrySnapshot telemetry() const override;
    [[nodiscard]] std::vector<AuditEntry> audit_entries() const override;
    [[nodiscard]] VideoSurface latest_video_surface() const override;
    [[nodiscard]] RuntimeState runtime_state() const override;
    [[nodiscard]] BackendPreferences preferences() const override;

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
    void preview_display_settings(int resolution_index, int window_mode,
                                  int display_index) override;
    void confirm_display_settings() override;
    void revert_display_settings() override;
    void apply_control_settings(float mouse_sensitivity, bool invert_y,
                                bool capture_mouse, bool send_keyboard) override;
    void apply_interface_settings(float hud_scale, float hud_opacity,
                                  bool show_input, bool show_statistics,
                                  bool show_ready, int language_index) override;
    void apply_diagnostics_settings(bool show_performance_graph,
                                    bool show_debug_info,
                                    bool verbose_log) override;
    void set_ready(bool ready) override;
    [[nodiscard]] MediaActionResult start_recording(
        const std::string& directory, int format_index, int quality,
        int split_minutes) override;
    void stop_recording() override;
    [[nodiscard]] MediaActionResult take_screenshot(
        const std::string& directory) override;
    [[nodiscard]] bool needs_composited_frame() const override;
    void submit_composited_frame(CompositedFrame frame) override;
    [[nodiscard]] MediaActionResult open_recordings_folder(
        const std::string& directory) override;
    [[nodiscard]] DirectorySelectionResult choose_recording_directory(
        const std::string& current_directory) override;
    void save_key_bindings(const std::vector<int>& bindings) override;
    void apply_gamepad_settings(float deadzone, bool vibration) override;
    void export_diagnostics() override;
    void export_audit_log() override;
    void clear_audit_log() override;
    void submit_control_input(const ControlInput& input) override;
    [[nodiscard]] std::string execute_console_command(const std::string& command) override;

    [[nodiscard]] bool vertical_sync_enabled() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace pip_link::backend
