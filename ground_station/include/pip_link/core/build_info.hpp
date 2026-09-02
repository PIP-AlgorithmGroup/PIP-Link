#pragma once

#include <string_view>

namespace pip_link::core {

struct BuildInfo final {
    [[nodiscard]] static constexpr std::string_view product_name() noexcept {
        return "PIP-Link Robot Control Client";
    }

    [[nodiscard]] static std::string_view version() noexcept;
    [[nodiscard]] static constexpr std::string_view language_standard() noexcept {
        return "C++20";
    }
};

}  // namespace pip_link::core
