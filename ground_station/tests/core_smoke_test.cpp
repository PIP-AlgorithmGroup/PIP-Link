#include "pip_link/core/build_info.hpp"
#include "pip_link/core/debounced_action.hpp"
#include "pip_link/core/input_validation.hpp"
#include "pip_link/core/log_tail.hpp"
#include "pip_link/core/media_output_path.hpp"
#include "pip_link/core/periodic_sampler.hpp"
#include "pip_link/core/smooth_scroll.hpp"

#include <array>
#include <iostream>

int main() {
    if (pip_link::core::BuildInfo::product_name().empty()) {
        std::cerr << "Product name must not be empty.\n";
        return 1;
    }
    if (pip_link::core::BuildInfo::version() != "3.0.1") {
        std::cerr << "Ground-station release version must be 3.0.1.\n";
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
    const std::filesystem::path videos = "C:/Users/test/Videos";
    if (pip_link::core::resolve_media_output_directory("recordings", videos) !=
            videos / "recordings" ||
        pip_link::core::resolve_media_output_directory("D:/Captures", videos) !=
            std::filesystem::path{"D:/Captures"}) {
        std::cerr << "Relative media paths still depend on the process working directory.\n";
        return 1;
    }

    for (const int frame_rate : std::array{30, 60, 144}) {
        float scroll_position = 0.0F;
        const int frame_count = (frame_rate * 3 + 9) / 10;
        for (int frame = 0; frame < frame_count; ++frame) {
            scroll_position = pip_link::core::advance_smooth_scroll(
                scroll_position, 100.0F, 1.0F / static_cast<float>(frame_rate));
        }
        if (scroll_position != 100.0F) {
            std::cerr << "Smooth scrolling still has a frame-rate-dependent settling tail.\n";
            return 1;
        }
    }

    for (const int frame_rate : std::array{30, 60, 144, 240}) {
        pip_link::core::PeriodicSampler sampler{0.5F};
        int samples = 0;
        for (int frame = 0; frame < frame_rate * 60; ++frame) {
            if (sampler.tick(1.0F / static_cast<float>(frame_rate))) ++samples;
        }
        if (samples < 119 || samples > 120) {
            std::cerr << "Diagnostic history does not cover a frame-rate-independent minute: "
                      << frame_rate << " Hz produced " << samples << " samples.\n";
            return 1;
        }
    }

    pip_link::core::LogTail tail;
    tail.sync(20, 180.0F);
    if (tail.target() != 180.0F || !tail.following()) {
        std::cerr << "Initial log entries did not follow the bottom.\n";
        return 1;
    }
    tail.on_wheel(1.0F, 180.0F);
    tail.sync(21, 220.0F);
    if (tail.target() == 220.0F || tail.following()) {
        std::cerr << "Log tail pulled the user away from historical entries.\n";
        return 1;
    }
    tail.on_wheel(-1.0F, 220.0F);
    tail.set_target(220.0F, 220.0F);
    tail.sync(22, 250.0F);
    if (tail.target() != 250.0F || !tail.following()) {
        std::cerr << "New log entries did not resume bottom following.\n";
        return 1;
    }
    return 0;
}
