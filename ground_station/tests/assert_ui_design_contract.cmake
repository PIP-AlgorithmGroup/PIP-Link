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
string(FIND "${ui_source}" "void draw_mouse_diagram(" mouse_diagram)
string(FIND "${ui_source}" "float movement_x, float movement_y" embedded_movement)
string(FIND "${ui_source}" "\"MOTION\"" external_motion_widget)
string(FIND "${ui_source}" "const ImVec2 movement_tail" centered_arrow)
string(FIND "${ui_source}" "draw->AddRectFilled({side_button_min.x, top}" side_background)
string(FIND "${ui_source}" "draw->AddRectFilled(mouse_body_min, mouse_body_max" mouse_body)
string(FIND "${ui_source}" "side_button_min.x + 2.0F * unit" shifted_side_label)
string(FIND "${ui_source}" "side_button_indices{4, 3}" swapped_side_buttons)
string(FIND "${ui_source}" "part_color(activity[5])" wheel_up_fill)
string(FIND "${ui_source}" "part_color(activity[6])" wheel_down_fill)
string(FIND "${ui_source}" "wheel_outline_color = part_color(activity[1])" middle_click_outline)
string(FIND "${ui_source}" "const float panel_w = 300.0F * unit;" compact_input_width)
string(FIND "${ui_source}" "const float panel_h = 148.0F * unit;" compact_input_height)
string(FIND "${ui_source}" "const float divider_x" horizontal_input_layout)
if(wheel_activity EQUAL -1 OR NOT font_wheel_up EQUAL -1 OR
   NOT font_wheel_down EQUAL -1 OR mouse_diagram EQUAL -1 OR
   embedded_movement EQUAL -1 OR NOT external_motion_widget EQUAL -1 OR
   centered_arrow EQUAL -1 OR side_background EQUAL -1 OR mouse_body EQUAL -1 OR
   side_background GREATER mouse_body OR shifted_side_label EQUAL -1 OR
   swapped_side_buttons EQUAL -1 OR wheel_up_fill EQUAL -1 OR
   wheel_down_fill EQUAL -1 OR middle_click_outline EQUAL -1 OR
   compact_input_width EQUAL -1 OR compact_input_height EQUAL -1 OR
   horizontal_input_layout EQUAL -1)
    message(FATAL_ERROR "Mouse diagram layering or centered motion arrow is incomplete")
endif()
