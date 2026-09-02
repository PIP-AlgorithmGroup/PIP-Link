file(READ "${DESKTOP_SOURCE}" desktop_source)
file(READ "${RUNTIME_SOURCE}" runtime_source)
file(READ "${UI_SOURCE}" ui_source)
file(READ "${MEDIA_SOURCE}" media_source)

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
        "AddCircleFilled")
    string(FIND "${ui_source}" "${required_text}" required_position)
    if(required_position EQUAL -1)
        message(FATAL_ERROR "Recording visual is missing: ${required_text}")
    endif()
endforeach()

string(FIND "${desktop_source}" "MouseDrawCursor" software_cursor)
if(NOT software_cursor EQUAL -1)
    message(FATAL_ERROR "Recording must preserve the low-latency native OS cursor")
endif()

string(FIND "${ui_source}" "AddRectFilledMultiColor" edge_gradient)
if(NOT edge_gradient EQUAL -1)
    message(FATAL_ERROR "Recording edge gradient was not removed")
endif()

foreach(required_banner_text "录制中" "recording_overlay_started_at_" "AddTriangleFilled")
    string(FIND "${ui_source}" "${required_banner_text}" banner_position)
    if(banner_position EQUAL -1)
        message(FATAL_ERROR "Recording banner/cursor marker is missing: ${required_banner_text}")
    endif()
endforeach()

string(FIND "${media_source}" "-pixel_format rgba" rgba_input)
if(rgba_input EQUAL -1)
    message(FATAL_ERROR "DXGI R8G8B8A8 capture is not declared as RGBA to FFmpeg")
endif()

string(FIND "${runtime_source}" "rgba_to_bgra_for_png" png_channel_conversion)
if(png_channel_conversion EQUAL -1)
    message(FATAL_ERROR "RGBA back-buffer screenshots are not converted for WIC BGRA")
endif()

string(FIND "${desktop_source}" "D3D11_MAP_FLAG_DO_NOT_WAIT" nonblocking_map)
if(nonblocking_map EQUAL -1)
    message(FATAL_ERROR "Composited capture still blocks the render thread on GPU readback")
endif()

string(FIND "${desktop_source}" "capture_interval = std::chrono::milliseconds(33)"
       bounded_capture_rate)
if(bounded_capture_rate EQUAL -1)
    message(FATAL_ERROR "Full-window recording capture rate is not bounded to 30 FPS")
endif()
