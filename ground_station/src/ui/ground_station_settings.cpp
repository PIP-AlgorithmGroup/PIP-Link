#include "pip_link/ui/ground_station_ui.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string_view>
#include <vector>

namespace pip_link::ui {
namespace {

constexpr ImVec4 accent{0.00F, 0.58F, 0.78F, 1.00F};
constexpr ImVec4 text_secondary{0.39F, 0.44F, 0.52F, 1.00F};
constexpr ImVec4 success{0.08F, 0.72F, 0.38F, 1.00F};
constexpr ImVec4 warning{0.96F, 0.57F, 0.10F, 1.00F};
constexpr ImVec4 danger{0.91F, 0.25F, 0.22F, 1.00F};

struct ColumnLayout final {
    bool wide;
    float width;
    float gap;
};

ColumnLayout columns(float scale, float minimum_column_width = 390.0F) {
    const float available = ImGui::GetContentRegionAvail().x;
    const float gap = 14.0F * scale;
    const bool wide = available >= minimum_column_width * 2.0F * scale + gap;
    return {wide, wide ? (available - gap) * 0.5F : available, gap};
}

void next_column_or_row(const ColumnLayout& layout, float scale) {
    if (layout.wide) ImGui::SameLine(0.0F, layout.gap);
    else ImGui::Dummy({0.0F, 12.0F * scale});
}

void section_title(const char* title, const char* description = nullptr) {
    ImGui::SetWindowFontScale(1.14F);
    ImGui::TextUnformatted(title);
    ImGui::SetWindowFontScale(1.0F);
    if (description != nullptr) ImGui::TextColored(text_secondary, "%s", description);
    ImGui::Dummy({0.0F, 5.0F});
}

void begin_card(const char* id, const ImVec2 size = {0.0F, 0.0F}) {
    const float density = std::clamp(ImGui::GetFontSize() / 18.0F, 1.0F, 2.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {18.0F * density, 16.0F * density});
    ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.995F, 0.998F, 1.0F, 1.0F});
    ImGui::PushStyleColor(ImGuiCol_Border, {0.76F, 0.82F, 0.86F, 0.82F});
    ImGui::BeginChild(id, size, ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}

void end_card() {
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
}

float animated_hover(ImGuiID id, bool hovered) {
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID animation_id = id ^ 0x7F4A7C15U;
    float value = storage->GetFloat(animation_id, 0.0F);
    const float target = hovered ? 1.0F : 0.0F;
    const float delta = std::min(ImGui::GetIO().DeltaTime, 0.1F);
    value += (target - value) * (1.0F - std::exp(-delta / 0.06F));
    if (std::abs(value - target) < 0.01F) value = target;
    storage->SetFloat(animation_id, value);
    return value;
}

enum class ButtonTone { primary, secondary, danger };

bool styled_button(const char* label, float width, ButtonTone tone) {
    const float density = std::clamp(ImGui::GetFontSize() / 18.0F, 1.0F, 2.0F);
    const float height = 36.0F * density;
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const ImGuiID id = ImGui::GetID(label);
    const bool clicked = ImGui::InvisibleButton(label, {width, height});
    const bool hovered = ImGui::IsItemHovered();
    const float hover = animated_hover(id, hovered);
    const float active = ImGui::IsItemActive() ? 1.0F : 0.0F;
    const float opacity = ImGui::GetStyle().Alpha;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec4 base = tone == ButtonTone::primary
                            ? ImVec4{0.00F, 0.58F, 0.78F, opacity}
                        : tone == ButtonTone::danger
                            ? ImVec4{0.86F, 0.25F, 0.22F, opacity}
                            : ImVec4{0.96F, 0.98F, 0.99F, opacity};
    const ImVec4 hovered_color = tone == ButtonTone::primary
                                     ? ImVec4{0.00F, 0.66F, 0.85F, opacity}
                                 : tone == ButtonTone::danger
                                     ? ImVec4{0.92F, 0.31F, 0.27F, opacity}
                                     : ImVec4{0.87F, 0.94F, 0.97F, opacity};
    ImVec4 fill{
        base.x + (hovered_color.x - base.x) * hover,
        base.y + (hovered_color.y - base.y) * hover,
        base.z + (hovered_color.z - base.z) * hover,
        opacity,
    };
    fill.x *= 1.0F - active * 0.08F;
    fill.y *= 1.0F - active * 0.08F;
    fill.z *= 1.0F - active * 0.08F;
    if (tone != ButtonTone::secondary) {
        draw->AddRectFilled({start.x, start.y + 2.0F * density},
                            {start.x + width, start.y + height + 3.0F * density},
                            IM_COL32(29, 84, 103,
                                       static_cast<int>((18.0F + hover * 12.0F) * opacity)),
                            7.0F * density);
    }
    draw->AddRectFilled(start, {start.x + width, start.y + height},
                        ImGui::GetColorU32(fill), 7.0F * density);
    if (tone == ButtonTone::secondary) {
        draw->AddRect(start, {start.x + width, start.y + height},
                      IM_COL32(171, 193, 204, static_cast<int>(220.0F * opacity)),
                      7.0F * density, 0, 1.0F * density);
    }
    const ImVec2 text_size = ImGui::CalcTextSize(label);
    draw->AddText({start.x + (width - text_size.x) * 0.5F,
                   start.y + (height - text_size.y) * 0.5F},
                  tone == ButtonTone::secondary
                      ? IM_COL32(45, 67, 79, static_cast<int>(255.0F * opacity))
                      : IM_COL32(250, 253, 255, static_cast<int>(255.0F * opacity)),
                  label);
    return clicked;
}

bool action_button(const char* label, float width = 150.0F) {
    return styled_button(label, width, ButtonTone::primary);
}

bool secondary_button(const char* label, float width = 150.0F) {
    return styled_button(label, width, ButtonTone::secondary);
}

bool danger_button(const char* label, float width = 150.0F) {
    return styled_button(label, width, ButtonTone::danger);
}

bool toggle_switch(const char* label, bool* value) {
    const float density = std::clamp(ImGui::GetFontSize() / 18.0F, 1.0F, 2.0F);
    const float height = 24.0F * density;
    const float track_width = 42.0F * density;
    const float gap = 9.0F * density;
    const ImVec2 label_size = ImGui::CalcTextSize(label);
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const ImGuiID id = ImGui::GetID(label);
    const bool clicked = ImGui::InvisibleButton(
        label, {track_width + gap + label_size.x, std::max(height, label_size.y)});
    if (clicked) *value = !*value;
    const float hover = animated_hover(id, ImGui::IsItemHovered());
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID progress_id = id ^ 0x51ED270BU;
    float progress = storage->GetFloat(progress_id, *value ? 1.0F : 0.0F);
    const float delta = std::min(ImGui::GetIO().DeltaTime, 0.1F);
    progress += ((*value ? 1.0F : 0.0F) - progress) *
                (1.0F - std::exp(-delta / 0.08F));
    storage->SetFloat(progress_id, progress);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const int track_red = static_cast<int>((176.0F - hover * 12.0F) * (1.0F - progress));
    const int track_green = static_cast<int>((189.0F - hover * 8.0F) * (1.0F - progress) +
                                             (151.0F + hover * 13.0F) * progress);
    const int track_blue = static_cast<int>((198.0F - hover * 4.0F) * (1.0F - progress) +
                                            (190.0F + hover * 13.0F) * progress);
    const ImU32 track_color = IM_COL32(track_red, track_green, track_blue, 255);
    draw->AddRectFilled(start, {start.x + track_width, start.y + height},
                        track_color, height * 0.5F);
    const float knob_radius = height * 0.38F;
    const float knob_x = start.x + height * 0.5F + (track_width - height) * progress;
    draw->AddCircleFilled({knob_x, start.y + height * 0.5F}, knob_radius,
                          IM_COL32(252, 254, 255, 255), 24);
    draw->AddText({start.x + track_width + gap,
                   start.y + (height - label_size.y) * 0.5F},
                  IM_COL32(49, 65, 77, 255), label);
    return clicked;
}

void status_chip(const char* label, const ImVec4 color) {
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const float radius = std::max(3.5F, ImGui::GetFontSize() * 0.20F);
    ImGui::GetWindowDrawList()->AddCircleFilled(
        {cursor.x + radius, cursor.y + ImGui::GetTextLineHeight() * 0.5F},
        radius, ImGui::GetColorU32(color));
    ImGui::SetCursorScreenPos({cursor.x + radius * 2.0F + 8.0F, cursor.y});
    ImGui::TextColored(color, "%s", label);
}

void metric_card(const char* id, const char* label, const char* value,
                 const ImVec2 size, const ImVec4 value_color = accent) {
    begin_card(id, size);
    ImGui::TextColored(text_secondary, "%s", label);
    ImGui::Dummy({0.0F, 5.0F});
    ImGui::SetWindowFontScale(1.32F);
    ImGui::TextColored(value_color, "%s", value);
    ImGui::SetWindowFontScale(1.0F);
    end_card();
}

}  // namespace

