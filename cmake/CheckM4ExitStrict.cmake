if(NOT DEFINED OGPLAY_TESTS)
    message(FATAL_ERROR "OGPLAY_TESTS is required")
endif()

function(expect_missing_fixture_failure name expected)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            --unset=OGPLAY_BIONIC_ORACLE_ROOT
            --unset=OGPLAY_MINIMAL_NDK_APK
            --unset=OGPLAY_M4_EXIT_APK
            OGPLAY_REQUIRE_M4_EXIT=1
            "${OGPLAY_TESTS}" "--test-case=${name}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(result EQUAL 0)
        message(FATAL_ERROR "${name}: strict exit unexpectedly passed without fixtures")
    endif()
    string(FIND "${output}${error}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "${name}: missing strict failure '${expected}': ${output}${error}")
    endif()
endfunction()

expect_missing_fixture_failure(
    "minimal APK NativeActivity renders and responds to guest input"
    "strict M4 exit requires Bionic oracle and minimal APK")
expect_missing_fixture_failure(
    "M4 exit APK renders and responds to key and pointer input"
    "strict M4 exit requires Bionic oracle and M4 exit APK")
