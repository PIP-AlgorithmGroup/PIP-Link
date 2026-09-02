#include "pip_link/backend/ground_station_backend.hpp"
#include "pip_link/ui/ground_station_ui.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <iostream>
#include <string>

namespace {

constexpr float frame_seconds = 1.0F / 60.0F;

void draw_frame(pip_link::ui::GroundStationUi& ui) {
    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = frame_seconds;
    ImGui::NewFrame();
    ui.draw(frame_seconds, 1.0F);
    ImGui::Render();
}

ImGuiWindow* find_child_window(ImGuiWindow* parent, ImGuiID child_id) {
    for (ImGuiWindow* window : GImGui->Windows) {
        if (window->ParentWindow == parent && window->ChildId == child_id) return window;
    }
    return nullptr;
}

std::string active_input_text(ImGuiID input_id) {
    const ImGuiInputTextState* state = ImGui::GetInputTextState(input_id);
    if (state == nullptr || state->TextA.Data == nullptr) return {};
    return {state->TextA.Data, static_cast<std::size_t>(state->TextLen)};
}

void press_key(pip_link::ui::GroundStationUi& ui, ImGuiKey key) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(key, true);
    draw_frame(ui);
    io.AddKeyEvent(key, false);
    draw_frame(ui);
}

}  // namespace