void GroundStationUi::apply_input_settings() {
    backend_.apply_input_settings(mouse_sensitivity_, field_of_view_, invert_pitch_);
    set_feedback("输入设置已立即生效");
}

void GroundStationUi::apply_control_settings() {
    backend_.apply_control_settings(mouse_sensitivity_, invert_y_, capture_mouse_, send_keyboard_);
    set_feedback("控制采集设置已立即生效");
}

void GroundStationUi::apply_interface_settings() {
    backend_.apply_interface_settings(hud_scale_, hud_opacity_, show_input_hud_,
                                      show_status_hud_, show_ready_hud_, language_index_);
    set_feedback("界面设置已立即生效");
}

void GroundStationUi::submit_video_settings() {
    backend_.apply_video_settings(quality_index_, resolution_index_, window_mode_,
                                  encoder_index_, decoder_index_, frame_rate_, bitrate_kbps_,
                                  fec_enabled_, fec_redundancy_, brightness_, contrast_,
                                  sharpness_, denoise_, low_latency_, vertical_sync_);
    set_feedback("图传参数已提交（120ms 防抖）");
}

void GroundStationUi::queue_video_settings(bool flush) {
    video_settings_debounce_.schedule();
    if (flush && video_settings_debounce_.flush()) submit_video_settings();
}

void GroundStationUi::begin_display_preview(int previous_resolution, int previous_window_mode,
                                            int previous_display) {
    if (!display_confirmation_open_) {
        previous_resolution_index_ = previous_resolution;
        previous_window_mode_ = previous_window_mode;
        previous_display_index_ = previous_display;
    }
    backend_.preview_display_settings(resolution_index_, window_mode_, display_index_);
    display_confirmation_deadline_ = ImGui::GetTime() + 30.0;
    display_confirmation_open_ = true;
    set_feedback("正在预览显示设置，30 秒内未确认将自动回滚");
}

