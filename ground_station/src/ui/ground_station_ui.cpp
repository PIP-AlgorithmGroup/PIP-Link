#include "pip_link/ui/ground_station_ui.hpp"

#include "pip_link/core/smooth_scroll.hpp"
#include "pip_link/ui/control_input_mapping.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>

namespace pip_link::ui {
namespace {

constexpr ImVec4 accent{0.00F, 0.44F, 0.62F, 1.00F};
constexpr ImVec4 text_primary{0.07F, 0.11F, 0.16F, 1.00F};
constexpr ImVec4 text_secondary{0.28F, 0.35F, 0.42F, 1.00F};
constexpr ImVec4 success{0.04F, 0.45F, 0.25F, 1.00F};
constexpr ImVec4 warning{0.66F, 0.32F, 0.02F, 1.00F};
constexpr ImVec4 danger{0.70F, 0.13F, 0.10F, 1.00F};

enum BindingIndex : int {
    toggle_ready_binding,
    toggle_hud_binding,
    toggle_console_binding,
    toggle_settings_binding,
    forward_binding,
    backward_binding,
    left_binding,
    right_binding,
    sprint_binding,
    special_one_binding,
    special_two_binding,
};

struct SettingsTab final {
    const char* label;
    const char* title;
    const char* description;
};

constexpr std::array<SettingsTab, 7> tabs{{
    {"连接", "连接", "发现机器人、建立会话并管理链路行为"},
    {"图传", "图传", "码流、解码、显示模式和图像增强"},
    {"控制", "控制", "第一视角输入、键位、手柄与安全策略"},
    {"录制", "录制", "录像、截图、文件格式和保存位置"},
    {"诊断", "诊断", "实时链路、视频性能、错误和开发信息"},
    {"日志", "日志", "检索、导出和管理操作记录"},
    {"界面", "界面", "HUD 显示、缩放、透明度和语言"},
}};

ImU32 color_with_alpha(const ImVec4& color, float alpha) {
    ImVec4 adjusted = color;
    adjusted.w *= std::clamp(alpha, 0.0F, 1.0F);
    return ImGui::GetColorU32(adjusted);
}

void draw_key_tag(ImDrawList* draw, ImVec2& cursor, const ImVec2 bounds,
                  const char* label, float scale, float font_size, float opacity) {
    const ImVec2 text_size = ImGui::GetFont()->CalcTextSizeA(
        font_size, 10000.0F, 0.0F, label);
    const float width = text_size.x + 15.0F * scale;
    const float height = 24.0F * scale;
    if (cursor.x + width > bounds.x) {
        cursor.x = bounds.y;
        cursor.y += height + 5.0F * scale;
    }
    draw->AddRectFilled(cursor, {cursor.x + width, cursor.y + height},
                        IM_COL32(0, 176, 216, static_cast<int>(205.0F * opacity)),
                        5.0F * scale);
    draw->AddText(ImGui::GetFont(), font_size,
                  {cursor.x + 7.5F * scale,
                   cursor.y + (height - text_size.y) * 0.5F},
                  IM_COL32(242, 250, 255, static_cast<int>(255.0F * opacity)), label);
    cursor.x += width + 5.0F * scale;
}

}  // namespace

GroundStationUi::GroundStationUi(backend::GroundStationBackend& backend) : backend_(backend) {
    key_bindings_ = {
        ImGuiKey_F6, ImGuiKey_Tab, ImGuiKey_GraveAccent, ImGuiKey_Escape,
        ImGuiKey_W, ImGuiKey_S, ImGuiKey_A, ImGuiKey_D,
        ImGuiKey_LeftShift, ImGuiKey_E, ImGuiKey_F,
    };
    const backend::BackendPreferences settings = backend_.preferences();
    heartbeat_ms_ = settings.heartbeat_ms;
    reconnect_seconds_ = settings.reconnect_seconds;
    mtu_ = settings.mtu;
    auto_reconnect_ = settings.auto_reconnect;
    mouse_sensitivity_ = settings.mouse_sensitivity;
    field_of_view_ = settings.field_of_view;
    invert_pitch_ = settings.invert_pitch;
    quality_index_ = settings.quality_index;
    resolution_index_ = settings.resolution_index;
    window_mode_ = settings.window_mode;
    display_index_ = settings.display_index;
    previous_resolution_index_ = resolution_index_;
    previous_window_mode_ = window_mode_;
    previous_display_index_ = display_index_;
    encoder_index_ = settings.encoder_index;
    decoder_index_ = settings.decoder_index;
    frame_rate_ = settings.frame_rate;
    bitrate_kbps_ = settings.bitrate_kbps;
    fec_enabled_ = settings.fec_enabled;
    fec_redundancy_ = settings.fec_redundancy;
    brightness_ = settings.brightness;
    contrast_ = settings.contrast;
    sharpness_ = settings.sharpness;
    denoise_ = settings.denoise;
    low_latency_ = settings.low_latency;
    vertical_sync_ = settings.vertical_sync;
    invert_y_ = settings.invert_y;
    capture_mouse_ = settings.capture_mouse;
    send_keyboard_ = settings.send_keyboard;
    hud_scale_ = settings.hud_scale;
    hud_opacity_ = settings.hud_opacity;
    show_input_hud_ = settings.show_input;
    show_status_hud_ = settings.show_statistics;
    show_ready_hud_ = settings.show_ready;
    language_index_ = settings.language_index;
    show_performance_graph_ = settings.show_performance_graph;
    show_debug_info_ = settings.show_debug_info;
    verbose_log_ = settings.verbose_log;
    gamepad_deadzone_ = settings.gamepad_deadzone;
    gamepad_vibration_ = settings.gamepad_vibration;
    recording_format_ = settings.recording_format;
    recording_quality_ = settings.recording_quality;
    split_minutes_ = settings.split_minutes;
    std::snprintf(service_name_.data(), service_name_.size(), "%s",
                  settings.service_name.c_str());
    std::snprintf(manual_address_.data(), manual_address_.size(), "%s",
                  settings.last_endpoint.c_str());
    std::snprintf(recording_directory_.data(), recording_directory_.size(), "%s",
                  settings.recording_directory.c_str());
    if (settings.key_bindings.size() == key_bindings_.size()) {
        std::copy(settings.key_bindings.begin(), settings.key_bindings.end(),
                  key_bindings_.begin());
    }
}

bool GroundStationUi::quit_requested() const noexcept {
    return quit_requested_;
}

bool GroundStationUi::wants_relative_mouse_mode() const noexcept {
    return ready_ && capture_mouse_ && !settings_open_ && !console_open_;
}

void GroundStationUi::set_mouse_capture_active(bool active) noexcept {
    mouse_capture_active_ = active;
}

bool GroundStationUi::is_connected() const noexcept {
    return connection_state_ == backend::ConnectionState::connected;
}

bool GroundStationUi::is_recording() const noexcept {
    return recording_state_ == backend::RecordingState::recording;
}

bool GroundStationUi::connection_busy() const noexcept {
    return connection_state_ == backend::ConnectionState::connecting ||
           connection_state_ == backend::ConnectionState::disconnecting;
}

void GroundStationUi::set_feedback(std::string message) {
    feedback_ = std::move(message);
}

void GroundStationUi::leave_ready(const char* reason) {
    if (!ready_) return;
    ready_ = false;
    backend_.set_ready(false);
    set_feedback(reason);
}

void GroundStationUi::on_focus_lost() {
    leave_ready("窗口失去焦点，已自动退出 READY");
}

void GroundStationUi::on_mouse_capture_failed() {
    mouse_capture_active_ = false;
    leave_ready("无法捕获鼠标，已自动退出 READY");
}

void GroundStationUi::sync_backend_state() {
    const bool was_connected = is_connected();
    const bool was_recording = is_recording();
    const backend::RuntimeState state = backend_.runtime_state();
    connection_state_ = state.connection;
    recording_state_ = state.recording;

    if (was_connected && !is_connected()) {
        leave_ready("连接已断开，已自动退出 READY");
    }
    if (!was_recording && is_recording()) {
        recording_started_at_ = ImGui::GetTime();
    } else if (was_recording && !is_recording()) {
        recording_started_at_ = 0.0;
    }
}

void GroundStationUi::open_settings() {
    leave_ready("打开设置，已自动退出 READY");
    settings_open_ = true;
}

void GroundStationUi::toggle_ready() {
    if (settings_open_ || console_open_) return;
    if (!is_connected()) {
        set_feedback("尚未连接机器人，不能进入 READY");
        return;
    }
    ready_ = !ready_;
    backend_.set_ready(ready_);
    set_feedback(ready_ ? "已请求进入 READY 状态" : "已请求退出 READY 状态");
}

void GroundStationUi::submit_control_input() {
    if (!ready_ || settings_open_ || console_open_) return;
    if (capture_mouse_ && !mouse_capture_active_) return;
    ImGuiIO& io = ImGui::GetIO();
    backend::ControlInput input{};
    input.mouse_delta_x = capture_mouse_ ? io.MouseDelta.x * mouse_sensitivity_ : 0.0F;
    input.mouse_delta_y = capture_mouse_
                              ? io.MouseDelta.y * mouse_sensitivity_ *
                                    (invert_y_ || invert_pitch_ ? -1.0F : 1.0F)
                              : 0.0F;
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) input.mouse_buttons |= 0x01U;
    if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) input.mouse_buttons |= 0x02U;
    if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) input.mouse_buttons |= 0x04U;
    if (ImGui::IsMouseDown(3)) input.mouse_buttons |= 0x08U;
    if (ImGui::IsMouseDown(4)) input.mouse_buttons |= 0x10U;
    input.mouse_wheel = static_cast<int>(std::clamp(io.MouseWheel, -1.0F, 1.0F));
    if (send_keyboard_) {
        for (const auto& visual : protocol_key_visuals()) {
            if (ImGui::IsKeyDown(static_cast<ImGuiKey>(visual.imgui_key))) {
                map_physical_key(input, visual.imgui_key, key_bindings_);
            }
        }
        for (std::size_t index = forward_binding; index <= special_two_binding; ++index) {
            const int physical_key = key_bindings_[index];
            if (ImGui::IsKeyDown(static_cast<ImGuiKey>(physical_key))) {
                map_physical_key(input, physical_key, key_bindings_);
            }
        }
    }
    backend_.submit_control_input(input);
}