int main() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = {1440.0F, 900.0F};
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    pip_link::backend::GroundStationBackendStub backend;
    pip_link::ui::GroundStationUi ui{backend};

    io.AddKeyEvent(ImGuiKey_GraveAccent, true);
    draw_frame(ui);
    io.AddKeyEvent(ImGuiKey_GraveAccent, false);
    for (int frame = 0; frame < 45; ++frame) draw_frame(ui);

    ImGuiWindow* console_window = ImGui::FindWindowByName("##DeveloperConsole");
    if (console_window == nullptr) {
        std::cerr << "Console command input was not drawn.\n";
        ImGui::DestroyContext();
        return 1;
    }
    const ImGuiID command_id = console_window->GetID("##ConsoleCommand");
    constexpr ImGuiWindowFlags fixed_console_flags =
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if ((console_window->Flags & fixed_console_flags) != fixed_console_flags) {
        std::cerr << "Console fixed title and command bar can enter the parent scroll range.\n";
        ImGui::DestroyContext();
        return 1;
    }

    ImGui::ActivateItemByID(command_id);
    draw_frame(ui);
    if (ImGui::GetActiveID() != command_id) {
        std::cerr << "Console command input could not receive focus.\n";
        ImGui::DestroyContext();
        return 1;
    }

    io.AddInputCharactersUTF8("help");
    draw_frame(ui);
    io.AddKeyEvent(ImGuiKey_Enter, true);
    draw_frame(ui);
    io.AddKeyEvent(ImGuiKey_Enter, false);
    draw_frame(ui);

    if (ImGui::GetActiveID() != command_id) {
        std::cerr << "Console command input lost focus after submission.\n";
        ImGui::DestroyContext();
        return 1;
    }

    io.AddKeyEvent(ImGuiKey_Enter, true);
    draw_frame(ui);
    io.AddKeyEvent(ImGuiKey_Enter, false);
    draw_frame(ui);
    if (ImGui::GetActiveID() != command_id) {
        std::cerr << "Console command input lost focus after an empty submission.\n";
        ImGui::DestroyContext();
        return 1;
    }

    io.AddInputCharactersUTF8("status");
    draw_frame(ui);
    press_key(ui, ImGuiKey_Enter);
    io.AddInputCharactersUTF8("draft");
    draw_frame(ui);

    press_key(ui, ImGuiKey_UpArrow);
    if (active_input_text(command_id) != "status") {
        std::cerr << "Up arrow did not select the newest console command.\n";
        ImGui::DestroyContext();
        return 1;
    }
    press_key(ui, ImGuiKey_UpArrow);
    if (active_input_text(command_id) != "help") {
        std::cerr << "Repeated up arrow did not select the older console command.\n";
        ImGui::DestroyContext();
        return 1;
    }
    press_key(ui, ImGuiKey_DownArrow);
    if (active_input_text(command_id) != "status") {
        std::cerr << "Down arrow did not select the newer console command.\n";
        ImGui::DestroyContext();
        return 1;
    }
    press_key(ui, ImGuiKey_DownArrow);
    if (active_input_text(command_id) != "draft") {
        std::cerr << "Down arrow did not restore the unsubmitted console draft.\n";
        ImGui::DestroyContext();
        return 1;
    }
    press_key(ui, ImGuiKey_Enter);

    for (int command = 0; command < 24; ++command) {
        io.AddInputCharactersUTF8("help");
        draw_frame(ui);
        io.AddKeyEvent(ImGuiKey_Enter, true);
        draw_frame(ui);
        io.AddKeyEvent(ImGuiKey_Enter, false);
        draw_frame(ui);
    }

    console_window = ImGui::FindWindowByName("##DeveloperConsole");
    if (console_window == nullptr) {
        std::cerr << "Console fixed layout is unavailable.\n";
        ImGui::DestroyContext();
        return 1;
    }
    console_window->Scroll.y = 0.0F;
    console_window->ScrollTarget.y = 0.0F;
    ImGuiWindow* output_window = find_child_window(
        console_window, console_window->GetID("##ConsoleOutput"));
    if (output_window == nullptr || output_window->ScrollMax.y <= 0.0F) {
        std::cerr << "Console output did not establish a scrollable region.\n";
        ImGui::DestroyContext();
        return 1;
    }
    const float output_scroll_before = output_window->Scroll.y;
    io.AddMousePosEvent(console_window->Pos.x + console_window->Size.x * 0.5F,
                        console_window->Pos.y + console_window->Size.y * 0.5F);
    draw_frame(ui);
    io.AddMouseWheelEvent(0.0F, 1.0F);
    draw_frame(ui);
    for (int frame = 0; frame < 12; ++frame) draw_frame(ui);

    console_window = ImGui::FindWindowByName("##DeveloperConsole");
    output_window = find_child_window(
        console_window, console_window->GetID("##ConsoleOutput"));
    if (output_window == nullptr || output_window->Scroll.y >= output_scroll_before - 1.0F) {
        std::cerr << "Console output did not scroll independently from fixed controls.\n";
        ImGui::DestroyContext();
        return 1;
    }
    io.AddMouseWheelEvent(0.0F, -1.0F);
    draw_frame(ui);
    for (int frame = 0; frame < 12; ++frame) draw_frame(ui);

    console_window = ImGui::FindWindowByName("##DeveloperConsole");
    if (console_window == nullptr || std::abs(console_window->Scroll.y) > 0.01F) {
        std::cerr << "Console title or command input moved with output scrolling.\n";
        ImGui::DestroyContext();
        return 1;
    }

    const float initial_console_height = console_window->Size.y;
    io.AddMousePosEvent(console_window->ContentRegionRect.GetCenter().x,
                        console_window->ContentRegionRect.Max.y - 2.0F);
    draw_frame(ui);
    io.AddMouseButtonEvent(0, true);
    draw_frame(ui);
    io.AddMousePosEvent(console_window->ContentRegionRect.GetCenter().x,
                        console_window->ContentRegionRect.Max.y + 120.0F);
    draw_frame(ui);
    io.AddMouseButtonEvent(0, false);
    draw_frame(ui);
    for (int frame = 0; frame < 20; ++frame) draw_frame(ui);

    console_window = ImGui::FindWindowByName("##DeveloperConsole");
    if (console_window == nullptr || console_window->Size.y < initial_console_height + 80.0F) {
        std::cerr << "Console height cannot be changed by dragging its lower edge.\n";
        ImGui::DestroyContext();
        return 1;
    }

    ImGui::DestroyContext();
    return 0;
}