void GroundStationUi::draw_connection_page(float scale) {
    const ColumnLayout layout = columns(scale);
    const auto devices = backend_.discovered_devices();
    if (!devices.empty()) scanning_ = false;
    if (selected_device_ >= static_cast<int>(devices.size())) selected_device_ = -1;
    if (scanning_ && ImGui::GetTime() - scanning_started_at_ >= 4.0) {
        scanning_ = false;
        if (devices.empty()) set_feedback("扫描完成，暂未发现机器人");
    }

    begin_card("ConnectionSession", {layout.width, 250.0F * scale});
    section_title("当前会话", "连接状态和直接操作");
    status_chip(connected_ ? "机器人已连接" : "等待连接", connected_ ? success : warning);
    if (connected_) {
        ImGui::TextColored(text_secondary, "控制和图传会话由后端维护");
        ImGui::Dummy({0.0F, 8.0F * scale});
        if (danger_button("安全断开", 150.0F * scale)) {
            leave_ready("断开连接，已退出 READY");
            backend_.disconnect_device();
            connected_ = false;
            selected_device_ = -1;
            set_feedback("断开请求已发送");
        }
    } else {
        ImGui::TextColored(text_secondary, "连接成功后才能进入 READY");
    }
    end_card();
    next_column_or_row(layout, scale);
    begin_card("ConnectionPolicy", {layout.width, 250.0F * scale});
    section_title("连接策略", "修改后立即生效，无需额外保存");
    bool policy_changed = toggle_switch("自动重连", &auto_reconnect_);
    ImGui::SetNextItemWidth(210.0F * scale);
    policy_changed |= ImGui::SliderInt("心跳 (ms)", &heartbeat_ms_, 250, 5000);
    ImGui::SetNextItemWidth(180.0F * scale);
    policy_changed |= ImGui::SliderInt("重连 (s)", &reconnect_seconds_, 1, 30);
    ImGui::SetNextItemWidth(210.0F * scale);
    policy_changed |= ImGui::SliderInt("UDP MTU", &mtu_, 576, 1500);
    if (policy_changed) {
        backend_.apply_connection_settings(heartbeat_ms_, reconnect_seconds_, mtu_, auto_reconnect_);
        set_feedback("连接策略已立即生效");
    }
    end_card();

    ImGui::Dummy({0.0F, layout.gap});
    begin_card("Discovery", {0.0F, 310.0F * scale});
    section_title("发现与连接", "使用 mDNS 扫描，也可以直接输入机器人地址");
    const bool discovery_wide = ImGui::GetContentRegionAvail().x >= 980.0F * scale;
    ImGui::SetNextItemWidth(330.0F * scale);
    ImGui::InputTextWithHint("##ServiceName", "mDNS 服务名", service_name_.data(),
                             service_name_.size());
    ImGui::SameLine();
    ImGui::BeginDisabled(scanning_);
    const bool scan_clicked =
        secondary_button(scanning_ ? "正在扫描..." : "扫描机器人", 145.0F * scale);
    ImGui::EndDisabled();
    if (scan_clicked) {
        backend_.scan_devices(service_name_.data());
        scanning_ = true;
        scanning_started_at_ = ImGui::GetTime();
        set_feedback("设备扫描已启动");
    }
    if (discovery_wide) ImGui::SameLine();
    else ImGui::Dummy({0.0F, 5.0F * scale});
    ImGui::SetNextItemWidth(280.0F * scale);
    ImGui::InputTextWithHint("##ManualAddress", "IP:端口", manual_address_.data(),
                             manual_address_.size());
    ImGui::SameLine();
    if (action_button("直接连接", 130.0F * scale)) {
        backend_.connect_device({"手动地址", manual_address_.data(), 0});
        connected_ = true;
        set_feedback("手动连接请求已发送");
    }

    ImGui::Separator();
    if (devices.empty()) {
        ImGui::Dummy({0.0F, 22.0F * scale});
        ImGui::TextColored(text_secondary, "暂无已发现的机器人");
        ImGui::TextWrapped("点击“扫描机器人”后，后端返回的设备会显示在这里。");
    } else {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.0F, 0.0F, 0.0F, 0.0F});
        ImGui::BeginChild("##DiscoveredDeviceList", {0.0F, 120.0F * scale},
                          ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        if (ImGui::BeginTable("DiscoveredDevices", 3,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("机器人", ImGuiTableColumnFlags_WidthStretch, 1.1F);
            ImGui::TableSetupColumn("地址", ImGuiTableColumnFlags_WidthStretch, 1.4F);
            ImGui::TableSetupColumn("信号", ImGuiTableColumnFlags_WidthStretch, 0.5F);
            for (int index = 0; index < static_cast<int>(devices.size()); ++index) {
                ImGui::PushID(index);
                const auto& device = devices[static_cast<std::size_t>(index)];
                ImGui::TableNextRow(0, 34.0F * scale);
                ImGui::TableNextColumn();
                if (ImGui::Selectable(device.name.c_str(), selected_device_ == index,
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                    selected_device_ = index;
                }
                ImGui::TableNextColumn();
                ImGui::TextColored(text_secondary, "%s", device.address.c_str());
                ImGui::TableNextColumn();
                ImGui::TextColored(text_secondary, "%d%%", device.signal_percent);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0F) {
            device_scroll_target_ -= ImGui::GetIO().MouseWheel * 100.0F * scale;
            nested_scroll_consumed_ = true;
        }
        device_scroll_target_ =
            std::clamp(device_scroll_target_, 0.0F, ImGui::GetScrollMaxY());
        const float device_scroll_difference = device_scroll_target_ - ImGui::GetScrollY();
        ImGui::SetScrollY(std::abs(device_scroll_difference) > 0.5F
                              ? ImGui::GetScrollY() +
                                    device_scroll_difference *
                                        (1.0F - std::exp(-ImGui::GetIO().DeltaTime / 0.08F))
                              : device_scroll_target_);
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::BeginDisabled(selected_device_ < 0);
        if (action_button("连接所选机器人", 175.0F * scale)) {
            backend_.connect_device(devices[static_cast<std::size_t>(selected_device_)]);
            connected_ = true;
            set_feedback("设备连接请求已发送");
        }
        ImGui::EndDisabled();
    }
    end_card();
}

void GroundStationUi::draw_video_page(float scale) {
    constexpr const char* resolutions[] = {
        "960 × 540", "1280 × 720", "1600 × 900", "1920 × 1080",
        "2560 × 1440", "3840 × 2160"};
    constexpr const char* window_modes[] = {"窗口", "无边框全屏"};
    constexpr const char* displays[] = {"主显示器", "显示器 2", "显示器 3"};
    constexpr const char* qualities[] = {"低", "中", "高", "超高"};
    constexpr const char* encoders[] = {"JPEG", "H.264"};
    constexpr const char* decoders[] = {"自动选择", "D3D11VA", "软件解码"};
    const ColumnLayout layout = columns(scale);

    begin_card("StreamSettings", {layout.width, 390.0F * scale});
    section_title("远端码流", "连续滑块采用 120ms 防抖，松开时立即提交");
    ImGui::SetNextItemWidth(300.0F * scale);
    if (ImGui::Combo("画质", &quality_index_, qualities, static_cast<int>(std::size(qualities))))
        queue_video_settings(true);
    ImGui::SetNextItemWidth(300.0F * scale);
    if (ImGui::Combo("编码器", &encoder_index_, encoders, static_cast<int>(std::size(encoders))))
        queue_video_settings(true);
    ImGui::SetNextItemWidth(300.0F * scale);
    if (ImGui::SliderInt("目标帧率", &frame_rate_, 24, 240, "%d FPS"))
        queue_video_settings(false);
    if (ImGui::IsItemDeactivatedAfterEdit()) queue_video_settings(true);
    ImGui::SetNextItemWidth(300.0F * scale);
    if (ImGui::SliderInt("目标码率", &bitrate_kbps_, 1000, 80000, "%d kbps"))
        queue_video_settings(false);
    if (ImGui::IsItemDeactivatedAfterEdit()) queue_video_settings(true);
    if (toggle_switch("低延迟模式", &low_latency_)) queue_video_settings(true);
    if (toggle_switch("前向纠错 (FEC)", &fec_enabled_)) queue_video_settings(true);
    if (fec_enabled_) {
        ImGui::SetNextItemWidth(300.0F * scale);
        if (ImGui::SliderFloat("FEC 冗余", &fec_redundancy_, 0.05F, 0.50F, "%.2f"))
            queue_video_settings(false);
        if (ImGui::IsItemDeactivatedAfterEdit()) queue_video_settings(true);
    }
    end_card();
    next_column_or_row(layout, scale);

    begin_card("DecodeSettings", {layout.width, 390.0F * scale});
    section_title("本地解码与图像", "解码选项和远端图像增强参数");
    ImGui::SetNextItemWidth(300.0F * scale);
    if (ImGui::Combo("解码器", &decoder_index_, decoders, static_cast<int>(std::size(decoders))))
        queue_video_settings(true);
    if (toggle_switch("垂直同步", &vertical_sync_)) queue_video_settings(true);
    ImGui::SetNextItemWidth(300.0F * scale);
    if (ImGui::SliderInt("亮度", &brightness_, -100, 100)) queue_video_settings(false);
    if (ImGui::IsItemDeactivatedAfterEdit()) queue_video_settings(true);
    ImGui::SetNextItemWidth(300.0F * scale);
    if (ImGui::SliderInt("对比度", &contrast_, -100, 100)) queue_video_settings(false);
    if (ImGui::IsItemDeactivatedAfterEdit()) queue_video_settings(true);
    ImGui::SetNextItemWidth(300.0F * scale);
    if (ImGui::SliderInt("锐度", &sharpness_, 0, 100)) queue_video_settings(false);
    if (ImGui::IsItemDeactivatedAfterEdit()) queue_video_settings(true);
    ImGui::SetNextItemWidth(300.0F * scale);
    if (ImGui::SliderInt("降噪", &denoise_, 0, 100)) queue_video_settings(false);
    if (ImGui::IsItemDeactivatedAfterEdit()) queue_video_settings(true);
    end_card();

    ImGui::Dummy({0.0F, layout.gap});
    const bool display_wide = ImGui::GetContentRegionAvail().x >= 820.0F * scale;
    begin_card("DisplaySettings", {0.0F, display_wide ? 190.0F * scale : 275.0F * scale});
    section_title("显示模式", "更改后立即预览；30 秒内未确认会自动恢复");
    int next_resolution = resolution_index_;
    int next_window_mode = window_mode_;
    int next_display = display_index_;
    ImGui::SetNextItemWidth(260.0F * scale);
    const bool resolution_changed =
        ImGui::Combo("分辨率", &next_resolution, resolutions, static_cast<int>(std::size(resolutions)));
    if (display_wide) ImGui::SameLine();
    ImGui::SetNextItemWidth(240.0F * scale);
    const bool window_changed =
        ImGui::Combo("窗口模式", &next_window_mode, window_modes,
                     static_cast<int>(std::size(window_modes)));
    if (display_wide) ImGui::SameLine();
    ImGui::SetNextItemWidth(220.0F * scale);
    const bool display_changed =
        ImGui::Combo("目标显示器", &next_display, displays, static_cast<int>(std::size(displays)));
    if (resolution_changed || window_changed || display_changed) {
        const int old_resolution = resolution_index_;
        const int old_window_mode = window_mode_;
        const int old_display = display_index_;
        resolution_index_ = next_resolution;
        window_mode_ = next_window_mode;
        display_index_ = next_display;
        begin_display_preview(old_resolution, old_window_mode, old_display);
    }
    ImGui::TextColored(text_secondary,
                       "第一视角图传始终使用完整窗口画布，设置页关闭后不保留导航占位。");
    end_card();
}

void GroundStationUi::draw_control_page(float scale) {
    constexpr std::array<const char*, 11> actions{
        "切换 READY", "显示输入 HUD", "开发者控制台", "打开设置",
        "前进", "后退", "左移", "右移", "加速", "动作 1", "动作 2"};
    constexpr std::array<int, 11> defaults{
        ImGuiKey_F6, ImGuiKey_Tab, ImGuiKey_GraveAccent, ImGuiKey_Escape,
        ImGuiKey_W, ImGuiKey_S, ImGuiKey_A, ImGuiKey_D,
        ImGuiKey_LeftShift, ImGuiKey_E, ImGuiKey_F};
    const ColumnLayout layout = columns(scale);

    begin_card("InputSettings", {layout.width, 330.0F * scale});
    section_title("第一视角输入", "本地参数即时生效并交给后端持久化");
    ImGui::SetNextItemWidth(300.0F * scale);
    if (ImGui::SliderFloat("鼠标灵敏度", &mouse_sensitivity_, 0.1F, 5.0F, "%.2f ×")) {
        apply_input_settings();
        apply_control_settings();
    }
    ImGui::SetNextItemWidth(300.0F * scale);
    if (ImGui::SliderFloat("视野 (FOV)", &field_of_view_, 30.0F, 120.0F, "%.0f°"))
        apply_input_settings();
    if (toggle_switch("反转俯仰", &invert_pitch_)) apply_input_settings();
    if (toggle_switch("反转鼠标纵轴", &invert_y_)) apply_control_settings();
    if (toggle_switch("READY 时捕获鼠标", &capture_mouse_)) apply_control_settings();
    if (toggle_switch("发送键盘输入", &send_keyboard_)) apply_control_settings();
    end_card();
    next_column_or_row(layout, scale);

    begin_card("SafetySettings", {layout.width, 330.0F * scale});
    section_title("安全状态", "设置页面不会允许机器人继续接收控制输入");
    status_chip("NOT READY", danger);
    ImGui::TextWrapped("打开设置、打开控制台、窗口失去焦点或断开连接时，客户端会立即请求退出 READY。只有返回第一视角后才能再次进入 READY。");
    ImGui::Dummy({0.0F, 12.0F * scale});
    ImGui::TextColored(text_secondary, "当前快捷键");
    ImGui::Text("%s  切换 READY", ImGui::GetKeyName(static_cast<ImGuiKey>(key_bindings_[0])));
    ImGui::Text("%s  设置页面", ImGui::GetKeyName(static_cast<ImGuiKey>(key_bindings_[3])));
    end_card();

    ImGui::Dummy({0.0F, layout.gap});
    begin_card("KeyBindings", {layout.width, 570.0F * scale});
    section_title("键盘绑定", "按下按钮后，再按新的键完成绑定");
    for (int index = 0; index < static_cast<int>(actions.size()); ++index) {
        ImGui::TextColored(text_secondary, "%s", actions[index]);
        ImGui::SameLine(210.0F * scale);
        ImGui::PushID(index);
        const char* key_name = rebinding_action_ == index
                                   ? "等待按键..."
                                   : ImGui::GetKeyName(static_cast<ImGuiKey>(key_bindings_[index]));
        if (ImGui::Button(key_name, {145.0F * scale, 29.0F * scale})) rebinding_action_ = index;
        ImGui::PopID();
    }
    if (rebinding_action_ >= 0) {
        for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key) {
            if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(key), false)) {
                key_bindings_[static_cast<std::size_t>(rebinding_action_)] = key;
                rebinding_action_ = -1;
                backend_.save_key_bindings(
                    std::vector<int>{key_bindings_.begin(), key_bindings_.end()});
                set_feedback("键位已更新并保存");
                break;
            }
        }
    }
    if (secondary_button("恢复旧版默认键位", 185.0F * scale)) {
        key_bindings_ = defaults;
        rebinding_action_ = -1;
        backend_.save_key_bindings(std::vector<int>{key_bindings_.begin(), key_bindings_.end()});
        set_feedback("已恢复旧版默认键位（READY 为 F6）");
    }
    end_card();
    next_column_or_row(layout, scale);

    begin_card("GamepadSettings", {layout.width, (layout.wide ? 570.0F : 300.0F) * scale});
    section_title("手柄", "保留旧版手柄配置入口");
    status_chip("未检测到手柄", text_secondary);
    ImGui::TextColored(text_secondary, "左摇杆移动，右摇杆控制视角");
    ImGui::SetNextItemWidth(300.0F * scale);
    if (ImGui::SliderFloat("摇杆死区", &gamepad_deadzone_, 0.0F, 0.5F, "%.2f"))
        backend_.apply_gamepad_settings(gamepad_deadzone_, gamepad_vibration_);
    if (toggle_switch("振动反馈", &gamepad_vibration_))
        backend_.apply_gamepad_settings(gamepad_deadzone_, gamepad_vibration_);
    ImGui::Dummy({0.0F, 12.0F * scale});
    ImGui::TextWrapped("A / B / X / Y、LT / RT 和摇杆数据将在真实输入后端中映射；当前页面已经暴露完整配置接口。");
    end_card();
}

