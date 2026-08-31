#pragma once

#include <array>

namespace pip_link::ui {

class GroundStationUi final {
public:
    void draw(float delta_seconds, float display_scale);

private:
    int active_page_{0};
    std::array<float, 8> navigation_hover_{};
};

}  // namespace pip_link::ui
