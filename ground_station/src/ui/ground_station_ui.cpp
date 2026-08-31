#include "pip_link/ui/ground_station_ui.hpp"

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

constexpr ImVec4 accent{0.00F, 0.58F, 0.78F, 1.00F};
constexpr ImVec4 text_primary{0.10F, 0.14F, 0.20F, 1.00F};
constexpr ImVec4 text_secondary{0.39F, 0.44F, 0.52F, 1.00F};
constexpr ImVec4 success{0.08F, 0.72F, 0.38F, 1.00F};
constexpr ImVec4 warning{0.96F, 0.57F, 0.10F, 1.00F};
constexpr ImVec4 danger{0.91F, 0.25F, 0.22F, 1.00F};

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

struct KeyVisual final {
    ImGuiKey key;
    int bit;
    const char* label;
};

// 与旧 Python 客户端发往协议 keyboard_state[10] 的位序保持一致。
constexpr std::array<KeyVisual, 70> key_visuals{{
    {ImGuiKey_Escape, 0, "ESC"}, {ImGuiKey_F1, 1, "F1"},
    {ImGuiKey_F2, 2, "F2"}, {ImGuiKey_F3, 3, "F3"},
    {ImGuiKey_F4, 4, "F4"}, {ImGuiKey_F6, 6, "F6"},
    {ImGuiKey_F7, 7, "F7"}, {ImGuiKey_F8, 8, "F8"},
    {ImGuiKey_F9, 9, "F9"}, {ImGuiKey_F10, 10, "F10"},
    {ImGuiKey_F11, 11, "F11"}, {ImGuiKey_F12, 12, "F12"},
    {ImGuiKey_GraveAccent, 13, "`"}, {ImGuiKey_1, 14, "1"},
    {ImGuiKey_2, 15, "2"}, {ImGuiKey_3, 16, "3"},
    {ImGuiKey_4, 17, "4"}, {ImGuiKey_5, 18, "5"},
    {ImGuiKey_6, 19, "6"}, {ImGuiKey_7, 20, "7"},
    {ImGuiKey_8, 21, "8"}, {ImGuiKey_9, 22, "9"},
    {ImGuiKey_0, 23, "0"}, {ImGuiKey_Minus, 24, "-"},
    {ImGuiKey_Equal, 25, "="}, {ImGuiKey_Backspace, 26, "BS"},
    {ImGuiKey_Tab, 27, "TAB"}, {ImGuiKey_Q, 28, "Q"},
    {ImGuiKey_W, 29, "W"}, {ImGuiKey_E, 30, "E"},
    {ImGuiKey_R, 31, "R"}, {ImGuiKey_T, 32, "T"},
    {ImGuiKey_Y, 33, "Y"}, {ImGuiKey_U, 34, "U"},
    {ImGuiKey_I, 35, "I"}, {ImGuiKey_O, 36, "O"},
    {ImGuiKey_P, 37, "P"}, {ImGuiKey_LeftBracket, 38, "["},
    {ImGuiKey_RightBracket, 39, "]"}, {ImGuiKey_Backslash, 40, "\\"},
    {ImGuiKey_CapsLock, 41, "CAPS"}, {ImGuiKey_A, 42, "A"},
    {ImGuiKey_S, 43, "S"}, {ImGuiKey_D, 44, "D"},
    {ImGuiKey_F, 45, "F"}, {ImGuiKey_G, 46, "G"},
    {ImGuiKey_H, 47, "H"}, {ImGuiKey_J, 48, "J"},
    {ImGuiKey_K, 49, "K"}, {ImGuiKey_L, 50, "L"},
    {ImGuiKey_Semicolon, 51, ";"}, {ImGuiKey_Apostrophe, 52, "'"},
    {ImGuiKey_Enter, 53, "ENT"}, {ImGuiKey_LeftShift, 54, "LSHF"},
    {ImGuiKey_Z, 55, "Z"}, {ImGuiKey_X, 56, "X"},
    {ImGuiKey_C, 57, "C"}, {ImGuiKey_V, 58, "V"},
    {ImGuiKey_B, 59, "B"}, {ImGuiKey_N, 60, "N"},
    {ImGuiKey_M, 61, "M"}, {ImGuiKey_Comma, 62, ","},
    {ImGuiKey_Period, 63, "."}, {ImGuiKey_Slash, 64, "/"},
    {ImGuiKey_RightShift, 65, "RSHF"}, {ImGuiKey_LeftCtrl, 66, "LCTL"},
    {ImGuiKey_LeftAlt, 67, "LALT"}, {ImGuiKey_Space, 68, "SPC"},
    {ImGuiKey_RightAlt, 69, "RALT"}, {ImGuiKey_RightCtrl, 70, "RCTL"},
}};

