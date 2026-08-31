#include "pip_link/core/build_info.hpp"

#include <iostream>

int main() {
    if (pip_link::core::BuildInfo::product_name().empty()) {
        std::cerr << "Product name must not be empty.\n";
        return 1;
    }
    if (pip_link::core::BuildInfo::version().empty()) {
        std::cerr << "Version must not be empty.\n";
        return 1;
    }
    return 0;
}
