#include "pip_link/platform/desktop_window.hpp"

#include "pip_link/backend/ground_station_backend.hpp"
#include "pip_link/ui/ground_station_ui.hpp"

#include <SDL3/SDL.h>
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_sdl3.h>
#include <d3d11.h>
#include <imgui.h>
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <utility>

namespace pip_link::platform {
namespace {

constexpr int preferred_width = 1440;
constexpr int preferred_height = 900;
constexpr int minimum_width = 960;
constexpr int minimum_height = 640;

void apply_window_icon(SDL_Window* window) {
    const char* base_path = SDL_GetBasePath();
    if (base_path == nullptr) {
        std::cerr << "Application icon path is unavailable: " << SDL_GetError() << '\n';
        return;
    }

    const std::filesystem::path icon_path = std::filesystem::path(base_path) / "icon.bmp";
    SDL_Surface* icon = SDL_LoadBMP(icon_path.string().c_str());
    if (icon == nullptr) {
        std::cerr << "Application icon could not be loaded from " << icon_path.string()
                  << ": " << SDL_GetError() << '\n';
        return;
    }

    const SDL_PixelFormatDetails* format = SDL_GetPixelFormatDetails(icon->format);
    if (format != nullptr) {
        const Uint32 transparent_magenta = SDL_MapRGB(format, nullptr, 255, 0, 255);
        SDL_SetSurfaceColorKey(icon, true, transparent_magenta);
    }
    if (!SDL_SetWindowIcon(window, icon)) {
        std::cerr << "Application icon could not be applied: " << SDL_GetError() << '\n';
    }
    SDL_DestroySurface(icon);
}

void release_render_target(ID3D11RenderTargetView*& target) {
    if (target != nullptr) {
        target->Release();
        target = nullptr;
    }
}

std::filesystem::path find_chinese_font() {
    constexpr const char* candidates[] = {
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/simsun.ttc",
    };
    for (const char* candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

void apply_theme(float scale) {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 7.0F;
    style.ChildRounding = 7.0F;
    style.FrameRounding = 5.0F;
    style.PopupRounding = 6.0F;
    style.GrabRounding = 4.0F;
    style.ScrollbarRounding = 7.0F;
    style.WindowBorderSize = 0.0F;
    style.ChildBorderSize = 1.0F;
    style.FrameBorderSize = 1.0F;
    style.DisabledAlpha = 0.82F;
    style.WindowPadding = {16.0F, 14.0F};
    style.FramePadding = {11.0F, 7.0F};
    style.ItemSpacing = {10.0F, 9.0F};
    style.ScaleAllSizes(scale);

    auto& colors = style.Colors;
    colors[ImGuiCol_Text] = {0.07F, 0.11F, 0.16F, 1.00F};
    colors[ImGuiCol_TextDisabled] = {0.34F, 0.40F, 0.46F, 1.00F};
    colors[ImGuiCol_WindowBg] = {0.91F, 0.94F, 0.96F, 1.00F};
    colors[ImGuiCol_ChildBg] = {0.98F, 0.99F, 1.00F, 1.00F};
    colors[ImGuiCol_PopupBg] = {1.00F, 1.00F, 1.00F, 1.00F};
    colors[ImGuiCol_Border] = {0.63F, 0.70F, 0.75F, 0.95F};
    colors[ImGuiCol_FrameBg] = {0.86F, 0.90F, 0.93F, 1.00F};
    colors[ImGuiCol_FrameBgHovered] = {0.76F, 0.85F, 0.89F, 1.00F};
    colors[ImGuiCol_FrameBgActive] = {0.68F, 0.80F, 0.85F, 1.00F};
    colors[ImGuiCol_Button] = {0.84F, 0.90F, 0.93F, 1.00F};
    colors[ImGuiCol_ButtonHovered] = {0.70F, 0.82F, 0.88F, 1.00F};
    colors[ImGuiCol_ButtonActive] = {0.60F, 0.76F, 0.83F, 1.00F};
    colors[ImGuiCol_CheckMark] = {0.00F, 0.44F, 0.62F, 1.00F};
    colors[ImGuiCol_SliderGrab] = {0.00F, 0.44F, 0.62F, 1.00F};
    colors[ImGuiCol_SliderGrabActive] = {0.00F, 0.55F, 0.72F, 1.00F};
    colors[ImGuiCol_Header] = {0.75F, 0.84F, 0.89F, 1.00F};
    colors[ImGuiCol_HeaderHovered] = {0.63F, 0.78F, 0.85F, 1.00F};
    colors[ImGuiCol_HeaderActive] = {0.53F, 0.72F, 0.80F, 1.00F};
    colors[ImGuiCol_Separator] = {0.65F, 0.72F, 0.77F, 0.95F};
    colors[ImGuiCol_TableHeaderBg] = {0.78F, 0.85F, 0.89F, 1.00F};
    colors[ImGuiCol_TableRowBg] = {0.98F, 0.99F, 1.00F, 1.00F};
    colors[ImGuiCol_TableRowBgAlt] = {0.91F, 0.94F, 0.96F, 1.00F};
    colors[ImGuiCol_PlotLines] = {0.00F, 0.44F, 0.62F, 1.00F};
    colors[ImGuiCol_PlotLinesHovered] = {0.00F, 0.57F, 0.75F, 1.00F};
    colors[ImGuiCol_NavHighlight] = {0.00F, 0.44F, 0.62F, 0.85F};
    colors[ImGuiCol_ModalWindowDimBg] = {0.17F, 0.24F, 0.31F, 0.28F};
    colors[ImGuiCol_ScrollbarBg] = {0.0F, 0.0F, 0.0F, 0.0F};
    colors[ImGuiCol_ScrollbarGrab] = {0.0F, 0.0F, 0.0F, 0.0F};
    colors[ImGuiCol_ScrollbarGrabHovered] = {0.0F, 0.0F, 0.0F, 0.0F};
    colors[ImGuiCol_ScrollbarGrabActive] = {0.0F, 0.0F, 0.0F, 0.0F};
}

}  // namespace

struct DesktopWindow::Impl final {
    SDL_Window* window{nullptr};
    ID3D11Device* device{nullptr};
    ID3D11DeviceContext* context{nullptr};
    IDXGISwapChain* swap_chain{nullptr};
    ID3D11RenderTargetView* render_target{nullptr};
    float display_scale{1.0F};
    std::unique_ptr<backend::GroundStationBackendRuntime> backend;
    std::unique_ptr<ui::GroundStationUi> ui;
    SDL_Gamepad* gamepad{nullptr};
    std::optional<bool> last_vibration_setting;

    void open_gamepad(SDL_JoystickID id) {
        if (gamepad != nullptr) return;
        gamepad = SDL_OpenGamepad(id);
        if (gamepad == nullptr) {
            std::cerr << "Gamepad open failed: " << SDL_GetError() << '\n';
        }
    }

    void open_first_gamepad() {
        if (gamepad != nullptr) return;
        int count = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&count);
        if (ids != nullptr && count > 0) open_gamepad(ids[0]);
        SDL_free(ids);
    }

    void close_gamepad() {
        if (gamepad != nullptr) {
            SDL_CloseGamepad(gamepad);
            gamepad = nullptr;
        }
        last_vibration_setting.reset();
    }

    void update_gamepad() {
        if (!ui) return;
        if (gamepad != nullptr && !SDL_GamepadConnected(gamepad)) close_gamepad();
        if (gamepad == nullptr) open_first_gamepad();
        ui::GamepadSnapshot snapshot{};
        if (gamepad != nullptr) {
            const auto axis = [this](SDL_GamepadAxis value) {
                return std::clamp(static_cast<float>(SDL_GetGamepadAxis(gamepad, value)) /
                                  32767.0F, -1.0F, 1.0F);
            };
            snapshot.connected = true;
            const char* name = SDL_GetGamepadName(gamepad);
            snapshot.name = name != nullptr ? name : "SDL Gamepad";
            snapshot.left_x = axis(SDL_GAMEPAD_AXIS_LEFTX);
            snapshot.left_y = axis(SDL_GAMEPAD_AXIS_LEFTY);
            snapshot.right_x = axis(SDL_GAMEPAD_AXIS_RIGHTX);
            snapshot.right_y = axis(SDL_GAMEPAD_AXIS_RIGHTY);
            snapshot.right_trigger = std::max(0.0F, axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
            snapshot.south = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_SOUTH);
            snapshot.left_shoulder = SDL_GetGamepadButton(
                gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
            const bool vibration = ui->gamepad_vibration_enabled();
            if (last_vibration_setting && *last_vibration_setting != vibration) {
                SDL_RumbleGamepad(gamepad, vibration ? 0x3000U : 0U,
                                  vibration ? 0x3000U : 0U, vibration ? 180U : 0U);
            }
            last_vibration_setting = vibration;
        }
        ui->set_gamepad_snapshot(std::move(snapshot));
    }

    bool create_render_target() {
        ID3D11Texture2D* back_buffer = nullptr;
        if (FAILED(swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer)))) {
            return false;
        }
        const HRESULT result = device->CreateRenderTargetView(back_buffer, nullptr, &render_target);
        back_buffer->Release();
        return SUCCEEDED(result);
    }

