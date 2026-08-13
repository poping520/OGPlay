if(NOT DEFINED OGPLAY_CLI)
    message(FATAL_ERROR "OGPLAY_CLI is required")
endif()

set(_cases
    "--smoke-frames|0"
    "--smoke-frames|3x"
    "--smoke-frames"
    "--smoke-frames|1|--smoke-frames|2"
    "--library-root"
    "--library-root|one|--library-root|two"
    "--unknown")

foreach(_case IN LISTS _cases)
    string(REPLACE "|" ";" _arguments "${_case}")
    execute_process(
        COMMAND "${OGPLAY_CLI}" gui ${_arguments}
        RESULT_VARIABLE _result
        OUTPUT_QUIET
        ERROR_QUIET)
    if(_result EQUAL 0)
        message(FATAL_ERROR "invalid gui options were accepted: ${_arguments}")
    endif()
endforeach()
