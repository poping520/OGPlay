if(POLICY CMP0057)
    cmake_policy(SET CMP0057 NEW)
endif()

if(NOT DEFINED ROOT)
    message(FATAL_ERROR "ROOT is required")
endif()

set(current "${ROOT}/docs/state/CURRENT.md")
if(NOT EXISTS "${current}")
    message(FATAL_ERROR "missing docs/state/CURRENT.md")
endif()
file(SIZE "${current}" current_size)
if(current_size GREATER 6144)
    message(FATAL_ERROR
        "docs/state/CURRENT.md is ${current_size} bytes; rolling handoff limit is 6144")
endif()

foreach(required IN ITEMS
        docs/state/KNOWN-ISSUES.md
        docs/state/M0-ACCEPTANCE.md
        docs/state/M1-ACCEPTANCE.md
        docs/state/M2-ACCEPTANCE.md
        docs/tasks/README.md)
    if(NOT EXISTS "${ROOT}/${required}")
        message(FATAL_ERROR "missing documentation entry: ${required}")
    endif()
endforeach()

foreach(runtime_submodule IN ITEMS
        jni framework bionic syscall execution vfs integration)
    set(module_contract
        "${ROOT}/src/runtime/${runtime_submodule}/MODULE.md")
    if(NOT EXISTS "${module_contract}")
        message(FATAL_ERROR
            "runtime submodule ${runtime_submodule} is missing MODULE.md")
    endif()
endforeach()

file(GLOB runtime_root_headers
    "${ROOT}/include/ogplay/runtime/*.h")
file(GLOB runtime_root_sources
    "${ROOT}/src/runtime/*.cpp")
if(runtime_root_headers OR runtime_root_sources)
    message(FATAL_ERROR
        "runtime production files must live in declared submodules")
endif()

file(GLOB root_work_units "${ROOT}/docs/tasks/WU-*.md")
if(root_work_units)
    message(FATAL_ERROR "Work Units must live below docs/tasks/m<number>")
endif()

file(GLOB_RECURSE work_units RELATIVE "${ROOT}"
    "${ROOT}/docs/tasks/m*/WU-*.md")
list(LENGTH work_units work_unit_count)
if(work_unit_count LESS 104)
    message(FATAL_ERROR
        "expected at least 104 milestone Work Units, found ${work_unit_count}")
endif()

set(ids)
set(numbers)
foreach(work_unit IN LISTS work_units)
    if(work_unit MATCHES
            "^docs/tasks/(m[0-9]+)/WU-M([0-9]+)-([0-9][0-9][0-9])\\.md$")
        set(milestone "${CMAKE_MATCH_1}")
        set(declared_milestone "m${CMAKE_MATCH_2}")
        set(id "M${CMAKE_MATCH_2}-${CMAKE_MATCH_3}")
        if(NOT milestone STREQUAL declared_milestone)
            message(FATAL_ERROR
                "WU-${id} belongs to ${declared_milestone}, found in ${milestone}")
        endif()
        if(id IN_LIST ids)
            message(FATAL_ERROR "duplicate Work Unit id: WU-${id}")
        endif()
        list(APPEND ids "${id}")
        continue()
    endif()
    if(NOT work_unit MATCHES
            "^docs/tasks/(m[0-9]+)/WU-([0-9][0-9][0-9][0-9])\\.md$")
        message(FATAL_ERROR "invalid Work Unit path: ${work_unit}")
    endif()
    set(milestone "${CMAKE_MATCH_1}")
    set(id "${CMAKE_MATCH_2}")
    if(id IN_LIST ids)
        message(FATAL_ERROR "duplicate Work Unit id: WU-${id}")
    endif()
    list(APPEND ids "${id}")

    string(REGEX REPLACE "^0+" "" number "${id}")
    if(number STREQUAL "")
        set(number 0)
    endif()
    list(APPEND numbers "${number}")
    if(number LESS_EQUAL 14)
        set(expected m0)
    elseif(number LESS_EQUAL 40)
        set(expected m1)
    elseif(number LESS_EQUAL 103)
        set(expected m2)
    else()
        set(expected "")
    endif()
    if(expected AND NOT milestone STREQUAL expected)
        message(FATAL_ERROR
            "WU-${id} belongs to ${expected}, found in ${milestone}")
    endif()
    if(NOT expected AND milestone MATCHES "^m[0-2]$")
        message(FATAL_ERROR
            "future WU-${id} cannot be added to completed ${milestone}")
    endif()
endforeach()

foreach(required_number RANGE 1 104)
    if(NOT required_number IN_LIST numbers)
        message(FATAL_ERROR "missing historical Work Unit number: ${required_number}")
    endif()
endforeach()

file(GLOB_RECURSE project_docs "${ROOT}/docs/*.md")
list(APPEND project_docs "${ROOT}/AGENTS.md" "${ROOT}/README.md")
foreach(document IN LISTS project_docs)
    file(READ "${document}" contents)
    string(FIND "${contents}" "docs/tasks/WU-" stale_path)
    if(NOT stale_path EQUAL -1)
        message(FATAL_ERROR "stale flat Work Unit path in ${document}")
    endif()
endforeach()