    bool create_device(HWND hwnd) {
        DXGI_SWAP_CHAIN_DESC description{};
        description.BufferCount = 2;
        description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.OutputWindow = hwnd;
        description.SampleDesc.Count = 1;
        description.Windowed = TRUE;
        description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        const D3D_FEATURE_LEVEL levels[] = {
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_0,
        };
        D3D_FEATURE_LEVEL selected_level{};
        const HRESULT result = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            0,
            levels,
            static_cast<UINT>(std::size(levels)),
            D3D11_SDK_VERSION,
            &description,
            &swap_chain,
            &device,
            &selected_level,
            &context);
        return SUCCEEDED(result) && create_render_target();
    }

    [[nodiscard]] bool resize(int width, int height) {
        if (width <= 0 || height <= 0 || swap_chain == nullptr) {
            return true;
        }
        release_render_target(render_target);
        const HRESULT resize_result = swap_chain->ResizeBuffers(
            0, static_cast<UINT>(width), static_cast<UINT>(height),
            DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(resize_result)) {
            std::cerr << "Direct3D 11 swap-chain resize failed.\n";
            return false;
        }
        if (!create_render_target()) {
            std::cerr << "Direct3D 11 render-target recreation failed.\n";
            return false;
        }
        return true;
    }

    void update_mouse_capture() {
        if (!ui) return;
        const bool requested = ui->wants_relative_mouse_mode();
        const bool active = SDL_GetWindowRelativeMouseMode(window);
        if (requested == active) {
            ui->set_mouse_capture_active(active);
            return;
        }
        if (!SDL_SetWindowRelativeMouseMode(window, requested)) {
            std::cerr << "Relative mouse mode change failed: " << SDL_GetError() << '\n';
            ui->set_mouse_capture_active(active);
            if (requested) ui->on_mouse_capture_failed();
            return;
        }
        ui->set_mouse_capture_active(requested);
    }

