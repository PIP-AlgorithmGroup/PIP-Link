#include "pip_link/ui/ground_station_ui.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <utility>
#include <vector>

namespace pip_link::ui {
namespace {

constexpr ImVec4 accent{0.00F, 0.85F, 1.00F, 1.00F};
constexpr ImVec4 text_primary{0.92F, 0.94F, 1.00F, 1.00F};
constexpr ImVec4 text_secondary{0.50F, 0.52F, 0.58F, 1.00F};
constexpr ImVec4 success{0.10F, 0.95F, 0.40F, 1.00F};
constexpr ImVec4 warning{1.00F, 0.65F, 0.10F, 1.00F};
constexpr ImVec4 danger{0.92F, 0.28F, 0.22F, 1.00F};

struct Tab final {
    const char* label;
    const char* title;
    const char* description;
};

constexpr std::array<Tab, 9> tabs{{
    {"连接", "连接", "发现机载端、建立会话并查看链路状态"},
    {"参数", "链路参数", "心跳、重连和数据传输参数"},
    {"视频", "视频", "图传分辨率、解码和显示策略"},
    {"录制", "录制", "录像、截图与文件分段"},
    {"诊断", "诊断", "实时性能曲线和链路统计"},
    {"控制", "控制", "输入采集、安全状态与快捷键"},
    {"调试", "调试", "前端覆盖层和开发选项"},
    {"审计", "审计", "过滤、导出和管理操作记录"},
    {"关于", "关于 PIP-Link", "版本、组件和快捷操作说明"},
}};

void section_title(const char* title, const char* description = nullptr) {
    ImGui::SetWindowFontScale(1.18F);
    ImGui::TextUnformatted(title);
    ImGui::SetWindowFontScale(1.0F);
    if (description != nullptr) {
        ImGui::TextColored(text_secondary, "%s", description);
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
}

void status_chip(const char* label, const ImVec4 color) {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::Text("● %s", label);
    ImGui::PopStyleColor();
}

void metric_card(const char* id, const char* label, const char* value, const ImVec2 size) {
    ImGui::BeginChild(id, size, ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::TextColored(text_secondary, "%s", label);
    ImGui::Spacing();
    ImGui::SetWindowFontScale(1.42F);
    ImGui::TextColored(accent, "%s", value);
    ImGui::SetWindowFontScale(1.0F);
    ImGui::EndChild();
}

bool action_button(const char* label, float width = 150.0F) {
    return ImGui::Button(label, {width, 36.0F});
}

void draw_empty_state(const char* title, const char* detail) {
    ImGui::Dummy({0.0F, 18.0F});
    ImGui::TextColored(text_secondary, "%s", title);
    ImGui::TextWrapped("%s", detail);
}

}  // namespace

GroundStationUi::GroundStationUi(backend::GroundStationBackend& backend) : backend_(backend) {
    key_bindings_ = {ImGuiKey_F5, ImGuiKey_Tab, ImGuiKey_GraveAccent, ImGuiKey_Escape};
}

bool GroundStationUi::quit_requested() const noexcept {
    return quit_requested_;
}

void GroundStationUi::set_feedback(std::string message) {
    feedback_ = std::move(message);
}

void GroundStationUi::toggle_ready() {
    ready_ = !ready_;
    backend_.set_ready(ready_);
    set_feedback(ready_ ? "已请求进入 READY 状态" : "已请求退出 READY 状态");
}

void GroundStationUi::draw(float delta_seconds, float display_scale) {
    ImGuiIO& io = ImGui::GetIO();
    const bool shortcuts_enabled = !io.WantTextInput && rebinding_action_ < 0;
    if (shortcuts_enabled &&
        ImGui::IsKeyPressed(static_cast<ImGuiKey>(key_bindings_[3]), false)) {
        settings_open_ = !settings_open_;
    }
    if (shortcuts_enabled &&
        ImGui::IsKeyPressed(static_cast<ImGuiKey>(key_bindings_[0]), false)) {
        toggle_ready();
    }
    if (shortcuts_enabled && !settings_open_ &&
        ImGui::IsKeyPressed(static_cast<ImGuiKey>(key_bindings_[1]), false)) {
        show_input_hud_ = !show_input_hud_;
    }
    if (shortcuts_enabled &&
        ImGui::IsKeyPressed(static_cast<ImGuiKey>(key_bindings_[2]), false)) {
        console_open_ = !console_open_;
    }

    const auto telemetry = backend_.telemetry();
    std::rotate(fps_history_.begin(), fps_history_.begin() + 1, fps_history_.end());
    std::rotate(latency_history_.begin(), latency_history_.begin() + 1, latency_history_.end());
    fps_history_.back() = telemetry.fps;
    latency_history_.back() = telemetry.latency_ms;

    if (settings_open_) {
        draw_settings(delta_seconds, display_scale);
    } else {
        draw_fpv(delta_seconds, display_scale);
    }
    draw_console(delta_seconds, display_scale);
}

void GroundStationUi::draw_fpv(float, float scale) {
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
    draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y},
                        IM_COL32(6, 8, 12, 255));

