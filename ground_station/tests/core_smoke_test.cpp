#include "pip_link/core/build_info.hpp"
#include "pip_link/core/debounced_action.hpp"
#include "pip_link/core/input_validation.hpp"

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

    pip_link::core::DebouncedAction debounce{0.12F};
    debounce.schedule();
    if (debounce.tick(0.06F)) {
        std::cerr << "Debounce fired too early.\n";
        return 1;
    }
    debounce.schedule();
    if (debounce.tick(0.08F) || !debounce.tick(0.04F)) {
        std::cerr << "Debounce did not restart or fire at its deadline.\n";
        return 1;
    }
    debounce.schedule();
    if (!debounce.flush() || debounce.flush()) {
        std::cerr << "Debounce flush semantics are invalid.\n";
        return 1;
    }
    debounce.schedule();
    debounce.cancel();
    if (debounce.pending() || debounce.tick(1.0F)) {
        std::cerr << "Debounce cancel semantics are invalid.\n";
        return 1;
    }

    if (!pip_link::core::is_valid_service_name("_pip-link._udp.local") ||
        pip_link::core::is_valid_service_name("   ")) {
        std::cerr << "Service-name validation is invalid.\n";
        return 1;
    }
    if (!pip_link::core::is_valid_endpoint("192.168.1.10:5800") ||
        !pip_link::core::is_valid_endpoint("[fe80::1]:5800") ||
        pip_link::core::is_valid_endpoint("192.168.1.10") ||
        pip_link::core::is_valid_endpoint(":5800") ||
        pip_link::core::is_valid_endpoint("192.168.1.10:70000")) {
        std::cerr << "Endpoint validation is invalid.\n";
        return 1;
    }
    if (!pip_link::core::is_valid_directory("recordings") ||
        pip_link::core::is_valid_directory("\t")) {
        std::cerr << "Directory validation is invalid.\n";
        return 1;
    }
    return 0;
}
