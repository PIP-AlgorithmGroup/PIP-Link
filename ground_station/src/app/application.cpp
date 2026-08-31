#include "pip_link/app/application.hpp"

#include "pip_link/core/build_info.hpp"

#include <iostream>

namespace pip_link::app {

int Application::run() const {
    std::cout << core::BuildInfo::product_name() << '\n'
              << "Version: " << core::BuildInfo::version() << '\n'
              << "Language: " << core::BuildInfo::language_standard() << '\n'
              << "Bootstrap project is ready.\n";
    return 0;
}

}  // namespace pip_link::app
