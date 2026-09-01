#pragma once

#include <algorithm>
#include <cmath>

namespace pip_link::core {

[[nodiscard]] inline float advance_smooth_scroll(float current, float target,
                                                 float delta_seconds,
                                                 float coordinate_scale = 1.0F) noexcept {
    constexpr float scroll_tau = 0.08F;
    constexpr float minimum_settling_speed = 90.0F;
    constexpr float snap_distance = 0.5F;

    const float scale = std::max(coordinate_scale, 0.01F);
    const float difference = target - current;
    if (std::abs(difference) <= snap_distance * scale) return target;

    const float delta = std::clamp(delta_seconds, 0.0F, 0.1F);
    float step = difference * (1.0F - std::exp(-delta / scroll_tau));
    const float minimum_step = minimum_settling_speed * scale * delta;
    if (std::abs(step) < minimum_step) {
        step = std::copysign(std::min(std::abs(difference), minimum_step), difference);
    }

    const float next = current + step;
    return std::abs(target - next) <= snap_distance * scale ? target : next;
}

}  // namespace pip_link::core
