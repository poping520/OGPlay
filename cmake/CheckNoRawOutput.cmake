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