    void shutdown() {
        close_gamepad();
        ui.reset();
        backend.reset();
        if (ImGui::GetCurrentContext() != nullptr) {
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplSDL3_Shutdown();
            ImGui::DestroyContext();
        }
        release_render_target(render_target);
        if (swap_chain != nullptr) {
            swap_chain->Release();
            swap_chain = nullptr;
        }
        if (context != nullptr) {
            context->Release();
            context = nullptr;
        }
        if (device != nullptr) {
            device->Release();
            device = nullptr;
        }
        if (window != nullptr) {
            SDL_DestroyWindow(window);
            window = nullptr;
        }
        SDL_Quit();
    }
};

DesktopWindow::DesktopWindow() : impl_(std::make_unique<Impl>()) {}
DesktopWindow::~DesktopWindow() = default;
DesktopWindow::DesktopWindow(DesktopWindow&&) noexcept = default;
DesktopWindow& DesktopWindow::operator=(DesktopWindow&&) noexcept = default;

int DesktopWindow::run() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << '\n';
        return 1;
    }

    int window_width = preferred_width;
    int window_height = preferred_height;
    SDL_Rect usable_bounds{};
    const SDL_DisplayID primary_display = SDL_GetPrimaryDisplay();
    if (primary_display != 0 && SDL_GetDisplayUsableBounds(primary_display, &usable_bounds)) {
        window_width = std::clamp(static_cast<int>(usable_bounds.w * 0.88F),
                                  minimum_width, preferred_width);
        window_height = std::clamp(static_cast<int>(usable_bounds.h * 0.88F),
                                   minimum_height, preferred_height);
    }

    impl_->window = SDL_CreateWindow(
        "PIP-Link",
        window_width,
        window_height,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (impl_->window == nullptr) {
        std::cerr << "Window creation failed: " << SDL_GetError() << '\n';
        impl_->shutdown();
        return 1;
    }
    apply_window_icon(impl_->window);
    SDL_SetWindowMinimumSize(impl_->window, minimum_width, minimum_height);
    SDL_SetWindowPosition(impl_->window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

    const SDL_PropertiesID properties = SDL_GetWindowProperties(impl_->window);
    const auto hwnd = static_cast<HWND>(
        SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (hwnd == nullptr || !impl_->create_device(hwnd)) {
        std::cerr << "Direct3D 11 initialization failed.\n";
        impl_->shutdown();
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    impl_->display_scale = std::clamp(SDL_GetWindowDisplayScale(impl_->window), 1.0F, 2.5F);
    const std::filesystem::path font_path = find_chinese_font();
    if (!font_path.empty()) {
        io.Fonts->AddFontFromFileTTF(font_path.string().c_str(), 18.0F * impl_->display_scale,
                                     nullptr, io.Fonts->GetGlyphRangesChineseFull());
        std::cout << "Chinese font loaded: " << font_path.string() << '\n';
    } else {
        io.Fonts->AddFontDefault();
        std::cerr << "Chinese font was not found; using the default font.\n";
    }
    apply_theme(impl_->display_scale);

    ImGui_ImplSDL3_InitForD3D(impl_->window);
    ImGui_ImplDX11_Init(impl_->device, impl_->context);
    impl_->backend = std::make_unique<backend::GroundStationBackendRuntime>(
        impl_->window, impl_->device, impl_->context);
    const backend::BackendPreferences preferences = impl_->backend->preferences();
    if (preferences.display_configured) {
        impl_->backend->preview_display_settings(preferences.resolution_index,
                                                 preferences.window_mode,
                                                 preferences.display_index);
        impl_->backend->confirm_display_settings();
    }
    impl_->ui = std::make_unique<ui::GroundStationUi>(*impl_->backend);
    impl_->open_first_gamepad();

    bool running = true;
    auto last_frame = std::chrono::steady_clock::now();
    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                 event.window.windowID == SDL_GetWindowID(impl_->window))) {
                running = false;
            }
            if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED &&
                event.window.windowID == SDL_GetWindowID(impl_->window)) {
                if (!impl_->resize(event.window.data1, event.window.data2)) {
                    running = false;
                }
            }
            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST &&
                event.window.windowID == SDL_GetWindowID(impl_->window)) {
                impl_->ui->on_focus_lost();
            }
            if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
                impl_->open_gamepad(event.gdevice.which);
            } else if (event.type == SDL_EVENT_GAMEPAD_REMOVED && impl_->gamepad != nullptr &&
                       SDL_GetGamepadID(impl_->gamepad) == event.gdevice.which) {
                impl_->close_gamepad();
            }
        }

        if (!running) break;

        const auto now = std::chrono::steady_clock::now();
        const float delta_seconds = std::min(
            std::chrono::duration<float>(now - last_frame).count(), 0.1F);
        last_frame = now;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        impl_->update_gamepad();
        impl_->ui->draw(delta_seconds, impl_->display_scale);
        impl_->update_mouse_capture();
        if (impl_->ui->quit_requested()) {
            running = false;
        }
        ImGui::Render();

        if (impl_->render_target == nullptr) {
            std::cerr << "Direct3D 11 render target is unavailable.\n";
            running = false;
            continue;
        }

        constexpr float clear_color[4] = {0.91F, 0.94F, 0.96F, 1.0F};
        impl_->context->OMSetRenderTargets(1, &impl_->render_target, nullptr);
        impl_->context->ClearRenderTargetView(impl_->render_target, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        impl_->swap_chain->Present(impl_->backend->vertical_sync_enabled() ? 1U : 0U, 0);
    }

    impl_->shutdown();
    return 0;
}

}  // namespace pip_link::platform
