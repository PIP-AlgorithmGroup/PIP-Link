#pragma once

#include <charconv>
#include <string_view>

namespace pip_link::core {

inline bool has_visible_text(std::string_view value) noexcept {
    return value.find_first_not_of(" \t\r\n") != std::string_view::npos;
}

inline bool is_valid_service_name(std::string_view value) noexcept {
    return has_visible_text(value);
}

inline bool is_valid_endpoint(std::string_view value) noexcept {
    const auto separator = value.rfind(':');
    if (separator == std::string_view::npos || separator == 0 || separator + 1 >= value.size()) {
        return false;
    }
    if (!has_visible_text(value.substr(0, separator))) return false;

    int port = 0;
    const std::string_view port_text = value.substr(separator + 1);
    const auto result = std::from_chars(port_text.data(), port_text.data() + port_text.size(), port);
    return result.ec == std::errc{} && result.ptr == port_text.data() + port_text.size() &&
           port >= 1 && port <= 65535;
}

inline bool is_valid_directory(std::string_view value) noexcept {
    return has_visible_text(value);
}

}  // namespace pip_link::core
