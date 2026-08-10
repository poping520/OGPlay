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

function(expect_usage expected)
    execute_process(
        COMMAND "${OGPLAY_CLI}"
        RESULT_VARIABLE result
        ERROR_VARIABLE error
    )
    if(NOT result EQUAL 2)
        message(FATAL_ERROR "usage: expected exit 2, got ${result}; stderr=${error}")
    endif()
    string(FIND "${error}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "usage: missing '${expected}' in stderr: ${error}")
    endif()
endfunction()

set(_base run-apk "${_missing_apk}" --system-dir "${_missing_system}")
expect_usage("[--mcp | --mcp-port <1..65535>]")
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
expect_failure(external_missing "unknown or incomplete run-apk option: --external-dir"
    ${_base} --external-dir)
expect_failure(external_once "run-apk accepts --external-dir only once"
    ${_base} --external-dir first --external-dir second)
expect_failure(external_valid "cannot open"
    ${_base} --external-dir host-data)
expect_failure(mcp_zero "--mcp-port requires an integer in 1..65535"
    ${_base} --mcp-port 0)
expect_failure(mcp_too_large "--mcp-port requires an integer in 1..65535"
    ${_base} --mcp-port 65536)
expect_failure(mcp_malformed "--mcp-port requires an integer in 1..65535"
    ${_base} --mcp-port 3000x)
expect_failure(mcp_missing "unknown or incomplete run-apk option: --mcp-port"
    ${_base} --mcp-port)
expect_failure(mcp_duplicate "run-apk accepts --mcp-port only once"
    ${_base} --mcp-port 3000 --mcp-port 3001)
expect_failure(mcp_preflight "--mcp-port cannot be combined with --preflight"
    ${_base} --mcp-port 3000 --preflight)
expect_failure(mcp_valid "cannot open" ${_base} --mcp-port 3000)
expect_failure(mcp_default_duplicate "run-apk accepts --mcp only once"
    ${_base} --mcp --mcp)
expect_failure(mcp_default_preflight "--mcp cannot be combined with --preflight"
    ${_base} --mcp --preflight)
expect_failure(mcp_default_custom_conflict "--mcp and --mcp-port cannot be combined"
    ${_base} --mcp --mcp-port 3000)
expect_failure(mcp_custom_default_conflict "--mcp and --mcp-port cannot be combined"
    ${_base} --mcp-port 3000 --mcp)
expect_failure(mcp_default_valid "cannot open" ${_base} --mcp)