void GroundStationUi::draw(float delta_seconds, float display_scale) {
    sync_backend_state();
    ImGuiIO& io = ImGui::GetIO();
    const bool shortcuts_enabled = !io.WantTextInput && rebinding_action_ < 0;
    if (shortcuts_enabled && !display_confirmation_open_ && !about_open_ &&
        ImGui::IsKeyPressed(static_cast<ImGuiKey>(key_bindings_[toggle_settings_binding]), false)) {
        if (settings_open_) settings_open_ = false;
        else open_settings();
    }
    if (shortcuts_enabled && !settings_open_ && !console_open_ &&
        ImGui::IsKeyPressed(static_cast<ImGuiKey>(key_bindings_[toggle_ready_binding]), false)) {
        toggle_ready();
    }
    if (shortcuts_enabled && !settings_open_ &&
        ImGui::IsKeyPressed(static_cast<ImGuiKey>(key_bindings_[toggle_hud_binding]), false)) {
        show_input_hud_ = !show_input_hud_;
        apply_interface_settings();
    }
    if (shortcuts_enabled &&
        ImGui::IsKeyPressed(static_cast<ImGuiKey>(key_bindings_[toggle_console_binding]), false)) {
        if (!console_open_) leave_ready("打开控制台，已自动退出 READY");
        console_open_ = !console_open_;
    }

    const auto telemetry = backend_.telemetry();
    std::rotate(fps_history_.begin(), fps_history_.begin() + 1, fps_history_.end());
    std::rotate(latency_history_.begin(), latency_history_.begin() + 1, latency_history_.end());
    fps_history_.back() = telemetry.fps;
    latency_history_.back() = telemetry.latency_ms;

    submit_control_input();
    if (settings_open_) draw_settings(delta_seconds, display_scale);
    else draw_fpv(delta_seconds, display_scale);
    draw_console(delta_seconds, display_scale);

    if (video_settings_debounce_.tick(delta_seconds)) submit_video_settings();
}