void GroundStationUi::draw_recording_page(float scale) {
    constexpr const char* formats[] = {"MP4 (H.264)", "MKV (H.264)", "原始码流"};
    const ColumnLayout layout = columns(scale);
    begin_card("RecordingFiles", {layout.width, 310.0F * scale});
    section_title("文件与质量", "录制使用机器人回传的原始图传内容");
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextWithHint("##RecordingDirectory", "保存目录", recording_directory_.data(),
                             recording_directory_.size());
    ImGui::SetNextItemWidth(280.0F * scale);
    ImGui::Combo("封装格式", &recording_format_, formats, static_cast<int>(std::size(formats)));
    ImGui::SetNextItemWidth(280.0F * scale);
    ImGui::SliderInt("录制质量", &recording_quality_, 1, 100);
    ImGui::SetNextItemWidth(280.0F * scale);
    ImGui::SliderInt("自动分段", &split_minutes_, 0, 120, "%d 分钟");
    end_card();
    next_column_or_row(layout, scale);
    begin_card("RecordingActions", {layout.width, 310.0F * scale});
    section_title("录制控制", "操作即时执行，不使用统一应用按钮");
    status_chip(recording_ ? "正在录制" : "未录制", recording_ ? danger : text_secondary);
    if (!recording_) {
        if (action_button("开始录制", 160.0F * scale)) {
            backend_.start_recording(recording_directory_.data(), recording_format_,
                                     recording_quality_, split_minutes_);
            recording_ = true;
            recording_started_at_ = ImGui::GetTime();
            set_feedback("录像启动请求已发送");
        }
    } else {
        const int elapsed = static_cast<int>(ImGui::GetTime() - recording_started_at_);
        ImGui::TextColored(danger, "REC  %02d:%02d:%02d", elapsed / 3600,
                           (elapsed / 60) % 60, elapsed % 60);
        if (action_button("停止并保存", 160.0F * scale)) {
            backend_.stop_recording();
            recording_ = false;
            recording_started_at_ = 0.0;
            set_feedback("录像停止请求已发送");
        }
    }
    ImGui::SameLine();
    if (secondary_button("截图", 110.0F * scale)) {
        backend_.take_screenshot(recording_directory_.data());
        set_feedback("截图请求已发送");
    }
    if (secondary_button("打开保存目录", 160.0F * scale)) {
        backend_.open_recordings_folder(recording_directory_.data());
        set_feedback("打开录像目录请求已发送");
    }
    end_card();
}

