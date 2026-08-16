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

# Runtime behavior must remain title-neutral, and UiTree must stay the sole UI
# geometry source after Layout UI convergence. Include public runtime headers because session
# state used to carry the retired LayoutViewFact side table there.
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
