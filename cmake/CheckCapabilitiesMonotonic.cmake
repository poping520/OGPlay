set(_current_path "${ROOT}/capabilities.toml")
if(NOT EXISTS "${_current_path}")
    message(FATAL_ERROR "capabilities.toml is missing")
endif()

set(_baseline_ref "$ENV{OGPLAY_CAPABILITY_BASE}")
if(NOT _baseline_ref)
    set(_baseline_ref "HEAD")
endif()

execute_process(
    COMMAND git -c safe.directory=${ROOT} show ${_baseline_ref}:capabilities.toml
    WORKING_DIRECTORY "${ROOT}"
    RESULT_VARIABLE _git_result
    OUTPUT_VARIABLE _baseline
    ERROR_VARIABLE _git_error
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT _git_result EQUAL 0)
    message(STATUS "Capability baseline ${_baseline_ref} unavailable; skipping: ${_git_error}")
    return()
endif()

file(READ "${_current_path}" _current)

function(_extract_capabilities text output)
    string(REGEX MATCHALL "\\[[^]]+\\][^[]*status[ \t]*=[ \t]*\"[^\"]+\"" _blocks "${text}")
    set(_result "")
    foreach(_block IN LISTS _blocks)
        string(REGEX REPLACE "^\\[([^]]+)\\].*" "\\1" _id "${_block}")
        string(REGEX REPLACE ".*status[ \t]*=[ \t]*\"([^\"]+)\".*" "\\1" _status "${_block}")
        list(APPEND _result "${_id}=${_status}")
    endforeach()
    set(${output} "${_result}" PARENT_SCOPE)
endfunction()

function(_status_rank status output)
    if(status STREQUAL "unimplemented")
        set(_rank 0)
    elseif(status STREQUAL "stub")
        set(_rank 1)
    elseif(status STREQUAL "partial")
        set(_rank 2)
    elseif(status STREQUAL "complete")
        set(_rank 3)
    else()
        message(FATAL_ERROR "Unknown capability status: ${status}")
    endif()
    set(${output} ${_rank} PARENT_SCOPE)
endfunction()

_extract_capabilities("${_baseline}" _baseline_entries)
_extract_capabilities("${_current}" _current_entries)

foreach(_baseline_entry IN LISTS _baseline_entries)
    string(REPLACE "=" ";" _parts "${_baseline_entry}")
    list(GET _parts 0 _id)
    list(GET _parts 1 _old_status)
    set(_new_status "")
    foreach(_current_entry IN LISTS _current_entries)
        if(_current_entry MATCHES "^${_id}=")
            string(REGEX REPLACE "^[^=]+=" "" _new_status "${_current_entry}")
            break()
        endif()
    endforeach()
    if(NOT _new_status)
        message(FATAL_ERROR "Capability removed from ledger: ${_id}")
    endif()
    _status_rank("${_old_status}" _old_rank)
    _status_rank("${_new_status}" _new_rank)
    if(_new_rank LESS _old_rank)
        message(FATAL_ERROR
            "Capability regressed: ${_id} ${_old_status} -> ${_new_status}")
    endif()
endforeach()

message(STATUS "Capability ledger is monotonic against ${_baseline_ref}")
