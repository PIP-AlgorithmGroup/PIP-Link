#include "pip_link/ui/ground_station_ui.hpp"

#include "pip_link/core/smooth_scroll.hpp"

#include "pip_link/core/input_validation.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string_view>
#include <vector>

namespace pip_link::ui {
namespace {

constexpr ImVec4 text_primary{0.07F, 0.11F, 0.16F, 1.00F};
constexpr ImVec4 accent{0.00F, 0.44F, 0.62F, 1.00F};
constexpr ImVec4 text_secondary{0.28F, 0.35F, 0.42F, 1.00F};
constexpr ImVec4 success{0.04F, 0.45F, 0.25F, 1.00F};
constexpr ImVec4 warning{0.66F, 0.32F, 0.02F, 1.00F};
constexpr ImVec4 danger{0.70F, 0.13F, 0.10F, 1.00F};

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
    ImGui::PushStyleColor(ImGuiCol_ChildBg, {1.0F, 1.0F, 1.0F, 1.0F});
    ImGui::PushStyleColor(ImGuiCol_Border, {0.66F, 0.73F, 0.78F, 0.92F});
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

bool animated_combo(const char* label, int* current_item,
                    const char* const items[], int item_count) {
    if (item_count <= 0) return false;
    *current_item = std::clamp(*current_item, 0, item_count - 1);

    ImGui::PushID(label);
    const float width = ImGui::CalcItemWidth();
    const float height = ImGui::GetFrameHeight();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImGuiID combo_id = ImGui::GetID("##AnimatedCombo");
    const bool clicked = ImGui::InvisibleButton("##AnimatedCombo", {width, height});
    const bool hovered = ImGui::IsItemHovered();
    const float hover = animated_hover(combo_id, hovered);
    if (clicked) ImGui::OpenPopup("##ComboPopup");

    const ImVec4 base = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
    const ImVec4 over = ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered);
    const ImVec4 fill{
        base.x + (over.x - base.x) * hover,
        base.y + (over.y - base.y) * hover,
        base.z + (over.z - base.z) * hover,
        base.w,
    };
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, {origin.x + width, origin.y + height},
                        ImGui::GetColorU32(fill), ImGui::GetStyle().FrameRounding);
    draw->AddRect(origin, {origin.x + width, origin.y + height},
                  ImGui::GetColorU32(ImGuiCol_Border), ImGui::GetStyle().FrameRounding);
    const ImVec2 preview_size = ImGui::CalcTextSize(items[*current_item]);
    draw->PushClipRect({origin.x + 8.0F, origin.y},
                       {origin.x + width - 28.0F, origin.y + height}, true);
    draw->AddText({origin.x + 10.0F,
                   origin.y + (height - preview_size.y) * 0.5F},
                  ImGui::GetColorU32(ImGuiCol_Text), items[*current_item]);
    draw->PopClipRect();
    const ImVec2 arrow_center{origin.x + width - 14.0F, origin.y + height * 0.5F};
    draw->AddTriangleFilled({arrow_center.x - 4.0F, arrow_center.y - 2.0F},
                            {arrow_center.x + 4.0F, arrow_center.y - 2.0F},
                            {arrow_center.x, arrow_center.y + 3.0F},
                            ImGui::GetColorU32(ImGuiCol_TextDisabled));

    const std::string_view label_view{label};
    const std::size_t marker = label_view.find("##");
    if (marker != 0) {
        const std::string_view visible = marker == std::string_view::npos
                                             ? label_view : label_view.substr(0, marker);
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(visible.data(), visible.data() + visible.size());
    }

    bool changed = false;
    const float step_height = ImGui::GetTextLineHeight() + 8.0F;
    const float full_height = step_height * static_cast<float>(item_count) + 8.0F;
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID height_id = combo_id ^ 0x4C5B1E29U;
    float popup_height = storage->GetFloat(height_id, 0.0F);
    const bool popup_open = ImGui::IsPopupOpen("##ComboPopup");
    const float target_height = popup_open ? full_height : 0.0F;
    popup_height += (target_height - popup_height) *
                    (1.0F - std::exp(-std::min(ImGui::GetIO().DeltaTime, 0.1F) / 0.06F));
    if (std::abs(popup_height - target_height) < 0.5F) popup_height = target_height;
    storage->SetFloat(height_id, popup_height);

