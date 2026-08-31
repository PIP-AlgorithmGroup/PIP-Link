#include "pip_link/platform/desktop_window.hpp"

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
#include <utility>

namespace pip_link::platform {
namespace {

constexpr int initial_width = 1440;
constexpr int initial_height = 900;

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
    style.FrameBorderSize = 0.0F;
    style.ScaleAllSizes(scale);

    auto& colors = style.Colors;
    colors[ImGuiCol_Text] = {0.88F, 0.91F, 0.96F, 1.00F};
    colors[ImGuiCol_TextDisabled] = {0.42F, 0.46F, 0.54F, 1.00F};
    colors[ImGuiCol_WindowBg] = {0.035F, 0.043F, 0.060F, 1.00F};
    colors[ImGuiCol_ChildBg] = {0.055F, 0.064F, 0.085F, 1.00F};
    colors[ImGuiCol_Border] = {0.16F, 0.19F, 0.25F, 0.72F};
    colors[ImGuiCol_FrameBg] = {0.075F, 0.088F, 0.115F, 1.00F};
    colors[ImGuiCol_FrameBgHovered] = {0.09F, 0.18F, 0.22F, 1.00F};
    colors[ImGuiCol_FrameBgActive] = {0.05F, 0.34F, 0.40F, 1.00F};
    colors[ImGuiCol_Button] = {0.06F, 0.15F, 0.19F, 1.00F};
    colors[ImGuiCol_ButtonHovered] = {0.02F, 0.43F, 0.50F, 1.00F};
    colors[ImGuiCol_ButtonActive] = {0.00F, 0.68F, 0.78F, 1.00F};
    colors[ImGuiCol_CheckMark] = {0.00F, 0.85F, 1.00F, 1.00F};
    colors[ImGuiCol_SliderGrab] = {0.00F, 0.72F, 0.85F, 1.00F};
    colors[ImGuiCol_SliderGrabActive] = {0.00F, 0.90F, 1.00F, 1.00F};
    colors[ImGuiCol_Header] = {0.04F, 0.20F, 0.25F, 1.00F};
    colors[ImGuiCol_HeaderHovered] = {0.02F, 0.39F, 0.46F, 1.00F};
    colors[ImGuiCol_HeaderActive] = {0.00F, 0.62F, 0.72F, 1.00F};
}

}  // namespace

struct DesktopWindow::Impl final {
    SDL_Window* window{nullptr};
    ID3D11Device* device{nullptr};
    ID3D11DeviceContext* context{nullptr};
    IDXGISwapChain* swap_chain{nullptr};
    ID3D11RenderTargetView* render_target{nullptr};
    float display_scale{1.0F};
    ui::GroundStationUi ui;

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

    void resize(int width, int height) {
        if (width <= 0 || height <= 0 || swap_chain == nullptr) {
            return;
        }
        release_render_target(render_target);
        if (SUCCEEDED(swap_chain->ResizeBuffers(0, static_cast<UINT>(width),
                                                static_cast<UINT>(height),
                                                DXGI_FORMAT_UNKNOWN, 0))) {
            create_render_target();
        }
    }

    void shutdown() {
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

    impl_->window = SDL_CreateWindow(
        "PIP-Link 地面站",
        initial_width,
        initial_height,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (impl_->window == nullptr) {
        std::cerr << "Window creation failed: " << SDL_GetError() << '\n';
        impl_->shutdown();
        return 1;
    }

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
                impl_->resize(event.window.data1, event.window.data2);
            }
        }

        const auto now = std::chrono::steady_clock::now();
        const float delta_seconds = std::min(
            std::chrono::duration<float>(now - last_frame).count(), 0.1F);
        last_frame = now;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        impl_->ui.draw(delta_seconds, impl_->display_scale);
        ImGui::Render();

        constexpr float clear_color[4] = {0.025F, 0.031F, 0.045F, 1.0F};
        impl_->context->OMSetRenderTargets(1, &impl_->render_target, nullptr);
        impl_->context->ClearRenderTargetView(impl_->render_target, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        impl_->swap_chain->Present(1, 0);
    }

    impl_->shutdown();
    return 0;
}

}  // namespace pip_link::platform