void GroundStationUi::draw_diagnostics_page(float scale) {
    const auto value = backend_.telemetry();
    char fps[32]{};
    char latency[32]{};
    char loss[32]{};
    char bandwidth[32]{};
    std::snprintf(fps, sizeof(fps), "%.1f FPS", value.fps);
    std::snprintf(latency, sizeof(latency), "%.1f ms", value.latency_ms);
    std::snprintf(loss, sizeof(loss), "%.2f %%", value.packet_loss_percent);
    std::snprintf(bandwidth, sizeof(bandwidth), "%.2f Mbps", value.bandwidth_mbps);
    const float gap = 10.0F * scale;
    const float metrics_available = ImGui::GetContentRegionAvail().x;
    const bool four_across = metrics_available >= 900.0F * scale;
    const int metrics_per_row = four_across ? 4 : 2;
    const float width =
        (metrics_available - gap * static_cast<float>(metrics_per_row - 1)) /
        static_cast<float>(metrics_per_row);
    metric_card("DiagFps", "图传帧率", fps, {width, 90.0F * scale});
    ImGui::SameLine(0.0F, gap);
    metric_card("DiagLatency", "往返延迟", latency, {width, 90.0F * scale});
    if (four_across) ImGui::SameLine(0.0F, gap);
    else ImGui::Dummy({0.0F, gap});
    metric_card("DiagLoss", "丢包率", loss, {width, 90.0F * scale},
                value.packet_loss_percent > 3.0F ? danger : success);
    ImGui::SameLine(0.0F, gap);
    metric_card("DiagBandwidth", "接收码率", bandwidth, {width, 90.0F * scale});

    if (show_performance_graph_) {
        ImGui::Dummy({0.0F, 14.0F * scale});
        const ColumnLayout graph_layout = columns(scale);
        begin_card("FpsGraph", {graph_layout.width, 250.0F * scale});
        section_title("帧率时间线");
        ImGui::PlotLines("##FpsHistory", fps_history_.data(),
                         static_cast<int>(fps_history_.size()), 0, nullptr,
                         0.0F, 240.0F, {-1.0F, 165.0F * scale});
        end_card();
        next_column_or_row(graph_layout, scale);
        begin_card("LatencyGraph", {graph_layout.width, 250.0F * scale});
        section_title("延迟时间线");
        ImGui::PlotLines("##LatencyHistory", latency_history_.data(),
                         static_cast<int>(latency_history_.size()), 0, nullptr,
                         0.0F, 300.0F, {-1.0F, 165.0F * scale});
        end_card();
    }

    if (show_debug_info_) {
        ImGui::Dummy({0.0F, 14.0F * scale});
        begin_card("RawDebugInfo", {0.0F, 135.0F * scale});
        section_title("原始调试信息");
        ImGui::Text("decoded_frames=%d  connected=%s  ready=%s  recording=%s",
                    value.decoded_frames, connected_ ? "true" : "false",
                    ready_ ? "true" : "false", recording_ ? "true" : "false");
        ImGui::TextColored(text_secondary,
                           "encoder=%d decoder=%d fps=%d bitrate_kbps=%d fec=%.2f",
                           encoder_index_, decoder_index_, frame_rate_, bitrate_kbps_,
                           fec_redundancy_);
        end_card();
    }

    ImGui::Dummy({0.0F, 14.0F * scale});
    const bool tools_wide = ImGui::GetContentRegionAvail().x >= 900.0F * scale;
    begin_card("DiagnosticTools", {0.0F, (tools_wide ? 190.0F : 330.0F) * scale});
    section_title("诊断与开发", "性能、链路与开发信息集中管理");
    bool diagnostics_changed = toggle_switch("显示性能曲线", &show_performance_graph_);
    if (tools_wide) ImGui::SameLine();
    diagnostics_changed |= toggle_switch("显示原始调试信息", &show_debug_info_);
    if (tools_wide) ImGui::SameLine();
    diagnostics_changed |= toggle_switch("详细日志", &verbose_log_);
    if (tools_wide) ImGui::SameLine();
    diagnostics_changed |= toggle_switch("前端模拟模式", &simulation_mode_);
    if (diagnostics_changed)
        backend_.apply_diagnostics_settings(show_performance_graph_, show_debug_info_,
                                            verbose_log_, simulation_mode_);
    if (secondary_button("清空性能曲线", 160.0F * scale)) {
        fps_history_.fill(0.0F);
        latency_history_.fill(0.0F);
        set_feedback("性能曲线已清空");
    }
    ImGui::SameLine();
    if (action_button("导出诊断报告", 170.0F * scale)) {
        backend_.export_diagnostics();
        set_feedback("诊断导出请求已发送");
    }
    end_card();
}

