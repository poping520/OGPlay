set(boundary_hle_source
    "${ROOT}/src/runtime/boundary/android_boundary_hle.cpp")
file(READ "${boundary_hle_source}" boundary)
file(GLOB virtual_so_module_sources
    "${ROOT}/src/runtime/boundary/*.h"
    "${ROOT}/src/runtime/boundary/*.cpp")
foreach(source IN LISTS virtual_so_module_sources)
    file(READ "${source}" contents)
    foreach(forbidden IN ITEMS
            "std::function"
            "InvokeModule" "InvokeAndroid" "InvokeEgl"
            "InvokeGles1" "InvokeGles2" "InvokeLegacyFast"
            "FastBinding" "hot_bindings_"
            "AndroidFunction" "EglFunction"
            "active_pc_" "SetActivePc"
            "Impl& runtime_" "Impl* runtime_")
        string(FIND "${contents}" "${forbidden}" found)
        if(NOT found EQUAL -1)
            message(FATAL_ERROR
                "Virtual SO module source contains forbidden legacy token "
                "${forbidden}: ${source}")
        endif()
    endforeach()
endforeach()

foreach(required IN ITEMS
        "struct AndroidModule final"
        "struct EglModule final"
        "struct Gles1Module final"
        "struct Gles2Module final"
        "struct LogModule final"
        "struct LibcOverrideModule final"
        "struct GraphicsBoundaryContext final"
        "struct AndroidBoundaryServices final"
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
