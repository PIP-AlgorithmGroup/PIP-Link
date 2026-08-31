#include "pip_link/ui/ground_station_ui.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace pip_link::ui {
namespace {

constexpr ImVec4 accent{0.00F, 0.85F, 1.00F, 1.00F};
constexpr ImVec4 secondary{0.48F, 0.53F, 0.62F, 1.00F};

struct Page final {
    const char* label;
    const char* description;
};

constexpr std::array<Page, 8> pages{{
    {"总览", "地面站运行状态与快捷操作"},
    {"连接", "设备发现、会话与自动重连"},
    {"视频", "解码器、码率、帧率与画面设置"},
    {"控制", "键盘、鼠标与安全状态"},
    {"录制", "录像、截图与存储管理"},
    {"诊断", "延迟、丢包与性能时间线"},
    {"日志", "运行日志、审计与导出"},
    {"设置", "界面、语言、显示器与高级选项"},
}};

void draw_metric_card(const char* id, const char* label, const char* value,
                      const char* hint, const ImVec2 size) {
    ImGui::BeginChild(id, size, ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::TextColored(secondary, "%s", label);
    ImGui::Spacing();
    ImGui::SetWindowFontScale(1.45F);
    ImGui::TextColored(accent, "%s", value);
    ImGui::SetWindowFontScale(1.0F);
    ImGui::TextColored(secondary, "%s", hint);
    ImGui::EndChild();
}

}  // namespace

void GroundStationUi::draw(float delta_seconds, float display_scale) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    constexpr ImGuiWindowFlags window_flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("PIP-Link##Root", nullptr, window_flags);

    const float sidebar_width = 236.0F * display_scale;
    ImGui::BeginChild("##Navigation", {sidebar_width, 0.0F}, ImGuiChildFlags_Borders);
    ImGui::SetWindowFontScale(1.30F);
    ImGui::TextColored(accent, "PIP-Link");
    ImGui::SetWindowFontScale(1.0F);
    ImGui::TextColored(secondary, "专业低延迟地面站");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    for (int index = 0; index < static_cast<int>(pages.size()); ++index) {
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        const float height = 42.0F * display_scale;
        ImGui::PushID(index);
        const bool clicked = ImGui::InvisibleButton("##Page", {width, height});
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();

        const float target = hovered ? 1.0F : 0.0F;
        const float interpolation = 1.0F - std::exp(-delta_seconds / 0.06F);
        navigation_hover_[index] += (target - navigation_hover_[index]) * interpolation;
        if (clicked) {
            active_page_ = index;
        }

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        const bool active = active_page_ == index;
        const float alpha = std::max(navigation_hover_[index] * 0.42F, active ? 0.55F : 0.0F);
        if (alpha > 0.01F) {
            draw_list->AddRectFilled(origin, {origin.x + width, origin.y + height},
                                     ImGui::GetColorU32({0.00F, 0.50F, 0.62F, alpha}),
                                     5.0F * display_scale);
        }
        if (active) {
            draw_list->AddRectFilled(ImVec2{origin.x, origin.y + 7.0F * display_scale},
                                     ImVec2{origin.x + 3.0F * display_scale,
                                            origin.y + height - 7.0F * display_scale},
                                     ImGui::GetColorU32(accent), 2.0F * display_scale);
        }
        const ImVec4 text_color = active ? accent : ImVec4{0.78F, 0.82F, 0.89F, 1.0F};
        draw_list->AddText(ImVec2{origin.x + 18.0F * display_scale,
                                  origin.y + (height - ImGui::GetTextLineHeight()) * 0.5F},
                           ImGui::GetColorU32(text_color), pages[index].label);
    }

    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 58.0F * display_scale);
    ImGui::Separator();
    ImGui::TextColored({0.28F, 0.85F, 0.48F, 1.0F}, "● 系统就绪");
    ImGui::TextColored(secondary, "C++20 · D3D11 · SDL3");
    ImGui::EndChild();

    ImGui::SameLine(0.0F, 18.0F * display_scale);
    ImGui::BeginChild("##Content", {0.0F, 0.0F}, ImGuiChildFlags_None);
    ImGui::SetWindowFontScale(1.55F);
    ImGui::Text("%s", pages[active_page_].label);
    ImGui::SetWindowFontScale(1.0F);
    ImGui::TextColored(secondary, "%s", pages[active_page_].description);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (active_page_ == 0) {
        const float gap = 12.0F * display_scale;
        const float card_width = (ImGui::GetContentRegionAvail().x - gap * 2.0F) / 3.0F;
        const ImVec2 card_size{card_width, 126.0F * display_scale};
        draw_metric_card("ConnectionCard", "连接状态", "未连接", "等待选择机载设备", card_size);
        ImGui::SameLine(0.0F, gap);
        draw_metric_card("LatencyCard", "往返延迟", "-- ms", "最近 1 秒平均值", card_size);
        ImGui::SameLine(0.0F, gap);
        draw_metric_card("VideoCard", "视频链路", "0 FPS", "等待视频数据", card_size);

        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::BeginChild("QuickStart", ImVec2{0.0F, 230.0F * display_scale},
                          ImGuiChildFlags_Borders);
        ImGui::TextColored(accent, "快速开始");
        ImGui::Spacing();
        ImGui::TextWrapped("首版 C++ 图形框架已经启动。下一阶段将接入设备发现、UDP 会话、"
                           "视频解码与安全控制状态。所有页面使用 UTF-8 中文和帧率无关动画。");
        ImGui::Spacing();
        if (ImGui::Button("扫描设备", ImVec2{150.0F * display_scale, 38.0F * display_scale})) {
            active_page_ = 1;
        }
        ImGui::SameLine();
        ImGui::BeginDisabled();
        ImGui::Button("进入 READY", ImVec2{150.0F * display_scale, 38.0F * display_scale});
        ImGui::EndDisabled();
        ImGui::EndChild();
    } else {
        ImGui::BeginChild("PagePlaceholder", ImVec2{0.0F, 220.0F * display_scale},
                          ImGuiChildFlags_Borders);
        ImGui::TextColored(accent, "%s模块", pages[active_page_].label);
        ImGui::Spacing();
        ImGui::TextWrapped("该页面的视觉框架已经建立，业务功能将在后续阶段接入。"
                           "原地面端核心能力会逐项迁移并增加对应测试。");
        ImGui::EndChild();
    }

    ImGui::EndChild();
    ImGui::End();
}

}  // namespace pip_link::ui
