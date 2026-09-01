cmake_minimum_required(VERSION 3.24)

project(
    PIPLinkGroundStation
    VERSION 0.1.0
    DESCRIPTION "PIP-Link C++ first-person robot control client"
    LANGUAGES CXX
)

option(PIP_LINK_BUILD_TESTS "Build PIP-Link client tests" ON)
option(PIP_LINK_BUILD_GUI "Build the SDL3 + Dear ImGui desktop shell" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

add_library(pip_link_core STATIC ground_station/src/core/build_info.cpp)

target_include_directories(pip_link_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/ground_station/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_definitions(pip_link_core
    PUBLIC
        PIP_LINK_VERSION="${PROJECT_VERSION}"
        $<$<PLATFORM_ID:Windows>:UNICODE>
        $<$<PLATFORM_ID:Windows>:_UNICODE>
        $<$<PLATFORM_ID:Windows>:NOMINMAX>
        $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
)

if(MSVC)
    target_compile_options(pip_link_core PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(pip_link_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(PIP_LINK_BUILD_GUI)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.2.16
        GIT_SHALLOW TRUE
        GIT_SUBMODULES ""
    )
    FetchContent_MakeAvailable(SDL3)

    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.91.9b
        GIT_SHALLOW TRUE
        GIT_SUBMODULES ""
    )
    FetchContent_MakeAvailable(imgui)

    # ImGui 1.91.9b uses a 0.8s visible / 0.4s hidden text-cursor cycle.
    set(imgui_widgets_file "${imgui_SOURCE_DIR}/imgui_widgets.cpp")
    file(READ "${imgui_widgets_file}" imgui_widgets_source)
    set(imgui_cursor_blink_default
        "ImFmod(state->CursorAnim, 1.20f) <= 0.80f")
    set(imgui_cursor_blink_equal
        "ImFmod(state->CursorAnim, 1.00f) <= 0.50f")
    string(FIND "${imgui_widgets_source}" "${imgui_cursor_blink_default}"
           imgui_cursor_blink_default_index)
    if(NOT imgui_cursor_blink_default_index EQUAL -1)
        string(REPLACE "${imgui_cursor_blink_default}" "${imgui_cursor_blink_equal}"
               imgui_widgets_source "${imgui_widgets_source}")
        file(WRITE "${imgui_widgets_file}" "${imgui_widgets_source}")
    else()
        string(FIND "${imgui_widgets_source}" "${imgui_cursor_blink_equal}"
               imgui_cursor_blink_equal_index)
        if(imgui_cursor_blink_equal_index EQUAL -1)
            message(FATAL_ERROR "Unsupported ImGui text-cursor blink implementation")
        endif()
    endif()

    add_library(pip_link_imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    )
    target_include_directories(pip_link_imgui PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )
    target_link_libraries(pip_link_imgui PUBLIC SDL3::SDL3 d3d11 dxgi d3dcompiler)

    add_library(pip_link_desktop STATIC
        ground_station/src/app/application.cpp
        ground_station/src/backend/media_pipeline.cpp
        ground_station/src/backend/protocol_codec.cpp
        ground_station/src/backend/ground_station_backend_runtime.cpp
        ground_station/src/backend/ground_station_backend_stub.cpp
        ground_station/src/platform/desktop_window.cpp
        ground_station/src/ui/control_input_mapping.cpp
        ground_station/src/ui/ground_station_ui.cpp
        ground_station/src/ui/ground_station_settings.cpp
    )
    target_include_directories(pip_link_desktop PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/ground_station/include>
    )
    target_link_libraries(pip_link_desktop
        PUBLIC pip_link_core
        PRIVATE pip_link_imgui SDL3::SDL3 d3d11 dxgi ole32 windowscodecs
                mf mfplat mfuuid mfreadwrite shlwapi ws2_32 dnsapi shell32
    )
    if(MSVC)
        target_compile_options(pip_link_desktop PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(pip_link_desktop PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_executable(pip_link_ground_station ground_station/src/main.cpp)
    target_link_libraries(pip_link_ground_station PRIVATE pip_link_desktop)
    if(MINGW)
        target_link_options(pip_link_ground_station PRIVATE
            -static
            -static-libgcc
            -static-libstdc++
        )
    endif()
    install(TARGETS pip_link_ground_station RUNTIME DESTINATION bin)
endif()

if(PIP_LINK_BUILD_TESTS)
    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(ground_station/tests)
    endif()
endif()
