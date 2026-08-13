cmake_minimum_required(VERSION 3.25)

project(OGPlay VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(OGPLAY_BUILD_TESTS "Build OGPlay unit and contract tests" ON)
option(OGPLAY_WARNINGS_AS_ERRORS "Treat OGPlay warnings as errors" OFF)
option(OGPLAY_ENABLE_SDL3 "Configure the bundled SDL3 platform dependency" ON)
option(OGPLAY_ENABLE_DYNARMIC "Configure the bundled Dynarmic CPU dependency" ON)
option(OGPLAY_ENABLE_ANGLE "Link an externally GN-built ANGLE SDK" ON)
set(OGPLAY_ANGLE_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/angle-prebuilt"
    CACHE PATH "Root of the platform-partitioned ANGLE prebuilt SDK")
set(OGPLAY_ANGLE_SDK_CONFIGURATION "release" CACHE STRING
    "ANGLE SDK configuration to consume (release or debug)")
set_property(CACHE OGPLAY_ANGLE_SDK_CONFIGURATION PROPERTY STRINGS release debug)

include(GNUInstallDirs)
include(cmake/CompilerWarnings.cmake)
include(cmake/ThirdParty.cmake)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

function(ogplay_stage_angle_runtime target)
    if(NOT OGPLAY_ENABLE_ANGLE)
        return()
    endif()
    # Windows keeps EGL/GLESv2 DLLs under bin/; Unix keeps shared libraries under
    # lib/ with install names like ./libEGL.dylib that resolve against the process
    # working directory (ctest uses the build dir beside the executable).
    set(_ogplay_angle_stage_commands)
    if(EXISTS "${_angle_sdk}/bin")
        list(APPEND _ogplay_angle_stage_commands
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${_angle_sdk}/bin" $<TARGET_FILE_DIR:${target}>
        )
    endif()
    if(APPLE)
        foreach(_ogplay_angle_dylib IN ITEMS libEGL.dylib libGLESv2.dylib)
            if(EXISTS "${_angle_sdk}/lib/${_ogplay_angle_dylib}")
                list(APPEND _ogplay_angle_stage_commands
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_angle_sdk}/lib/${_ogplay_angle_dylib}"
                        "$<TARGET_FILE_DIR:${target}>/${_ogplay_angle_dylib}"
                )
            endif()
        endforeach()
    endif()
    if(_ogplay_angle_stage_commands)
        add_custom_command(TARGET ${target} POST_BUILD
            ${_ogplay_angle_stage_commands}
        )
    endif()
endfunction()

