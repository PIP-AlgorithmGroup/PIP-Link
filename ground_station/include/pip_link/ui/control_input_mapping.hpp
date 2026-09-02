#pragma once

#include "pip_link/backend/ground_station_backend.hpp"

#include <array>
#include <cstddef>
#include <span>

namespace pip_link::ui {

struct KeyVisual final {
    int imgui_key;
    int protocol_bit;
    const char* label;
};

enum BindingIndex : std::size_t {
    toggle_ready_binding,
    toggle_hud_binding,
    toggle_console_binding,
    toggle_settings_binding,
    forward_binding,
    backward_binding,
    left_binding,
    right_binding,
    sprint_binding,
    special_one_binding,
    special_two_binding,
    start_recording_binding,
    take_screenshot_binding,
    pause_recording_binding,
    stop_recording_binding,
    binding_count,
};

enum class RecordingShortcutAction {
    none,
    start,
    toggle_pause,
    stop,
};

inline constexpr int unbound_key = 0;

[[nodiscard]] std::array<int, binding_count> default_key_bindings() noexcept;
[[nodiscard]] bool key_bindings_may_share(std::size_t first,
                                          std::size_t second) noexcept;
[[nodiscard]] int assign_key_binding(std::span<int> bindings,
                                     std::size_t action, int key) noexcept;
[[nodiscard]] bool is_bindable_keyboard_key(int key) noexcept;
[[nodiscard]] RecordingShortcutAction resolve_recording_shortcut(
    backend::RecordingState state, bool start_pressed,
    bool pause_pressed, bool stop_pressed) noexcept;

[[nodiscard]] std::span<const KeyVisual> protocol_key_visuals() noexcept;

void map_physical_key(backend::ControlInput& input, int physical_key,
                      std::span<const int> bindings) noexcept;

}  // namespace pip_link::ui