void GroundStationUi::draw_audit_page(float scale) {
    const bool tools_wide = ImGui::GetContentRegionAvail().x >= 820.0F * scale;
    begin_card("LogTools", {0.0F, (tools_wide ? 100.0F : 155.0F) * scale});
    ImGui::SetNextItemWidth(std::min(460.0F * scale, ImGui::GetContentRegionAvail().x));
    ImGui::InputTextWithHint("##AuditFilter", "过滤时间、级别或消息", audit_filter_.data(),
                             audit_filter_.size());
    if (tools_wide) ImGui::SameLine();
    else ImGui::Dummy({0.0F, 4.0F * scale});
    if (secondary_button("导出日志", 125.0F * scale)) {
        backend_.export_audit_log();
        set_feedback("日志导出请求已发送");
    }
    ImGui::SameLine();
    if (danger_button("清空日志", 125.0F * scale)) {
        backend_.clear_audit_log();
        set_feedback("日志清空请求已发送");
    }
    end_card();
    ImGui::Dummy({0.0F, 12.0F * scale});
    const auto entries = backend_.audit_entries();
    if (ImGui::BeginTable("AuditTable", 3,
                          ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
                          {0.0F, 410.0F * scale})) {
        ImGui::TableSetupColumn("时间", ImGuiTableColumnFlags_WidthFixed, 145.0F * scale);
        ImGui::TableSetupColumn("级别", ImGuiTableColumnFlags_WidthFixed, 90.0F * scale);
        ImGui::TableSetupColumn("消息");
        ImGui::TableHeadersRow();
        const std::string_view filter{audit_filter_.data()};
        for (const auto& entry : entries) {
            if (!filter.empty() && entry.time.find(filter) == std::string::npos &&
                entry.level.find(filter) == std::string::npos &&
                entry.message.find(filter) == std::string::npos) continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(entry.time.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(entry.level.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(entry.message.c_str());
        }
        if (entries.empty()) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(text_secondary, "暂无日志记录");
        }
        ImGui::EndTable();
    }
}

void GroundStationUi::draw_interface_page(float scale) {
    const ColumnLayout layout = columns(scale);
    begin_card("HudComponents", {layout.width, 325.0F * scale});
    section_title("HUD 组件", "只保留第一视角操作需要的信息");
    bool changed = toggle_switch("左下角键鼠输入", &show_input_hud_);
    changed |= toggle_switch("右下角网络统计", &show_status_hud_);
    changed |= toggle_switch("底部 READY 状态", &show_ready_hud_);
    ImGui::SetNextItemWidth(300.0F * scale);
    changed |= ImGui::SliderFloat("HUD 缩放", &hud_scale_, 0.75F, 1.40F, "%.2f ×");
    ImGui::SetNextItemWidth(300.0F * scale);
    changed |= ImGui::SliderFloat("HUD 透明度", &hud_opacity_, 0.35F, 1.0F, "%.2f");
    if (changed) apply_interface_settings();
    if (secondary_button("恢复 HUD 默认值", 180.0F * scale)) {
        show_input_hud_ = true;
        show_status_hud_ = true;
        show_ready_hud_ = true;
        hud_scale_ = 1.0F;
        hud_opacity_ = 0.86F;
        apply_interface_settings();
    }
    end_card();
    next_column_or_row(layout, scale);

    begin_card("HudPreview", {layout.width, 325.0F * scale});
    section_title("HUD 预览", "实际 HUD 以覆盖层方式绘制，不压缩视频");
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1{p0.x + ImGui::GetContentRegionAvail().x, p0.y + 190.0F * scale};
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(p0, p1, IM_COL32(18, 28, 38, 255), 8.0F * scale);
    draw->AddText({p0.x + 16.0F * scale, p0.y + 20.0F * scale},
                  IM_COL32(241, 247, 250, 230), "W   SHIFT   MOUSE-L");
    draw->AddText({p1.x - 180.0F * scale, p1.y - 116.0F * scale},
                  IM_COL32(0, 190, 225, 240), "FPS        60.0");
    draw->AddText({p1.x - 180.0F * scale, p1.y - 90.0F * scale},
                  IM_COL32(225, 236, 242, 230), "BITRATE  12.4 Mbps");
    draw->AddText({p1.x - 180.0F * scale, p1.y - 64.0F * scale},
                  IM_COL32(225, 236, 242, 230), "RTT        18 ms");
    draw->AddText({p1.x - 180.0F * scale, p1.y - 38.0F * scale},
                  IM_COL32(225, 236, 242, 230), "LOSS        0.2 %");
    const char* ready = "NOT READY";
    const ImVec2 ready_size = ImGui::CalcTextSize(ready);
    draw->AddText({(p0.x + p1.x - ready_size.x) * 0.5F, p1.y - 28.0F * scale},
                  IM_COL32(238, 94, 84, 240), ready);
    ImGui::Dummy({0.0F, 190.0F * scale});
    end_card();

    ImGui::Dummy({0.0F, layout.gap});
    begin_card("Language", {0.0F, 150.0F * scale});
    section_title("文字与语言", "所有界面文本使用 UTF-8 和完整中文字符集");
    ImGui::Text("简体中文  /  UTF-8");
    ImGui::TextColored(text_secondary,
                       "字体优先使用微软雅黑，并回退到黑体或宋体；中文不再依赖系统 ANSI 编码。");
    end_card();
}

void GroundStationUi::draw_display_confirmation(float scale) {
    if (!display_confirmation_open_) return;
    ImGui::OpenPopup("确认显示设置");
    ImGui::SetNextWindowSize({430.0F * scale, 190.0F * scale}, ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("确认显示设置", nullptr, ImGuiWindowFlags_NoResize)) {
        const int remaining = std::max(
            0, static_cast<int>(std::ceil(display_confirmation_deadline_ - ImGui::GetTime())));
        ImGui::TextWrapped("当前显示设置是否正常？%d 秒后将自动恢复原设置。", remaining);
        ImGui::Dummy({0.0F, 14.0F * scale});
        if (action_button("保留当前设置", 170.0F * scale)) {
            backend_.confirm_display_settings();
            display_confirmation_open_ = false;
            set_feedback("显示设置已确认");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (secondary_button("立即恢复", 150.0F * scale) || remaining <= 0) {
            resolution_index_ = previous_resolution_index_;
            window_mode_ = previous_window_mode_;
            display_index_ = previous_display_index_;
            backend_.revert_display_settings();
            display_confirmation_open_ = false;
            set_feedback(remaining <= 0 ? "确认超时，显示设置已自动回滚" : "显示设置已回滚");
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void GroundStationUi::draw_about_dialog(float scale) {
    if (!about_open_) return;
    ImGui::OpenPopup("关于 PIP-Link");
    ImGui::SetNextWindowSize({520.0F * scale, 285.0F * scale}, ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("关于 PIP-Link", nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::SetWindowFontScale(1.45F);
        ImGui::TextColored(accent, "PIP-Link");
        ImGui::SetWindowFontScale(1.0F);
        ImGui::Text("版本 0.1.0");
        ImGui::TextColored(text_secondary, "C++20 · SDL3 · Dear ImGui · Direct3D 11");
        ImGui::Separator();
        ImGui::TextWrapped("面向机器人的第一视角控制客户端。视频画面始终占据完整操作空间，HUD 只显示键鼠输入、FPS、码率、RTT、丢包率和 READY 状态。");
        ImGui::Dummy({0.0F, 10.0F * scale});
        ImGui::TextColored(text_secondary, "%s 设置  ·  %s READY  ·  %s 输入 HUD  ·  %s 控制台",
                           ImGui::GetKeyName(static_cast<ImGuiKey>(key_bindings_[3])),
                           ImGui::GetKeyName(static_cast<ImGuiKey>(key_bindings_[0])),
                           ImGui::GetKeyName(static_cast<ImGuiKey>(key_bindings_[1])),
                           ImGui::GetKeyName(static_cast<ImGuiKey>(key_bindings_[2])));
        if (secondary_button("关闭", 120.0F * scale)) {
            about_open_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

}  // namespace pip_link::ui
