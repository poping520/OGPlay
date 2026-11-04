# frontend.gui_library_smoke: a non-empty library with a long CJK title loads in the real WebView.
# It needs a visible desktop session, so it stays a separate test from the entry checks below.
if(DEFINED GUI_LIBRARY_ROOT)
    file(REMOVE_RECURSE "${GUI_LIBRARY_ROOT}")
    file(MAKE_DIRECTORY "${GUI_LIBRARY_ROOT}/library/org.example.game")
    file(WRITE "${GUI_LIBRARY_ROOT}/library/org.example.game/game.apk" "fixture")
    file(WRITE "${GUI_LIBRARY_ROOT}/library/org.example.game/meta.toml"
        "schema = 1\n"
        "package = \"org.example.game\"\n"
        "display_name = \"示例游戏名称很长用于验证磁贴省略\"\n"
        "version_code = 1\n"
        "version_name = \"1.0\"\n"
        "imported_at = \"2026-08-13T00:00:00Z\"\n")

    execute_process(
        COMMAND "${OGPLAY_GUI}" --library-root "${GUI_LIBRARY_ROOT}" --smoke-frames 3
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR
            "non-empty GUI library smoke failed (${result})\nstdout:\n${output}\nstderr:\n${error}")
    endif()
    return()
endif()

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
get_filename_component(entry_directory "${OGPLAY_CLI}" DIRECTORY)
set(fixtures "${entry_directory}/entrypoint-checks")
file(MAKE_DIRECTORY "${fixtures}")
file(WRITE "${fixtures}/request.jsonl" "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"session.open\"}\n")
execute_process(COMMAND "${OGPLAY_CLI}" agent-stdio INPUT_FILE "${fixtures}/request.jsonl"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 5)
string(FIND "${output}" "\"result\"" found)
if(NOT result EQUAL 0 OR found EQUAL -1)
    message(FATAL_ERROR "CLI stdio transport failed: ${result}; ${output}${error}")
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

# Inspect the actual dynamic boundary, not object-file private extern symbols.
if(DEFINED OGPLAY_CORE)
    if(WIN32)
        find_program(symbol_tool dumpbin REQUIRED)
        execute_process(COMMAND "${symbol_tool}" /exports "${OGPLAY_CORE}"
            RESULT_VARIABLE symbol_result OUTPUT_VARIABLE symbols)
        string(REGEX MATCHALL "[\r\n][ ]+[0-9]+[ ]+[0-9A-F]+[ ]+[0-9A-F]+[ ]+[^\r\n]+" exports "${symbols}")
    elseif(APPLE)
        set(symbol_tool /usr/bin/nm)
        execute_process(COMMAND "${symbol_tool}" -gUj "${OGPLAY_CORE}"
            RESULT_VARIABLE symbol_result OUTPUT_VARIABLE exports)
    else()
        find_program(symbol_tool nm REQUIRED)
        execute_process(COMMAND "${symbol_tool}" -D --defined-only -j "${OGPLAY_CORE}"
            RESULT_VARIABLE symbol_result OUTPUT_VARIABLE exports)
    endif()
    if(NOT symbol_result EQUAL 0 OR "${exports}" STREQUAL "")
        message(FATAL_ERROR "cannot inspect ogplaycore exports")
    endif()
    string(REPLACE "\n" ";" exports "${exports}")
    foreach(symbol IN LISTS exports)
        string(STRIP "${symbol}" symbol)
        if(symbol STREQUAL "")
            continue()
        endif()
        if(NOT symbol MATCHES "^_?_Z(NK?|T[ISV]N)6ogplay3app" AND
           NOT symbol MATCHES "@app@ogplay@@")
            message(FATAL_ERROR "non-facade export: ${symbol}")
        endif()
    endforeach()
    foreach(executable IN ITEMS "${OGPLAY_CLI}" "${OGPLAY_GUI}")
        if("${executable}" STREQUAL "")
            continue()
        endif()
        if(WIN32)
            execute_process(COMMAND "${symbol_tool}" /imports "${executable}"
                RESULT_VARIABLE import_result OUTPUT_VARIABLE imports)
        else()
            execute_process(COMMAND "${symbol_tool}" -u "${executable}"
                RESULT_VARIABLE import_result OUTPUT_VARIABLE imports)
        endif()
        if(NOT import_result EQUAL 0)
            message(FATAL_ERROR "cannot inspect EXE imports: ${executable}")
        endif()
        string(REGEX MATCHALL "[^\n]*(_ZNK?6ogplay|@ogplay@@)[^\n]*" ogplay_imports "${imports}")
        foreach(symbol IN LISTS ogplay_imports)
            if(NOT symbol MATCHES "_ZNK?6ogplay3app" AND NOT symbol MATCHES "@app@ogplay@@")
                message(FATAL_ERROR "EXE imports internal symbol: ${symbol}")
            endif()
        endforeach()
        if(executable STREQUAL OGPLAY_CLI AND imports MATCHES "[Ww]eb[Vv]iew")
            message(FATAL_ERROR "CLI imports WebView")
        endif()
    endforeach()
endif()

if(DEFINED OGPLAY_GUI AND NOT "${OGPLAY_GUI}" STREQUAL "")
    # Invalid launcher options must fail.
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
            COMMAND "${OGPLAY_GUI}" ${_arguments}
            RESULT_VARIABLE _result
            OUTPUT_QUIET
            ERROR_QUIET)
        if(_result EQUAL 0)
            message(FATAL_ERROR "invalid gui options were accepted: ${_arguments}")
        endif()
    endforeach()
endif()