add_library(ogplay_core
    third_party/yyjson/src/yyjson.c
    src/core/capability_ledger.cpp
    src/core/frame_compare.cpp
    src/core/json.cpp
    src/core/logger.cpp
    src/core/software_surface.cpp
)
add_library(OGPlay::Core ALIAS ogplay_core)
target_include_directories(ogplay_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(ogplay_core SYSTEM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/yyjson/src)
if(MSVC)
    set_source_files_properties(third_party/yyjson/src/yyjson.c PROPERTIES
        COMPILE_OPTIONS "/w")
else()
    set_source_files_properties(third_party/yyjson/src/yyjson.c PROPERTIES
        COMPILE_OPTIONS "-w")
endif()
target_compile_features(ogplay_core PUBLIC cxx_std_20)
ogplay_set_project_warnings(ogplay_core)

add_library(ogplay_hal
    src/hal/clock.cpp
    src/hal/fs.cpp
    src/hal/sdl_audio_output.cpp
    src/hal/sdl_window_input.cpp
    src/hal/thread.cpp
)
if(WIN32)
    target_sources(ogplay_hal PRIVATE
        src/hal/windows/host_environment.cpp
        src/hal/windows/shared_library.cpp
        src/hal/windows/virtual_memory.cpp)
elseif(APPLE)
    target_sources(ogplay_hal PRIVATE
        src/hal/macos/host_environment.cpp
        src/hal/macos/shared_library.cpp
        src/hal/macos/virtual_memory.cpp)
elseif(UNIX)
    target_sources(ogplay_hal PRIVATE
        src/hal/linux/host_environment.cpp
        src/hal/linux/shared_library.cpp
        src/hal/linux/virtual_memory.cpp)
else()
    message(FATAL_ERROR "OGPlay has no virtual memory HAL for this host platform")
endif()
add_library(OGPlay::Hal ALIAS ogplay_hal)
target_include_directories(ogplay_hal PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
if(OGPLAY_ENABLE_SDL3)
    target_compile_definitions(ogplay_hal PRIVATE OGPLAY_HAS_SDL3=1)
    target_link_libraries(ogplay_hal PRIVATE SDL3::SDL3)
else()
    target_compile_definitions(ogplay_hal PRIVATE OGPLAY_HAS_SDL3=0)
endif()
if(NOT WIN32)
    target_link_libraries(ogplay_hal PRIVATE ${CMAKE_DL_LIBS})
endif()
ogplay_set_project_warnings(ogplay_hal)

add_library(ogplay_audio
    src/audio/java_sound_pool_mixer.cpp
    src/audio/ogg_vorbis.cpp
    third_party/stb/stb_vorbis.c
)
add_library(OGPlay::Audio ALIAS ogplay_audio)
target_include_directories(ogplay_audio PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
set_source_files_properties(third_party/stb/stb_vorbis.c PROPERTIES LANGUAGE CXX)
if(MSVC)
    set_source_files_properties(third_party/stb/stb_vorbis.c PROPERTIES
        COMPILE_OPTIONS "/w")
else()
    set_source_files_properties(third_party/stb/stb_vorbis.c PROPERTIES
        COMPILE_OPTIONS "-w")
endif()
ogplay_set_project_warnings(ogplay_audio)

add_library(ogplay_video
    src/video/video_player.cpp
    src/video/fake_video_player.cpp
    src/video/ffmpeg_loader.cpp
    src/video/ffmpeg_video_player.cpp
    src/video/rgba_canvas.cpp
)
add_library(OGPlay::Video ALIAS ogplay_video)
target_link_libraries(ogplay_video PRIVATE OGPlay::Hal)
target_include_directories(ogplay_video PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
ogplay_set_project_warnings(ogplay_video)

add_library(ogplay_input
    src/input/mouse_touch_mapper.cpp
    src/input/template_catalog.cpp
)
add_library(OGPlay::Input ALIAS ogplay_input)
target_link_libraries(ogplay_input PUBLIC OGPlay::Hal)
target_include_directories(ogplay_input PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
ogplay_set_project_warnings(ogplay_input)

add_library(ogplay_memory
    src/memory/address_space.cpp
    src/memory/bus.cpp
)
add_library(OGPlay::Memory ALIAS ogplay_memory)
target_link_libraries(ogplay_memory PRIVATE OGPlay::Hal)
target_include_directories(ogplay_memory PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
ogplay_set_project_warnings(ogplay_memory)

add_library(ogplay_cpu
    src/cpu/futex.cpp
    src/cpu/interpreter.cpp
    src/cpu/state.cpp
    src/cpu/thread_group.cpp
)
add_library(OGPlay::Cpu ALIAS ogplay_cpu)
target_link_libraries(ogplay_cpu PUBLIC OGPlay::Memory)
target_include_directories(ogplay_cpu PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
if(OGPLAY_ENABLE_DYNARMIC)
    target_sources(ogplay_cpu PRIVATE src/cpu/dynarmic.cpp)
    target_compile_definitions(ogplay_cpu PUBLIC OGPLAY_HAS_DYNARMIC=1)
    target_link_libraries(ogplay_cpu PRIVATE dynarmic)
else()
    target_compile_definitions(ogplay_cpu PUBLIC OGPLAY_HAS_DYNARMIC=0)
endif()
ogplay_set_project_warnings(ogplay_cpu)

add_library(ogplay_loader
    src/loader/apk.cpp
    src/loader/apk_manifest.cpp
    src/loader/apk_native.cpp
    src/loader/binary_xml.cpp
    src/loader/dex.cpp
    src/loader/dex_analysis.cpp
    src/loader/dex_class_data.cpp
    src/loader/arsc.cpp
    src/loader/dex_code.cpp
    src/loader/dynamic_linker.cpp
    src/loader/elf.cpp
    src/loader/lifecycle.cpp
    src/loader/link_namespace.cpp
    src/loader/module_loader.cpp
    src/loader/relocation.cpp
    src/loader/symbol_version.cpp
    src/loader/tls.cpp
)
add_library(OGPlay::Loader ALIAS ogplay_loader)
target_link_libraries(ogplay_loader PUBLIC OGPlay::Memory)
target_include_directories(ogplay_loader PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
ogplay_set_project_warnings(ogplay_loader)

add_library(ogplay_pvrtc_vendor OBJECT
    third_party/powervr/PVRTDecompress.cpp
)
target_include_directories(ogplay_pvrtc_vendor PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/powervr")
target_compile_features(ogplay_pvrtc_vendor PRIVATE cxx_std_20)
set_target_properties(ogplay_pvrtc_vendor PROPERTIES
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON)

add_library(ogplay_gles
    $<TARGET_OBJECTS:ogplay_pvrtc_vendor>
    src/gles/angle_backend.cpp
    src/gles/angle_frame.cpp
    src/gles/angle_frame_framebuffer.cpp
    src/gles/angle_frame_shader.cpp
    src/gles/angle_frame_state.cpp
    src/gles/egl_lifecycle.cpp
    src/gles/etc1.cpp
    src/gles/gles_call_preparation.cpp
    src/gles/gles_dispatch.cpp
    src/gles/gles_transfer_state.cpp
    src/gles/guest_transfer.cpp
    src/gles/pvrtc.cpp
    src/gles/supersample.cpp
)
set(OGPLAY_GLES_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(OGPLAY_GLES2_CATALOG
    "${OGPLAY_GLES_GENERATED_DIR}/ogplay/gles/generated/gles2_catalog.h")
set(OGPLAY_GLES1_CATALOG
    "${OGPLAY_GLES_GENERATED_DIR}/ogplay/gles/generated/gles1_catalog.h")
set(OGPLAY_GLES1_EXTENSIONS_CATALOG
    "${OGPLAY_GLES_GENERATED_DIR}/ogplay/gles/generated/gles1_extensions_catalog.h")
add_custom_command(
    OUTPUT "${OGPLAY_GLES2_CATALOG}"
    COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_gles_api.py"
        --idl "${CMAKE_CURRENT_SOURCE_DIR}/data/gles/gles2.json"
        --output "${OGPLAY_GLES2_CATALOG}"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_gles_api.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/data/gles/gles2.json"
    VERBATIM
)
add_custom_target(ogplay_gles_codegen DEPENDS "${OGPLAY_GLES2_CATALOG}")
set(OGPLAY_DEXVM_OPCODE_TABLE
    "${OGPLAY_GLES_GENERATED_DIR}/ogplay/runtime/dexvm/generated/opcode_table.h")
add_custom_command(
    OUTPUT "${OGPLAY_DEXVM_OPCODE_TABLE}"
    COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_dexvm_opcodes.py"
        --catalog "${CMAKE_CURRENT_SOURCE_DIR}/data/dexvm/dalvik_opcodes.json"
        --output "${OGPLAY_DEXVM_OPCODE_TABLE}"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_dexvm_opcodes.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/data/dexvm/dalvik_opcodes.json"
    VERBATIM
)
add_custom_target(ogplay_dexvm_codegen DEPENDS "${OGPLAY_DEXVM_OPCODE_TABLE}")
set(OGPLAY_DEXVM_FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/dexvm_fixtures")
file(GLOB OGPLAY_DEXVM_FIXTURE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/dexvm/fixtures/*.dexasm")
file(GLOB OGPLAY_DEXVM_INTRINSIC_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/dexvm/intrinsics/*.cpp")
file(GLOB OGPLAY_DEXVM_ANDROID_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/integration/dexvm_android/*.cpp")
add_custom_command(
    OUTPUT "${OGPLAY_DEXVM_FIXTURE_DIR}/.stamp"
    COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/dexasm.py"
        --catalog "${CMAKE_CURRENT_SOURCE_DIR}/data/dexvm/dalvik_opcodes.json"
        --batch-input "${CMAKE_CURRENT_SOURCE_DIR}/tests/dexvm/fixtures"
        --batch-output "${OGPLAY_DEXVM_FIXTURE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E touch "${OGPLAY_DEXVM_FIXTURE_DIR}/.stamp"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/dexasm.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/dex_survey_lib.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/data/dexvm/dalvik_opcodes.json"
        ${OGPLAY_DEXVM_FIXTURE_SOURCES}
    VERBATIM
)
add_custom_target(ogplay_dexvm_fixtures
    DEPENDS "${OGPLAY_DEXVM_FIXTURE_DIR}/.stamp")
add_custom_command(
    OUTPUT "${OGPLAY_GLES1_CATALOG}"
    COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_gles_api.py"
        --idl "${CMAKE_CURRENT_SOURCE_DIR}/data/gles/gles1.json"
        --output "${OGPLAY_GLES1_CATALOG}"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_gles_api.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/data/gles/gles1.json"
    VERBATIM
)
add_custom_target(ogplay_gles1_codegen DEPENDS "${OGPLAY_GLES1_CATALOG}")
add_dependencies(ogplay_gles_codegen ogplay_gles1_codegen)
add_custom_command(
    OUTPUT "${OGPLAY_GLES1_EXTENSIONS_CATALOG}"
    COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_gles_api.py"
        --idl "${CMAKE_CURRENT_SOURCE_DIR}/data/gles/gles1_extensions.json"
        --output "${OGPLAY_GLES1_EXTENSIONS_CATALOG}"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_gles_api.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/data/gles/gles1_extensions.json"
    VERBATIM
)
add_custom_target(ogplay_gles1_extensions_codegen
    DEPENDS "${OGPLAY_GLES1_EXTENSIONS_CATALOG}")
add_dependencies(ogplay_gles_codegen ogplay_gles1_extensions_codegen)
add_dependencies(ogplay_gles ogplay_gles_codegen)
add_library(OGPlay::Gles ALIAS ogplay_gles)
target_include_directories(ogplay_gles PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        "${OGPLAY_GLES_GENERATED_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/powervr"
)
ogplay_set_project_warnings(ogplay_gles)
target_link_libraries(ogplay_gles PRIVATE OGPlay::Hal OGPlay::Memory)
if(OGPLAY_ENABLE_ANGLE)
    target_compile_definitions(ogplay_gles PRIVATE
        OGPLAY_HAS_ANGLE=1
        OGPLAY_ANGLE_HAS_SWIFTSHADER=$<BOOL:${_angle_has_swiftshader}>)
    target_link_libraries(ogplay_gles PRIVATE ANGLE::EGL ANGLE::GLESv2)
else()
    target_compile_definitions(ogplay_gles PRIVATE
        OGPLAY_HAS_ANGLE=0
        OGPLAY_ANGLE_HAS_SWIFTSHADER=0)
endif()

add_library(ogplay_runtime
    src/runtime/syscall/arm_kernel_helpers.cpp
    src/runtime/bionic/bionic_module_set.cpp
    src/runtime/bionic/bionic_profile.cpp
    src/runtime/bionic/bionic_selfcheck.cpp
    src/runtime/bionic/bionic_tls.cpp
    src/runtime/framework/framework_asset.cpp
    src/runtime/framework/preferences_xml.cpp
    src/runtime/framework/framework_lifecycle.cpp
    src/runtime/framework/framework_locale.cpp
    src/runtime/framework/framework_package.cpp
    src/runtime/framework/framework_preferences.cpp
    src/runtime/execution/guest_lifecycle.cpp
    src/runtime/integration/api19_guest_process.cpp
    src/runtime/integration/android_guest_call_session.cpp
    src/runtime/integration/android_guest_java_movie.cpp
    src/runtime/integration/android_guest_java_platform.cpp
    src/runtime/integration/headless_bionic_runner.cpp
    src/runtime/boundary/guest_gl_context.cpp
    src/runtime/boundary/a32_call_frame.cpp
    src/runtime/boundary/android_boundary_gles.cpp
    src/runtime/boundary/android_boundary_gles1.cpp
    src/runtime/boundary/android_boundary_gles1_draw.cpp
    src/runtime/boundary/android_boundary_gles1_fixed.cpp
    src/runtime/boundary/android_boundary_gles1_query.cpp
    src/runtime/boundary/android_boundary_hle.cpp
    src/runtime/boundary/android_boundary_symbols.cpp
    src/runtime/integration/android_link_preflight.cpp
    ${OGPLAY_DEXVM_ANDROID_SOURCES}
    src/runtime/integration/dexvm_bridge.cpp
    src/runtime/integration/host_image_decode.cpp
    src/runtime/jni_guest/jni_guest_abi.cpp
    src/runtime/jni_guest/jni_guest_bindings.cpp
    src/runtime/jni_guest/jni_guest_array_bindings.cpp
    src/runtime/jni_guest/jni_guest_dispatch.cpp
    src/runtime/jni_guest/jni_guest_library_lifecycle.cpp
    src/runtime/jni_guest/jni_guest_static_calls.cpp
    src/runtime/jni_guest/jni_guest_static_fields.cpp
    src/runtime/jni_guest/jni_guest_string_bindings.cpp
    src/runtime/integration/native_activity_runner.cpp
    src/runtime/integration/headless_jni_contract.cpp
    src/runtime/dexvm/class_linker.cpp
    src/runtime/dexvm/class_linker_resolve.cpp
    ${OGPLAY_DEXVM_INTRINSIC_SOURCES}
    src/runtime/dexvm/gap_survey.cpp
    src/runtime/dexvm/method_precheck.cpp
    src/runtime/dexvm/object_model.cpp
    src/runtime/dexvm/interpreter.cpp
    src/runtime/dexvm/interpreter_context.cpp
    src/runtime/dexvm/vm_monitors.cpp
    src/runtime/dexvm/vm_threads.cpp
    src/runtime/dexvm/interp_exec.cpp
    src/runtime/dexvm/interp_object.cpp
    src/runtime/dexvm/interp_arith.cpp
    src/runtime/dexvm/intrinsic_builder.cpp
    src/runtime/jni/jni.cpp
    src/runtime/jni/jni_exception.cpp
    src/runtime/jni/jni_field_store.cpp
    src/runtime/jni/jni_environment.cpp
    src/runtime/jni/jni_java_vm.cpp
    src/runtime/jni/jni_array.cpp
    src/runtime/jni/jni_class_registry.cpp
    src/runtime/jni/jni_invocation.cpp
    src/runtime/jni/jni_native_registry.cpp
    src/runtime/jni/jni_native_export.cpp
    src/runtime/jni/jni_object.cpp
    src/runtime/jni/jni_object_array.cpp
    src/runtime/jni/jni_signature.cpp
    src/runtime/jni/jni_utf.cpp
    src/runtime/syscall/guest_thread_lifecycle.cpp
    src/runtime/execution/guest_clone_thread_runtime.cpp
    src/runtime/execution/guest_thread_runner.cpp
    src/runtime/syscall/syscall.cpp
    src/runtime/syscall/syscall_arm.cpp
    src/runtime/syscall/syscall_bridge.cpp
    src/runtime/syscall/syscall_file_metadata.cpp
    src/runtime/syscall/syscall_process.cpp
    src/runtime/syscall/syscall_signal.cpp
    src/runtime/syscall/syscall_thread_lifecycle.cpp
    src/runtime/vfs/sandbox_store.cpp
    src/runtime/vfs/vfs.cpp
    src/runtime/vfs/vfs_sandbox.cpp
)
add_library(OGPlay::Runtime ALIAS ogplay_runtime)
target_link_libraries(ogplay_runtime PUBLIC
    OGPlay::Audio OGPlay::Core OGPlay::Cpu OGPlay::Loader OGPlay::Memory
    OGPlay::Video
)
target_link_libraries(ogplay_runtime PRIVATE OGPlay::Gles)
target_include_directories(ogplay_runtime PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE "${OGPLAY_GLES_GENERATED_DIR}"
)
add_dependencies(ogplay_runtime ogplay_dexvm_codegen)
target_include_directories(ogplay_runtime SYSTEM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb)
if(MSVC)
    set_source_files_properties(src/runtime/integration/host_image_decode.cpp
        PROPERTIES COMPILE_OPTIONS "/w")
else()
    set_source_files_properties(src/runtime/integration/host_image_decode.cpp
        PROPERTIES COMPILE_OPTIONS "-w")
endif()
ogplay_set_project_warnings(ogplay_runtime)

add_library(ogplay_session
    src/session/dex_activity_lifecycle.cpp
    src/session/lifecycle.cpp
    src/session/profile_entry_scope.cpp
    src/session/profile_asset_bundle.cpp
    src/session/profile_apk.cpp
    src/session/profile_audio.cpp
    src/session/profile_vfs.cpp
    src/session/quirk_registry.cpp
    src/session/session.cpp
    src/session/title_profile.cpp
    src/session/title_profile_toml.cpp
)
add_library(OGPlay::Session ALIAS ogplay_session)
target_link_libraries(ogplay_session PUBLIC
    OGPlay::Audio OGPlay::Core OGPlay::Hal OGPlay::Input OGPlay::Memory
    OGPlay::Runtime
)
target_include_directories(ogplay_session PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
ogplay_set_project_warnings(ogplay_session)

add_library(ogplay_agent
    src/agent/control_service.cpp
    src/agent/coordinate_overlay.cpp
    src/agent/mcp_input.cpp
    src/agent/mcp_protocol.cpp
    src/agent/mcp_session_control.cpp
    src/agent/stb_image_write.cpp
)
add_library(OGPlay::Agent ALIAS ogplay_agent)
target_link_libraries(ogplay_agent PUBLIC OGPlay::Core OGPlay::Session)
target_include_directories(ogplay_agent SYSTEM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb)
if(MSVC)
    set_source_files_properties(src/agent/stb_image_write.cpp PROPERTIES
        COMPILE_OPTIONS "/w")
else()
    set_source_files_properties(src/agent/stb_image_write.cpp PROPERTIES
        COMPILE_OPTIONS "-w")
endif()
ogplay_set_project_warnings(ogplay_agent)

add_library(ogplay_frontend
    src/frontend/data_directory.cpp
    src/frontend/mcp_http_server.cpp
    src/frontend/mcp_input_dispatch.cpp
    src/frontend/run_apk_progress.cpp
    src/frontend/gui/import.cpp
    src/frontend/gui/launch.cpp
    src/frontend/gui/model.cpp
    src/frontend/gui/view_model.cpp
    src/frontend/gui/visuals.cpp
    src/frontend/user_data_dir.cpp
)
if(OGPLAY_ENABLE_SDL3 AND OGPLAY_ENABLE_ANGLE)
    add_library(ogplay_imgui STATIC
        third_party/imgui/imgui.cpp
        third_party/imgui/imgui_draw.cpp
        third_party/imgui/imgui_tables.cpp
        third_party/imgui/imgui_widgets.cpp
        third_party/imgui/backends/imgui_impl_sdl3.cpp
        third_party/imgui/backends/imgui_impl_opengl3.cpp
    )
    target_include_directories(ogplay_imgui SYSTEM PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
    target_compile_definitions(ogplay_imgui PRIVATE IMGUI_IMPL_OPENGL_ES2)
    target_link_libraries(ogplay_imgui PUBLIC SDL3::SDL3 ANGLE::GLESv2)
    if(MSVC)
        target_compile_options(ogplay_imgui PRIVATE /w)
    else()
        target_compile_options(ogplay_imgui PRIVATE -w)
    endif()

    target_sources(ogplay_frontend PRIVATE
        src/frontend/gui/import_ui.cpp
        src/frontend/gui/management_ui.cpp
        src/frontend/gui/process_manager.cpp
        src/frontend/gui/settings_ui.cpp
        src/frontend/gui/shell.cpp
        src/frontend/gui/ui_button.cpp)
    target_link_libraries(ogplay_frontend PRIVATE ogplay_imgui ANGLE::EGL ANGLE::GLESv2)
    target_compile_definitions(ogplay_frontend PUBLIC OGPLAY_HAS_GUI=1)
else()
    target_compile_definitions(ogplay_frontend PUBLIC OGPLAY_HAS_GUI=0)
endif()
add_library(OGPlay::Frontend ALIAS ogplay_frontend)
target_link_libraries(ogplay_frontend PUBLIC
    OGPlay::Agent OGPlay::Input OGPlay::Runtime)
target_include_directories(ogplay_frontend SYSTEM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ext-boost
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb)
target_compile_definitions(ogplay_frontend PRIVATE
    OGPLAY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
if(WIN32)
    target_link_libraries(ogplay_frontend PRIVATE ws2_32)
endif()
ogplay_set_project_warnings(ogplay_frontend)

add_executable(ogplay src/frontend/cli/main.cpp src/frontend/cli/run_apk.cpp
    src/frontend/cli/run_apk_vfs.cpp)
target_link_libraries(ogplay PRIVATE
    OGPlay::Frontend OGPlay::Hal OGPlay::Loader OGPlay::Runtime)
target_compile_definitions(ogplay PRIVATE OGPLAY_VERSION="${PROJECT_VERSION}")
target_compile_definitions(ogplay PRIVATE OGPLAY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
if(WIN32)
    set(OGPLAY_NATIVE_ANGLE_RENDERER d3d11)
elseif(APPLE)
    set(OGPLAY_NATIVE_ANGLE_RENDERER metal)
elseif(UNIX)
    set(OGPLAY_NATIVE_ANGLE_RENDERER vulkan)
else()
    message(FATAL_ERROR "OGPlay has no ANGLE renderer default for this host platform")
endif()
target_compile_definitions(ogplay PRIVATE
    OGPLAY_NATIVE_ANGLE_RENDERER="${OGPLAY_NATIVE_ANGLE_RENDERER}"
)
ogplay_set_project_warnings(ogplay)
ogplay_stage_angle_runtime(ogplay)

add_custom_target(ogplay_stage_data ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:ogplay>/data"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/data/quirks.toml"
        "$<TARGET_FILE_DIR:ogplay>/data/quirks.toml"
    COMMAND ${CMAKE_COMMAND} -E remove_directory
        "$<TARGET_FILE_DIR:ogplay>/data/profiles"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/data/profiles"
        "$<TARGET_FILE_DIR:ogplay>/data/profiles"
    DEPENDS ogplay
    COMMENT "Staging OGPlay Profile and quirk data"
    VERBATIM)

if(OGPLAY_ENABLE_SDL3 AND OGPLAY_ENABLE_ANGLE)
    add_executable(ogplay-gui WIN32 MACOSX_BUNDLE src/frontend/gui/main.cpp)
    target_link_libraries(ogplay-gui PRIVATE OGPlay::Frontend SDL3::SDL3)
    add_dependencies(ogplay-gui ogplay_stage_data)
    set_target_properties(ogplay-gui PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "OGPlay"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.ogplay.launcher")
    if(APPLE)
        set_target_properties(ogplay-gui PROPERTIES OUTPUT_NAME "OGPlay")
        add_custom_command(TARGET ogplay-gui POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:ogplay>
                $<TARGET_BUNDLE_CONTENT_DIR:ogplay-gui>/MacOS/ogplay
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                $<TARGET_FILE_DIR:ogplay>/data
                $<TARGET_BUNDLE_CONTENT_DIR:ogplay-gui>/Resources/data
            COMMENT "Embedding the OGPlay CLI in the launcher bundle"
            VERBATIM)
    endif()
    ogplay_set_project_warnings(ogplay-gui)
    ogplay_stage_angle_runtime(ogplay-gui)
    install(TARGETS ogplay-gui
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        BUNDLE DESTINATION .)
endif()

install(TARGETS ogplay ogplay_core ogplay_hal ogplay_audio ogplay_video ogplay_input ogplay_memory ogplay_cpu ogplay_loader ogplay_gles ogplay_runtime ogplay_agent ogplay_frontend ogplay_session
    EXPORT OGPlayTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES data/quirks.toml
    DESTINATION ${CMAKE_INSTALL_BINDIR}/data)
install(DIRECTORY data/profiles
    DESTINATION ${CMAKE_INSTALL_BINDIR}/data)

if(OGPLAY_BUILD_TESTS)
    include(CTest)
    include(cmake/TestDependencies.cmake)

    set(OGPLAY_TEST_SOURCES
        tests/test_main.cpp
        tests/core/capability_ledger_tests.cpp
        tests/core/json_tests.cpp
        tests/core/logger_tests.cpp
        tests/agent/control_service_tests.cpp
        tests/agent/mcp_protocol_tests.cpp
        tests/frontend/mcp_http_server_tests.cpp
        tests/frontend/mcp_input_dispatch_tests.cpp
        tests/frontend/run_apk_progress_tests.cpp
        tests/frontend/data_directory_tests.cpp
        tests/frontend/gui_import_tests.cpp
        tests/frontend/gui_launch_tests.cpp
        tests/frontend/gui_model_tests.cpp
        tests/frontend/gui_view_model_tests.cpp
        tests/frontend/gui_visuals_tests.cpp
        tests/golden/golden_frame_tests.cpp
        tests/gles/angle_backend_tests.cpp
        tests/gles/angle_frame_tests.cpp
        tests/gles/egl_lifecycle_tests.cpp
        tests/gles/etc1_tests.cpp
        tests/gles/gles_call_preparation_tests.cpp
        tests/gles/gles_dispatch_tests.cpp
        tests/gles/gles_transfer_state_tests.cpp
        tests/gles/guest_transfer_tests.cpp
        tests/gles/supersample_tests.cpp
        tests/hal/clock_tests.cpp
        tests/hal/audio_output_tests.cpp
        tests/hal/host_environment_tests.cpp
        tests/hal/service_contract_tests.cpp
        tests/hal/thread_tests.cpp
        tests/hal/window_input_tests.cpp
        tests/hal/virtual_memory_tests.cpp
        tests/input/template_catalog_tests.cpp
        tests/input/mouse_touch_mapper_tests.cpp
        tests/memory/address_tests.cpp
        tests/memory/address_space_tests.cpp
        tests/memory/bus_tests.cpp
        tests/memory/snapshot_tests.cpp
        tests/runtime/syscall_tests.cpp
        tests/runtime/syscall_bridge_tests.cpp
        tests/runtime/syscall_process_tests.cpp
        tests/runtime/syscall_signal_tests.cpp
        tests/runtime/bionic_module_set_tests.cpp
        tests/runtime/bionic_profile_tests.cpp
        tests/runtime/bionic_selfcheck_tests.cpp
        tests/runtime/bionic_tls_tests.cpp
        tests/runtime/framework_asset_tests.cpp
        tests/runtime/framework_lifecycle_tests.cpp
        tests/runtime/framework_locale_tests.cpp
        tests/runtime/framework_package_tests.cpp
        tests/runtime/framework_preferences_tests.cpp
        tests/runtime/guest_lifecycle_tests.cpp
        tests/runtime/headless_bionic_runner_tests.cpp
        tests/runtime/headless_jni_contract_tests.cpp
        tests/runtime/jni_tests.cpp
        tests/runtime/jni_exception_tests.cpp
        tests/runtime/jni_field_store_tests.cpp
        tests/runtime/jni_environment_tests.cpp
        tests/runtime/jni_guest_abi_tests.cpp
        tests/runtime/jni_guest_bindings_tests.cpp
        tests/runtime/jni_guest_instance_calls_tests.cpp
        tests/runtime/jni_guest_library_lifecycle_tests.cpp
        tests/runtime/jni_java_vm_tests.cpp
        tests/runtime/jni_array_tests.cpp
        tests/runtime/jni_class_registry_tests.cpp
        tests/runtime/jni_invocation_tests.cpp
        tests/runtime/jni_native_registry_tests.cpp
        tests/runtime/jni_native_export_tests.cpp
        tests/runtime/jni_object_tests.cpp
        tests/runtime/jni_object_array_tests.cpp
        tests/runtime/jni_signature_tests.cpp
        tests/runtime/jni_utf_tests.cpp
        tests/runtime/guest_thread_lifecycle_tests.cpp
        tests/runtime/guest_clone_thread_runtime_tests.cpp
        tests/runtime/guest_thread_runner_tests.cpp
        tests/runtime/api19_guest_process_tests.cpp
        tests/runtime/android_guest_call_session_tests.cpp
        tests/runtime/android_guest_framework_platform_tests.cpp
        tests/runtime/android_guest_java_media_tests.cpp
        tests/runtime/android_boundary_hle_tests.cpp
        tests/runtime/android_boundary_gles1_fixed_tests.cpp
        tests/runtime/android_link_preflight_tests.cpp
        tests/runtime/native_activity_runner_tests.cpp
        tests/runtime/preferences_xml_tests.cpp
        tests/runtime/sandbox_store_tests.cpp
        tests/runtime/vfs_sandbox_tests.cpp
        tests/runtime/vfs_tests.cpp
        tests/loader/apk_tests.cpp
        tests/loader/apk_manifest_tests.cpp
        tests/loader/binary_xml_tests.cpp
        tests/loader/apk_native_tests.cpp
        tests/loader/elf_tests.cpp
        tests/loader/dex_tests.cpp
        tests/loader/dex_analysis_tests.cpp
        tests/loader/dex_class_data_tests.cpp
        tests/loader/dynamic_linker_tests.cpp
        tests/loader/lifecycle_tests.cpp
        tests/loader/link_namespace_tests.cpp
        tests/loader/module_loader_tests.cpp
        tests/loader/relocation_tests.cpp
        tests/loader/symbol_version_tests.cpp
        tests/loader/tls_tests.cpp
        tests/cpu/state_tests.cpp
        tests/cpu/futex_tests.cpp
        tests/cpu/interpreter_tests.cpp
        tests/cpu/m1_guest_sample_tests.cpp
        tests/cpu/thread_group_tests.cpp
        tests/gles/pvrtc_tests.cpp
        tests/audio/ogg_vorbis_tests.cpp
        tests/audio/java_sound_pool_mixer_tests.cpp
        tests/video/video_player_tests.cpp
        tests/video/ffmpeg_video_player_tests.cpp
        tests/session/quirk_registry_tests.cpp
        tests/session/lifecycle_tests.cpp
        tests/session/profile_asset_bundle_tests.cpp
        tests/session/profile_apk_tests.cpp
        tests/session/profile_entry_scope_tests.cpp
        tests/session/profile_audio_tests.cpp
        tests/session/profile_vfs_tests.cpp
        tests/session/session_tests.cpp
        tests/session/title_profile_tests.cpp
        tests/dexvm/dexasm_readback_tests.cpp
        tests/dexvm/file_vfs_tests.cpp
        tests/dexvm/gap_survey_tests.cpp
        tests/dexvm/dex_code_tests.cpp
        tests/dexvm/interpreter_tests.cpp
        tests/dexvm/intrinsics_p1_tests.cpp
        tests/dexvm/vm_monitor_tests.cpp
        tests/dexvm/vm_thread_tests.cpp
        tests/dexvm/videoview_tests.cpp
        tests/dexvm/widget_click_tests.cpp
        tests/dexvm/arsc_tests.cpp
    )
    if(OGPLAY_ENABLE_DYNARMIC)
        list(APPEND OGPLAY_TEST_SOURCES
            tests/cpu/dynarmic_tests.cpp
            tests/runtime/arm_kernel_helpers_tests.cpp
        )
    endif()
    add_executable(ogplay_tests ${OGPLAY_TEST_SOURCES})
    target_link_libraries(ogplay_tests PRIVATE
        OGPlay::Frontend OGPlay::Cpu OGPlay::Gles OGPlay::Loader OGPlay::Runtime OGPlay::Video doctest::doctest
    )
    target_include_directories(ogplay_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/samples/m1_guest/include
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    target_include_directories(ogplay_tests SYSTEM PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ext-boost)
    if(OGPLAY_ENABLE_SDL3)
        target_compile_definitions(ogplay_tests PRIVATE OGPLAY_TEST_HAS_SDL3=1)
        target_link_libraries(ogplay_tests PRIVATE SDL3::SDL3)
    else()
        target_compile_definitions(ogplay_tests PRIVATE OGPLAY_TEST_HAS_SDL3=0)
    endif()
    target_compile_definitions(ogplay_tests PRIVATE
        OGPLAY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        OGPLAY_DEXVM_FIXTURE_DIR="${OGPLAY_DEXVM_FIXTURE_DIR}"
        OGPLAY_ANGLE_HAS_SWIFTSHADER=$<BOOL:${_angle_has_swiftshader}>
    )
    add_dependencies(ogplay_tests ogplay_dexvm_fixtures)
    add_dependencies(ogplay_tests ogplay_stage_data)
    if(MSVC)
        target_compile_options(ogplay_tests PRIVATE /wd5285)
        target_link_libraries(ogplay_tests PRIVATE ws2_32)
    endif()
    if(CYGWIN)
        target_compile_definitions(ogplay_tests PRIVATE DOCTEST_CONFIG_NO_POSIX_SIGNALS)
    endif()
    ogplay_set_project_warnings(ogplay_tests)

    ogplay_stage_angle_runtime(ogplay_tests)

    include(${OGPLAY_DOCTEST_CMAKE_MODULE})
    doctest_discover_tests(ogplay_tests
        PROPERTIES LABELS "unit;contract"
    )

    add_test(
        NAME architecture.no_raw_output
        COMMAND ${CMAKE_COMMAND}
            -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckNoRawOutput.cmake
    )
    set_tests_properties(architecture.no_raw_output PROPERTIES LABELS architecture)

    add_test(
        NAME architecture.capabilities_monotonic
        COMMAND ${CMAKE_COMMAND}
            -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckCapabilitiesMonotonic.cmake
    )
    set_tests_properties(architecture.capabilities_monotonic PROPERTIES LABELS architecture)

    add_test(
        NAME architecture.platform_boundaries
        COMMAND ${CMAKE_COMMAND}
            -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckPlatformBoundaries.cmake
    )
    set_tests_properties(architecture.platform_boundaries PROPERTIES LABELS architecture)

    add_test(
        NAME architecture.documentation_layout
        COMMAND ${CMAKE_COMMAND}
            -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckDocumentationLayout.cmake
    )
    set_tests_properties(architecture.documentation_layout PROPERTIES LABELS architecture)

    add_test(
        NAME frontend.run_apk_options
        COMMAND ${CMAKE_COMMAND}
            -DOGPLAY_CLI=$<TARGET_FILE:ogplay>
            -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckRunApkOptions.cmake
    )
    set_tests_properties(frontend.run_apk_options PROPERTIES LABELS "unit;frontend")

    if(OGPLAY_ENABLE_SDL3 AND OGPLAY_ENABLE_ANGLE)
        add_test(
            NAME frontend.gui_options
            COMMAND ${CMAKE_COMMAND}
                -DOGPLAY_CLI=$<TARGET_FILE:ogplay>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckGuiOptions.cmake
        )
        set_tests_properties(frontend.gui_options PROPERTIES LABELS "unit;frontend")

        add_test(
            NAME frontend.gui_smoke
            COMMAND $<TARGET_FILE:ogplay> gui
                --library-root ${CMAKE_CURRENT_BINARY_DIR}/gui-smoke-library
                --smoke-frames 3
        )
        set_tests_properties(frontend.gui_smoke PROPERTIES
            LABELS "integration;frontend"
            TIMEOUT 30)

        add_test(
            NAME frontend.gui_library_smoke
            COMMAND ${CMAKE_COMMAND}
                -DOGPLAY_CLI=$<TARGET_FILE:ogplay>
                -DGUI_LIBRARY_ROOT=${CMAKE_CURRENT_BINARY_DIR}/gui-library-smoke
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckGuiLibrarySmoke.cmake
        )
        set_tests_properties(frontend.gui_library_smoke PROPERTIES
            LABELS "integration;frontend"
            TIMEOUT 30)
    endif()

    add_test(
        NAME integration.m4_exit_strict_environment
        COMMAND ${CMAKE_COMMAND}
            -DOGPLAY_TESTS=$<TARGET_FILE:ogplay_tests>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckM4ExitStrict.cmake
    )
    set_tests_properties(integration.m4_exit_strict_environment
        PROPERTIES LABELS "integration;fixtures")

    add_test(
        NAME tools.gles_idl_generator_self_test
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_gles_api.py
            --self-test
    )
    set_tests_properties(tools.gles_idl_generator_self_test PROPERTIES LABELS tools)

    add_test(
        NAME tools.dexvm_stub_generator_self_test
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/dexvm_stub_gen.py
            --self-test
    )
    set_tests_properties(tools.dexvm_stub_generator_self_test PROPERTIES LABELS tools)

    add_test(
        NAME tools.gles2_catalog_current
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_gles_api.py
            --idl ${CMAKE_CURRENT_SOURCE_DIR}/data/gles/gles2.json
            --output ${OGPLAY_GLES2_CATALOG}
            --check
            --verify-header
                ${OGPLAY_ANGLE_SDK_ROOT}/include/GLES2/gl2.h
    )
    set_tests_properties(tools.gles2_catalog_current PROPERTIES LABELS tools)

    add_test(
        NAME tools.gles1_catalog_current
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_gles_api.py
            --idl ${CMAKE_CURRENT_SOURCE_DIR}/data/gles/gles1.json
            --output ${OGPLAY_GLES1_CATALOG}
            --check
            --verify-header
                ${OGPLAY_ANGLE_SDK_ROOT}/include/GLES/gl.h
    )
    set_tests_properties(tools.gles1_catalog_current PROPERTIES LABELS tools)

    add_test(
        NAME tools.gles1_extensions_catalog_current
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_gles_api.py
            --idl ${CMAKE_CURRENT_SOURCE_DIR}/data/gles/gles1_extensions.json
            --output ${OGPLAY_GLES1_EXTENSIONS_CATALOG}
            --check
            --verify-header
                ${OGPLAY_ANGLE_SDK_ROOT}/include/GLES/glext.h
    )
    set_tests_properties(tools.gles1_extensions_catalog_current PROPERTIES LABELS tools)

    add_test(
        NAME tools.title_profile_validator_self_test
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate_title_profiles.py
            --schema
                ${CMAKE_CURRENT_SOURCE_DIR}/data/profiles/title-profile-v2.schema.json
            --self-test
    )
    set_tests_properties(tools.title_profile_validator_self_test
        PROPERTIES LABELS "tools;profiles")

    add_test(
        NAME tools.title_profiles_current
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate_title_profiles.py
            --schema
                ${CMAKE_CURRENT_SOURCE_DIR}/data/profiles/title-profile-v2.schema.json
            --profiles ${CMAKE_CURRENT_SOURCE_DIR}/data/profiles
    )
    set_tests_properties(tools.title_profiles_current PROPERTIES LABELS "tools;profiles")

    add_test(
        NAME tools.scenario_validator_self_test
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate_scenarios.py
            --schema
                ${CMAKE_CURRENT_SOURCE_DIR}/data/scenarios/scenario-v1.schema.json
            --self-test
    )
    set_tests_properties(tools.scenario_validator_self_test
        PROPERTIES LABELS "tools;automation;scenarios")

    add_test(
        NAME tools.scenarios_current
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate_scenarios.py
            --schema
                ${CMAKE_CURRENT_SOURCE_DIR}/data/scenarios/scenario-v1.schema.json
            --scenarios ${CMAKE_CURRENT_SOURCE_DIR}/data/scenarios
            --profiles ${CMAKE_CURRENT_SOURCE_DIR}/data/profiles
    )
    set_tests_properties(tools.scenarios_current
        PROPERTIES LABELS "tools;automation;scenarios")

    add_test(
        NAME tools.scenario_model_self_test
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/scenario_model.py
            --result-schema
                ${CMAKE_CURRENT_SOURCE_DIR}/data/scenarios/scenario-result-v1.schema.json
            --self-test
    )
    set_tests_properties(tools.scenario_model_self_test
        PROPERTIES LABELS "tools;automation;scenarios")

    add_test(
        NAME tools.scenario_runner_self_test
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/run_scenario.py
            --result-schema
                ${CMAKE_CURRENT_SOURCE_DIR}/data/scenarios/scenario-result-v1.schema.json
            --self-test
    )
    set_tests_properties(tools.scenario_runner_self_test
        PROPERTIES LABELS "tools;automation;scenarios")

    add_test(
        NAME tools.quirk_registry_validator_self_test
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate_quirks.py
            --self-test
    )
    set_tests_properties(tools.quirk_registry_validator_self_test
        PROPERTIES LABELS "tools;profiles;quirks")

    add_test(
        NAME tools.quirk_registry_current
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate_quirks.py
            --registry ${CMAKE_CURRENT_SOURCE_DIR}/data/quirks.toml
            --profiles ${CMAKE_CURRENT_SOURCE_DIR}/data/profiles
            --root ${CMAKE_CURRENT_SOURCE_DIR}
    )
    set_tests_properties(tools.quirk_registry_current
        PROPERTIES LABELS "tools;profiles;quirks")

    add_test(
        NAME tools.m4_exit_sample_contract
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/samples/m4_exit/tools/build.py
            --contract-only
    )
    set_tests_properties(tools.m4_exit_sample_contract PROPERTIES LABELS "tools;fixtures")

    add_test(
        NAME tools.remote_incremental_self_test
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/remote_incremental.py
            --self-test
    )
    set_tests_properties(tools.remote_incremental_self_test PROPERTIES LABELS tools)

    add_test(
        NAME tools.aosp_dalvik_baseline_self_test
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/verify_aosp_dalvik.py
            --self-test
    )
    set_tests_properties(tools.aosp_dalvik_baseline_self_test
        PROPERTIES LABELS "tools;dexvm")

    add_test(
        NAME tools.aosp_dalvik_baseline_current
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/verify_aosp_dalvik.py
            --baseline ${CMAKE_CURRENT_SOURCE_DIR}/third_party/aosp-dalvik
    )
    set_tests_properties(tools.aosp_dalvik_baseline_current
        PROPERTIES LABELS "tools;dexvm")

    add_test(
        NAME tools.dexvm_opcode_generator_self_test
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_dexvm_opcodes.py
            --self-test
    )
    set_tests_properties(tools.dexvm_opcode_generator_self_test
        PROPERTIES LABELS "tools;dexvm")

    add_test(
        NAME tools.dexvm_opcode_catalog_current
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_dexvm_opcodes.py
            --catalog ${CMAKE_CURRENT_SOURCE_DIR}/data/dexvm/dalvik_opcodes.json
            --verify-aosp ${CMAKE_CURRENT_SOURCE_DIR}/third_party/aosp-dalvik
            --output ${OGPLAY_DEXVM_OPCODE_TABLE}
            --check
    )
    set_tests_properties(tools.dexvm_opcode_catalog_current
        PROPERTIES LABELS "tools;dexvm")

    add_test(
        NAME tools.dexasm_self_test
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/dexasm.py
            --catalog ${CMAKE_CURRENT_SOURCE_DIR}/data/dexvm/dalvik_opcodes.json
            --self-test
    )
    set_tests_properties(tools.dexasm_self_test
        PROPERTIES LABELS "tools;dexvm")

    add_test(
        NAME tools.dex_dependency_survey_self_test
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/dex_dependency_survey.py
            --self-test
    )
    set_tests_properties(tools.dex_dependency_survey_self_test
        PROPERTIES LABELS "tools;dexvm")

    add_test(
        NAME tools.m4_local_exit_self_test
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/m4_exit.py
            --self-test
    )
    set_tests_properties(tools.m4_local_exit_self_test
        PROPERTIES LABELS "tools;fixtures")

    add_test(
        NAME tools.angle_build_driver_self_test
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/angle-prebuilt/tools/build_angle.py
            --self-test
    )
    set_tests_properties(tools.angle_build_driver_self_test PROPERTIES LABELS tools)

    add_test(
        NAME tools.angle_sdk_packager_self_test
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/angle-prebuilt/tools/package_angle_sdk.py
            --self-test
    )
    set_tests_properties(tools.angle_sdk_packager_self_test PROPERTIES LABELS tools)
endif()
