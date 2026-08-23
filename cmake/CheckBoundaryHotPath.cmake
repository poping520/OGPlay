file(READ "${ROOT}/src/runtime/boundary/android_boundary_hle.cpp" boundary)
file(GLOB boundary_sources
    "${ROOT}/src/runtime/boundary/*.h"
    "${ROOT}/src/runtime/boundary/*.cpp")
foreach(source IN LISTS boundary_sources)
    file(READ "${source}" contents)
    string(FIND "${contents}" "std::function" found)
    if(NOT found EQUAL -1)
        message(FATAL_ERROR
            "Virtual SO boundary hot implementation uses std::function: ${source}")
    endif()
endforeach()

foreach(forbidden IN ITEMS
        "InvokeModule" "FastBinding" "hot_bindings_"
        "AndroidFunction" "EglFunction" "InvokeLegacyFast")
    string(FIND "${boundary}" "${forbidden}" found)
    if(NOT found EQUAL -1)
        message(FATAL_ERROR
            "legacy boundary dispatch token remains: ${forbidden}")
    endif()
endforeach()

foreach(required IN ITEMS
        "struct AndroidModule final"
        "struct EglModule final"
        "struct Gles1Module final"
        "struct Gles2Module final"
        "struct LogModule final"
        "return hot_[slot].invoke(hot_[slot].self, call)")
    string(FIND "${boundary}" "${required}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR
            "boundary direct-binding invariant is missing: ${required}")
    endif()
endforeach()

string(REGEX MATCH
    "cpu::HostCallResult TryFastCall\\(cpu::A32HostCallContext& call\\) noexcept \\{[^}]*\\}"
    fast_path "${boundary}")
if(fast_path STREQUAL "")
    message(FATAL_ERROR "cannot locate boundary TryFastCall hot router")
endif()
foreach(forbidden IN ITEMS
        "GetState" "SetState" "HaltExecution" "std::function"
        "descriptor" "library" "local_id" "symbol")
    string(FIND "${fast_path}" "${forbidden}" found)
    if(NOT found EQUAL -1)
        message(FATAL_ERROR
            "boundary hot router contains forbidden operation: ${forbidden}")
    endif()
endforeach()
