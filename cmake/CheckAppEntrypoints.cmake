if(NOT DEFINED OGPLAY_CLI OR NOT DEFINED ROOT)
    message(FATAL_ERROR "OGPLAY_CLI and ROOT are required")
endif()

function(expect_entry executable expected_code expected_text)
    execute_process(COMMAND "${executable}" ${ARGN}
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 5)
    if(NOT "${result}" STREQUAL "${expected_code}")
        message(FATAL_ERROR "${executable} ${ARGN}: exit ${result}, expected ${expected_code}; ${output}${error}")
    endif()
    string(FIND "${output}${error}" "${expected_text}" found)
    if(found EQUAL -1)
        string(SUBSTRING "${output}${error}" 0 1000 detail)
        message(FATAL_ERROR "${executable} ${ARGN}: missing '${expected_text}'; ${detail}")
    endif()
endfunction()

expect_entry("${OGPLAY_CLI}" 0 "OGPlay " --version)
expect_entry("${OGPLAY_CLI}" 0 "usage: ogplay-cli" --help)
expect_entry("${OGPLAY_CLI}" 2 "usage: ogplay-cli")
expect_entry("${OGPLAY_CLI}" 2 "usage: ogplay-cli" gui)
expect_entry("${OGPLAY_CLI}" 2 "usage: ogplay-cli" unknown)
expect_entry("${OGPLAY_CLI}" 0 "agent.control_service" capabilities)
get_filename_component(entry_directory "${OGPLAY_CLI}" DIRECTORY)
set(fixtures "${entry_directory}/entrypoint-checks")
file(MAKE_DIRECTORY "${fixtures}")
configure_file("${ROOT}/capabilities.toml" "${fixtures}/能力 账本.toml" COPYONLY)
expect_entry("${OGPLAY_CLI}" 0 "agent.control_service" capabilities "${fixtures}/能力 账本.toml")
file(WRITE "${fixtures}/request.jsonl" "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"session.open\"}\n")
execute_process(COMMAND "${OGPLAY_CLI}" agent-stdio INPUT_FILE "${fixtures}/request.jsonl"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 5)
string(FIND "${output}" "\"result\"" found)
if(NOT result EQUAL 0 OR found EQUAL -1)
    message(FATAL_ERROR "DLL stdio transport failed: ${result}; ${output}${error}")
endif()

if(DEFINED OGPLAY_GUI AND NOT "${OGPLAY_GUI}" STREQUAL "")
    expect_entry("${OGPLAY_GUI}" 0 "OGPlay " --version)
    expect_entry("${OGPLAY_GUI}" 0 "usage: ogplay" --help)
    expect_entry("${OGPLAY_GUI}" 1 "run-apk requires <apk>" --run-game)
    expect_entry("${OGPLAY_GUI}" 1 "unknown or incomplete gui option" run-apk)
    # Exercise the full existing invalid-option matrix through the real game mode.
    execute_process(COMMAND "${CMAKE_COMMAND}"
        "-DOGPLAY_CLI=${OGPLAY_GUI}" "-DROOT=${ROOT}"
        -DOGPLAY_RUN_COMMAND=--run-game -DOGPLAY_GAME_MODE=ON
        -P "${ROOT}/cmake/CheckRunApkOptions.cmake"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 20)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "desktop game options failed: ${output}${error}")
    endif()
endif()