void GroundStationUi::draw_fpv(float delta_seconds, float scale) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0F, 0.0F});
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::Begin("PIP-Link##FPV", nullptr, flags);

    const ImVec2 origin = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    const auto video = backend_.latest_video_surface();
    const bool has_video = video.native_texture != nullptr && video.width > 0 && video.height > 0;
    if (!has_video) {
        const ImVec2 bottom_right{origin.x + size.x, origin.y + size.y};
        draw->AddRectFilled(origin, bottom_right, IM_COL32(232, 239, 244, 255));

        const float grid_step = 58.0F * scale;
        const ImU32 grid_color = IM_COL32(68, 120, 144, 30);
        for (float x = origin.x; x < bottom_right.x; x += grid_step) {
            draw->AddLine({x, origin.y}, {x, bottom_right.y}, grid_color);
        }
        for (float y = origin.y; y < bottom_right.y; y += grid_step) {
            draw->AddLine({origin.x, y}, {bottom_right.x, y}, grid_color);
        }

        draw->AddCircle({origin.x + size.x * 0.15F, origin.y + size.y * 0.20F},
                        150.0F * scale, IM_COL32(0, 112, 158, 30), 80, 2.0F * scale);
        draw->AddCircle({origin.x + size.x * 0.86F, origin.y + size.y * 0.78F},
                        220.0F * scale, IM_COL32(0, 112, 158, 24), 96, 2.0F * scale);

        draw->AddText({origin.x + 30.0F * scale, origin.y + 24.0F * scale},
                      IM_COL32(0, 112, 158, 255), "PIP-Link");
        draw->AddText({origin.x + 30.0F * scale, origin.y + 52.0F * scale},
                      IM_COL32(62, 81, 95, 255), "ROBOT FIRST-PERSON CONTROL");

        const float card_width = std::min(570.0F * scale, size.x - 64.0F * scale);
        const float card_height = std::min(300.0F * scale, size.y - 140.0F * scale);
        const ImVec2 center{origin.x + size.x * 0.5F, origin.y + size.y * 0.5F};
        const ImVec2 card_min{center.x - card_width * 0.5F,
                              center.y - card_height * 0.5F};
        const ImVec2 card_max{center.x + card_width * 0.5F,
                              center.y + card_height * 0.5F};
        draw->AddRectFilled({card_min.x + 3.0F * scale, card_min.y + 7.0F * scale},
                            {card_max.x + 3.0F * scale, card_max.y + 7.0F * scale},
                            IM_COL32(42, 72, 88, 20), 14.0F * scale);
        draw->AddRectFilled(card_min, card_max, IM_COL32(252, 254, 255, 255),
                            14.0F * scale);
        draw->AddRect(card_min, card_max, IM_COL32(190, 211, 222, 190),
                      14.0F * scale, 0, 1.0F * scale);

        const ImVec2 icon_center{center.x, card_min.y + 70.0F * scale};
        draw->AddCircleFilled(icon_center, 30.0F * scale, IM_COL32(220, 244, 250, 255));
        draw->AddCircle(icon_center, 19.0F * scale, IM_COL32(0, 151, 190, 230),
                        48, 3.0F * scale);
        draw->AddCircleFilled(icon_center, 5.0F * scale, IM_COL32(0, 151, 190, 255));
        draw->AddLine({icon_center.x - 12.0F * scale, icon_center.y + 21.0F * scale},
                      {icon_center.x + 12.0F * scale, icon_center.y + 21.0F * scale},
                      IM_COL32(0, 151, 190, 175), 2.0F * scale);

        const char* title = "等待机器人视频信号";
        const float title_font_size = ImGui::GetFontSize() * 1.38F;
        const ImVec2 title_size = ImGui::GetFont()->CalcTextSizeA(
            title_font_size, 10000.0F, 0.0F, title);
        draw->AddText(ImGui::GetFont(), title_font_size,
                      {center.x - title_size.x * 0.5F,
                       card_min.y + 112.0F * scale},
                      IM_COL32(28, 47, 60, 255), title);
        const char* description = is_connected()
                                      ? "控制链路已建立，正在等待后端提供首个视频帧"
                                      : "连接机器人后，视频画面将在这里铺满整个窗口";
        const ImVec2 description_size = ImGui::CalcTextSize(description);
        draw->AddText({center.x - description_size.x * 0.5F,
                       card_min.y + 161.0F * scale},
                       IM_COL32(63, 82, 97, 255), description);

        const ImVec2 status_center{center.x - 55.0F * scale,
                                   card_min.y + 202.0F * scale};
        draw->AddCircleFilled(status_center, 4.0F * scale,
                              is_connected() ? IM_COL32(35, 181, 104, 255)
                                         : IM_COL32(240, 151, 45, 255));
        draw->AddText({status_center.x + 11.0F * scale,
                       status_center.y - ImGui::GetTextLineHeight() * 0.5F},
                       IM_COL32(49, 67, 80, 255),
                      is_connected() ? "链路已连接" : "尚未连接" );

        ImGui::SetCursorScreenPos({center.x - 125.0F * scale,
                                   card_max.y - 62.0F * scale});
        char no_signal_action[96]{};
        std::snprintf(no_signal_action, sizeof(no_signal_action), "%s  [%s]",
                      is_connected() ? "打开图传设置" : "打开连接设置",
                      ImGui::GetKeyName(
                          static_cast<ImGuiKey>(key_bindings_[toggle_settings_binding])));
        ImGui::PushStyleColor(ImGuiCol_Text, {1.0F, 1.0F, 1.0F, 1.0F});
        ImGui::PushStyleColor(ImGuiCol_Button, {0.00F, 0.44F, 0.62F, 1.0F});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.00F, 0.52F, 0.70F, 1.0F});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, {0.00F, 0.36F, 0.52F, 1.0F});
        if (ImGui::Button(no_signal_action, {250.0F * scale, 42.0F * scale})) {
            active_settings_tab_ = is_connected() ? 1 : 0;
            open_settings();
        }
        ImGui::PopStyleColor(4);

        draw->AddText({origin.x + 30.0F * scale,
                       bottom_right.y - 34.0F * scale},
                      IM_COL32(70, 88, 101, 235),
                      "C++20  ·  SDL3  ·  Direct3D 11");
        ImGui::End();
        ImGui::PopStyleVar();
        return;
    }

    draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y}, IM_COL32(5, 7, 10, 255));
    if (has_video) {
        const float canvas_aspect = size.x / size.y;
        const float video_aspect = static_cast<float>(video.width) /
                                   static_cast<float>(video.height);
        ImVec2 uv0{0.0F, 0.0F};
        ImVec2 uv1{1.0F, 1.0F};
        if (video_aspect > canvas_aspect) {
            const float visible = canvas_aspect / video_aspect;
            uv0.x = (1.0F - visible) * 0.5F;
            uv1.x = 1.0F - uv0.x;
        } else {
            const float visible = video_aspect / canvas_aspect;
            uv0.y = (1.0F - visible) * 0.5F;
            uv1.y = 1.0F - uv0.y;
        }
        const auto texture_id = static_cast<ImTextureID>(
            reinterpret_cast<std::uintptr_t>(video.native_texture));
        draw->AddImage(texture_id, origin, {origin.x + size.x, origin.y + size.y}, uv0, uv1);
    }

    const float unit = scale * hud_scale_;
    const float hud_font_size = ImGui::GetFontSize() * hud_scale_;
    const float opacity = hud_opacity_;
    const float margin = 20.0F * unit;

    if (show_input_hud_) {
        ImGuiIO& io = ImGui::GetIO();
        const float target_x = std::clamp(io.MouseDelta.x * 2.3F, -30.0F, 30.0F);
        const float target_y = std::clamp(io.MouseDelta.y * 2.3F, -30.0F, 30.0F);
        const float mouse_t = 1.0F - std::exp(-delta_seconds / 0.055F);
        mouse_indicator_x_ += (target_x - mouse_indicator_x_) * mouse_t;
        mouse_indicator_y_ += (target_y - mouse_indicator_y_) * mouse_t;

        const float panel_w = 390.0F * unit;
        const float panel_h = 202.0F * unit;
        const ImVec2 p0{origin.x + margin, origin.y + size.y - margin - panel_h};
        const ImVec2 p1{p0.x + panel_w, p0.y + panel_h};
        draw->AddRectFilled(p0, p1, IM_COL32(7, 12, 18, static_cast<int>(175.0F * opacity)),
                            10.0F * unit);
        draw->AddRect(p0, p1, IM_COL32(190, 220, 234, static_cast<int>(75.0F * opacity)),
                      10.0F * unit);

        const ImVec2 mouse_center{p0.x + 64.0F * unit, p0.y + 66.0F * unit};
        draw->AddCircleFilled(mouse_center, 43.0F * unit,
                              IM_COL32(4, 8, 13, static_cast<int>(210.0F * opacity)));
        draw->AddCircle(mouse_center, 43.0F * unit,
                        IM_COL32(154, 183, 198, static_cast<int>(100.0F * opacity)), 0,
                        1.0F * unit);
        draw->AddLine({mouse_center.x - 32.0F * unit, mouse_center.y},
                      {mouse_center.x + 32.0F * unit, mouse_center.y},
                      IM_COL32(117, 150, 166, static_cast<int>(70.0F * opacity)));
        draw->AddLine({mouse_center.x, mouse_center.y - 32.0F * unit},
                      {mouse_center.x, mouse_center.y + 32.0F * unit},
                      IM_COL32(117, 150, 166, static_cast<int>(70.0F * opacity)));
        const ImVec2 dot{mouse_center.x + mouse_indicator_x_ * unit,
                         mouse_center.y + mouse_indicator_y_ * unit};
        draw->AddLine(mouse_center, dot, color_with_alpha(accent, opacity * 0.55F), 2.0F * unit);
        draw->AddCircleFilled(dot, 5.5F * unit, color_with_alpha(accent, opacity));

        constexpr std::array<const char*, 5> mouse_labels{"L", "M", "R", "M4", "M5"};
        const std::array<bool, 5> mouse_down{
            ImGui::IsMouseDown(ImGuiMouseButton_Left), ImGui::IsMouseDown(ImGuiMouseButton_Middle),
            ImGui::IsMouseDown(ImGuiMouseButton_Right), ImGui::IsMouseDown(3),
            ImGui::IsMouseDown(4),
        };
        float mouse_chip_x = p0.x + 122.0F * unit;
        for (std::size_t index = 0; index < mouse_labels.size(); ++index) {
            const float chip_w = (index < 3 ? 31.0F : 38.0F) * unit;
            const ImVec2 c0{mouse_chip_x, p0.y + 29.0F * unit};
            const ImVec2 c1{c0.x + chip_w, c0.y + 27.0F * unit};
            draw->AddRectFilled(c0, c1,
                                mouse_down[index]
                                    ? color_with_alpha(accent, opacity)
                                    : IM_COL32(43, 54, 65, static_cast<int>(185.0F * opacity)),
                                5.0F * unit);
            const ImVec2 label_size = ImGui::GetFont()->CalcTextSizeA(
                hud_font_size, 10000.0F, 0.0F, mouse_labels[index]);
            draw->AddText(ImGui::GetFont(), hud_font_size,
                          {c0.x + (chip_w - label_size.x) * 0.5F,
                           c0.y + (27.0F * unit - label_size.y) * 0.5F},
                          IM_COL32(238, 247, 252, static_cast<int>(255.0F * opacity)),
                          mouse_labels[index]);
            mouse_chip_x += chip_w + 5.0F * unit;
        }
        const char* movement = std::abs(io.MouseWheel) > 0.01F
                                   ? (io.MouseWheel > 0.0F ? "WHEEL  ↑" : "WHEEL  ↓")
                                   : "MOUSE VECTOR";
        draw->AddText(ImGui::GetFont(), hud_font_size,
                      {p0.x + 122.0F * unit, p0.y + 70.0F * unit},
                      IM_COL32(180, 197, 207, static_cast<int>(230.0F * opacity)), movement);

        draw->AddLine({p0.x + 14.0F * unit, p0.y + 116.0F * unit},
                      {p1.x - 14.0F * unit, p0.y + 116.0F * unit},
                      IM_COL32(160, 185, 198, static_cast<int>(55.0F * opacity)));
        ImVec2 tag_cursor{p0.x + 14.0F * unit, p0.y + 132.0F * unit};
        const ImVec2 tag_bounds{p1.x - 14.0F * unit, p0.x + 14.0F * unit};
        int pressed_count = 0;
        draw->PushClipRect({p0.x + 12.0F * unit, p0.y + 126.0F * unit},
                           {p1.x - 12.0F * unit, p1.y - 10.0F * unit}, true);
        for (const auto& visual : protocol_key_visuals()) {
            if (ImGui::IsKeyDown(static_cast<ImGuiKey>(visual.imgui_key))) {
                draw_key_tag(draw, tag_cursor, tag_bounds, visual.label, unit,
                             hud_font_size, opacity);
                ++pressed_count;
            }
        }
        if (pressed_count == 0) {
            draw->AddText(ImGui::GetFont(), hud_font_size, tag_cursor,
                          IM_COL32(170, 186, 197, static_cast<int>(220.0F * opacity)),
                          "NO KEY INPUT");
        }
        draw->PopClipRect();
    }

    const auto telemetry = backend_.telemetry();
    if (show_status_hud_) {
        constexpr std::array<const char*, 4> labels{"FPS", "BITRATE", "RTT", "LOSS"};
        char values[4][32]{};
        std::snprintf(values[0], sizeof(values[0]), "%.1f", telemetry.fps);
        std::snprintf(values[1], sizeof(values[1]), "%.2f Mbps", telemetry.bandwidth_mbps);
        std::snprintf(values[2], sizeof(values[2]), "%.1f ms", telemetry.latency_ms);
        std::snprintf(values[3], sizeof(values[3]), "%.2f %%", telemetry.packet_loss_percent);
        const float panel_w = 250.0F * unit;
        const float panel_h = 124.0F * unit;
        const ImVec2 p1{origin.x + size.x - margin, origin.y + size.y - margin};
        const ImVec2 p0{p1.x - panel_w, p1.y - panel_h};
        draw->AddRectFilled(p0, p1, IM_COL32(7, 12, 18, static_cast<int>(165.0F * opacity)),
                            9.0F * unit);
        for (int index = 0; index < 4; ++index) {
            const float row_y = p0.y + (14.0F + index * 27.0F) * unit;
            draw->AddText(ImGui::GetFont(), hud_font_size,
                          {p0.x + 16.0F * unit, row_y},
                          IM_COL32(169, 190, 201, static_cast<int>(245.0F * opacity)),
                          labels[static_cast<std::size_t>(index)]);
            const ImVec2 value_size = ImGui::GetFont()->CalcTextSizeA(
                hud_font_size, 10000.0F, 0.0F, values[index]);
            draw->AddText(ImGui::GetFont(), hud_font_size,
                          {p1.x - 16.0F * unit - value_size.x, row_y},
                          index == 0 ? IM_COL32(34, 210, 240,
                                                       static_cast<int>(255.0F * opacity))
                                     : IM_COL32(242, 248, 251,
                                                static_cast<int>(255.0F * opacity)),
                          values[index]);
        }
    }

    if (show_ready_hud_) {
        char ready_text[64]{};
        std::snprintf(ready_text, sizeof(ready_text), ready_ ? "READY" : "NOT READY  [%s]",
                      ImGui::GetKeyName(
                          static_cast<ImGuiKey>(key_bindings_[toggle_ready_binding])));
        const ImVec2 ready_size = ImGui::GetFont()->CalcTextSizeA(
            hud_font_size, 10000.0F, 0.0F, ready_text);
        const float pill_w = ready_size.x + 42.0F * unit;
        const float pill_h = 34.0F * unit;
        const ImVec2 center{origin.x + size.x * 0.5F, origin.y + size.y - margin};
        const ImVec2 p0{center.x - pill_w * 0.5F, center.y - pill_h};
        const ImVec2 p1{center.x + pill_w * 0.5F, center.y};
        draw->AddRectFilled(p0, p1,
                            ready_ ? IM_COL32(11, 88, 56, static_cast<int>(205.0F * opacity))
                                   : IM_COL32(83, 25, 23, static_cast<int>(205.0F * opacity)),
                            pill_h * 0.5F);
        draw->AddCircleFilled({p0.x + 17.0F * unit, center.y - pill_h * 0.5F},
                              4.0F * unit, color_with_alpha(ready_ ? success : danger, opacity));
        draw->AddText(ImGui::GetFont(), hud_font_size,
                      {p0.x + 29.0F * unit, p0.y + (pill_h - ready_size.y) * 0.5F},
                      IM_COL32(247, 250, 252, static_cast<int>(255.0F * opacity)), ready_text);
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

void GroundStationUi::draw_settings(float delta_seconds, float scale) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {30.0F * scale, 22.0F * scale});
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::Begin("PIP-Link##Settings", nullptr, flags);

    const float window_width = ImGui::GetWindowWidth();
    const bool show_header_detail = window_width >= 1080.0F * scale;
    char return_label[96]{};
    std::snprintf(return_label, sizeof(return_label), "返回第一视角  [%s]",
                  ImGui::GetKeyName(
                      static_cast<ImGuiKey>(key_bindings_[toggle_settings_binding])));
    const float header_height = 40.0F * scale;
    if (ImGui::BeginTable("SettingsHeader", show_header_detail ? 3 : 2,
                          ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("##Brand", ImGuiTableColumnFlags_WidthStretch);
        if (show_header_detail) {
            ImGui::TableSetupColumn("##ConnectionStatus", ImGuiTableColumnFlags_WidthFixed,
                                    105.0F * scale);
        }
        ImGui::TableSetupColumn("##Return", ImGuiTableColumnFlags_WidthFixed, 175.0F * scale);
        ImGui::TableNextRow(0, header_height);
        ImGui::TableNextColumn();
        const ImVec2 brand_origin = ImGui::GetCursorScreenPos();
        const float brand_font_size = ImGui::GetFontSize() * 1.42F;
        const ImVec2 brand_size = ImGui::GetFont()->CalcTextSizeA(
            brand_font_size, 10000.0F, 0.0F, "PIP-Link");
        ImDrawList* header_draw = ImGui::GetWindowDrawList();
        header_draw->AddText(ImGui::GetFont(), brand_font_size,
                             {brand_origin.x,
                              brand_origin.y + (header_height - brand_size.y) * 0.5F},
                             ImGui::GetColorU32(accent), "PIP-Link");
        if (show_header_detail) {
            const char* subtitle = "机器人第一视角控制客户端";
            const ImVec2 subtitle_size = ImGui::CalcTextSize(subtitle);
            header_draw->AddText(
                {brand_origin.x + brand_size.x + 18.0F * scale,
                 brand_origin.y + (header_height - subtitle_size.y) * 0.5F},
                ImGui::GetColorU32(text_secondary), subtitle);
        }
        ImGui::Dummy({0.0F, header_height});

        if (show_header_detail) {
            ImGui::TableNextColumn();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() +
                                 (header_height - ImGui::GetTextLineHeight()) * 0.5F);
            ImGui::TextColored(is_connected() ? success : warning,
                               is_connected() ? "已连接" : "未连接");
        }
        ImGui::TableNextColumn();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() +
                             (header_height - ImGui::GetFrameHeight()) * 0.5F);
        if (ImGui::Button(return_label, {-1.0F, 0.0F})) settings_open_ = false;
        ImGui::EndTable();
    }
    ImGui::Dummy({0.0F, 5.0F * scale});
    draw_settings_tabs(delta_seconds, scale);
    ImGui::Dummy({0.0F, 8.0F * scale});

    ImGui::SetWindowFontScale(1.52F);
    ImGui::TextUnformatted(tabs[active_settings_tab_].title);
    ImGui::SetWindowFontScale(1.0F);
    ImGui::TextColored(text_secondary, "%s", tabs[active_settings_tab_].description);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    char content_id[32]{};
    std::snprintf(content_id, sizeof(content_id), "##SettingsContent%d", active_settings_tab_);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.93F, 0.95F, 0.97F, 0.0F});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {4.0F * scale, 4.0F * scale});
    ImGui::BeginChild(content_id, {0.0F, -40.0F * scale},
                      ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);
    nested_scroll_consumed_ = false;
    switch (active_settings_tab_) {
        case 0: draw_connection_page(scale); break;
        case 1: draw_video_page(scale); break;
        case 2: draw_control_page(scale); break;
        case 3: draw_recording_page(scale); break;
        case 4: draw_diagnostics_page(scale); break;
        case 5: draw_audit_page(scale); break;
        case 6: draw_interface_page(scale); break;
        default: break;
    }
    ImGui::Dummy({0.0F, 18.0F * scale});

    float& scroll_target = settings_scroll_targets_[static_cast<std::size_t>(active_settings_tab_)];
    if (!nested_scroll_consumed_ &&
        ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
        ImGui::GetIO().MouseWheel != 0.0F) {
        scroll_target -= ImGui::GetIO().MouseWheel * 100.0F * scale;
    }
    scroll_target = std::clamp(scroll_target, 0.0F, ImGui::GetScrollMaxY());
    ImGui::SetScrollY(core::advance_smooth_scroll(
        ImGui::GetScrollY(), scroll_target, delta_seconds, scale));
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    if (ImGui::BeginTable("SettingsFooter", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("##Feedback", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("##About", ImGuiTableColumnFlags_WidthFixed, 72.0F * scale);
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(text_secondary, "%s", feedback_.c_str());
        ImGui::TableNextColumn();
        if (ImGui::Button("关于", {-1.0F, 0.0F})) about_open_ = true;
        ImGui::EndTable();
    }
    draw_display_confirmation(scale);
    draw_about_dialog(scale);
    ImGui::End();
    ImGui::PopStyleVar();
}

void GroundStationUi::draw_settings_tabs(float delta_seconds, float scale) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {4.0F * scale, 3.0F * scale});
    ImGui::BeginChild("##SettingsTabs", {0.0F, 48.0F * scale},
                      ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollbar);
    for (int index = 0; index < static_cast<int>(tabs.size()); ++index) {
        if (index > 0) ImGui::SameLine(0.0F, 8.0F * scale);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 text_size = ImGui::CalcTextSize(tabs[index].label);
        const float width = text_size.x + 38.0F * scale;
        const float height = 42.0F * scale;
        ImGui::PushID(index);
        const bool clicked = ImGui::InvisibleButton("##Tab", {width, height});
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        if (clicked) {
            active_settings_tab_ = index;
        }
        const float target = hovered ? 1.0F : 0.0F;
        tab_hover_[index] += (target - tab_hover_[index]) *
                             (1.0F - std::exp(-delta_seconds / 0.06F));
        ImDrawList* draw = ImGui::GetWindowDrawList();
        if (active_settings_tab_ == index || tab_hover_[index] > 0.01F) {
            const float alpha = active_settings_tab_ == index ? 0.14F : 0.07F * tab_hover_[index];
            draw->AddRectFilled(origin, {origin.x + width, origin.y + height},
                                color_with_alpha(accent, alpha), 7.0F * scale);
        }
        const ImVec4 inactive_text{
            text_secondary.x + (text_primary.x - text_secondary.x) * tab_hover_[index],
            text_secondary.y + (text_primary.y - text_secondary.y) * tab_hover_[index],
            text_secondary.z + (text_primary.z - text_secondary.z) * tab_hover_[index],
            1.0F,
        };
        draw->AddText({origin.x + (width - text_size.x) * 0.5F,
                       origin.y + (height - text_size.y) * 0.5F - 1.0F * scale},
                      active_settings_tab_ == index
                          ? ImGui::GetColorU32(accent)
                          : ImGui::GetColorU32(inactive_text),
                      tabs[index].label);
        if (active_settings_tab_ == index) {
            draw->AddLine({origin.x + 12.0F * scale, origin.y + height - 2.0F * scale},
                          {origin.x + width - 12.0F * scale, origin.y + height - 2.0F * scale},
                          ImGui::GetColorU32(accent), 2.5F * scale);
        }
    }
    if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0F) {
        tab_scroll_target_ -= ImGui::GetIO().MouseWheel * 100.0F * scale;
    }
    tab_scroll_target_ = std::clamp(tab_scroll_target_, 0.0F, ImGui::GetScrollMaxX());
    ImGui::SetScrollX(core::advance_smooth_scroll(
        ImGui::GetScrollX(), tab_scroll_target_, delta_seconds, scale));
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void GroundStationUi::draw_console(float delta_seconds, float scale) {
    const float target_height = console_open_ ? 290.0F * scale : 0.0F;
    console_height_ += (target_height - console_height_) *
                       (1.0F - std::exp(-delta_seconds / 0.08F));
    if (std::abs(console_height_ - target_height) < 0.5F) console_height_ = target_height;
    if (console_height_ < 1.0F) return;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize({viewport->WorkSize.x, console_height_});
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha,
                        std::clamp(console_height_ / (290.0F * scale), 0.0F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, {0.94F, 0.96F, 0.97F, 0.99F});
    ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.99F, 1.00F, 1.00F, 1.00F});
    ImGui::PushStyleColor(ImGuiCol_Text, text_primary);
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, {0.30F, 0.37F, 0.43F, 1.00F});
    ImGui::PushStyleColor(ImGuiCol_Border, {0.57F, 0.66F, 0.71F, 1.00F});
    ImGui::PushStyleColor(ImGuiCol_Separator, {0.62F, 0.70F, 0.75F, 1.00F});
    ImGui::PushStyleColor(ImGuiCol_FrameBg, {0.82F, 0.88F, 0.91F, 1.00F});
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, {0.75F, 0.84F, 0.88F, 1.00F});
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, {0.68F, 0.80F, 0.85F, 1.00F});
    ImGui::PushStyleColor(ImGuiCol_Button, {0.82F, 0.88F, 0.91F, 1.00F});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.71F, 0.82F, 0.87F, 1.00F});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, {0.62F, 0.77F, 0.83F, 1.00F});
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("##DeveloperConsole", nullptr, flags);
    ImGui::TextColored({0.0F, 0.82F, 1.0F, 1.0F}, "开发者控制台");
    ImGui::SameLine();
    ImGui::TextDisabled("设置与控制输入已暂停");
    ImGui::SameLine(ImGui::GetWindowWidth() - 150.0F * scale);
    if (ImGui::SmallButton("清空")) console_lines_.clear();
    ImGui::SameLine();
    if (ImGui::SmallButton("关闭")) console_open_ = false;
    ImGui::Separator();
    ImGui::BeginChild("##ConsoleOutput", {0.0F, -38.0F * scale}, ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    for (const auto& line : console_lines_) ImGui::TextUnformatted(line.c_str());
    if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0F) {
        console_scroll_target_ -= ImGui::GetIO().MouseWheel * 100.0F * scale;
    }
    console_scroll_target_ = std::clamp(console_scroll_target_, 0.0F, ImGui::GetScrollMaxY());
    ImGui::SetScrollY(core::advance_smooth_scroll(
        ImGui::GetScrollY(), console_scroll_target_, delta_seconds, scale));
    ImGui::EndChild();
    ImGui::SetNextItemWidth(-1.0F);
    if (ImGui::InputText("##ConsoleCommand", console_command_.data(), console_command_.size(),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        const std::string command{console_command_.data()};
        if (!command.empty()) {
            console_lines_.push_back("> " + command);
            if (command == "clear") console_lines_.clear();
            else if (command == "close") console_open_ = false;
            else {
                const std::string result = backend_.execute_console_command(command);
                console_lines_.push_back(result.empty() ? "后端尚未返回结果" : result);
            }
            console_command_.fill('\0');
        }
        ImGui::SetKeyboardFocusHere(-1);
    }
    ImGui::End();
    ImGui::PopStyleColor(12);
    ImGui::PopStyleVar();
}

}  // namespace pip_link::ui
