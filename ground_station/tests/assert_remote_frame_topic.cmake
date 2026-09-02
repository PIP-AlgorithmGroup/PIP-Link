file(READ "${REMOTE_LINK_NODE_SOURCE}" remote_source)
file(READ "${TEST_FRAME_NODE_SOURCE}" test_source)

foreach(parameter_name IN ITEMS frame_topic command_topic stats_topic)
    string(FIND "${remote_source}" "\"${parameter_name}\"" parameter_index)
    if(parameter_index EQUAL -1)
        message(FATAL_ERROR "remote_link is missing ${parameter_name}")
    endif()
endforeach()

string(FIND "${remote_source}" "topic_descriptor.read_only = true" read_only_index)
if(read_only_index EQUAL -1)
    message(FATAL_ERROR "remote_link topic parameters must be read-only")
endif()

foreach(binding IN ITEMS
        "command_topic, rclcpp::QoS(10)"
        "frame_topic, qos"
        "stats_topic, rclcpp::QoS(10)")
    string(FIND "${remote_source}" "${binding}" binding_index)
    if(binding_index EQUAL -1)
        message(FATAL_ERROR "ROS endpoint is not bound from parameter: ${binding}")
    endif()
endforeach()

string(FIND "${test_source}" "std::string(\"/io/video_frame\")" test_default_index)
if(test_default_index EQUAL -1)
    message(FATAL_ERROR "test frame publisher must default to /io/video_frame")
endif()
