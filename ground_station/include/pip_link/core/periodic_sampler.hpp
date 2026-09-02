#pragma once

#include <algorithm>

namespace pip_link::core {

class PeriodicSampler final {
public:
    explicit PeriodicSampler(float interval_seconds) noexcept
        : interval_seconds_(std::max(static_cast<double>(interval_seconds), 0.001)) {}

    [[nodiscard]] bool tick(float delta_seconds) noexcept {
        elapsed_seconds_ += static_cast<double>(
            std::clamp(delta_seconds, 0.0F, 0.1F));
        if (elapsed_seconds_ < interval_seconds_) return false;
        elapsed_seconds_ -= interval_seconds_;
        return true;
    }

    void reset() noexcept { elapsed_seconds_ = 0.0F; }

private:
    double interval_seconds_;
    double elapsed_seconds_{};
};

}  // namespace pip_link::core
