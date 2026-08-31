cmake_minimum_required(VERSION 3.24)

project(
    PIPLinkGroundStation
    VERSION 0.1.0
    DESCRIPTION "PIP-Link C++ ground station"
    LANGUAGES CXX
)

option(PIP_LINK_BUILD_TESTS "Build PIP-Link ground station tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(pip_link_core STATIC
    ground_station/src/app/application.cpp
    ground_station/src/core/build_info.cpp
)

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

add_executable(pip_link_ground_station ground_station/src/main.cpp)
target_link_libraries(pip_link_ground_station PRIVATE pip_link_core)

if(PIP_LINK_BUILD_TESTS)
    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(ground_station/tests)
    endif()
endif()

install(TARGETS pip_link_ground_station RUNTIME DESTINATION bin)
