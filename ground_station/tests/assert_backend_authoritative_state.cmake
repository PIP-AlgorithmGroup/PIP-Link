if(NOT DEFINED SETTINGS_SOURCE)
    message(FATAL_ERROR "SETTINGS_SOURCE is required")
endif()

file(READ "${SETTINGS_SOURCE}" settings_source)
string(REPLACE "==" "comparison" settings_without_comparisons "${settings_source}")
foreach(forbidden_assignment
        "connection_state_ ="
        "recording_state_ ="
        "connected_ = true"
        "recording_ = true")
    string(FIND "${settings_without_comparisons}" "${forbidden_assignment}" assignment_index)
    if(NOT assignment_index EQUAL -1)
        message(FATAL_ERROR
                "Settings actions must not mutate backend-authoritative state: ${forbidden_assignment}")
    endif()
endforeach()
