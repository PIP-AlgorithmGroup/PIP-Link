file(READ "${REMOTE_LINK_NODE_SOURCE}" node_source)
file(READ "${VIDEO_SENDER_SOURCE}" sender_source)
file(READ "${FRAME_ENCODER_SOURCE}" encoder_source)

foreach(required IN ITEMS
        "declare_parameter(\"udp_mtu\""
        "j[\"udp_mtu\"]"
        "cfg.udp_mtu")
    string(FIND "${node_source}" "${required}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "remote transport contract is missing: ${required}")
    endif()
endforeach()

string(FIND "${sender_source}" "k_parity = 1" forced_single_parity)
if(NOT forced_single_parity EQUAL -1)
    message(FATAL_ERROR "FEC redundancy is still forced to a single parity chunk")
endif()

string(FIND "${sender_source}" "cfg_.udp_mtu" mtu_usage)
if(mtu_usage EQUAL -1)
    message(FATAL_ERROR "VideoSender does not apply udp_mtu")
endif()

string(FIND "${encoder_source}" "quality_ = std::clamp(cfg.quality" quality_update)
if(quality_update EQUAL -1)
    message(FATAL_ERROR "FrameEncoder does not apply JPEG quality updates")
endif()

string(FIND "${encoder_source}" "repeat-headers=1" repeated_h264_headers)
if(repeated_h264_headers EQUAL -1)
    message(FATAL_ERROR "H.264 keyframes do not repeat SPS/PPS for mid-stream recording")
endif()
