#pragma once

#include "pip_link/backend/ground_station_backend.hpp"
#include "pip_link/core/debounced_action.hpp"

#include <array>
#include <string>
#include <vector>

namespace pip_link::ui {

class GroundStationUi final {
public:
    explicit GroundStationUi(backend::GroundStationBackend& backend);

    void draw(float delta_seconds, float display_scale);
    void on_focus_lost();
    void on_mouse_capture_failed();
    void set_mouse_capture_active(bool active) noexcept;
    [[nodiscard]] bool quit_requested() const noexcept;
    [[nodiscard]] bool wants_relative_mouse_mode() const noexcept;

private:
    void draw_fpv(float delta_seconds, float scale);
    void draw_settings(float delta_seconds, float scale);
    void draw_settings_tabs(float delta_seconds, float scale);
    void draw_console(float delta_seconds, float scale);
    void draw_connection_page(float scale);
    void draw_video_page(float scale);
    void draw_recording_page(float scale);
    void draw_diagnostics_page(float scale);
    void draw_control_page(float scale);
    void draw_audit_page(float scale);
    void draw_interface_page(float scale);
    void draw_about_dialog(float scale);
    void draw_display_confirmation(float scale);
    void open_settings();
    void leave_ready(const char* reason);
    void toggle_ready();
    void submit_control_input();
    void submit_video_settings();
    void queue_video_settings(bool flush);
    void apply_input_settings();
    void apply_control_settings();
    void apply_interface_settings();
    void begin_display_preview(int previous_resolution, int previous_window_mode,
                               int previous_display);
    void set_feedback(std::string message);
    void sync_backend_state();
    [[nodiscard]] bool is_connected() const noexcept;
    [[nodiscard]] bool is_recording() const noexcept;
    [[nodiscard]] bool connection_busy() const noexcept;

    backend::GroundStationBackend& backend_;
    bool settings_open_{false};
    bool console_open_{false};
    bool quit_requested_{false};
    bool ready_{false};
    backend::ConnectionState connection_state_{backend::ConnectionState::disconnected};
    backend::RecordingState recording_state_{backend::RecordingState::idle};
    bool scanning_{false};
    bool auto_reconnect_{true};
    bool low_latency_{true};
    bool vertical_sync_{true};
    bool fec_enabled_{false};
    bool invert_y_{false};
    bool invert_pitch_{false};
    bool capture_mouse_{true};
    bool mouse_capture_active_{false};
    bool send_keyboard_{true};
    bool show_input_hud_{true};
    bool show_status_hud_{true};
    bool show_ready_hud_{true};
    bool show_performance_graph_{true};
    bool show_debug_info_{false};
    bool verbose_log_{false};
    bool simulation_mode_{false};
    bool gamepad_vibration_{true};
    bool about_open_{false};
    bool display_confirmation_open_{false};
    bool nested_scroll_consumed_{false};
    int active_settings_tab_{0};
    int selected_device_{-1};
    int heartbeat_ms_{1000};
    int reconnect_seconds_{3};
    int mtu_{1400};
    int resolution_index_{3};
    int quality_index_{2};
    int window_mode_{0};
    int display_index_{0};
    int previous_resolution_index_{3};
    int previous_window_mode_{0};
    int previous_display_index_{0};
    int encoder_index_{1};
    int decoder_index_{0};
    int frame_rate_{60};
    int bitrate_kbps_{12000};
    int recording_format_{0};
    int recording_quality_{85};
    int split_minutes_{30};
    int brightness_{0};
    int contrast_{0};
    int sharpness_{0};
    int denoise_{0};
    int language_index_{0};
    int rebinding_action_{-1};
    float mouse_sensitivity_{1.0F};
    float field_of_view_{90.0F};
    float fec_redundancy_{0.20F};
    float gamepad_deadzone_{0.15F};
    float hud_scale_{1.0F};
    float hud_opacity_{0.86F};
    float mouse_indicator_x_{0.0F};
    float mouse_indicator_y_{0.0F};
    float console_height_{0.0F};
    std::array<float, 7> settings_scroll_targets_{};
    float tab_scroll_target_{0.0F};
    float console_scroll_target_{0.0F};
    float device_scroll_target_{0.0F};
    double recording_started_at_{0.0};
    double scanning_started_at_{0.0};
    double display_confirmation_deadline_{0.0};
    std::array<int, 11> key_bindings_{};
    std::array<float, 7> tab_hover_{};
    std::array<float, 120> fps_history_{};
    std::array<float, 120> latency_history_{};
    std::array<char, 128> service_name_{"_pip-link._udp.local"};
    std::array<char, 128> manual_address_{"192.168.1.10:5800"};
    std::array<char, 260> recording_directory_{"recordings"};
    std::array<char, 128> audit_filter_{};
    std::array<char, 256> console_command_{};
    std::vector<std::string> console_lines_{"PIP-Link developer console", "输入 help 查看后端命令"};
    std::string feedback_{"前端已就绪，后端接口等待实现"};
    core::DebouncedAction video_settings_debounce_{0.12F};
};

}  // namespace pip_link::ui
