file(READ "${SETTINGS_SOURCE}" settings_source)
file(READ "${UI_SOURCE}" ui_source)

foreach(forbidden IN ITEMS "ImGui::Combo(" "ImGui::Selectable(" "ImGuiTableFlags_ScrollY")
    string(FIND "${settings_source}" "${forbidden}" found)
    if(NOT found EQUAL -1)
        message(FATAL_ERROR "Settings UI still uses a forbidden native widget: ${forbidden}")
    endif()
endforeach()

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
