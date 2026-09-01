file(GLOB_RECURSE remote_link_files
     "${REMOTE_LINK_SOURCE}/CMakeLists.txt"
     "${REMOTE_LINK_SOURCE}/package.xml"
     "${REMOTE_LINK_SOURCE}/*.hpp"
     "${REMOTE_LINK_SOURCE}/*.cpp")

foreach(source_file IN LISTS remote_link_files)
    file(READ "${source_file}" source_text)
    string(CONCAT legacy_package "pip_vision" "_interfaces")
    string(FIND "${source_text}" "${legacy_package}" old_package_index)
    if(NOT old_package_index EQUAL -1)
        message(FATAL_ERROR "Legacy ROS message package remains in ${source_file}")
    endif()
endforeach()

set(required_files
    "${REMOTE_LINK_SOURCE}/CMakeLists.txt"
    "${REMOTE_LINK_SOURCE}/package.xml"
    "${REMOTE_LINK_SOURCE}/include/remote_link/remote_link_node.hpp"
    "${REMOTE_LINK_SOURCE}/src/remote_link_node.cpp")

foreach(source_file IN LISTS required_files)
    file(READ "${source_file}" source_text)
    string(FIND "${source_text}" "pip_msgs" new_package_index)
    if(new_package_index EQUAL -1)
        message(FATAL_ERROR "pip_msgs dependency is missing from ${source_file}")
    endif()
endforeach()
