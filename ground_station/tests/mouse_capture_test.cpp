#include "platform/mouse_capture.hpp"

#include <SDL3/SDL.h>
#include <backends/imgui_impl_sdl3.h>
#include <imgui.h>

#include <iostream>

namespace {

bool test_capture(SDL_Window* window) {
    using pip_link::platform::set_relative_mouse_capture;
    if (!set_relative_mouse_capture(window, false) || !SDL_CursorVisible()) {
        std::cerr << "The cursor is unavailable before capture.\n";
        return false;
    }

    if (!set_relative_mouse_capture(window, true) ||
        !SDL_GetWindowRelativeMouseMode(window) || SDL_CursorVisible()) {
        std::cerr << "Entering relative mouse capture did not hide the cursor.\n";
        return false;
    }

    for (int frame = 0; frame < 3; ++frame) {
        SDL_PumpEvents();
        ImGui_ImplSDL3_NewFrame();
        if (SDL_CursorVisible()) {
            std::cerr << "The ImGui SDL backend showed the captured cursor again.\n";
            return false;
        }
        ImGui::NewFrame();
        ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
        if (!set_relative_mouse_capture(window, true)) return false;
        if (ImGui::GetMouseCursor() != ImGuiMouseCursor_None ||
            !SDL_GetWindowRelativeMouseMode(window)) {
            std::cerr << "Cursor shape changes interrupted relative mouse capture.\n";
            return false;
        }
        ImGui::Render();
    }

    if (!set_relative_mouse_capture(window, false) ||
        SDL_GetWindowRelativeMouseMode(window) || !SDL_CursorVisible()) {
        std::cerr << "Leaving capture did not restore the cursor.\n";
        return false;
    }
    ImGui_ImplSDL3_NewFrame();
    if (!SDL_CursorVisible() ||
        (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) != 0) {
        std::cerr << "ImGui did not regain cursor control after capture.\n";
        return false;
    }
    if ((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) == 0) {
        std::cerr << "Mouse capture changed unrelated ImGui configuration.\n";
        return false;
    }
    return true;
}

}  // namespace

int main() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << '\n';
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow("Mouse capture regression", 320, 240,
                                           SDL_WINDOW_HIDDEN);
    if (window == nullptr) {
        std::cerr << "Test window creation failed: " << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    if (!ImGui_ImplSDL3_InitForD3D(window)) {
        std::cerr << "ImGui SDL initialization failed.\n";
        ImGui::DestroyContext();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    const bool passed = test_capture(window);
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return passed ? 0 : 1;
}
