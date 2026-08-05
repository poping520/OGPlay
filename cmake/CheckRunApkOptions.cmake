if(NOT DEFINED OGPLAY_CLI OR NOT DEFINED ROOT)
    message(FATAL_ERROR "OGPLAY_CLI and ROOT are required")
endif()

set(_missing_apk "${ROOT}/.local/run-apk-options-missing.apk")
set(_missing_system "${ROOT}/.local/run-apk-options-missing-system")

function(expect_failure label expected)
    execute_process(
        COMMAND "${OGPLAY_CLI}" ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(NOT result EQUAL 1)
        message(FATAL_ERROR "${label}: expected exit 1, got ${result}; stdout=${output}; stderr=${error}")
    endif()
    string(FIND "${error}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "${label}: missing '${expected}' in stderr: ${error}")
    endif()
endfunction()

set(_base run-apk "${_missing_apk}" --system-dir "${_missing_system}")
expect_failure(zero "--supersample requires an integer in 1..4"
    ${_base} --supersample 0)
expect_failure(too_large "--supersample requires an integer in 1..4"
    ${_base} --supersample 5)
expect_failure(malformed "--supersample requires an integer in 1..4"
    ${_base} --supersample 2x)
expect_failure(missing_value "unknown or incomplete run-apk option: --supersample"
    ${_base} --supersample)
expect_failure(valid_one "cannot open" ${_base} --supersample 1)
expect_failure(valid_two "cannot open" ${_base} --supersample 2)
expect_failure(valid_four "cannot open" ${_base} --supersample 4)
expect_failure(default_one "cannot open" ${_base})
