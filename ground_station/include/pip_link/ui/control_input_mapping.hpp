#pragma once

#include "pip_link/backend/ground_station_backend.hpp"

#include <array>
#include <span>

namespace pip_link::ui {

struct KeyVisual final {
    int imgui_key;
    int protocol_bit;
    const char* label;
};

[[nodiscard]] std::span<const KeyVisual> protocol_key_visuals() noexcept;

void map_physical_key(backend::ControlInput& input, int physical_key,
                      const std::array<int, 11>& bindings) noexcept;

}  // namespace pip_link::ui
