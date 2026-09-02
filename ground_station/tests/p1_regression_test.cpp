#include "pip_link/backend/ground_station_backend.hpp"
#include "pip_link/ui/control_input_mapping.hpp"
#include "pip_link/ui/ground_station_ui.hpp"

#include <imgui.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {

constexpr float frame_seconds = 1.0F / 60.0F;

class TestBackend final : public pip_link::backend::GroundStationBackendStub {
public:
    [[nodiscard]] pip_link::backend::RuntimeState runtime_state() const override {
        return state;
    }

    [[nodiscard]] pip_link::backend::VideoSurface latest_video_surface() const override {
        return video_surface_available
                   ? pip_link::backend::VideoSurface{reinterpret_cast<void*>(1), 1280, 720}
                   : pip_link::backend::VideoSurface{};
    }

    void set_ready(bool ready) override {
        last_ready = ready;
        state.ready = ready;
        ++ready_requests;
    }

    void disconnect_device() override { ++disconnect_requests; }

    void submit_control_input(const pip_link::backend::ControlInput& input) override {
        last_input = input;
        ++control_packets;
    }

    pip_link::backend::RuntimeState state{};
    pip_link::backend::ControlInput last_input{};
    bool last_ready{false};
    int ready_requests{0};
    int disconnect_requests{0};
    int control_packets{0};
    bool video_surface_available{true};
};

void draw_frame(pip_link::ui::GroundStationUi& ui) {
    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = frame_seconds;
    ImGui::NewFrame();
    ui.draw(frame_seconds, 1.0F);
    ImGui::Render();
}

void press_key(pip_link::ui::GroundStationUi& ui, ImGuiKey key) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(key, true);
    draw_frame(ui);
    io.AddKeyEvent(key, false);
    draw_frame(ui);
}

bool bit_is_set(const pip_link::backend::ControlInput& input, int bit) {
    return (input.keyboard[static_cast<std::size_t>(bit / 8)] &
            static_cast<std::uint8_t>(1U << (bit % 8))) != 0;
}

bool video_is_fitted_without_cropping() {
    const ImTextureID video_texture = static_cast<ImTextureID>(1);
    const ImDrawData* draw_data = ImGui::GetDrawData();
    for (int list_index = 0; list_index < draw_data->CmdListsCount; ++list_index) {
        const ImDrawList* list = draw_data->CmdLists[list_index];
        for (const ImDrawCmd& command : list->CmdBuffer) {
            if (command.GetTexID() != video_texture) continue;
            ImVec2 min_position{std::numeric_limits<float>::max(),
                                std::numeric_limits<float>::max()};
            ImVec2 max_position{std::numeric_limits<float>::lowest(),
                                std::numeric_limits<float>::lowest()};
            ImVec2 min_uv{1.0F, 1.0F};
            ImVec2 max_uv{0.0F, 0.0F};
            for (unsigned int index = 0; index < command.ElemCount; ++index) {
                const ImDrawIdx vertex_index =
                    list->IdxBuffer[command.IdxOffset + index];
                const ImDrawVert& vertex =
                    list->VtxBuffer[command.VtxOffset + vertex_index];
                min_position.x = std::min(min_position.x, vertex.pos.x);
                min_position.y = std::min(min_position.y, vertex.pos.y);
                max_position.x = std::max(max_position.x, vertex.pos.x);
                max_position.y = std::max(max_position.y, vertex.pos.y);
                min_uv.x = std::min(min_uv.x, vertex.uv.x);
                min_uv.y = std::min(min_uv.y, vertex.uv.y);
                max_uv.x = std::max(max_uv.x, vertex.uv.x);
                max_uv.y = std::max(max_uv.y, vertex.uv.y);
            }
            const float width = max_position.x - min_position.x;
            const float height = max_position.y - min_position.y;
            return std::abs(width / height - 16.0F / 9.0F) < 0.01F &&
                   min_uv.x == 0.0F && min_uv.y == 0.0F &&
                   max_uv.x == 1.0F && max_uv.y == 1.0F;
        }
    }
    return false;
}

}  // namespace