    // 图传纹理直接覆盖整块画布，使用 cover 裁切，不为导航或设置栏预留空间。
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
    }

    const ImVec2 center{origin.x + size.x * 0.5F, origin.y + size.y * 0.5F};
    const ImU32 grid = IM_COL32(35, 43, 54, 65);
    draw->AddLine({center.x - 24.0F * scale, center.y},
                  {center.x + 24.0F * scale, center.y}, grid, 1.0F);
    draw->AddLine({center.x, center.y - 24.0F * scale},
                  {center.x, center.y + 24.0F * scale}, grid, 1.0F);
    if (video.native_texture == nullptr) {
        const char* no_signal = "NO SIGNAL";
        const ImVec2 no_signal_size = ImGui::CalcTextSize(no_signal);
        draw->AddText({center.x - no_signal_size.x * 0.5F, center.y + 38.0F * scale},
                      IM_COL32(100, 107, 119, 150), no_signal);
    }

    const float margin = 18.0F * scale;
    const float top_h = 38.0F * scale;
    draw->AddRectFilled({origin.x + margin, origin.y + margin},
                        {origin.x + 245.0F * scale, origin.y + margin + top_h},
                        IM_COL32(11, 13, 19, 205), 6.0F * scale);
    draw->AddText({origin.x + margin + 12.0F * scale,
                   origin.y + margin + 9.0F * scale},
                  connected_ ? ImGui::GetColorU32(success) : ImGui::GetColorU32(warning),
                  connected_ ? "● LINK CONNECTED" : "● WAITING FOR LINK");

    char settings_label[64]{};
    std::snprintf(settings_label, sizeof(settings_label), "设置  [%s]",
                  ImGui::GetKeyName(static_cast<ImGuiKey>(key_bindings_[3])));
    ImGui::SetCursorScreenPos({origin.x + size.x - margin - 122.0F * scale,
                               origin.y + margin});
    if (ImGui::Button(settings_label, {122.0F * scale, top_h})) {
        settings_open_ = true;
    }

    if (show_input_hud_) {
        const ImVec2 p0{origin.x + margin, center.y - 118.0F * scale};
        const ImVec2 p1{p0.x + 220.0F * scale, p0.y + 236.0F * scale};
        draw->AddRectFilled(p0, p1, IM_COL32(10, 12, 18, 205), 7.0F * scale);
        draw->AddRect(p0, p1, IM_COL32(65, 72, 88, 160), 7.0F * scale);
        draw->AddText({p0.x + 14.0F * scale, p0.y + 12.0F * scale},
                      IM_COL32(130, 136, 151, 255), "INPUT");
        const ImVec2 stick0{p0.x + 14.0F * scale, p0.y + 48.0F * scale};
        const ImVec2 stick1{stick0.x + 106.0F * scale, stick0.y + 106.0F * scale};
        draw->AddRectFilled(stick0, stick1, IM_COL32(5, 7, 11, 230), 5.0F * scale);
        draw->AddRect(stick0, stick1, IM_COL32(70, 77, 94, 180), 5.0F * scale);
        const ImVec2 stick_center{(stick0.x + stick1.x) * 0.5F,
                                  (stick0.y + stick1.y) * 0.5F};
        draw->AddLine({stick0.x + 8.0F * scale, stick_center.y},
                      {stick1.x - 8.0F * scale, stick_center.y}, grid);
        draw->AddLine({stick_center.x, stick0.y + 8.0F * scale},
                      {stick_center.x, stick1.y - 8.0F * scale}, grid);
        draw->AddCircleFilled(stick_center, 5.0F * scale, ImGui::GetColorU32(accent));
        draw->AddText({p0.x + 145.0F * scale, p0.y + 62.0F * scale},
                      IM_COL32(145, 151, 165, 255), "MOUSE");
        draw->AddText({p0.x + 145.0F * scale, p0.y + 92.0F * scale},
                      IM_COL32(91, 99, 116, 255), "L   M   R");
        draw->AddLine({p0.x + 14.0F * scale, p0.y + 174.0F * scale},
                      {p1.x - 14.0F * scale, p0.y + 174.0F * scale},
                      IM_COL32(60, 66, 80, 160));
        draw->AddText({p0.x + 14.0F * scale, p0.y + 190.0F * scale},
                      IM_COL32(130, 136, 151, 255), "KEYBOARD");
        draw->AddText({p0.x + 106.0F * scale, p0.y + 190.0F * scale},
                      IM_COL32(80, 87, 101, 220), "---");
    }

