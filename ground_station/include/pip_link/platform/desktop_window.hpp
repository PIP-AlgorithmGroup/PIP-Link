#pragma once

#include <memory>

namespace pip_link::platform {

class DesktopWindow final {
public:
    DesktopWindow();
    ~DesktopWindow();

    DesktopWindow(const DesktopWindow&) = delete;
    DesktopWindow& operator=(const DesktopWindow&) = delete;
    DesktopWindow(DesktopWindow&&) noexcept;
    DesktopWindow& operator=(DesktopWindow&&) noexcept;

    [[nodiscard]] int run();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace pip_link::platform
