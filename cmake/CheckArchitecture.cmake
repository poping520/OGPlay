# Source rules from AGENTS.md: no raw output, no title-specific code, platform code stays in hal/.
if(NOT DEFINED ROOT)
    message(FATAL_ERROR "ROOT is required")
endif()

file(GLOB_RECURSE _sources
    "${ROOT}/src/*.cpp"
    "${ROOT}/src/*.cc"
    "${ROOT}/src/*.cxx"
    "${ROOT}/src/*.h"
    "${ROOT}/src/*.hpp"
)

set(_violations "")
foreach(_source IN LISTS _sources)
    file(READ "${_source}" _content)
    if(_content MATCHES "std::(cout|cerr)|(^|[^A-Za-z0-9_])printf[ \\t]*\\(")
        list(APPEND _violations "${_source}")
    endif()
endforeach()

if(_violations)
    list(JOIN _violations "\n  " _formatted)
    message(FATAL_ERROR "Production sources contain raw output:\n  ${_formatted}")
endif()

set(_platform_modules
    "${ROOT}/src/hal/windows/MODULE.md"
    "${ROOT}/src/hal/linux/MODULE.md"
    "${ROOT}/src/hal/macos/MODULE.md"
    "${ROOT}/src/hal/android/MODULE.md"
    "${ROOT}/src/frontend/android/MODULE.md"
)
foreach(_module IN LISTS _platform_modules)
    if(NOT EXISTS "${_module}")
        message(FATAL_ERROR "Missing platform HAL contract: ${_module}")
    endif()
endforeach()

# Runtime behavior and public runtime headers must remain title-neutral.
file(GLOB_RECURSE _runtime_policy_files LIST_DIRECTORIES FALSE
    "${ROOT}/src/*.c" "${ROOT}/src/*.cc" "${ROOT}/src/*.cpp"
    "${ROOT}/src/*.h" "${ROOT}/src/*.hpp"
    "${ROOT}/include/ogplay/runtime/*.h"
    "${ROOT}/include/ogplay/runtime/*.hpp")
foreach(_file IN LISTS _runtime_policy_files)
    file(READ "${_file}" _content)
    string(TOLOWER "${_content}" _lower_content)
    if(_lower_content MATCHES "(asphalt|gameloft|gloft|dungeon[ _-]*hunter|tales[ _-]*from)")
        message(FATAL_ERROR "Title-specific runtime branch or identity: ${_file}")
    endif()
    if(_content MATCHES "(LayoutViewFact|layout_views)")
        message(FATAL_ERROR "Retired parallel UI geometry fact source: ${_file}")
    endif()
endforeach()

file(GLOB_RECURSE _production_files LIST_DIRECTORIES FALSE
    "${ROOT}/src/*.c" "${ROOT}/src/*.cc" "${ROOT}/src/*.cpp"
    "${ROOT}/src/*.h" "${ROOT}/src/*.hpp")

foreach(_file IN LISTS _production_files)
    file(RELATIVE_PATH _relative "${ROOT}/src" "${_file}")
    # sdl_window_input.cpp is the SDL facade over per-platform window handles.
    # Android's native entry lives in frontend/android because JNI calls reach upward into frontend.
    if(_relative MATCHES "^hal/(windows|linux|macos|android)/|^hal/sdl_window_input\\.cpp$|^frontend/android/")
        continue()
    endif()

    file(READ "${_file}" _content)
    if(_content MATCHES "#[ \t]*include[ \t]*<(windows\\.h|X11/|TargetConditionals\\.h|Cocoa/|android/)")
        message(FATAL_ERROR "Platform header outside hal/<platform>: ${_relative}")
    endif()
    if(_content MATCHES "(^|[\n\r])[ \t]*#[ \t]*(if|ifdef|ifndef)[^\n\r]*(_WIN32|__linux__|__APPLE__|__ANDROID__)")
        message(FATAL_ERROR "Platform preprocessor branch outside hal/<platform>: ${_relative}")
    endif()
endforeach()