    const auto telemetry = backend_.telemetry();
    if (show_status_hud_) {
        char fps[32]{};
        char latency[32]{};
        char loss[32]{};
        char bandwidth[32]{};
        std::snprintf(fps, sizeof(fps), "FPS             %.1f", telemetry.fps);
        std::snprintf(latency, sizeof(latency), "LATENCY     %.1f ms", telemetry.latency_ms);
        std::snprintf(loss, sizeof(loss), "LOSS           %.2f %%", telemetry.packet_loss_percent);
        std::snprintf(bandwidth, sizeof(bandwidth), "BITRATE      %.2f Mbps", telemetry.bandwidth_mbps);
        const ImVec2 p1{origin.x + size.x - margin, origin.y + size.y - margin};
        const ImVec2 p0{p1.x - 250.0F * scale, p1.y - 150.0F * scale};
        draw->AddRectFilled(p0, p1, IM_COL32(10, 12, 18, 215), 7.0F * scale);
        draw->AddRect(p0, p1, IM_COL32(65, 72, 88, 160), 7.0F * scale);
        draw->AddText({p0.x + 15.0F * scale, p0.y + 14.0F * scale},
                      ImGui::GetColorU32(accent), fps);
        draw->AddText({p0.x + 15.0F * scale, p0.y + 45.0F * scale},
                      ImGui::GetColorU32(text_primary), latency);
        draw->AddText({p0.x + 15.0F * scale, p0.y + 76.0F * scale},
                      ImGui::GetColorU32(text_primary), loss);
        draw->AddText({p0.x + 15.0F * scale, p0.y + 107.0F * scale},
                      ImGui::GetColorU32(text_primary), bandwidth);
    }

    char ready_text[64]{};
    std::snprintf(ready_text, sizeof(ready_text), ready_ ? "● READY" : "● NOT READY  [%s]",
                  ImGui::GetKeyName(static_cast<ImGuiKey>(key_bindings_[0])));
    draw->AddText({origin.x + margin, origin.y + size.y - margin - 22.0F * scale},
                  ImGui::GetColorU32(ready_ ? success : danger), ready_text);
    char hint[160]{};
    std::snprintf(hint, sizeof(hint), "%s 设置  ·  %s 输入 HUD  ·  %s READY",
                  ImGui::GetKeyName(static_cast<ImGuiKey>(key_bindings_[3])),
                  ImGui::GetKeyName(static_cast<ImGuiKey>(key_bindings_[1])),
                  ImGui::GetKeyName(static_cast<ImGuiKey>(key_bindings_[0])));
    const ImVec2 hint_size = ImGui::CalcTextSize(hint);
    draw->AddText({center.x - hint_size.x * 0.5F,
                   origin.y + size.y - margin - 22.0F * scale},
                  IM_COL32(120, 126, 140, 190), hint);

    ImGui::End();
    ImGui::PopStyleVar();
}

void GroundStationUi::draw_settings(float delta_seconds, float scale) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {24.0F * scale, 18.0F * scale});
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("PIP-Link##Settings", nullptr, flags);

    ImGui::SetWindowFontScale(1.35F);
    ImGui::TextColored(accent, "PIP-Link");
    ImGui::SetWindowFontScale(1.0F);
    ImGui::SameLine();
    ImGui::TextColored(text_secondary, "地面站设置");
    const float back_width = 138.0F * scale;
    ImGui::SameLine(ImGui::GetWindowWidth() - back_width - 24.0F * scale);
    if (ImGui::Button("返回图传  [Esc]", {back_width, 36.0F * scale})) {
        settings_open_ = false;
    }
    ImGui::Spacing();
    draw_settings_tabs(delta_seconds, scale);
    ImGui::Spacing();

    ImGui::BeginChild("##SettingsContent", {0.0F, -38.0F * scale},
                      ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollWithMouse |
                                                   ImGuiWindowFlags_NoScrollbar);
    ImGui::SetWindowFontScale(1.48F);
    ImGui::Text("%s", tabs[active_settings_tab_].title);
    ImGui::SetWindowFontScale(1.0F);
    ImGui::TextColored(text_secondary, "%s", tabs[active_settings_tab_].description);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    switch (active_settings_tab_) {
        case 0: draw_connection_page(scale); break;
        case 1: draw_parameters_page(scale); break;
        case 2: draw_video_page(scale); break;
        case 3: draw_recording_page(scale); break;
        case 4: draw_diagnostics_page(scale); break;
        case 5: draw_control_page(scale); break;
        case 6: draw_debug_page(scale); break;
        case 7: draw_audit_page(scale); break;
        case 8: draw_about_page(scale); break;
        default: break;
    }
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
        ImGui::GetIO().MouseWheel != 0.0F) {
        settings_scroll_target_ -= ImGui::GetIO().MouseWheel * 100.0F * scale;
    }
    settings_scroll_target_ = std::clamp(settings_scroll_target_, 0.0F,
                                         ImGui::GetScrollMaxY());
    const float scroll_diff = settings_scroll_target_ - ImGui::GetScrollY();
    if (std::abs(scroll_diff) > 0.5F) {
        ImGui::SetScrollY(ImGui::GetScrollY() +
                          scroll_diff * (1.0F - std::exp(-delta_seconds / 0.08F)));
    } else {
        ImGui::SetScrollY(settings_scroll_target_);
    }
    ImGui::EndChild();
    ImGui::TextColored(text_secondary, "%s", feedback_.c_str());
    ImGui::End();
    ImGui::PopStyleVar();
}

