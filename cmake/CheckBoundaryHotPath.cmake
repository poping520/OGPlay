set(boundary_hle_source
    "${ROOT}/src/runtime/boundary/facade/android_boundary_hle.cpp")
file(READ "${boundary_hle_source}" boundary)
set(boundary_router_source
    "${ROOT}/src/runtime/boundary/core/boundary_fast_router.cpp")
file(READ "${boundary_router_source}" boundary_router)
file(GLOB_RECURSE virtual_so_module_sources
    "${ROOT}/src/runtime/boundary/core/*.h"
    "${ROOT}/src/runtime/boundary/core/*.cpp"
    "${ROOT}/src/runtime/boundary/services/*.h"
    "${ROOT}/src/runtime/boundary/services/*.cpp"
    "${ROOT}/src/runtime/boundary/modules/*.h"
    "${ROOT}/src/runtime/boundary/modules/*.cpp"
    "${ROOT}/src/runtime/boundary/facade/*.h"
    "${ROOT}/src/runtime/boundary/facade/*.cpp")
set(virtual_so_modules "")
list(APPEND virtual_so_module_sources
    "${ROOT}/src/runtime/bionic/libc_override_module.h")
foreach(source IN LISTS virtual_so_module_sources)
    file(READ "${source}" contents)
    string(APPEND virtual_so_modules "\n${contents}")
    foreach(forbidden IN ITEMS
            "std::function"
            "InvokeModule" "InvokeAndroid" "InvokeEgl"
            "InvokeGles1" "InvokeGles2" "InvokeLegacyFast"
            "FastBinding" "hot_bindings_"
            "AndroidFunction" "EglFunction"
            "Android boundary HLE is not implemented"
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
        "class AndroidModule final"
        "class EglModule final"
        "class Gles1Module final"
        "class Gles2Module final"
        "class OpenSlesModule final"
        "class LibcOverrideModule final"
        "struct GraphicsBoundaryContext final"
        "class AndroidBoundaryServices final"
        "return hot_[slot].invoke(hot_[slot].self, call)")
    string(FIND "${virtual_so_modules}" "${required}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR
            "boundary direct-binding invariant is missing: ${required}")
    endif()
endforeach()

string(FIND "${virtual_so_modules}" "class LogModule final" log_module_found)
if(log_module_found EQUAL -1)
    message(FATAL_ERROR
        "boundary direct-binding invariant is missing: class LogModule final")
endif()

foreach(required_path IN ITEMS
        "core/a32_call_frame.h"
        "core/boundary_catalog.cpp"
        "core/boundary_binding.h"
        "core/boundary_fast_router.cpp"
        "core/boundary_fault.cpp"
        "core/boundary_symbols.cpp"
        "core/boundary_thunk_arena.cpp"
        "services/frame_service.cpp"
        "services/graphics_boundary_context.h"
        "services/graphics_dispatch.cpp"
        "services/guest_gl_context.cpp"
        "modules/android/android_exports.h"
        "modules/android/android_module.cpp"
        "modules/egl/egl_exports.h"
        "modules/egl/egl_module.cpp"
        "modules/gles1/gles1_dispatch.cpp"
        "modules/gles1/gles1_module.h"
        "modules/gles2/gles2_module.h"
        "modules/gles2/gles2_shader_completion.cpp"
        "modules/gles2/gles2_transfer.cpp"
        "modules/gles2/gles2_vertex_completion.cpp"
        "modules/log/log_module.cpp"
        "modules/log/log_exports.h"
        "facade/android_boundary_hle.cpp")
    if(NOT EXISTS "${ROOT}/src/runtime/boundary/${required_path}")
        message(FATAL_ERROR "boundary ownership path is missing: ${required_path}")
    endif()
endforeach()

string(REGEX MATCH
    "cpu::HostCallResult BoundaryFastRouter::TryFastCall\\([
 ]*cpu::A32HostCallContext& call\\) noexcept \\{[^}]*\\}"
    fast_path "${boundary_router}")
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

file(GLOB_RECURSE boundary_core_sources
    "${ROOT}/src/runtime/boundary/core/*.h"
    "${ROOT}/src/runtime/boundary/core/*.cpp")
foreach(source IN LISTS boundary_core_sources)
    file(READ "${source}" contents)
    foreach(forbidden_dependency IN ITEMS
            "runtime/boundary/modules/"
            "runtime/boundary/services/"
            "runtime/boundary/facade/"
            "runtime/bionic/")
        string(FIND "${contents}" "${forbidden_dependency}" found)
        if(NOT found EQUAL -1)
            message(FATAL_ERROR
                "boundary core has forbidden upward dependency "
                "${forbidden_dependency}: ${source}")
        endif()
    endforeach()
endforeach()

file(GLOB_RECURSE boundary_service_sources
    "${ROOT}/src/runtime/boundary/services/*.h"
    "${ROOT}/src/runtime/boundary/services/*.cpp")
foreach(source IN LISTS boundary_service_sources)
    file(READ "${source}" contents)
    foreach(forbidden_dependency IN ITEMS
            "runtime/boundary/modules/"
            "runtime/boundary/facade/")
        string(FIND "${contents}" "${forbidden_dependency}" found)
        if(NOT found EQUAL -1)
            message(FATAL_ERROR
                "boundary service has forbidden upward dependency "
                "${forbidden_dependency}: ${source}")
        endif()
    endforeach()
endforeach()

file(GLOB_RECURSE concrete_module_sources
    "${ROOT}/src/runtime/boundary/modules/*.h"
    "${ROOT}/src/runtime/boundary/modules/*.cpp")
foreach(source IN LISTS concrete_module_sources)
    file(READ "${source}" contents)
    foreach(forbidden_dependency IN ITEMS
            "runtime/boundary/facade/"
            "AndroidBoundaryHle::Impl")
        string(FIND "${contents}" "${forbidden_dependency}" found)
        if(NOT found EQUAL -1)
            message(FATAL_ERROR
                "concrete module has forbidden facade dependency "
                "${forbidden_dependency}: ${source}")
        endif()
    endforeach()
endforeach()

foreach(concrete_module IN ITEMS
        "class AndroidModule final"
        "class EglModule final"
        "class Gles1Module final"
        "class Gles2Module final")
    string(FIND "${boundary}" "${concrete_module}" found)
    if(NOT found EQUAL -1)
        message(FATAL_ERROR
            "facade still owns concrete module implementation: ${concrete_module}")
    endif()
endforeach()
