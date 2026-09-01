#include "pip_link/backend/ground_station_backend.hpp"
#include "pip_link/ui/control_input_mapping.hpp"
#include "pip_link/ui/ground_station_ui.hpp"

#include <imgui.h>
#include <array>
#include <cstdint>
#include <iostream>

namespace {

constexpr float frame_seconds = 1.0F / 60.0F;

class TestBackend final : public pip_link::backend::GroundStationBackendStub {
public:
    [[nodiscard]] pip_link::backend::RuntimeState runtime_state() const override {
        return state;
    }

    void set_ready(bool ready) override {
        last_ready = ready;
        ++ready_requests;
    }

    void submit_control_input(const pip_link::backend::ControlInput& input) override {
        last_input = input;
        ++control_packets;
    }

    pip_link::backend::RuntimeState state{};
    pip_link::backend::ControlInput last_input{};
    bool last_ready{false};
    int ready_requests{0};
    int control_packets{0};
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

}  // namespace

int main() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = {1440.0F, 900.0F};
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    TestBackend backend;
    pip_link::ui::GroundStationUi ui{backend};
    draw_frame(ui);

    press_key(ui, ImGuiKey_F6);
    if (backend.ready_requests != 0) {
        std::cerr << "READY was accepted before backend connection confirmation.\n";
        return 1;
    }

    backend.state.connection = pip_link::backend::ConnectionState::connected;
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
    draw_frame(ui);
    if (backend.control_packets != 1) {
        std::cerr << "Control input was not sent after mouse capture confirmation.\n";
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

    ImGui::DestroyContext();
    return 0;
}