    if (popup_open) {
        ImGui::SetNextWindowPos({origin.x, origin.y + height + 2.0F});
        ImGui::SetNextWindowSize({width, std::max(2.0F, popup_height)});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0F, 4.0F});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0F, 0.0F});
        if (ImGui::BeginPopup("##ComboPopup",
                              ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
                                  ImGuiWindowFlags_NoScrollWithMouse)) {
            for (int index = 0; index < item_count; ++index) {
                ImGui::PushID(index);
                const ImVec2 item_origin = ImGui::GetCursorScreenPos();
                const ImGuiID item_id = ImGui::GetID("##ComboItem");
                const bool selected = index == *current_item;
                const bool item_clicked = ImGui::InvisibleButton(
                    "##ComboItem", {ImGui::GetContentRegionAvail().x, step_height});
                const float item_hover = animated_hover(item_id, ImGui::IsItemHovered());
                const float background_alpha = selected ? 0.13F : 0.08F * item_hover;
                if (background_alpha > 0.001F) {
                    draw = ImGui::GetWindowDrawList();
                    draw->AddRectFilled(
                        {item_origin.x + 4.0F, item_origin.y + 1.0F},
                        {item_origin.x + width - 4.0F, item_origin.y + step_height - 1.0F},
                        ImGui::GetColorU32({accent.x, accent.y, accent.z, background_alpha}),
                        4.0F);
                }
                const ImVec2 item_text_size = ImGui::CalcTextSize(items[index]);
                ImGui::GetWindowDrawList()->AddText(
                    {item_origin.x + 10.0F,
                     item_origin.y + (step_height - item_text_size.y) * 0.5F},
                    ImGui::GetColorU32(selected ? accent : text_primary), items[index]);
                if (item_clicked) {
                    changed = !selected;
                    *current_item = index;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar(2);
    }
    ImGui::PopID();
    return changed;
}

enum class ButtonTone { primary, secondary, danger };

bool styled_button(const char* label, float width, ButtonTone tone) {
    const float density = std::clamp(ImGui::GetFontSize() / 18.0F, 1.0F, 2.0F);
    const float height = ImGui::GetFrameHeight();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const ImGuiID id = ImGui::GetID(label);
    const bool clicked = ImGui::InvisibleButton(label, {width, height});
    const bool hovered = ImGui::IsItemHovered();
    const float hover = animated_hover(id, hovered);
    const float active = ImGui::IsItemActive() ? 1.0F : 0.0F;
    const float opacity = ImGui::GetStyle().Alpha;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec4 base = tone == ButtonTone::primary
                            ? ImVec4{0.00F, 0.44F, 0.62F, opacity}
                        : tone == ButtonTone::danger
                            ? ImVec4{0.70F, 0.13F, 0.10F, opacity}
                            : ImVec4{0.92F, 0.95F, 0.97F, opacity};
    const ImVec4 hovered_color = tone == ButtonTone::primary
                                     ? ImVec4{0.00F, 0.52F, 0.70F, opacity}
                                 : tone == ButtonTone::danger
                                     ? ImVec4{0.79F, 0.19F, 0.15F, opacity}
                                     : ImVec4{0.84F, 0.91F, 0.94F, opacity};
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
                      IM_COL32(151, 174, 188, static_cast<int>(235.0F * opacity)),
                      7.0F * density, 0, 1.0F * density);
    }
    const ImVec2 text_size = ImGui::CalcTextSize(label);
    draw->AddText({start.x + (width - text_size.x) * 0.5F,
                   start.y + (height - text_size.y) * 0.5F},
                  tone == ButtonTone::secondary
                      ? IM_COL32(31, 48, 61, static_cast<int>(255.0F * opacity))
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
    const int track_red = static_cast<int>((151.0F - hover * 10.0F) * (1.0F - progress));
    const int track_green = static_cast<int>((169.0F - hover * 8.0F) * (1.0F - progress) +
                                             (122.0F + hover * 10.0F) * progress);
    const int track_blue = static_cast<int>((181.0F - hover * 4.0F) * (1.0F - progress) +
                                            (151.0F + hover * 10.0F) * progress);
    const ImU32 track_color = IM_COL32(track_red, track_green, track_blue, 255);
    draw->AddRectFilled(start, {start.x + track_width, start.y + height},
                        track_color, height * 0.5F);
    const float knob_radius = height * 0.38F;
    const float knob_x = start.x + height * 0.5F + (track_width - height) * progress;
    draw->AddCircleFilled({knob_x, start.y + height * 0.5F}, knob_radius,
                          IM_COL32(252, 254, 255, 255), 24);
    draw->AddText({start.x + track_width + gap,
                   start.y + (height - label_size.y) * 0.5F},
                  ImGui::GetColorU32(text_primary), label);
    return clicked;
}

