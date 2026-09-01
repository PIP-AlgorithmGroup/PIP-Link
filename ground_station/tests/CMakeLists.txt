add_executable(pip_link_core_smoke_test core_smoke_test.cpp)
target_link_libraries(pip_link_core_smoke_test PRIVATE pip_link_core)

add_test(NAME pip_link_core_smoke_test COMMAND pip_link_core_smoke_test)

if(TARGET pip_link_desktop)
    add_executable(pip_link_ui_input_test ui_input_test.cpp)
    target_link_libraries(pip_link_ui_input_test PRIVATE pip_link_desktop pip_link_imgui)
    add_test(NAME pip_link_ui_input_test COMMAND pip_link_ui_input_test)
endif()
