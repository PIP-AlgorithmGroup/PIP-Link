#pragma once

#include <filesystem>

namespace pip_link::core {

inline std::filesystem::path resolve_media_output_directory(
    const std::filesystem::path& configured_directory,
    const std::filesystem::path& default_base_directory) {
    if (configured_directory.is_absolute()) {
        return configured_directory.lexically_normal();
    }
    return (default_base_directory / configured_directory).lexically_normal();
}

}  // namespace pip_link::core
