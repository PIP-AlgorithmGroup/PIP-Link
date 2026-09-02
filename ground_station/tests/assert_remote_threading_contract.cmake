file(READ "${REMOTE_LINK_HEADER}" node_header)
file(READ "${REMOTE_LINK_SOURCE}" node_source)
file(READ "${MDNS_HEADER}" mdns_header)
file(READ "${MDNS_SOURCE}" mdns_source)

foreach(required IN ITEMS
        "std::atomic<double> client_timeout_s_"
        "std::atomic<bool> debug_verbose_")
    string(FIND "${node_header}" "${required}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Remote node is missing synchronized parameter state: ${required}")
    endif()
endforeach()

foreach(required IN ITEMS
        "msg->step < minimum_step"
        "msg->encoding == \"rgb8\""
        "cv::COLOR_RGBA2BGR")
    string(FIND "${node_source}" "${required}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Remote frame validation is missing: ${required}")
    endif()
endforeach()

string(FIND "${mdns_header}" "std::mutex         avahi_poll_mutex_" mdns_mutex)
string(FIND "${mdns_source}" "std::lock_guard lock(avahi_poll_mutex_)" mdns_lock)
if(mdns_mutex EQUAL -1 OR mdns_lock EQUAL -1)
    message(FATAL_ERROR "mDNS poll lifetime is not synchronized")
endif()
