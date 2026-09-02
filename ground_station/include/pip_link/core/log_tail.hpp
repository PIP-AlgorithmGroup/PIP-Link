#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace pip_link::core {

class LogTail final {
public:
    void sync(std::size_t entry_count, float maximum_scroll) noexcept {
        maximum_scroll = std::max(maximum_scroll, 0.0F);
        if (following_) target_ = maximum_scroll;
        else target_ = std::clamp(target_, 0.0F, maximum_scroll);
        observed_entry_count_ = entry_count;
    }

    void on_wheel(float wheel, float maximum_scroll) noexcept {
        if (wheel > 0.0F && maximum_scroll > 0.0F) following_ = false;
    }

    void set_target(float target, float maximum_scroll) noexcept {
        maximum_scroll = std::max(maximum_scroll, 0.0F);
        target_ = std::clamp(target, 0.0F, maximum_scroll);
        if (std::abs(target_ - maximum_scroll) <= 0.5F) following_ = true;
    }

    [[nodiscard]] float target() const noexcept { return target_; }
    [[nodiscard]] bool following() const noexcept { return following_; }

private:
    std::size_t observed_entry_count_{};
    float target_{};
    bool following_{true};
};

}  // namespace pip_link::core
