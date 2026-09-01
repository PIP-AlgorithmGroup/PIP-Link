add_executable(pip_link_core_smoke_test core_smoke_test.cpp)
target_link_libraries(pip_link_core_smoke_test PRIVATE pip_link_core)

add_test(NAME pip_link_core_smoke_test COMMAND pip_link_core_smoke_test)

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
