#include "pip_link/core/build_info.hpp"

namespace pip_link::core {

std::string_view BuildInfo::version() noexcept {
    return PIP_LINK_VERSION;
}

}  // namespace pip_link::core
