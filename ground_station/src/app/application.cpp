#include "pip_link/app/application.hpp"

#include "pip_link/core/build_info.hpp"
#include "pip_link/platform/desktop_window.hpp"

#include <iostream>

namespace pip_link::app {

int Application::run() const {
    std::cout << core::BuildInfo::product_name() << '\n'
              << "Version: " << core::BuildInfo::version() << '\n'
              << "Language: " << core::BuildInfo::language_standard() << '\n'
              << "Starting SDL3 + Dear ImGui desktop shell.\n";

    platform::DesktopWindow window;
    return window.run();
}

}  // namespace pip_link::app