ImU32 color_with_alpha(const ImVec4& color, float alpha) {
    ImVec4 adjusted = color;
    adjusted.w *= std::clamp(alpha, 0.0F, 1.0F);
    return ImGui::GetColorU32(adjusted);
}

void draw_key_tag(ImDrawList* draw, ImVec2& cursor, const ImVec2 bounds,
                  const char* label, float scale, float opacity) {
    const ImVec2 text_size = ImGui::CalcTextSize(label);
    const float width = text_size.x + 15.0F * scale;
    const float height = 24.0F * scale;
    if (cursor.x + width > bounds.x) {
        cursor.x = bounds.y;
        cursor.y += height + 5.0F * scale;
    }
    draw->AddRectFilled(cursor, {cursor.x + width, cursor.y + height},
                        IM_COL32(0, 176, 216, static_cast<int>(205.0F * opacity)),
                        5.0F * scale);
    draw->AddText({cursor.x + 7.5F * scale,
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
}

bool GroundStationUi::quit_requested() const noexcept {
    return quit_requested_;
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

void GroundStationUi::open_settings() {
    leave_ready("打开设置，已自动退出 READY");
    settings_open_ = true;
}

void GroundStationUi::toggle_ready() {
    if (settings_open_ || console_open_) return;
    if (!connected_) {
        set_feedback("尚未连接机器人，不能进入 READY");
        return;
    }
    ready_ = !ready_;
    backend_.set_ready(ready_);
    set_feedback(ready_ ? "已请求进入 READY 状态" : "已请求退出 READY 状态");
}

void GroundStationUi::submit_control_input() {
    if (!ready_ || settings_open_ || console_open_) return;
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
        for (const auto& visual : key_visuals) {
            if (ImGui::IsKeyDown(visual.key)) {
                input.keyboard[static_cast<std::size_t>(visual.bit / 8)] |=
                    static_cast<std::uint8_t>(1U << (visual.bit % 8));
            }
        }
    }
    backend_.submit_control_input(input);
}

void GroundStationUi::draw(float delta_seconds, float display_scale) {
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
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("PIP-Link##FPV", nullptr, flags);

    const ImVec2 origin = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y}, IM_COL32(5, 7, 10, 255));

    const auto video = backend_.latest_video_surface();
    if (video.native_texture != nullptr && video.width > 0 && video.height > 0) {
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
    } else {
        const ImVec2 center{origin.x + size.x * 0.5F, origin.y + size.y * 0.5F};
        const char* no_signal = "NO VIDEO SIGNAL";
        const ImVec2 text_size = ImGui::CalcTextSize(no_signal);
        draw->AddText({center.x - text_size.x * 0.5F, center.y - text_size.y * 0.5F},
                      IM_COL32(140, 148, 160, 175), no_signal);
    }

    const float unit = scale * hud_scale_;
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
        const float panel_h = 178.0F * unit;
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
            const ImVec2 label_size = ImGui::CalcTextSize(mouse_labels[index]);
            draw->AddText({c0.x + (chip_w - label_size.x) * 0.5F,
                           c0.y + (27.0F * unit - label_size.y) * 0.5F},
                          IM_COL32(238, 247, 252, static_cast<int>(255.0F * opacity)),
                          mouse_labels[index]);
            mouse_chip_x += chip_w + 5.0F * unit;
        }
        const char* movement = std::abs(io.MouseWheel) > 0.01F
                                   ? (io.MouseWheel > 0.0F ? "WHEEL  ↑" : "WHEEL  ↓")
                                   : "MOUSE VECTOR";
        draw->AddText({p0.x + 122.0F * unit, p0.y + 70.0F * unit},
                      IM_COL32(180, 197, 207, static_cast<int>(210.0F * opacity)), movement);

        draw->AddLine({p0.x + 14.0F * unit, p0.y + 116.0F * unit},
                      {p1.x - 14.0F * unit, p0.y + 116.0F * unit},
                      IM_COL32(160, 185, 198, static_cast<int>(55.0F * opacity)));
        ImVec2 tag_cursor{p0.x + 14.0F * unit, p0.y + 132.0F * unit};
        const ImVec2 tag_bounds{p1.x - 14.0F * unit, p0.x + 14.0F * unit};
        int pressed_count = 0;
        for (const auto& visual : key_visuals) {
            if (ImGui::IsKeyDown(visual.key)) {
                draw_key_tag(draw, tag_cursor, tag_bounds, visual.label, unit, opacity);
                ++pressed_count;
            }
        }
        if (pressed_count == 0) {
            draw->AddText(tag_cursor, IM_COL32(150, 163, 174, static_cast<int>(150.0F * opacity)),
                          "NO KEY INPUT");
        }
    }

    const auto telemetry = backend_.telemetry();
    if (show_status_hud_) {
        char rows[4][48]{};
        std::snprintf(rows[0], sizeof(rows[0]), "FPS       %6.1f", telemetry.fps);
        std::snprintf(rows[1], sizeof(rows[1]), "BITRATE   %6.2f Mbps", telemetry.bandwidth_mbps);
        std::snprintf(rows[2], sizeof(rows[2]), "RTT       %6.1f ms", telemetry.latency_ms);
        std::snprintf(rows[3], sizeof(rows[3]), "LOSS      %6.2f %%", telemetry.packet_loss_percent);
        const float panel_w = 232.0F * unit;
        const float panel_h = 124.0F * unit;
        const ImVec2 p1{origin.x + size.x - margin, origin.y + size.y - margin};
        const ImVec2 p0{p1.x - panel_w, p1.y - panel_h};
        draw->AddRectFilled(p0, p1, IM_COL32(7, 12, 18, static_cast<int>(165.0F * opacity)),
                            9.0F * unit);
        for (int index = 0; index < 4; ++index) {
            draw->AddText({p0.x + 16.0F * unit, p0.y + (14.0F + index * 27.0F) * unit},
                          index == 0 ? color_with_alpha(accent, opacity)
                                     : IM_COL32(232, 242, 247,
                                                static_cast<int>(245.0F * opacity)),
                          rows[index]);
        }
    }

    if (show_ready_hud_) {
        char ready_text[64]{};
        std::snprintf(ready_text, sizeof(ready_text), ready_ ? "READY" : "NOT READY  [%s]",
                      ImGui::GetKeyName(
                          static_cast<ImGuiKey>(key_bindings_[toggle_ready_binding])));
        const ImVec2 ready_size = ImGui::CalcTextSize(ready_text);
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
        draw->AddText({p0.x + 29.0F * unit, p0.y + (pill_h - ready_size.y) * 0.5F},
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
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("PIP-Link##Settings", nullptr, flags);

    ImGui::SetWindowFontScale(1.42F);
    ImGui::TextColored(accent, "PIP-Link");
    ImGui::SetWindowFontScale(1.0F);
    ImGui::SameLine();
    ImGui::TextColored(text_secondary, "机器人第一视角控制客户端");
    ImGui::SameLine(ImGui::GetWindowWidth() - 320.0F * scale);
    ImGui::TextColored(connected_ ? success : warning, connected_ ? "● 已连接" : "● 未连接");
    ImGui::SameLine(ImGui::GetWindowWidth() - 170.0F * scale);
    if (ImGui::Button("返回第一视角  [Esc]", {145.0F * scale, 36.0F * scale})) {
        settings_open_ = false;
    }
    ImGui::Dummy({0.0F, 5.0F * scale});
    draw_settings_tabs(delta_seconds, scale);
    ImGui::Dummy({0.0F, 8.0F * scale});

    ImGui::BeginChild("##SettingsContent", {0.0F, -40.0F * scale},
                      ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);
    ImGui::SetWindowFontScale(1.52F);
    ImGui::TextUnformatted(tabs[active_settings_tab_].title);
    ImGui::SetWindowFontScale(1.0F);
    ImGui::TextColored(text_secondary, "%s", tabs[active_settings_tab_].description);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

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

    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
        ImGui::GetIO().MouseWheel != 0.0F) {
        settings_scroll_target_ -= ImGui::GetIO().MouseWheel * 100.0F * scale;
    }
    settings_scroll_target_ = std::clamp(settings_scroll_target_, 0.0F, ImGui::GetScrollMaxY());
    const float difference = settings_scroll_target_ - ImGui::GetScrollY();
    if (std::abs(difference) > 0.5F) {
        ImGui::SetScrollY(ImGui::GetScrollY() +
                          difference * (1.0F - std::exp(-delta_seconds / 0.08F)));
    } else {
        ImGui::SetScrollY(settings_scroll_target_);
    }
    ImGui::EndChild();

    ImGui::TextColored(text_secondary, "%s", feedback_.c_str());
    ImGui::SameLine(ImGui::GetWindowWidth() - 105.0F * scale);
    if (ImGui::SmallButton("关于")) about_open_ = true;
    draw_display_confirmation(scale);
    draw_about_dialog(scale);
    ImGui::End();
    ImGui::PopStyleVar();
}

