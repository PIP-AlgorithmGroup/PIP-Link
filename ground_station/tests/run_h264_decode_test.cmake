execute_process(
    COMMAND "${FFMPEG_EXECUTABLE}"
            -hide_banner -loglevel error -y -loop 1 -i "${TEST_IMAGE}"
            -frames:v 40 -vf scale=640:360 -c:v libx264 -preset ultrafast
            -tune zerolatency -x264-params aud=1:keyint=30:repeat-headers=1
            -f h264 "${H264_OUTPUT}"
    RESULT_VARIABLE encode_result
)
if(NOT encode_result EQUAL 0)
    message(FATAL_ERROR "Unable to generate the H.264 test stream")
endif()

execute_process(
    COMMAND "${TEST_EXECUTABLE}" "${H264_OUTPUT}"
    RESULT_VARIABLE test_result
    OUTPUT_VARIABLE test_output
    ERROR_VARIABLE test_error
)
file(REMOVE "${H264_OUTPUT}")
if(NOT test_result EQUAL 0)
    message(FATAL_ERROR "H.264 pipeline test failed: ${test_output}${test_error}")
endif()
