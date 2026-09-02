file(READ "${DESKTOP_SOURCE}" desktop_source)
file(READ "${RUNTIME_SOURCE}" runtime_source)
file(READ "${UI_SOURCE}" ui_source)

string(FIND "${desktop_source}" "ImGui_ImplDX11_RenderDrawData" render_position)
string(FIND "${desktop_source}" "capture_composited_frame();" capture_position)
string(FIND "${desktop_source}" "swap_chain->Present" present_position)
if(render_position EQUAL -1 OR capture_position EQUAL -1 OR present_position EQUAL -1 OR
   NOT render_position LESS capture_position OR NOT capture_position LESS present_position)
    message(FATAL_ERROR "Composited capture must happen after UI rendering and before Present")
endif()

string(FIND "${runtime_source}" "recorder_.write(codec, encoded)" raw_stream_recording)
if(NOT raw_stream_recording EQUAL -1)
    message(FATAL_ERROR "Runtime still records the remote encoded stream")
endif()

foreach(required_text
        "draw_recording_overlay"
        "AddRectFilledMultiColor"
        "AddCircleFilled")
    string(FIND "${ui_source}" "${required_text}" required_position)
    if(required_position EQUAL -1)
        message(FATAL_ERROR "Recording visual is missing: ${required_text}")
    endif()
endforeach()

string(FIND "${desktop_source}" "io.MouseDrawCursor = impl_->backend->needs_composited_frame()"
       software_cursor)
if(software_cursor EQUAL -1)
    message(FATAL_ERROR "Captured frames do not include the software mouse cursor")
endif()
