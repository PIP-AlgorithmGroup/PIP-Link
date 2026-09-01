file(READ "${ROOT_CMAKE}" root_cmake)

foreach(required IN ITEMS "if(MINGW)" "-static" "-static-libgcc" "-static-libstdc++")
    string(FIND "${root_cmake}" "${required}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "MinGW standalone runtime contract is missing: ${required}")
    endif()
endforeach()