void GroundStationUi::draw_settings_tabs(float delta_seconds, float scale) {
    ImGui::BeginChild("##SettingsTabs", {0.0F, 48.0F * scale}, ImGuiChildFlags_None,
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
            settings_scroll_target_ = 0.0F;
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
        draw->AddText({origin.x + (width - text_size.x) * 0.5F,
                       origin.y + (height - text_size.y) * 0.5F - 1.0F * scale},
                      active_settings_tab_ == index
                          ? ImGui::GetColorU32(accent)
                          : color_with_alpha(text_primary, 0.65F + tab_hover_[index] * 0.25F),
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
    const float difference = tab_scroll_target_ - ImGui::GetScrollX();
    ImGui::SetScrollX(std::abs(difference) > 0.5F
                          ? ImGui::GetScrollX() +
                                difference * (1.0F - std::exp(-delta_seconds / 0.08F))
                          : tab_scroll_target_);
    ImGui::EndChild();
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
    ImGui::PushStyleColor(ImGuiCol_WindowBg, {0.025F, 0.030F, 0.042F, 0.98F});
    ImGui::PushStyleColor(ImGuiCol_Text, {0.88F, 0.92F, 0.97F, 1.0F});
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
    ImGui::BeginChild("##ConsoleOutput", {0.0F, -38.0F * scale}, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    for (const auto& line : console_lines_) ImGui::TextUnformatted(line.c_str());
    if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0F) {
        console_scroll_target_ -= ImGui::GetIO().MouseWheel * 100.0F * scale;
    }
    console_scroll_target_ = std::clamp(console_scroll_target_, 0.0F, ImGui::GetScrollMaxY());
    const float difference = console_scroll_target_ - ImGui::GetScrollY();
    ImGui::SetScrollY(std::abs(difference) > 0.5F
                          ? ImGui::GetScrollY() +
                                difference * (1.0F - std::exp(-delta_seconds / 0.08F))
                          : console_scroll_target_);
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
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
}

}  // namespace pip_link::ui
