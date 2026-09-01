#include "pip_link/backend/ground_station_backend.hpp"
#include "pip_link/ui/ground_station_ui.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <iostream>

namespace {

constexpr float frame_seconds = 1.0F / 60.0F;

void draw_frame(pip_link::ui::GroundStationUi& ui) {
    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = frame_seconds;
    ImGui::NewFrame();
    ui.draw(frame_seconds, 1.0F);
    ImGui::Render();
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

    ImGui::DestroyContext();
    return 0;
}
