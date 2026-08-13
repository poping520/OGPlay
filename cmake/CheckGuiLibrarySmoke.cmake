if(NOT DEFINED OGPLAY_CLI OR NOT DEFINED GUI_LIBRARY_ROOT)
    message(FATAL_ERROR "GUI library smoke requires OGPLAY_CLI and GUI_LIBRARY_ROOT")
endif()

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
    COMMAND "${OGPLAY_CLI}" gui --library-root "${GUI_LIBRARY_ROOT}" --smoke-frames 3
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR
        "non-empty GUI library smoke failed (${result})\nstdout:\n${output}\nstderr:\n${error}")
endif()
