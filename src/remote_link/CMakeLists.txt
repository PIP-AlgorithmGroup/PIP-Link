cmake_minimum_required(VERSION 3.8)
project(remote_link)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(pip_vision_interfaces REQUIRED)
find_package(OpenCV 4 REQUIRED)
find_package(ZLIB REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(AVAHI REQUIRED avahi-client)

# nlohmann_json: try ament-packaged form first, fallback to system header
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
  pkg_check_modules(NLOHMANN_JSON REQUIRED nlohmann_json)
endif()

# FFmpeg (H.264 Phase 3 – link now so headers are available)
pkg_check_modules(AVCODEC REQUIRED libavcodec)
pkg_check_modules(AVUTIL  REQUIRED libavutil)
pkg_check_modules(SWSCALE REQUIRED libswscale)

set(SOURCES
  src/protocol_codec.cpp
  src/mdns_service.cpp
  src/control_receiver.cpp
  src/video_sender.cpp
  src/frame_encoder.cpp
  src/remote_link_node.cpp
)

add_library(${PROJECT_NAME} SHARED ${SOURCES})

target_include_directories(${PROJECT_NAME} PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
  ${OpenCV_INCLUDE_DIRS}
  ${AVAHI_INCLUDE_DIRS}
  ${AVCODEC_INCLUDE_DIRS}
  ${SWSCALE_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME}
  ${OpenCV_LIBS}
  ${AVAHI_LIBRARIES}
  ZLIB::ZLIB
  ${AVCODEC_LIBRARIES}
  ${AVUTIL_LIBRARIES}
  ${SWSCALE_LIBRARIES}
)

if(nlohmann_json_FOUND)
  target_link_libraries(${PROJECT_NAME} nlohmann_json::nlohmann_json)
else()
  target_include_directories(${PROJECT_NAME} PRIVATE ${NLOHMANN_JSON_INCLUDE_DIRS})
endif()

ament_target_dependencies(${PROJECT_NAME}
  rclcpp
  rclcpp_components
  sensor_msgs
  std_msgs
  pip_vision_interfaces
)

rclcpp_components_register_node(${PROJECT_NAME}
  PLUGIN "remote_link::RemoteLinkNode"
  EXECUTABLE remote_link_node
)

install(TARGETS ${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)
install(DIRECTORY include/ DESTINATION include)

ament_package()