int main() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1440.0F, 900.0F};
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    TestBackend backend;
    pip_link::ui::GroundStationUi ui{backend};
    backend.video_surface_available = false;
    draw_frame(ui);
    const int no_video_hud_vertices = ImGui::GetDrawData()->TotalVtxCount;
    press_key(ui, ImGuiKey_Tab);
    for (int frame = 0; frame < 90; ++frame) draw_frame(ui);
    const int no_video_hidden_input_vertices = ImGui::GetDrawData()->TotalVtxCount;
    if (no_video_hud_vertices <= no_video_hidden_input_vertices) {
        std::cerr << "No-video state did not render the input HUD overlay.\n";
        return 1;
    }
    press_key(ui, ImGuiKey_Tab);
    for (int frame = 0; frame < 90; ++frame) draw_frame(ui);
    backend.video_surface_available = true;
    io.DisplaySize = {900.0F, 900.0F};
    draw_frame(ui);
    if (!video_is_fitted_without_cropping()) {
        std::cerr << "Video was cropped instead of fitted during window resize.\n";
        return 1;
    }

    press_key(ui, ImGuiKey_F6);
    if (backend.ready_requests != 0) {
        std::cerr << "READY was accepted before backend connection confirmation.\n";
        return 1;
    }

    backend.state.connection = pip_link::backend::ConnectionState::connected;
    backend.state.video_available = true;
    draw_frame(ui);
    press_key(ui, ImGuiKey_F6);
    if (backend.ready_requests != 1 || !backend.last_ready ||
        !ui.wants_relative_mouse_mode()) {
        std::cerr << "Confirmed connection did not enter READY and request mouse capture.\n";
        return 1;
    }

    draw_frame(ui);
    if (backend.control_packets != 0) {
        std::cerr << "Control input was sent before relative mouse capture was confirmed.\n";
        return 1;
    }
    ui.set_mouse_capture_active(true);
    pip_link::ui::GamepadSnapshot gamepad{};
    gamepad.connected = true;
    gamepad.name = "Test Gamepad";
    gamepad.left_y = -1.0F;
    gamepad.right_x = 0.5F;
    gamepad.left_shoulder = true;
    ui.set_gamepad_snapshot(gamepad);
    draw_frame(ui);
    if (backend.control_packets != 1) {
        std::cerr << "Control input was not sent after mouse capture confirmation.\n";
        return 1;
    }
    if (!bit_is_set(backend.last_input, 29) || !bit_is_set(backend.last_input, 54) ||
        backend.last_input.mouse_delta_x <= 0.0F) {
        std::cerr << "Gamepad movement was not mapped into control input.\n";
        return 1;
    }

    ui.on_focus_lost();
    if (backend.ready_requests != 2 || backend.last_ready ||
        backend.disconnect_requests != 0) {
        std::cerr << "Focus loss should neutralize control without disconnecting video.\n";
        return 1;
    }

    backend.state.connection = pip_link::backend::ConnectionState::disconnected;
    draw_frame(ui);
    if (backend.ready_requests != 2 || backend.last_ready ||
        ui.wants_relative_mouse_mode()) {
        std::cerr << "Backend disconnect did not force NOT READY and release capture.\n";
        return 1;
    }

    std::array<int, 11> bindings{
        ImGuiKey_F6, ImGuiKey_Tab, ImGuiKey_GraveAccent, ImGuiKey_Escape,
        ImGuiKey_UpArrow, ImGuiKey_S, ImGuiKey_A, ImGuiKey_D,
        ImGuiKey_LeftShift, ImGuiKey_E, ImGuiKey_F,
    };
    pip_link::backend::ControlInput remapped{};
    pip_link::ui::map_physical_key(remapped, ImGuiKey_UpArrow, bindings);
    if (!bit_is_set(remapped, 29)) {
        std::cerr << "Rebound forward key did not produce the protocol W action bit.\n";
        return 1;
    }
    pip_link::backend::ControlInput old_binding{};
    pip_link::ui::map_physical_key(old_binding, ImGuiKey_W, bindings);
    if (bit_is_set(old_binding, 29)) {
        std::cerr << "Old forward key remained active after rebinding.\n";
        return 1;
    }
    const auto visuals = pip_link::ui::protocol_key_visuals();
    if (std::none_of(visuals.begin(), visuals.end(), [](const auto& visual) {
            return visual.imgui_key == ImGuiKey_F5;
        })) {
        std::cerr << "F5 is missing from the input protocol preview.\n";
        return 1;
    }

    draw_frame(ui);
    io.AddKeyEvent(ImGuiKey_Tab, true);
    draw_frame(ui);
    const int transition_vertices = ImGui::GetDrawData()->TotalVtxCount;
    io.AddKeyEvent(ImGuiKey_Tab, false);
    for (int frame = 0; frame < 90; ++frame) draw_frame(ui);
    const int hidden_vertices = ImGui::GetDrawData()->TotalVtxCount;
    if (transition_vertices <= hidden_vertices) {
        std::cerr << "Input HUD disappeared without a hide transition.\n";
        return 1;
    }

    ImGui::DestroyContext();
    return 0;
}
