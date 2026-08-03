set(_platform_modules
    "${ROOT}/src/hal/windows/MODULE.md"
    "${ROOT}/src/hal/linux/MODULE.md"
    "${ROOT}/src/hal/macos/MODULE.md"
)
foreach(_module IN LISTS _platform_modules)
    if(NOT EXISTS "${_module}")
        message(FATAL_ERROR "Missing platform HAL contract: ${_module}")
    endif()
endforeach()

file(GLOB_RECURSE _production_files LIST_DIRECTORIES FALSE
    "${ROOT}/src/*.c" "${ROOT}/src/*.cc" "${ROOT}/src/*.cpp"
    "${ROOT}/src/*.h" "${ROOT}/src/*.hpp")

foreach(_file IN LISTS _production_files)
    file(RELATIVE_PATH _relative "${ROOT}/src" "${_file}")
    if(_relative MATCHES "^hal/(windows|linux|macos)/")
        continue()
    endif()

    file(READ "${_file}" _content)
    if(_content MATCHES "#[ \t]*include[ \t]*[<\"](windows\\.h|X11/|TargetConditionals\\.h|Cocoa/)")
        message(FATAL_ERROR "Platform header outside hal/<platform>: ${_relative}")
    endif()
    if(_content MATCHES "(^|[\n\r])[ \t]*#[ \t]*(if|ifdef|ifndef)[^\n\r]*(_WIN32|__linux__|__APPLE__)")
        message(FATAL_ERROR "Platform preprocessor branch outside hal/<platform>: ${_relative}")
    endif()
endforeach()
