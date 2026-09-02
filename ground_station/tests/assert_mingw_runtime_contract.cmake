file(READ "${ROOT_CMAKE}" root_cmake)
file(READ "${DESKTOP_WINDOW_SOURCE}" desktop_window_source)

string(FIND "${root_cmake}" "ground_station/resources/app_icon.rc" app_icon_resource)
if(app_icon_resource EQUAL -1)
    message(FATAL_ERROR "Ground-station executable icon resource is not wired")
endif()

string(FIND "${root_cmake}" "OBJECT_DEPENDS" app_icon_dependency)
if(app_icon_dependency EQUAL -1)
    message(FATAL_ERROR "Ground-station executable icon resource does not track icon.ico")
endif()

string(FIND "${desktop_window_source}" "SDL_SetWindowIcon" window_icon_call)
if(window_icon_call EQUAL -1)
    message(FATAL_ERROR "Ground-station window icon is not wired")
endif()

foreach(required IN ITEMS "if(MINGW)" "-static" "-static-libgcc" "-static-libstdc++")
    string(FIND "${root_cmake}" "${required}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "MinGW standalone runtime contract is missing: ${required}")
    endif()
endforeach()