void GroundStationUi::draw_console(float delta_seconds, float scale) {
    const float target_height = console_open_ ? 290.0F * scale : 0.0F;
    const float t = 1.0F - std::exp(-delta_seconds / 0.08F);
    console_height_ += (target_height - console_height_) * t;
    if (std::abs(console_height_ - target_height) < 0.5F) {
        console_height_ = target_height;
    }
    if (console_height_ < 1.0F) {
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize({viewport->WorkSize.x, console_height_});
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha,
                        std::clamp(console_height_ / (290.0F * scale), 0.0F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, {0.025F, 0.028F, 0.040F, 0.97F});
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("##DeveloperConsole", nullptr, flags);
    ImGui::TextColored(accent, "开发者控制台");
    ImGui::SameLine();
    ImGui::TextColored(text_secondary, "后端命令接口");
    ImGui::SameLine(ImGui::GetWindowWidth() - 150.0F * scale);
    if (ImGui::SmallButton("清空")) {
        console_lines_.clear();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("关闭")) {
        console_open_ = false;
    }
    ImGui::Separator();
    ImGui::BeginChild("##ConsoleOutput", {0.0F, -38.0F * scale}, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    for (const auto& line : console_lines_) {
        ImGui::TextUnformatted(line.c_str());
    }
    if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0F) {
        console_scroll_target_ -= ImGui::GetIO().MouseWheel * 100.0F * scale;
    }
    console_scroll_target_ = std::clamp(console_scroll_target_, 0.0F,
                                        ImGui::GetScrollMaxY());
    const float console_diff = console_scroll_target_ - ImGui::GetScrollY();
    ImGui::SetScrollY(std::abs(console_diff) > 0.5F
                          ? ImGui::GetScrollY() +
                                console_diff * (1.0F - std::exp(-delta_seconds / 0.08F))
                          : console_scroll_target_);
    ImGui::EndChild();
    ImGui::SetNextItemWidth(-1.0F);
    if (ImGui::InputText("##ConsoleCommand", console_command_.data(), console_command_.size(),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        const std::string command{console_command_.data()};
        if (!command.empty()) {
            console_lines_.push_back("> " + command);
            if (command == "clear") {
                console_lines_.clear();
            } else if (command == "close") {
                console_open_ = false;
            } else {
                const std::string result = backend_.execute_console_command(command);
                console_lines_.push_back(result.empty() ? "后端尚未返回结果" : result);
            }
            console_command_.fill('\0');
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void GroundStationUi::draw_settings_tabs(float delta_seconds, float scale) {
    ImGui::BeginChild("##SettingsTabs", {0.0F, 46.0F * scale}, ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollbar);
    for (int index = 0; index < static_cast<int>(tabs.size()); ++index) {
        if (index > 0) {
            ImGui::SameLine(0.0F, 7.0F * scale);
        }
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 text_size = ImGui::CalcTextSize(tabs[index].label);
        const float width = text_size.x + 34.0F * scale;
        const float height = 40.0F * scale;
        ImGui::PushID(index);
        const bool clicked = ImGui::InvisibleButton("##Tab", {width, height});
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        if (clicked) {
            active_settings_tab_ = index;
            settings_scroll_target_ = 0.0F;
        }
        const float target = hovered ? 1.0F : 0.0F;
        const float t = 1.0F - std::exp(-delta_seconds / 0.06F);
        tab_hover_[index] += (target - tab_hover_[index]) * t;
        const bool active = active_settings_tab_ == index;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        if (tab_hover_[index] > 0.01F) {
            draw->AddRectFilled(origin, {origin.x + width, origin.y + height},
                                ImGui::GetColorU32({0.08F, 0.22F, 0.27F,
                                                   tab_hover_[index] * 0.65F}),
                                4.0F * scale);
        }
        draw->AddText({origin.x + (width - text_size.x) * 0.5F,
                       origin.y + (height - text_size.y) * 0.5F},
                      ImGui::GetColorU32(active ? accent : text_secondary),
                      tabs[index].label);
        if (active) {
            draw->AddRectFilled({origin.x + 7.0F * scale, origin.y + height - 2.0F * scale},
                                {origin.x + width - 7.0F * scale, origin.y + height},
                                ImGui::GetColorU32(accent), 1.0F * scale);
        }
    }
    if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0F) {
        tab_scroll_target_ -= ImGui::GetIO().MouseWheel * 100.0F * scale;
    }
    tab_scroll_target_ = std::clamp(tab_scroll_target_, 0.0F, ImGui::GetScrollMaxX());
    const float tab_diff = tab_scroll_target_ - ImGui::GetScrollX();
    ImGui::SetScrollX(std::abs(tab_diff) > 0.5F
                          ? ImGui::GetScrollX() +
                                tab_diff * (1.0F - std::exp(-delta_seconds / 0.08F))
                          : tab_scroll_target_);
    ImGui::EndChild();
}

void GroundStationUi::draw_connection_page(float scale) {
    const auto devices = backend_.discovered_devices();
    if (!devices.empty()) {
        scanning_ = false;
    }
    if (ImGui::BeginTable("ConnectionColumns", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        section_title("会话状态");
        status_chip(connected_ ? "已连接" : (scanning_ ? "正在扫描" : "未连接"),
                    connected_ ? success : (scanning_ ? warning : text_secondary));
        ImGui::TextColored(text_secondary, "服务名称");
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::InputText("##Service", service_name_.data(), service_name_.size());
        if (action_button("扫描设备", 160.0F * scale)) {
            backend_.scan_devices(service_name_.data());
            scanning_ = true;
            set_feedback("设备扫描请求已发送");
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!connected_);
        if (action_button("断开连接", 160.0F * scale)) {
            backend_.disconnect_device();
            connected_ = false;
            set_feedback("断开请求已发送");
        }
        ImGui::EndDisabled();
        ImGui::Spacing();
        section_title("手动连接");
        ImGui::TextColored(text_secondary, "地址与端口");
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::InputText("##ManualAddress", manual_address_.data(), manual_address_.size());
        if (action_button("连接到地址", 160.0F * scale)) {
            backend_.connect_device({"手动设备", manual_address_.data(), 0});
            connected_ = true;
            scanning_ = false;
            set_feedback("手动连接请求已发送");
        }
        ImGui::SameLine();
        if (action_button("退出地面站", 150.0F * scale)) {
            quit_requested_ = true;
        }

        ImGui::TableNextColumn();
        section_title("发现的设备");
        if (devices.empty()) {
            draw_empty_state("暂无设备", "点击“扫描设备”后，后端发现结果会显示在这里。");
        } else {
            for (int index = 0; index < static_cast<int>(devices.size()); ++index) {
                const auto& device = devices[index];
                ImGui::PushID(index);
                const bool selected = selected_device_ == index;
                if (ImGui::Selectable(device.name.c_str(), selected, 0, {0.0F, 34.0F * scale})) {
                    selected_device_ = index;
                }
                ImGui::SameLine();
                ImGui::TextColored(text_secondary, "%s  ·  %d%%",
                                   device.address.c_str(), device.signal_percent);
                ImGui::PopID();
            }
            ImGui::BeginDisabled(selected_device_ < 0);
            if (action_button("连接所选设备", 180.0F * scale)) {
                backend_.connect_device(devices[static_cast<std::size_t>(selected_device_)]);
                connected_ = true;
                scanning_ = false;
                set_feedback("设备连接请求已发送");
            }
            ImGui::EndDisabled();
        }
        ImGui::EndTable();
    }
}

void GroundStationUi::draw_parameters_page(float scale) {
    if (ImGui::BeginTable("ParameterColumns", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        section_title("输入参数", "保留旧版地面端的第一视角操作参数");
        ImGui::SetNextItemWidth(360.0F * scale);
        ImGui::SliderFloat("鼠标灵敏度", &mouse_sensitivity_, 0.1F, 5.0F, "%.2f ×");
        ImGui::SetNextItemWidth(360.0F * scale);
        ImGui::SliderFloat("视野 (FOV)", &field_of_view_, 30.0F, 120.0F, "%.0f°");
        ImGui::Checkbox("反转俯仰", &invert_pitch_);
        if (action_button("应用输入参数", 180.0F * scale)) {
            backend_.apply_input_settings(mouse_sensitivity_, field_of_view_, invert_pitch_);
            set_feedback("输入参数已提交");
        }
        ImGui::Spacing();
        ImGui::TextColored(text_secondary, "当前值");
        ImGui::Text("灵敏度 %.2f ×  ·  FOV %.0f°  ·  反转 %s",
                    mouse_sensitivity_, field_of_view_, invert_pitch_ ? "开启" : "关闭");

        ImGui::TableNextColumn();
        section_title("链路保活", "这些参数在下一次应用时统一提交给后端");
        ImGui::Checkbox("自动重连", &auto_reconnect_);
        ImGui::SetNextItemWidth(360.0F * scale);
        ImGui::SliderInt("心跳间隔 (ms)", &heartbeat_ms_, 250, 5000);
        ImGui::SetNextItemWidth(360.0F * scale);
        ImGui::SliderInt("重连间隔 (s)", &reconnect_seconds_, 1, 30);
        section_title("传输参数");
        ImGui::SetNextItemWidth(360.0F * scale);
        ImGui::SliderInt("UDP MTU", &mtu_, 576, 1500);
        ImGui::TextColored(text_secondary, "推荐在普通以太网链路使用 1400，避免 IP 分片。");
        ImGui::Spacing();
        if (action_button("应用链路参数", 180.0F * scale)) {
            backend_.apply_connection_settings(heartbeat_ms_, reconnect_seconds_, mtu_,
                                               auto_reconnect_);
            set_feedback("链路参数已提交");
        }
        ImGui::EndTable();
    }
}

void GroundStationUi::draw_video_page(float scale) {
    constexpr const char* resolutions[] = {
        "960 × 540", "1280 × 720", "1600 × 900", "1920 × 1080",
        "2560 × 1440", "3840 × 2160"};
    constexpr const char* decoders[] = {"自动选择", "D3D11VA", "软件解码"};
    constexpr const char* qualities[] = {"低", "中", "高", "超高"};
    constexpr const char* window_modes[] = {"窗口", "全屏"};
    constexpr const char* encoders[] = {"JPEG", "H.264"};
    if (ImGui::BeginTable("VideoColumns", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        section_title("视频与窗口");
        ImGui::SetNextItemWidth(320.0F * scale);
        ImGui::Combo("画质", &quality_index_, qualities,
                     static_cast<int>(std::size(qualities)));
        ImGui::SetNextItemWidth(320.0F * scale);
        ImGui::Combo("分辨率", &resolution_index_, resolutions,
                     static_cast<int>(std::size(resolutions)));
        ImGui::SetNextItemWidth(320.0F * scale);
        ImGui::Combo("窗口模式", &window_mode_, window_modes,
                     static_cast<int>(std::size(window_modes)));
        ImGui::Spacing();
        section_title("码流");
        ImGui::SetNextItemWidth(320.0F * scale);
        ImGui::Combo("编码器", &encoder_index_, encoders,
                     static_cast<int>(std::size(encoders)));
        ImGui::SetNextItemWidth(320.0F * scale);
        ImGui::SliderInt("帧率", &frame_rate_, 24, 240, "%d FPS");
        ImGui::SetNextItemWidth(320.0F * scale);
        ImGui::SliderInt("目标码率", &bitrate_kbps_, 1000, 80000, "%d kbps");
        ImGui::Checkbox("低延迟模式", &low_latency_);
        ImGui::Checkbox("前向纠错 (FEC)", &fec_enabled_);
        if (fec_enabled_) {
            ImGui::SetNextItemWidth(320.0F * scale);
            ImGui::SliderFloat("FEC 冗余", &fec_redundancy_, 0.05F, 0.50F, "%.2f");
        }
        ImGui::TableNextColumn();
        section_title("解码与显示");
        ImGui::SetNextItemWidth(320.0F * scale);
        ImGui::Combo("解码器", &decoder_index_, decoders,
                     static_cast<int>(std::size(decoders)));
        ImGui::Checkbox("垂直同步", &vertical_sync_);
        ImGui::Spacing();
        section_title("图像增强");
        ImGui::SetNextItemWidth(320.0F * scale);
        ImGui::SliderInt("亮度", &brightness_, -100, 100);
        ImGui::SetNextItemWidth(320.0F * scale);
        ImGui::SliderInt("对比度", &contrast_, -100, 100);
        ImGui::SetNextItemWidth(320.0F * scale);
        ImGui::SliderInt("锐度", &sharpness_, 0, 100);
        ImGui::SetNextItemWidth(320.0F * scale);
        ImGui::SliderInt("降噪", &denoise_, 0, 100);
        ImGui::TextWrapped("图传纹理始终直接绘制到完整第一视角画布。设置页关闭后，"
                           "不会保留侧栏、标题栏或内容区占用画面空间。");
        ImGui::Spacing();
        if (action_button("应用视频设置", 180.0F * scale)) {
            backend_.apply_video_settings(quality_index_, resolution_index_, window_mode_,
                                          encoder_index_, decoder_index_, frame_rate_,
                                          bitrate_kbps_, fec_enabled_, fec_redundancy_,
                                          brightness_, contrast_, sharpness_, denoise_,
                                          low_latency_, vertical_sync_);
            set_feedback("视频设置已提交");
        }
        ImGui::EndTable();
    }
}

void GroundStationUi::draw_recording_page(float scale) {
    constexpr const char* formats[] = {"MP4 (H.264)", "MKV (H.264)", "原始码流"};
    section_title("保存位置");
    ImGui::SetNextItemWidth(std::min(680.0F * scale, ImGui::GetContentRegionAvail().x));
    ImGui::InputText("##RecordingDirectory", recording_directory_.data(),
                     recording_directory_.size());
    ImGui::Spacing();
    if (ImGui::BeginTable("RecordingColumns", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        section_title("封装与质量");
        ImGui::SetNextItemWidth(320.0F * scale);
        ImGui::Combo("格式", &recording_format_, formats,
                     static_cast<int>(std::size(formats)));
        ImGui::SetNextItemWidth(320.0F * scale);
        ImGui::SliderInt("质量", &recording_quality_, 1, 100);
        ImGui::SetNextItemWidth(320.0F * scale);
        ImGui::SliderInt("自动分段", &split_minutes_, 0, 120, "%d 分钟");
        ImGui::TableNextColumn();
        section_title("录制控制");
        status_chip(recording_ ? "正在录制" : "未录制", recording_ ? danger : text_secondary);
        if (!recording_) {
            if (action_button("开始录制", 170.0F * scale)) {
                backend_.start_recording(recording_directory_.data(), recording_format_,
                                         recording_quality_, split_minutes_);
                recording_ = true;
                recording_started_at_ = ImGui::GetTime();
                set_feedback("录像启动请求已发送");
            }
        } else if (action_button("停止并保存", 170.0F * scale)) {
            backend_.stop_recording();
            recording_ = false;
            recording_started_at_ = 0.0;
            set_feedback("录像停止请求已发送");
        }
        if (recording_) {
            const int elapsed = static_cast<int>(ImGui::GetTime() - recording_started_at_);
            ImGui::TextColored(danger, "REC  %02d:%02d:%02d",
                               elapsed / 3600, (elapsed / 60) % 60, elapsed % 60);
        }
        ImGui::SameLine();
        if (action_button("保存截图", 150.0F * scale)) {
            backend_.take_screenshot(recording_directory_.data());
            set_feedback("截图请求已发送");
        }
        ImGui::SameLine();
        if (action_button("打开目录", 150.0F * scale)) {
            backend_.open_recordings_folder(recording_directory_.data());
            set_feedback("打开录像目录请求已发送");
        }
        ImGui::EndTable();
    }
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
    const float width = (ImGui::GetContentRegionAvail().x - gap * 3.0F) / 4.0F;
    metric_card("DiagFps", "渲染帧率", fps, {width, 92.0F * scale});
    ImGui::SameLine(0.0F, gap);
    metric_card("DiagLatency", "往返延迟", latency, {width, 92.0F * scale});
    ImGui::SameLine(0.0F, gap);
    metric_card("DiagLoss", "丢包率", loss, {width, 92.0F * scale});
    ImGui::SameLine(0.0F, gap);
    metric_card("DiagBandwidth", "接收码率", bandwidth, {width, 92.0F * scale});
    ImGui::Spacing();
    if (ImGui::BeginTable("DiagnosticPlots", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        section_title("帧率时间线");
        ImGui::PlotLines("##FpsHistory", fps_history_.data(),
                         static_cast<int>(fps_history_.size()), 0, nullptr,
                         0.0F, 240.0F, {-1.0F, 150.0F * scale});
        ImGui::TableNextColumn();
        section_title("延迟时间线");
        ImGui::PlotLines("##LatencyHistory", latency_history_.data(),
                         static_cast<int>(latency_history_.size()), 0, nullptr,
                         0.0F, 300.0F, {-1.0F, 150.0F * scale});
        ImGui::EndTable();
    }
    if (action_button("导出诊断报告", 180.0F * scale)) {
        backend_.export_diagnostics();
        set_feedback("诊断导出请求已发送");
    }
}

void GroundStationUi::draw_control_page(float scale) {
    constexpr std::array<const char*, 4> actions{
        "切换 READY", "显示输入 HUD", "打开控制台", "打开设置"};
    if (ImGui::BeginTable("ControlColumns", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        section_title("输入采集");
        ImGui::SetNextItemWidth(340.0F * scale);
        ImGui::SliderFloat("鼠标灵敏度", &mouse_sensitivity_, 0.1F, 4.0F, "%.2f");
        ImGui::Checkbox("反转纵轴", &invert_y_);
        ImGui::Checkbox("捕获鼠标", &capture_mouse_);
        ImGui::Checkbox("发送键盘输入", &send_keyboard_);
        if (action_button("应用控制设置", 180.0F * scale)) {
            backend_.apply_control_settings(mouse_sensitivity_, invert_y_, capture_mouse_,
                                            send_keyboard_);
            set_feedback("控制设置已提交");
        }
        ImGui::Spacing();
        section_title("安全状态");
        status_chip(ready_ ? "READY" : "NOT READY", ready_ ? success : danger);
        if (action_button(ready_ ? "退出 READY" : "进入 READY", 180.0F * scale)) {
            toggle_ready();
        }

        ImGui::TableNextColumn();
        section_title("快捷键", "点击按键后，按下新的键完成绑定");
        for (int index = 0; index < static_cast<int>(actions.size()); ++index) {
            ImGui::TextColored(text_secondary, "%s", actions[index]);
            ImGui::SameLine(220.0F * scale);
            ImGui::PushID(index);
            const char* key_name = rebinding_action_ == index
                                       ? "按下任意键..."
                                       : ImGui::GetKeyName(static_cast<ImGuiKey>(key_bindings_[index]));
            if (ImGui::Button(key_name, {150.0F * scale, 30.0F * scale})) {
                rebinding_action_ = index;
            }
            ImGui::PopID();
        }
        if (rebinding_action_ >= 0) {
            for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key) {
                if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(key), false) &&
                    key != ImGuiKey_Escape) {
                    key_bindings_[rebinding_action_] = key;
                    rebinding_action_ = -1;
                    set_feedback("快捷键绑定已更新");
                    break;
                }
            }
        }
        if (action_button("恢复默认按键", 170.0F * scale)) {
            key_bindings_ = {ImGuiKey_F5, ImGuiKey_Tab, ImGuiKey_GraveAccent,
                             ImGuiKey_Escape};
            rebinding_action_ = -1;
            set_feedback("快捷键已恢复默认值");
        }
        ImGui::SameLine();
        if (action_button("保存按键方案", 170.0F * scale)) {
            backend_.save_key_bindings(
                std::vector<int>{key_bindings_.begin(), key_bindings_.end()});
            set_feedback("快捷键方案已提交");
        }
        ImGui::Spacing();
        section_title("手柄");
        status_chip("未检测到手柄", text_secondary);
        ImGui::TextColored(text_secondary, "左摇杆：移动  ·  右摇杆：视角");
        ImGui::TextColored(text_secondary, "A：跳跃  B：蹲伏  X/Y：动作  LT/RT：制动/加速");
        ImGui::SetNextItemWidth(320.0F * scale);
        ImGui::SliderFloat("摇杆死区", &gamepad_deadzone_, 0.0F, 0.5F, "%.2f");
        ImGui::Checkbox("启用振动反馈", &gamepad_vibration_);
        if (action_button("应用手柄设置", 180.0F * scale)) {
            backend_.apply_gamepad_settings(gamepad_deadzone_, gamepad_vibration_);
            set_feedback("手柄设置已提交");
        }
        ImGui::EndTable();
    }
}

void GroundStationUi::draw_debug_page(float scale) {
    if (ImGui::BeginTable("DebugColumns", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        section_title("覆盖层");
        ImGui::Checkbox("第一视角显示输入 HUD", &show_input_hud_);
        ImGui::Checkbox("第一视角显示性能 HUD", &show_status_hud_);
        ImGui::Checkbox("详细日志", &verbose_log_);
        ImGui::Checkbox("前端模拟模式", &simulation_mode_);
        ImGui::TableNextColumn();
        section_title("开发工具");
        ImGui::TextColored(text_secondary, "这些操作只影响前端本地状态。");
        if (action_button("清空性能曲线", 180.0F * scale)) {
            fps_history_.fill(0.0F);
            latency_history_.fill(0.0F);
            set_feedback("性能曲线已清空");
        }
        ImGui::SameLine();
        if (action_button("恢复界面默认值", 190.0F * scale)) {
            show_input_hud_ = true;
            show_status_hud_ = true;
            verbose_log_ = false;
            simulation_mode_ = false;
            set_feedback("界面选项已恢复默认值");
        }
        ImGui::Spacing();
        ImGui::TextWrapped("后端 Stub 当前不会生成模拟遥测。接入真实接口后，本页可直接用于"
                           "观察原始状态和控制前端调试覆盖层。");
        ImGui::EndTable();
    }
}

void GroundStationUi::draw_audit_page(float scale) {
    ImGui::SetNextItemWidth(std::min(460.0F * scale, ImGui::GetContentRegionAvail().x));
    ImGui::InputTextWithHint("##AuditFilter", "过滤时间、级别或消息",
                             audit_filter_.data(), audit_filter_.size());
    ImGui::SameLine();
    if (action_button("导出日志", 130.0F * scale)) {
        backend_.export_audit_log();
        set_feedback("审计日志导出请求已发送");
    }
    ImGui::SameLine();
    if (action_button("清空日志", 130.0F * scale)) {
        backend_.clear_audit_log();
        set_feedback("审计日志清空请求已发送");
    }
    ImGui::Spacing();
    const auto entries = backend_.audit_entries();
    if (ImGui::BeginTable("AuditTable", 3,
                          ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
                          {0.0F, 330.0F * scale})) {
        ImGui::TableSetupColumn("时间", ImGuiTableColumnFlags_WidthFixed, 130.0F * scale);
        ImGui::TableSetupColumn("级别", ImGuiTableColumnFlags_WidthFixed, 90.0F * scale);
        ImGui::TableSetupColumn("消息");
        ImGui::TableHeadersRow();
        const std::string_view filter{audit_filter_.data()};
        for (const auto& entry : entries) {
            if (!filter.empty() && entry.time.find(filter) == std::string::npos &&
                entry.level.find(filter) == std::string::npos &&
                entry.message.find(filter) == std::string::npos) {
                continue;
            }
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(entry.time.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(entry.level.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(entry.message.c_str());
        }
        if (entries.empty()) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(text_secondary, "暂无审计记录");
        }
        ImGui::EndTable();
    }
}

void GroundStationUi::draw_about_page(float scale) {
    ImGui::Dummy({0.0F, 2.0F * scale});
    if (ImGui::BeginTable("AboutColumns", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        section_title("PIP-Link C++ Ground Station");
        ImGui::SetWindowFontScale(1.65F);
        ImGui::TextColored(accent, "PIP-Link");
        ImGui::SetWindowFontScale(1.0F);
        ImGui::Text("版本 0.1.0");
        ImGui::TextColored(text_secondary, "C++20 · SDL3 · Dear ImGui · Direct3D 11");
        ImGui::Spacing();
        ImGui::TextWrapped("Windows 原生地面端重构。第一视角图传是主工作区，设置仅在需要时"
                           "整页显示，不会长期占用或压缩视频画面。");
        ImGui::TableNextColumn();
        section_title("快捷操作");
        ImGui::BulletText("Esc：图传 / 设置整页切换");
        ImGui::BulletText("F5：切换 READY 安全状态");
        ImGui::BulletText("Tab：显示 / 隐藏输入 HUD");
        ImGui::BulletText("所有业务按钮均通过 GroundStationBackend 接口调用");
        ImGui::Spacing();
        ImGui::TextColored(text_secondary, "后端实现文件中保留了明确的 TODO 接入点。");
        ImGui::EndTable();
    }
}

}  // namespace pip_link::ui
