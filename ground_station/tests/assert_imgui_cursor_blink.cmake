if(NOT DEFINED IMGUI_WIDGETS_FILE OR NOT EXISTS "${IMGUI_WIDGETS_FILE}")
    message(FATAL_ERROR "ImGui widget source was not found")
endif()

file(READ "${IMGUI_WIDGETS_FILE}" imgui_widgets_source)
string(FIND "${imgui_widgets_source}"
       "ImFmod(state->CursorAnim, 1.00f) <= 0.50f"
       equal_cursor_blink_index)
if(equal_cursor_blink_index EQUAL -1)
    message(FATAL_ERROR "Text cursor blink must use equal 0.5s intervals")
endif()
