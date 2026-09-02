file(READ "${SETTINGS_SOURCE}" settings_source)
file(READ "${UI_SOURCE}" ui_source)
file(READ "${UI_HEADER}" ui_header)

foreach(forbidden IN ITEMS "ImGui::Combo(" "ImGui::Selectable(" "ImGuiTableFlags_ScrollY")
    string(FIND "${settings_source}" "${forbidden}" found)
    if(NOT found EQUAL -1)
        message(FATAL_ERROR "Settings UI still uses a forbidden native widget: ${forbidden}")
    endif()
endforeach()

string(FIND "${settings_source}" "##RecordingDirectory" editable_recording_path)
if(NOT editable_recording_path EQUAL -1)
    message(FATAL_ERROR "Recording directory must use a folder picker, not text input")
endif()

string(FIND "${settings_source}" "choose_recording_directory(" folder_picker)
if(folder_picker EQUAL -1)
    message(FATAL_ERROR "Recording page does not expose a native folder picker")
endif()

string(FIND "${ui_header}" "diagnostics_sampler_{0.5F}" one_minute_sampler)
string(FIND "${ui_header}" "std::array<float, 120> fps_history_" one_minute_history)
if(one_minute_sampler EQUAL -1 OR one_minute_history EQUAL -1)
    message(FATAL_ERROR "Diagnostic history must contain 120 samples over 60 seconds")
endif()

foreach(required IN ITEMS
        "animated_combo("
        "advance_smooth_scroll("
        "##AuditRows")
    string(FIND "${settings_source}" "${required}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Settings UI design contract is missing: ${required}")
    endif()
endforeach()

string(FIND "${ui_source}" "if (!has_video)" no_video)
string(FIND "${ui_source}" "const float unit = scale * animated_hud_scale_" hud)
if(no_video EQUAL -1 OR hud EQUAL -1 OR hud LESS no_video)
    message(FATAL_ERROR "HUD must be rendered after the no-video state")
endif()

string(FIND "${ui_header}" "std::array<float, 7> mouse_input_activity_" wheel_activity)
string(FIND "${ui_source}" "\"↑\"" font_wheel_up)
string(FIND "${ui_source}" "\"↓\"" font_wheel_down)
string(FIND "${ui_source}" "const ImVec2 mouse_body_min" mouse_diagram)
if(wheel_activity EQUAL -1 OR NOT font_wheel_up EQUAL -1 OR
   NOT font_wheel_down EQUAL -1 OR mouse_diagram EQUAL -1)
    message(FATAL_ERROR "Mouse inputs must use a font-independent mouse diagram")
endif()
