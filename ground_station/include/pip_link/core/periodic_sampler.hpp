#pragma once

#include <algorithm>
#include <cmath>

namespace pip_link::core {

class PeriodicSampler final {
public:
    explicit PeriodicSampler(float interval_seconds) noexcept
        : interval_seconds_(std::max(interval_seconds, 0.001F)) {}

    [[nodiscard]] bool tick(float delta_seconds) noexcept {
        elapsed_seconds_ += std::clamp(delta_seconds, 0.0F, 0.1F);
        if (elapsed_seconds_ + 0.000001F < interval_seconds_) return false;
        elapsed_seconds_ = std::fmod(elapsed_seconds_, interval_seconds_);
        return true;
    }

    void reset() noexcept { elapsed_seconds_ = 0.0F; }

private:
    float interval_seconds_;
    float elapsed_seconds_{};
};

}  // namespace pip_link::core
