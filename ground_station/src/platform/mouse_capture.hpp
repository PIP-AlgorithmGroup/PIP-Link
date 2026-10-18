#pragma once

struct SDL_Window;

namespace pip_link::platform {

bool set_relative_mouse_capture(SDL_Window* window, bool enabled);

}  // namespace pip_link::platform
