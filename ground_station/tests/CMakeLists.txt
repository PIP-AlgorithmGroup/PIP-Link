add_executable(pip_link_core_smoke_test core_smoke_test.cpp)
target_link_libraries(pip_link_core_smoke_test PRIVATE pip_link_core)

add_test(NAME pip_link_core_smoke_test COMMAND pip_link_core_smoke_test)

add_test(
    NAME pip_link_ros_message_package_test
    COMMAND ${CMAKE_COMMAND}
            -DREMOTE_LINK_SOURCE=${CMAKE_SOURCE_DIR}/src/remote_link
            -P ${CMAKE_CURRENT_SOURCE_DIR}/assert_ros_message_package.cmake
)

if(TARGET pip_link_desktop)
    add_executable(pip_link_protocol_codec_test protocol_codec_test.cpp)
    target_link_libraries(pip_link_protocol_codec_test PRIVATE pip_link_desktop)
    add_test(NAME pip_link_protocol_codec_test COMMAND pip_link_protocol_codec_test)
    add_executable(pip_link_backend_loopback_test backend_loopback_test.cpp)
    target_link_libraries(pip_link_backend_loopback_test PRIVATE pip_link_desktop ws2_32)
    target_compile_definitions(pip_link_backend_loopback_test PRIVATE
        PIP_LINK_TEST_IMAGE="${CMAKE_SOURCE_DIR}/assets/icon.png"
        PIP_LINK_TEST_OUTPUT="${CMAKE_CURRENT_BINARY_DIR}/backend-loopback-output")
    add_test(NAME pip_link_backend_loopback_test COMMAND pip_link_backend_loopback_test)
    add_executable(pip_link_media_pipeline_test media_pipeline_test.cpp)
    target_include_directories(pip_link_media_pipeline_test PRIVATE
        ${CMAKE_SOURCE_DIR}/ground_station/src/backend)
    target_link_libraries(pip_link_media_pipeline_test PRIVATE pip_link_desktop)
    target_compile_definitions(pip_link_media_pipeline_test PRIVATE
        PIP_LINK_TEST_IMAGE="${CMAKE_SOURCE_DIR}/assets/icon.png"
        PIP_LINK_MEDIA_TEST_OUTPUT="${CMAKE_CURRENT_BINARY_DIR}/media-pipeline-output")
    add_test(NAME pip_link_media_pipeline_test COMMAND pip_link_media_pipeline_test)
    find_program(PIP_LINK_FFMPEG_EXECUTABLE ffmpeg)
    if(PIP_LINK_FFMPEG_EXECUTABLE)
        add_test(
            NAME pip_link_media_h264_test
            COMMAND ${CMAKE_COMMAND}
                    -DFFMPEG_EXECUTABLE=${PIP_LINK_FFMPEG_EXECUTABLE}
                    -DTEST_EXECUTABLE=$<TARGET_FILE:pip_link_media_pipeline_test>
                    -DTEST_IMAGE=${CMAKE_SOURCE_DIR}/assets/icon.png
                    -DH264_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/media-test.h264
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/run_h264_decode_test.cmake
        )
    endif()
endif()

if(TARGET pip_link_desktop)
    add_executable(pip_link_ui_input_test ui_input_test.cpp)
    target_link_libraries(pip_link_ui_input_test PRIVATE pip_link_desktop pip_link_imgui)
    add_test(NAME pip_link_ui_input_test COMMAND pip_link_ui_input_test)
    add_executable(pip_link_p1_regression_test p1_regression_test.cpp)
    target_link_libraries(pip_link_p1_regression_test PRIVATE pip_link_desktop pip_link_imgui)
    add_test(NAME pip_link_p1_regression_test COMMAND pip_link_p1_regression_test)
    add_test(
        NAME pip_link_backend_authoritative_state_test
        COMMAND ${CMAKE_COMMAND}
                -DSETTINGS_SOURCE=${CMAKE_SOURCE_DIR}/ground_station/src/ui/ground_station_settings.cpp
                -P ${CMAKE_CURRENT_SOURCE_DIR}/assert_backend_authoritative_state.cmake
    )
    add_test(
        NAME pip_link_imgui_cursor_blink_test
        COMMAND ${CMAKE_COMMAND}
                -DIMGUI_WIDGETS_FILE=${imgui_SOURCE_DIR}/imgui_widgets.cpp
                -P ${CMAKE_CURRENT_SOURCE_DIR}/assert_imgui_cursor_blink.cmake
    )
endif()
