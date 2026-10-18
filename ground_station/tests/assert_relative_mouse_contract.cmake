file(READ "${DESKTOP_WINDOW_SOURCE}" desktop_window_source)
file(READ "${UI_SOURCE}" ui_source)
file(READ "${UI_HEADER}" ui_header)

foreach(required IN ITEMS
        "SDL_EVENT_MOUSE_MOTION"
        "SDL_GetWindowRelativeMouseMode(impl_->window)"
        "event.motion.xrel"
        "event.motion.yrel"
        "impl_->ui->add_relative_mouse_delta")
    string(FIND "${desktop_window_source}" "${required}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Relative mouse input is not forwarded from SDL events: ${required}")
    endif()
endforeach()

foreach(required IN ITEMS
        "add_relative_mouse_delta(float x, float y) noexcept"
        "relative_mouse_delta_x_"
        "last_relative_mouse_delta_x_")
    string(FIND "${ui_header}" "${required}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "UI relative mouse input state is missing: ${required}")
    endif()
endforeach()

string(FIND "${ui_source}"
       "input.mouse_delta_x = relative_mouse_delta_x * mouse_sensitivity_"
       uses_relative_delta)
if(uses_relative_delta EQUAL -1)
    message(FATAL_ERROR "READY control input does not use accumulated relative mouse motion")
endif()

string(FIND "${ui_source}" "io.MouseDelta.x * mouse_sensitivity_" uses_absolute_delta)
if(NOT uses_absolute_delta EQUAL -1)
    message(FATAL_ERROR "READY control input still derives mouse motion from ImGui coordinates")
endif()
