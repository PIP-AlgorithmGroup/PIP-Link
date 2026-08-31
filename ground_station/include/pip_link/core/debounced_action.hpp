#pragma once

#include <algorithm>

namespace pip_link::core {

class DebouncedAction final {
public:
    explicit DebouncedAction(float delay_seconds) noexcept
        : delay_seconds_(std::max(delay_seconds, 0.0F)) {}

    void schedule() noexcept {
        pending_ = true;
        elapsed_seconds_ = 0.0F;
    }

    [[nodiscard]] bool tick(float delta_seconds) noexcept {
        if (!pending_) {
            return false;
        }
        elapsed_seconds_ += std::max(delta_seconds, 0.0F);
        if (elapsed_seconds_ < delay_seconds_) {
            return false;
        }
        pending_ = false;
        elapsed_seconds_ = 0.0F;
        return true;
    }

    [[nodiscard]] bool flush() noexcept {
        if (!pending_) {
            return false;
        }
        pending_ = false;
        elapsed_seconds_ = 0.0F;
        return true;
    }

    void cancel() noexcept {
        pending_ = false;
        elapsed_seconds_ = 0.0F;
    }

    [[nodiscard]] bool pending() const noexcept { return pending_; }

private:
    float delay_seconds_{};
    float elapsed_seconds_{};
    bool pending_{false};
};

}  // namespace pip_link::core