void status_chip(const char* label, const ImVec4 color) {
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const float radius = std::max(3.5F, ImGui::GetFontSize() * 0.20F);
    ImGui::GetWindowDrawList()->AddCircleFilled(
        {cursor.x + radius, cursor.y + ImGui::GetTextLineHeight() * 0.5F},
        radius, ImGui::GetColorU32(color));
    ImGui::SetCursorScreenPos({cursor.x + radius * 2.0F + 8.0F, cursor.y});
    ImGui::TextColored(text_primary, "%s", label);
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
    const char* connection_label = "等待连接";
    ImVec4 connection_color = warning;
    if (connection_state_ == backend::ConnectionState::connecting) {
        connection_label = "正在连接";
        connection_color = accent;
    } else if (connection_state_ == backend::ConnectionState::connected) {
        connection_label = "机器人已连接";
        connection_color = success;
    } else if (connection_state_ == backend::ConnectionState::disconnecting) {
        connection_label = "正在断开";
        connection_color = warning;
    } else if (connection_state_ == backend::ConnectionState::failed) {
        connection_label = "连接失败";
        connection_color = danger;
    }
    status_chip(connection_label, connection_color);
    if (is_connected()) {
        ImGui::TextColored(text_secondary, "控制和图传会话由后端维护");
        ImGui::Dummy({0.0F, 8.0F * scale});
        if (danger_button("安全断开", 150.0F * scale)) {
            leave_ready("断开连接，已退出 READY");
            backend_.disconnect_device();
            selected_device_ = -1;
            set_feedback("断开请求已发送，等待后端确认");
        }
    } else {
        ImGui::TextColored(text_secondary, "连接成功后才能进入 READY");
    }
    end_card();
    next_column_or_row(layout, scale);
    begin_card("ConnectionPolicy", {layout.width, 250.0F * scale});
    section_title("连接策略", "修改后立即生效，无需额外保存");
    bool policy_changed = toggle_switch("自动重连", &auto_reconnect_);
    ImGui::SetNextItemWidth(300.0F * scale);
    policy_changed |= ImGui::SliderInt("心跳 (ms)", &heartbeat_ms_, 250, 5000);
    ImGui::SetNextItemWidth(300.0F * scale);
    policy_changed |= ImGui::SliderInt("重连 (s)", &reconnect_seconds_, 1, 30);
    ImGui::SetNextItemWidth(300.0F * scale);
    policy_changed |= ImGui::SliderInt("UDP MTU", &mtu_, 576, 1500);
    if (policy_changed) {
        backend_.apply_connection_settings(heartbeat_ms_, reconnect_seconds_, mtu_, auto_reconnect_);
        set_feedback("连接策略已立即生效");
    }
    end_card();

    ImGui::Dummy({0.0F, layout.gap});
    begin_card("Discovery", {0.0F, 310.0F * scale});
    section_title("发现与连接",
                  "mDNS 自动扫描；手动连接请填写机载日志中的 ctrl 控制端口");
    const bool discovery_wide = ImGui::GetContentRegionAvail().x >= 980.0F * scale;
    bool scan_clicked = false;
    bool direct_connect_clicked = false;
    const int input_columns = discovery_wide ? 2 : 1;
    if (ImGui::BeginTable("DiscoveryInputs", input_columns,
                          ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        const float scan_button_width = 145.0F * scale;
        ImGui::SetNextItemWidth(std::max(160.0F * scale,
            ImGui::GetContentRegionAvail().x - scan_button_width - ImGui::GetStyle().ItemSpacing.x));
        ImGui::InputTextWithHint("##ServiceName", "mDNS 服务名", service_name_.data(),
                                 service_name_.size());
        ImGui::SameLine();
        ImGui::BeginDisabled(scanning_);
        scan_clicked = secondary_button(scanning_ ? "正在扫描..." : "扫描机器人",
                                        scan_button_width);
        ImGui::EndDisabled();

        ImGui::TableNextColumn();
        const float connect_button_width = 130.0F * scale;
        const float video_port_width = 105.0F * scale;
        ImGui::SetNextItemWidth(std::max(160.0F * scale,
            ImGui::GetContentRegionAvail().x - connect_button_width - video_port_width -
            ImGui::GetStyle().ItemSpacing.x * 2.0F));
        ImGui::InputTextWithHint("##ManualAddress", "IP:控制端口（ctrl）", manual_address_.data(),
                                 manual_address_.size());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(video_port_width);
        ImGui::InputInt("##ManualVideoPort", &manual_video_port_, 0, 0);
        manual_video_port_ = std::clamp(manual_video_port_, 1, 65535);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("视频端口");
        ImGui::SameLine();
        ImGui::BeginDisabled(is_connected() || connection_busy());
        direct_connect_clicked = action_button("直接连接", connect_button_width);
        ImGui::EndDisabled();
        ImGui::EndTable();
    }
    if (scan_clicked) {
        if (!core::is_valid_service_name(service_name_.data())) {
            set_feedback("mDNS 服务名不能为空");
        } else {
            backend_.scan_devices(service_name_.data());
            scanning_ = true;
            scanning_started_at_ = ImGui::GetTime();
            set_feedback("设备扫描已启动");
        }
    }
    if (direct_connect_clicked) {
        if (!core::is_valid_endpoint(manual_address_.data())) {
            set_feedback("地址格式无效，请使用 IP:控制端口（ctrl）");
        } else {
            backend_.connect_device({"手动地址", manual_address_.data(), 0,
                                     static_cast<std::uint16_t>(manual_video_port_)});
            set_feedback("手动连接请求已发送，等待后端确认");
        }
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
        const float row_width = ImGui::GetContentRegionAvail().x;
        const float address_x = row_width * 0.39F;
        const float signal_x = row_width * 0.86F;
        const ImVec2 header_origin = ImGui::GetCursorScreenPos();
        ImDrawList* device_draw = ImGui::GetWindowDrawList();
        device_draw->AddText(header_origin, ImGui::GetColorU32(text_secondary), "机器人");
        device_draw->AddText({header_origin.x + address_x, header_origin.y},
                             ImGui::GetColorU32(text_secondary), "地址");
        device_draw->AddText({header_origin.x + signal_x, header_origin.y},
                             ImGui::GetColorU32(text_secondary), "信号");
        ImGui::Dummy({row_width, ImGui::GetTextLineHeight() + 5.0F * scale});
        for (int index = 0; index < static_cast<int>(devices.size()); ++index) {
            ImGui::PushID(index);
            const auto& device = devices[static_cast<std::size_t>(index)];
            const ImVec2 row_origin = ImGui::GetCursorScreenPos();
            const float row_height = 34.0F * scale;
            const ImGuiID row_id = ImGui::GetID("##DeviceRow");
            if (ImGui::InvisibleButton("##DeviceRow", {row_width, row_height})) {
                selected_device_ = index;
            }
            const float row_hover = animated_hover(row_id, ImGui::IsItemHovered());
            const bool selected = selected_device_ == index;
            const float row_alpha = selected ? 0.13F : 0.07F * row_hover;
            if (row_alpha > 0.001F) {
                device_draw->AddRectFilled(
                    {row_origin.x + 2.0F * scale, row_origin.y + 1.0F * scale},
                    {row_origin.x + row_width - 2.0F * scale,
                     row_origin.y + row_height - 1.0F * scale},
                    ImGui::GetColorU32({accent.x, accent.y, accent.z, row_alpha}),
                    5.0F * scale);
            }
            char signal[16]{};
            std::snprintf(signal, sizeof(signal), "%d%%", device.signal_percent);
            const float text_y = row_origin.y +
                                 (row_height - ImGui::GetTextLineHeight()) * 0.5F;
            device_draw->PushClipRect(
                {row_origin.x + 8.0F * scale, row_origin.y},
                {row_origin.x + address_x - 10.0F * scale, row_origin.y + row_height}, true);
            device_draw->AddText({row_origin.x + 8.0F * scale, text_y},
                                 ImGui::GetColorU32(selected ? accent : text_primary),
                                 device.name.c_str());
            device_draw->PopClipRect();
            device_draw->PushClipRect(
                {row_origin.x + address_x, row_origin.y},
                {row_origin.x + signal_x - 10.0F * scale, row_origin.y + row_height}, true);
            device_draw->AddText({row_origin.x + address_x, text_y},
                                 ImGui::GetColorU32(text_secondary), device.address.c_str());
            device_draw->PopClipRect();
            device_draw->AddText({row_origin.x + signal_x, text_y},
                                 ImGui::GetColorU32(text_secondary), signal);
            ImGui::PopID();
        }
        const float device_scroll_max = ImGui::GetScrollMaxY();
        const float device_wheel = ImGui::GetIO().MouseWheel;
        if (ImGui::IsWindowHovered() && device_wheel != 0.0F &&
            ((device_wheel > 0.0F && device_scroll_target_ > 0.0F) ||
             (device_wheel < 0.0F && device_scroll_target_ < device_scroll_max))) {
            device_scroll_target_ -= device_wheel * 100.0F * scale;
            nested_scroll_consumed_ = true;
        }
        device_scroll_target_ = std::clamp(device_scroll_target_, 0.0F, device_scroll_max);
        ImGui::SetScrollY(core::advance_smooth_scroll(
            ImGui::GetScrollY(), device_scroll_target_, ImGui::GetIO().DeltaTime, scale));
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::BeginDisabled(selected_device_ < 0 || is_connected() || connection_busy());
        if (action_button("连接所选机器人", 175.0F * scale)) {
            backend_.connect_device(devices[static_cast<std::size_t>(selected_device_)]);
            set_feedback("设备连接请求已发送，等待后端确认");
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
    constexpr const char* decoders[] = {
        "自动（Windows / FFmpeg）", "Windows Media Foundation", "FFmpeg 软件解码"};
    const ColumnLayout layout = columns(scale);

    begin_card("StreamSettings", {layout.width, 390.0F * scale});
    section_title("远端码流", "连续滑块采用 120ms 防抖，松开时立即提交");
    ImGui::SetNextItemWidth(300.0F * scale);
    if (animated_combo("画质", &quality_index_, qualities,
                       static_cast<int>(std::size(qualities))))
        queue_video_settings(true);
    ImGui::SetNextItemWidth(300.0F * scale);
    if (animated_combo("编码器", &encoder_index_, encoders,
                       static_cast<int>(std::size(encoders))))
        queue_video_settings(true);
    ImGui::SetNextItemWidth(300.0F * scale);
    if (ImGui::SliderInt("目标帧率", &frame_rate_, 24, 240, "%d FPS"))
        queue_video_settings(false);
    if (ImGui::IsItemDeactivatedAfterEdit()) queue_video_settings(true);
    ImGui::SetNextItemWidth(300.0F * scale);
    if (ImGui::SliderInt("目标码率", &bitrate_kbps_, 100, 80000, "%d kbps"))
        queue_video_settings(false);
    if (ImGui::IsItemDeactivatedAfterEdit()) queue_video_settings(true);
    if (toggle_switch("低延迟模式", &low_latency_)) queue_video_settings(true);
    if (toggle_switch("前向纠错 (FEC)", &fec_enabled_)) queue_video_settings(true);
    if (fec_enabled_) {
        ImGui::SetNextItemWidth(300.0F * scale);
        if (ImGui::SliderFloat("FEC 冗余", &fec_redundancy_, 0.0F, 1.0F, "%.2f"))
            queue_video_settings(false);
        if (ImGui::IsItemDeactivatedAfterEdit()) queue_video_settings(true);
    }
    end_card();
    next_column_or_row(layout, scale);

    begin_card("DecodeSettings", {layout.width, 390.0F * scale});
    section_title("本地解码与图像", "解码选项和远端图像增强参数");
    ImGui::SetNextItemWidth(300.0F * scale);
    if (animated_combo("解码器", &decoder_index_, decoders,
                       static_cast<int>(std::size(decoders))))
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
    begin_card("DisplaySettings", {0.0F, display_wide ? 205.0F * scale : 330.0F * scale});
    section_title("显示模式", "更改后立即预览；30 秒内未确认会自动恢复");
    int next_resolution = resolution_index_;
    int next_window_mode = window_mode_;
    int next_display = display_index_;
    bool resolution_changed = false;
    bool window_changed = false;
    bool display_changed = false;
    if (ImGui::BeginTable("DisplayOptions", display_wide ? 3 : 1,
                          ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        ImGui::TextColored(text_secondary, "分辨率");
        ImGui::SetNextItemWidth(-1.0F);
        resolution_changed = animated_combo("##Resolution", &next_resolution, resolutions,
                                            static_cast<int>(std::size(resolutions)));

        ImGui::TableNextColumn();
        ImGui::TextColored(text_secondary, "窗口模式");
        ImGui::SetNextItemWidth(-1.0F);
        window_changed = animated_combo("##WindowMode", &next_window_mode, window_modes,
                                        static_cast<int>(std::size(window_modes)));

        ImGui::TableNextColumn();
        ImGui::TextColored(text_secondary, "目标显示器");
        ImGui::SetNextItemWidth(-1.0F);
        display_changed = animated_combo("##TargetDisplay", &next_display, displays,
                                         static_cast<int>(std::size(displays)));
        ImGui::EndTable();
    }
    if (resolution_changed || window_changed || display_changed) {
        const int old_resolution = resolution_index_;
        const int old_window_mode = window_mode_;
        const int old_display = display_index_;
        resolution_index_ = next_resolution;
        window_mode_ = next_window_mode;
        display_index_ = next_display;
        if (resolution_changed) queue_video_settings(true);
        begin_display_preview(old_resolution, old_window_mode, old_display);
    }
    ImGui::TextColored(text_secondary,
                       "第一视角图传始终使用完整窗口画布，设置页关闭后不保留导航占位。");
    end_card();
}

void GroundStationUi::draw_control_page(float scale) {
    constexpr std::array<const char*, binding_count> actions{
        "切换 READY", "显示输入 HUD", "开发者控制台", "打开设置",
        "前进", "后退", "左移", "右移", "加速", "动作 1", "动作 2",
        "开始录制", "截屏", "暂停 / 继续录制", "结束录制"};
    const auto defaults = default_key_bindings();
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
    ImGui::Text("%s  开始录制", ImGui::GetKeyName(
        static_cast<ImGuiKey>(key_bindings_[start_recording_binding])));
    ImGui::Text("%s  截屏", ImGui::GetKeyName(
        static_cast<ImGuiKey>(key_bindings_[take_screenshot_binding])));
    end_card();

    ImGui::Dummy({0.0F, layout.gap});
    begin_card("KeyBindings", {layout.width, 700.0F * scale});
    section_title("键盘绑定", "录制可与暂停或结束共用，暂停与结束不能共用");
    if (ImGui::BeginTable("KeyBindingRows", 3, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("动作", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("按键", ImGuiTableColumnFlags_WidthFixed, 125.0F * scale);
        ImGui::TableSetupColumn("清空", ImGuiTableColumnFlags_WidthFixed, 68.0F * scale);
        for (int index = 0; index < static_cast<int>(actions.size()); ++index) {
            ImGui::TableNextRow(0, ImGui::GetFrameHeight());
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(text_secondary, "%s", actions[index]);
            ImGui::TableNextColumn();
            ImGui::PushID(index);
            const char* key_name = rebinding_action_ == index
                                       ? "等待按键..."
                                   : key_bindings_[index] == unbound_key
                                       ? "未绑定"
                                       : ImGui::GetKeyName(
                                             static_cast<ImGuiKey>(key_bindings_[index]));
            if (ImGui::Button(key_name, {-1.0F, 0.0F})) {
                rebinding_action_ = index;
                rebinding_started_frame_ = ImGui::GetFrameCount();
            }
            ImGui::PopID();
            ImGui::TableNextColumn();
            ImGui::PushID(index);
            ImGui::BeginDisabled(key_bindings_[index] == unbound_key);
            if (ImGui::Button("清空", {-1.0F, 0.0F})) {
                (void)assign_key_binding(key_bindings_, index, unbound_key);
                rebinding_action_ = -1;
                rebinding_started_frame_ = -1;
                backend_.save_key_bindings(
                    std::vector<int>{key_bindings_.begin(), key_bindings_.end()});
                set_feedback("快捷键已清除");
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (rebinding_action_ >= 0 && secondary_button("取消本次绑定", 150.0F * scale)) {
        rebinding_action_ = -1;
        rebinding_started_frame_ = -1;
        set_feedback("已取消键位绑定");
    }
    if (rebinding_action_ >= 0 && ImGui::GetFrameCount() > rebinding_started_frame_) {
        for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_GamepadStart; ++key) {
            if (!is_bindable_keyboard_key(key)) continue;
            if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(key), false)) {
                const int action = rebinding_action_;
                const int cleared = assign_key_binding(
                    key_bindings_, static_cast<std::size_t>(action), key);
                rebinding_action_ = -1;
                rebinding_started_frame_ = -1;
                backend_.save_key_bindings(
                    std::vector<int>{key_bindings_.begin(), key_bindings_.end()});
                set_feedback(cleared > 0 ? "键位已保存，冲突动作已解除绑定"
                                         : "键位已更新并保存");
                break;
            }
        }
    }
    if (secondary_button("恢复旧版默认键位", 185.0F * scale)) {
        key_bindings_ = defaults;
        rebinding_action_ = -1;
        rebinding_started_frame_ = -1;
        backend_.save_key_bindings(std::vector<int>{key_bindings_.begin(), key_bindings_.end()});
        set_feedback("已恢复默认键位（录制 F9，截屏 F10）");
    }
    end_card();
    next_column_or_row(layout, scale);

    begin_card("GamepadSettings", {layout.width, (layout.wide ? 570.0F : 300.0F) * scale});
    section_title("手柄", "SDL3 实时输入与振动反馈");
    status_chip(gamepad_.connected ? gamepad_.name.c_str() : "未检测到手柄",
                gamepad_.connected ? success : text_secondary);
    ImGui::TextColored(text_secondary, "左摇杆移动，右摇杆控制视角");
    if (gamepad_.connected) {
        ImGui::Text("左摇杆  X %.2f  Y %.2f", gamepad_.left_x, gamepad_.left_y);
        ImGui::Text("右摇杆  X %.2f  Y %.2f", gamepad_.right_x, gamepad_.right_y);
    }
    ImGui::SetNextItemWidth(300.0F * scale);
    if (ImGui::SliderFloat("摇杆死区", &gamepad_deadzone_, 0.0F, 0.5F, "%.2f")) {
        backend_.apply_gamepad_settings(gamepad_deadzone_, gamepad_vibration_);
        set_feedback("手柄死区已立即生效");
    }
    if (toggle_switch("振动反馈", &gamepad_vibration_)) {
        backend_.apply_gamepad_settings(gamepad_deadzone_, gamepad_vibration_);
        set_feedback("手柄振动设置已立即生效");
    }
    ImGui::Dummy({0.0F, 12.0F * scale});
    ImGui::TextWrapped("左肩键映射冲刺，A 映射交互，RT 映射主要动作；所有输入只在 READY 时发送。");
    end_card();
}

void GroundStationUi::start_recording_action() {
    if (!core::is_valid_directory(recording_directory_)) {
        set_feedback("保存目录不能为空");
        return;
    }
    const backend::MediaActionResult result = backend_.start_recording(
        recording_directory_, recording_format_, recording_quality_, split_minutes_);
    set_feedback(result.message);
}

void GroundStationUi::take_screenshot_action() {
    if (!core::is_valid_directory(recording_directory_)) {
        set_feedback("保存目录不能为空");
        return;
    }
    const backend::MediaActionResult result =
        backend_.take_screenshot(recording_directory_);
    set_feedback(result.message);
}

void GroundStationUi::toggle_recording_paused_action() {
    const backend::MediaActionResult result = backend_.set_recording_paused(
        recording_state_ != backend::RecordingState::paused);
    set_feedback(result.message);
}

void GroundStationUi::stop_recording_action() {
    backend_.stop_recording();
    set_feedback("录像已结束并保存");
}

void GroundStationUi::draw_recording_page(float scale) {
    constexpr const char* formats[] = {"MP4 (H.264)", "MKV (H.264)", "MKV (FFV1 无损)"};
    const ColumnLayout layout = columns(scale);
    begin_card("RecordingFiles", {layout.width, 335.0F * scale});
    section_title("文件与质量", "录制完整窗口、HUD、设置页面与软件光标");
    ImGui::BeginDisabled(recording_state_ != backend::RecordingState::idle &&
                         recording_state_ != backend::RecordingState::failed);
    ImGui::TextColored(text_secondary, "保存目录");
    ImGui::TextWrapped("%s", recording_directory_.c_str());
    if (secondary_button("选择保存目录...", 160.0F * scale)) {
        const backend::DirectorySelectionResult result =
            backend_.choose_recording_directory(recording_directory_);
        if (result.selected) {
            recording_directory_ = result.directory;
        }
        if (!result.message.empty()) set_feedback(result.message);
    }
    ImGui::Dummy({0.0F, 6.0F * scale});
    ImGui::SetNextItemWidth(300.0F * scale);
    animated_combo("封装格式", &recording_format_, formats,
                   static_cast<int>(std::size(formats)));
    ImGui::SetNextItemWidth(300.0F * scale);
    ImGui::SliderInt("录制质量", &recording_quality_, 1, 100);
    ImGui::SetNextItemWidth(300.0F * scale);
    ImGui::SliderInt("自动分段", &split_minutes_, 0, 120, "%d 分钟");
    ImGui::EndDisabled();
    end_card();
    next_column_or_row(layout, scale);
    begin_card("RecordingActions", {layout.width, 335.0F * scale});
    section_title("录制控制", "操作即时执行，不使用统一应用按钮");
    const char* recording_label = "未录制";
    ImVec4 recording_color = text_secondary;
    if (recording_state_ == backend::RecordingState::starting) {
        recording_label = "正在启动录制";
        recording_color = warning;
    } else if (recording_state_ == backend::RecordingState::recording) {
        recording_label = "正在录制";
        recording_color = danger;
    } else if (recording_state_ == backend::RecordingState::paused) {
        recording_label = "录制已暂停";
        recording_color = warning;
    } else if (recording_state_ == backend::RecordingState::stopping) {
        recording_label = "正在保存录制";
        recording_color = warning;
    } else if (recording_state_ == backend::RecordingState::failed) {
        recording_label = "录制失败";
        recording_color = danger;
    }
    status_chip(recording_label, recording_color);
    if (recording_state_ == backend::RecordingState::idle ||
        recording_state_ == backend::RecordingState::failed) {
        if (action_button("开始录制", 160.0F * scale)) {
            start_recording_action();
        }
    } else if (is_recording()) {
        const int elapsed = static_cast<int>(recording_elapsed_seconds_);
        ImGui::TextColored(recording_state_ == backend::RecordingState::paused
                               ? warning
                               : danger,
                           "%s  %02d:%02d:%02d",
                           recording_state_ == backend::RecordingState::paused
                               ? "PAUSED"
                               : "REC",
                           elapsed / 3600,
                           (elapsed / 60) % 60, elapsed % 60);
        if (action_button(recording_state_ == backend::RecordingState::paused
                              ? "继续录制"
                              : "暂停录制",
                          130.0F * scale)) {
            toggle_recording_paused_action();
        }
        ImGui::SameLine();
        if (action_button("停止并保存", 160.0F * scale)) {
            stop_recording_action();
        }
    }
    ImGui::SameLine();
    if (secondary_button("截图", 110.0F * scale)) {
        take_screenshot_action();
    }
    if (secondary_button("打开保存目录", 160.0F * scale)) {
        if (!core::is_valid_directory(recording_directory_)) {
            set_feedback("保存目录不能为空");
        } else {
            const backend::MediaActionResult result =
                backend_.open_recordings_folder(recording_directory_);
            set_feedback(result.message);
        }
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
        section_title("帧率时间线", "最近 60 秒 · 每 0.5 秒采样");
        ImGui::PlotLines("##FpsHistory", fps_history_.data(),
                         static_cast<int>(fps_history_.size()), 0, nullptr,
                         0.0F, 240.0F, {-1.0F, 165.0F * scale});
        end_card();
        next_column_or_row(graph_layout, scale);
        begin_card("LatencyGraph", {graph_layout.width, 250.0F * scale});
        section_title("延迟时间线", "最近 60 秒 · 每 0.5 秒采样");
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
                    value.decoded_frames, is_connected() ? "true" : "false",
                    ready_ ? "true" : "false", is_recording() ? "true" : "false");
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
    if (diagnostics_changed) {
        backend_.apply_diagnostics_settings(show_performance_graph_, show_debug_info_,
                                             verbose_log_);
        set_feedback("诊断显示与后端选项已立即生效");
    }
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
        ImGui::OpenPopup("确认清空日志");
    }
    ImGui::SetNextWindowSize({420.0F * scale, 190.0F * scale}, ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("确认清空日志", nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::TextWrapped("这会删除当前全部审计记录，且无法在前端撤销。确定继续吗？");
        ImGui::Dummy({0.0F, 14.0F * scale});
        if (danger_button("确认清空", 135.0F * scale)) {
            backend_.clear_audit_log();
            set_feedback("日志清空请求已发送");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (secondary_button("取消", 110.0F * scale)) {
            set_feedback("已取消清空日志");
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    end_card();
    ImGui::Dummy({0.0F, 12.0F * scale});
    const auto entries = backend_.audit_entries();
    begin_card("AuditLog", {0.0F, 410.0F * scale});
    if (ImGui::BeginTable("AuditHeader", 3, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("时间", ImGuiTableColumnFlags_WidthFixed, 145.0F * scale);
        ImGui::TableSetupColumn("级别", ImGuiTableColumnFlags_WidthFixed, 90.0F * scale);
        ImGui::TableSetupColumn("消息");
        ImGui::TableHeadersRow();
        ImGui::EndTable();
    }
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.0F, 0.0F, 0.0F, 0.0F});
    ImGui::BeginChild("##AuditRows", {0.0F, -1.0F}, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (ImGui::BeginTable("AuditRows", 3,
                          ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("##Time", ImGuiTableColumnFlags_WidthFixed, 145.0F * scale);
        ImGui::TableSetupColumn("##Level", ImGuiTableColumnFlags_WidthFixed, 90.0F * scale);
        ImGui::TableSetupColumn("##Message");
        const std::string_view filter{audit_filter_.data()};
        bool has_match = false;
        for (const auto& entry : entries) {
            if (!filter.empty() && entry.time.find(filter) == std::string::npos &&
                entry.level.find(filter) == std::string::npos &&
                entry.message.find(filter) == std::string::npos) continue;
            has_match = true;
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(entry.time.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(entry.level.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(entry.message.c_str());
        }
        if (!has_match) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(text_secondary, "%s",
                               entries.empty() ? "暂无日志记录" : "没有匹配的日志记录");
        }
        ImGui::EndTable();
    }
    const float audit_scroll_max = ImGui::GetScrollMaxY();
    audit_log_tail_.sync(entries.size(), audit_scroll_max);
    const float audit_wheel = ImGui::GetIO().MouseWheel;
    if (ImGui::IsWindowHovered() && audit_wheel != 0.0F &&
        ((audit_wheel > 0.0F && audit_log_tail_.target() > 0.0F) ||
         (audit_wheel < 0.0F && audit_log_tail_.target() < audit_scroll_max))) {
        audit_log_tail_.on_wheel(audit_wheel, audit_scroll_max);
        audit_log_tail_.set_target(
            audit_log_tail_.target() - audit_wheel * 100.0F * scale,
            audit_scroll_max);
        nested_scroll_consumed_ = true;
    }
    ImGui::SetScrollY(core::advance_smooth_scroll(
        ImGui::GetScrollY(), audit_log_tail_.target(), ImGui::GetIO().DeltaTime, scale));
    ImGui::EndChild();
    ImGui::PopStyleColor();
    end_card();
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
    const float preview_unit = scale * animated_hud_scale_;
    const float preview_font_size = ImGui::GetFontSize() * animated_hud_scale_;
    if (input_hud_visibility_ > 0.001F) {
        const int preview_alpha = static_cast<int>(
            255.0F * animated_hud_opacity_ * input_hud_visibility_);
        draw->AddText(ImGui::GetFont(), preview_font_size,
                      {p0.x + 16.0F * preview_unit, p0.y + 16.0F * preview_unit},
                      IM_COL32(241, 247, 250, preview_alpha), "W  SHIFT  MOUSE-L");
    }
    if (status_hud_visibility_ > 0.001F) {
        const int preview_alpha = static_cast<int>(
            255.0F * animated_hud_opacity_ * status_hud_visibility_);
        constexpr std::array<const char*, 4> preview_labels{"FPS", "BITRATE", "RTT", "LOSS"};
        constexpr std::array<const char*, 4> preview_values{
            "60.0", "12.4 Mbps", "18 ms", "0.2 %"};
        const float left = p1.x - 210.0F * preview_unit;
        for (int index = 0; index < 4; ++index) {
            const float y = p0.y + (18.0F + index * 27.0F) * preview_unit;
            draw->AddText(ImGui::GetFont(), preview_font_size, {left, y},
                          IM_COL32(170, 191, 202, preview_alpha),
                          preview_labels[static_cast<std::size_t>(index)]);
            const ImVec2 value_size = ImGui::GetFont()->CalcTextSizeA(
                preview_font_size, 10000.0F, 0.0F,
                preview_values[static_cast<std::size_t>(index)]);
            draw->AddText(ImGui::GetFont(), preview_font_size,
                          {p1.x - 16.0F * preview_unit - value_size.x, y},
                          index == 0 ? IM_COL32(34, 210, 240, preview_alpha)
                                     : IM_COL32(241, 247, 250, preview_alpha),
                          preview_values[static_cast<std::size_t>(index)]);
        }
    }
    if (ready_hud_visibility_ > 0.001F) {
        const int preview_alpha = static_cast<int>(
            255.0F * animated_hud_opacity_ * ready_hud_visibility_);
        const char* ready = "NOT READY";
        const ImVec2 ready_size = ImGui::GetFont()->CalcTextSizeA(
            preview_font_size, 10000.0F, 0.0F, ready);
        draw->AddText(ImGui::GetFont(), preview_font_size,
                      {(p0.x + p1.x - ready_size.x) * 0.5F,
                       p1.y - 28.0F * preview_unit},
                      IM_COL32(238, 94, 84, preview_alpha), ready);
    }
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
            queue_video_settings(true);
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
    if (ImGui::BeginPopupModal("关于 PIP-Link", &about_open_, ImGuiWindowFlags_NoResize)) {
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
