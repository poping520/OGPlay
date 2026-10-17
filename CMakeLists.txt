cmake_minimum_required(VERSION 3.25)

project(OGPlay VERSION 0.1.0 LANGUAGES C CXX)
if(WIN32)
    enable_language(RC)
    configure_file(cmake/ogplay.rc.in generated/ogplay.rc @ONLY)
    set(_ogplay_icon_resource "${CMAKE_CURRENT_BINARY_DIR}/generated/ogplay.rc")
endif()
if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(OGPLAY_BUILD_TESTS "Build OGPlay unit and contract tests" ON)
option(OGPLAY_WARNINGS_AS_ERRORS "Treat OGPlay warnings as errors" OFF)
option(OGPLAY_ENABLE_SDL3 "Configure the bundled SDL3 platform dependency" ON)
set(_ogplay_gui_default OFF)
if((WIN32 OR APPLE) AND OGPLAY_ENABLE_SDL3)
    set(_ogplay_gui_default ON)
endif()
option(OGPLAY_BUILD_GUI "Build the desktop WebView host (Windows WebView2, macOS WKWebView)" ${_ogplay_gui_default})
if(OGPLAY_BUILD_GUI AND (NOT (WIN32 OR APPLE) OR NOT OGPLAY_ENABLE_SDL3))
    message(FATAL_ERROR "OGPLAY_BUILD_GUI requires the Windows or macOS SDL3 host; other GUI hosts are not implemented")
endif()
# All internal and bundled static libraries must also be usable in ogplaycore.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
option(OGPLAY_ENABLE_DYNARMIC "Configure the bundled Dynarmic CPU dependency" ON)
option(OGPLAY_ENABLE_ANGLE "Link an externally GN-built ANGLE SDK" ON)
set(OGPLAY_ANGLE_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/angle-prebuilt"
    CACHE PATH "Root of the platform-partitioned ANGLE prebuilt SDK")
set(OGPLAY_ANGLE_SDK_CONFIGURATION "release" CACHE STRING
    "ANGLE SDK configuration to consume (release or debug)")
set_property(CACHE OGPLAY_ANGLE_SDK_CONFIGURATION PROPERTY STRINGS release debug)
set(OGPLAY_ENABLE_FFMPEG "AUTO" CACHE STRING
    "Link the prebuilt ogplay_ffmpeg SDK: AUTO (when this platform has a package), ON or OFF")
set_property(CACHE OGPLAY_ENABLE_FFMPEG PROPERTY STRINGS AUTO ON OFF)
set(OGPLAY_FFMPEG_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg-prebuilt"
    CACHE PATH "Root of the platform-partitioned ogplay_ffmpeg prebuilt SDK")

include(GNUInstallDirs)
include(cmake/CompilerWarnings.cmake)
include(cmake/ThirdParty.cmake)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

include(cmake/RuntimeStaging.cmake)

set(OGPLAY_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")

# Internal static module: OGPlay::<alias>, public include/ tree, project warnings.
function(ogplay_add_module target alias)
    add_library(${target} STATIC ${ARGN})
    add_library(OGPlay::${alias} ALIAS ${target})
    target_include_directories(${target} PUBLIC "${PROJECT_SOURCE_DIR}/include")
    ogplay_set_project_warnings(${target})
endfunction()

ogplay_add_module(ogplay_core Core
    src/core/sha256.cpp
    third_party/yyjson/src/yyjson.c
    src/core/capability_ledger.cpp
    src/core/encoding.cpp
    src/core/frame_compare.cpp
    src/core/json.cpp
    src/core/logger.cpp
    src/core/software_surface.cpp
    src/core/text.cpp
)
target_include_directories(ogplay_core SYSTEM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/yyjson/src)
ogplay_suppress_source_warnings(third_party/yyjson/src/yyjson.c)

ogplay_add_module(ogplay_hal Hal
    src/hal/clock.cpp
    src/hal/fs.cpp
    src/hal/sdl_audio_output.cpp
    src/hal/sdl_window_input.cpp
    src/hal/thread.cpp
)
if(WIN32)
    target_sources(ogplay_hal PRIVATE src/hal/rgb_surface_storage.cpp)
    target_link_libraries(ogplay_hal PRIVATE bcrypt)
    target_sources(ogplay_hal PRIVATE
        src/hal/windows/file_lock.cpp
        src/hal/windows/diagnostic_trigger.cpp
        src/hal/from_chars.cpp
        src/hal/windows/host_environment.cpp
        src/hal/windows/host_read_file.cpp
        src/hal/windows/shared_library.cpp
        src/hal/windows/virtual_memory.cpp)
elseif(APPLE)
    target_sources(ogplay_hal PRIVATE src/hal/macos/rgb_surface_storage.cpp)
    target_sources(ogplay_hal PRIVATE src/hal/macos/packed_rgb_surface_storage.mm)
    target_link_libraries(ogplay_hal PRIVATE "-framework Metal" "-framework Foundation")
    target_link_libraries(ogplay_hal PRIVATE "-framework IOSurface" "-framework CoreFoundation")
    target_sources(ogplay_hal PRIVATE
        src/hal/macos/file_lock.cpp
        src/hal/macos/diagnostic_trigger.cpp
        src/hal/macos/from_chars.cpp
        src/hal/macos/host_environment.cpp
        src/hal/macos/host_read_file.cpp
        src/hal/macos/shared_library.cpp
        src/hal/macos/virtual_memory.cpp)
elseif(UNIX)
    target_sources(ogplay_hal PRIVATE src/hal/rgb_surface_storage.cpp)
    target_sources(ogplay_hal PRIVATE
        src/hal/linux/file_lock.cpp
        src/hal/linux/diagnostic_trigger.cpp
        src/hal/from_chars.cpp
        src/hal/linux/host_environment.cpp
        src/hal/linux/host_read_file.cpp
        src/hal/linux/shared_library.cpp
        src/hal/linux/virtual_memory.cpp)
else()
    message(FATAL_ERROR "OGPlay has no virtual memory HAL for this host platform")
endif()
if(OGPLAY_ENABLE_SDL3)
    target_compile_definitions(ogplay_hal PRIVATE OGPLAY_HAS_SDL3=1)
    target_link_libraries(ogplay_hal PRIVATE SDL3::SDL3)
else()
    target_compile_definitions(ogplay_hal PRIVATE OGPLAY_HAS_SDL3=0)
endif()
if(NOT WIN32)
    target_link_libraries(ogplay_hal PRIVATE ${CMAKE_DL_LIBS})
endif()

ogplay_add_module(ogplay_audio Audio
    src/audio/java_sound_pool_mixer.cpp
    src/audio/mp3.cpp
    src/audio/open_sles_pcm_mixer.cpp
    src/audio/ogg_vorbis.cpp
    src/audio/pcm_mix.cpp
    src/audio/wav.cpp
    src/audio/encoded_audio.cpp
    src/audio/encoded_music.cpp
    src/audio/encoded_audio_stream.cpp
    third_party/stb/stb_vorbis.c
)
set_source_files_properties(third_party/stb/stb_vorbis.c PROPERTIES LANGUAGE CXX)
ogplay_suppress_source_warnings(third_party/stb/stb_vorbis.c)

ogplay_add_module(ogplay_video Video
    src/video/video_player.cpp
    src/video/fake_video_player.cpp
    src/video/buffered_video_player.cpp
    src/video/rgba_canvas.cpp
)
target_link_libraries(ogplay_video PRIVATE OGPlay::Hal)
if(OGPLAY_FFMPEG_FOUND)
    target_sources(ogplay_video PRIVATE src/video/ffmpeg_video_player.cpp)
    target_link_libraries(ogplay_video PRIVATE FFmpeg::FFmpeg)
    target_compile_definitions(ogplay_video PUBLIC OGPLAY_HAS_FFMPEG=1)
else()
    target_sources(ogplay_video PRIVATE src/video/ffmpeg_disabled.cpp)
    target_compile_definitions(ogplay_video
        PUBLIC OGPLAY_HAS_FFMPEG=0
        PRIVATE "OGPLAY_FFMPEG_MISSING_REASON=\"${OGPLAY_FFMPEG_MISSING_REASON}\"")
endif()

ogplay_add_module(ogplay_input Input
    src/input/mouse_touch_mapper.cpp
    src/input/touch_mapper.cpp
    src/input/template_catalog.cpp
    src/input/control_mapper.cpp
)
target_link_libraries(ogplay_input PUBLIC OGPlay::Hal OGPlay::Core)

ogplay_add_module(ogplay_memory Memory
    src/memory/address_space.cpp
    src/memory/bus.cpp
)
target_link_libraries(ogplay_memory PRIVATE OGPlay::Hal)

ogplay_add_module(ogplay_cpu Cpu
    src/cpu/execution_budget.cpp
    src/cpu/futex.cpp
    src/cpu/interpreter.cpp
    src/cpu/state.cpp
    src/cpu/thread_group.cpp
)
target_link_libraries(ogplay_cpu PUBLIC OGPlay::Memory PRIVATE OGPlay::Hal)
if(OGPLAY_ENABLE_DYNARMIC)
    target_sources(ogplay_cpu PRIVATE src/cpu/dynarmic.cpp)
    target_compile_definitions(ogplay_cpu PUBLIC OGPLAY_HAS_DYNARMIC=1)
    target_link_libraries(ogplay_cpu PRIVATE dynarmic)
else()
    target_compile_definitions(ogplay_cpu PUBLIC OGPLAY_HAS_DYNARMIC=0)
endif()

ogplay_add_module(ogplay_loader Loader
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
target_link_libraries(ogplay_loader PUBLIC OGPlay::Memory PRIVATE OGPlay::Core)

add_library(ogplay_pvrtc_vendor OBJECT
    third_party/powervr/PVRTDecompress.cpp
)
target_include_directories(ogplay_pvrtc_vendor PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/powervr")

# Pinned upstream decoder inputs must remain byte-for-byte reproducible.
foreach(_atc_input IN ITEMS
    "compressonatori_tc.c:ceef7ffc0b97a3c8d624cd650f0adea3d5fd8cdb5c6459c2c5d4bbb9c2e4013d"
    "compressonator_tc.h:02871dd9632c093aaab7cc0d52adc08c68716dcb994aed3821ea3a9f2a2db653"
    "LICENSE.txt:a408ebd610b0e0390aaa4be2139808e84f1968a54cd8ef40de8a3bed43b38906")
    string(REPLACE ":" ";" _atc_parts "${_atc_input}")
    list(GET _atc_parts 0 _atc_file)
    list(GET _atc_parts 1 _atc_expected)
    file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/third_party/compressonator/${_atc_file}" _atc_actual)
    if(NOT _atc_actual STREQUAL _atc_expected)
        message(FATAL_ERROR "Pinned ATC vendor file changed: ${_atc_file}")
    endif()
endforeach()

add_library(ogplay_atc_vendor OBJECT
    third_party/compressonator/compressonatori_tc.c)
target_include_directories(ogplay_atc_vendor PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/compressonator")

ogplay_add_module(ogplay_gles Gles
    src/gles/angle_readback.cpp
    $<TARGET_OBJECTS:ogplay_atc_vendor>
    src/gles/atc.cpp
    $<TARGET_OBJECTS:ogplay_pvrtc_vendor>
    src/gles/angle_backend.cpp
    src/gles/angle_frame.cpp
    src/gles/angle_frame_framebuffer.cpp
    src/gles/angle_frame_gles3.cpp
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
include(cmake/Codegen.cmake)
file(GLOB OGPLAY_DEXVM_INTRINSIC_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/dexvm/intrinsics/*.cpp")
file(GLOB OGPLAY_DEXVM_ANDROID_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/integration/dexvm_android/*.cpp")
add_dependencies(ogplay_gles ogplay_gles_codegen)
target_include_directories(ogplay_gles PRIVATE
    "${OGPLAY_GENERATED_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/powervr"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/compressonator"
)
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

set(OGPLAY_BOOST_URL_CODEC_SOURCES
    third_party/ext-boost/libs/url/src/detail/decode.cpp
    third_party/ext-boost/libs/url/src/detail/except.cpp
    third_party/ext-boost/libs/url/src/error.cpp
    third_party/ext-boost/libs/url/src/grammar/error.cpp
    third_party/ext-boost/libs/url/src/pct_string_view.cpp
)

add_library(ogplay_sqlite STATIC third_party/sqlite-amalgamation/sqlite3.c)
target_include_directories(ogplay_sqlite PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite-amalgamation")
target_compile_definitions(ogplay_sqlite PRIVATE
    SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION=1
    SQLITE_DEFAULT_MEMSTATUS=0)
set_target_properties(ogplay_sqlite PROPERTIES C_EXTENSIONS ON)

ogplay_add_module(ogplay_runtime Runtime
    src/runtime/database/database_runtime.cpp
    src/runtime/database/sqlite_vfs.cpp
    src/runtime/debug/stall_diagnostics.cpp
    src/runtime/ui/ui_layout.cpp
    src/runtime/ui/ui_renderer.cpp
    src/runtime/ui/ui_fallback_font.cpp
    src/runtime/ui/ui_tree.cpp
    src/runtime/syscall/arm_kernel_helpers.cpp
    src/runtime/bionic/bionic_module_set.cpp
    src/runtime/bionic/bionic_profile.cpp
    src/runtime/bionic/guest_symbol_override.cpp
    src/runtime/bionic/bionic_selfcheck.cpp
    src/runtime/bionic/bionic_tls.cpp
    src/runtime/framework/framework_asset.cpp
    src/runtime/framework/preferences_xml.cpp
    src/runtime/framework/framework_lifecycle.cpp
    src/runtime/framework/framework_locale.cpp
    src/runtime/framework/framework_package.cpp
    src/runtime/framework/framework_preferences.cpp
    src/runtime/execution/guest_lifecycle.cpp
    src/runtime/integration/guest_process_environment.cpp
    src/runtime/integration/api19_guest_process.cpp
    src/runtime/integration/api19_linker_view.cpp
    src/runtime/integration/android_guest_call_session.cpp
    src/runtime/integration/guest_cpu_environment.cpp
    src/runtime/integration/guest_memory_facts.cpp
    src/runtime/integration/nested_guest_cpu_pool.cpp
    src/runtime/integration/android_guest_java_movie.cpp
    src/runtime/integration/android_guest_java_platform.cpp
    src/runtime/integration/native_activity_runtime.cpp
    src/runtime/integration/native_library_loader.cpp
    src/runtime/integration/headless_bionic_runner.cpp
    src/runtime/boundary/core/a32_call_frame.cpp
    src/runtime/boundary/core/boundary_catalog.cpp
    src/runtime/boundary/core/boundary_fast_router.cpp
    src/runtime/boundary/core/boundary_fault.cpp
    src/runtime/boundary/core/boundary_symbols.cpp
    src/runtime/boundary/core/boundary_thunk_arena.cpp
    src/runtime/boundary/services/frame_service.cpp
    src/runtime/boundary/services/graphics_dispatch.cpp
    src/runtime/boundary/services/buffer_mapping.cpp
    src/runtime/boundary/services/guest_gl_context.cpp
    src/runtime/boundary/modules/bitmap/bitmap_module.cpp
    src/runtime/boundary/modules/android/android_module.cpp
    src/runtime/boundary/modules/android/android_input.cpp
    src/runtime/boundary/modules/android/android_looper.cpp
    src/runtime/boundary/modules/android/android_surface.cpp
    src/runtime/boundary/modules/android/android_sensor.cpp
    src/runtime/boundary/modules/android/android_asset.cpp
    src/runtime/boundary/modules/egl/egl_module.cpp
    src/runtime/boundary/modules/gles1/gles1_bounds.cpp
    src/runtime/boundary/modules/gles1/gles1_completion.cpp
    src/runtime/boundary/modules/gles1/gles1_dispatch.cpp
    src/runtime/boundary/modules/gles1/gles1_draw.cpp
    src/runtime/boundary/modules/gles1/gles1_fixed.cpp
    src/runtime/boundary/modules/gles1/gles1_query.cpp
    src/runtime/boundary/modules/gles1/gles1_remaining.cpp
    src/runtime/boundary/modules/gles2/gles2_shader_completion.cpp
    src/runtime/boundary/modules/gles2/gles2_transfer.cpp
    src/runtime/boundary/modules/gles2/gles2_vertex_completion.cpp
    src/runtime/boundary/modules/log/log_module.cpp
    src/runtime/boundary/modules/opensles/opensles_abi.cpp
    src/runtime/boundary/modules/opensles/opensles_module.cpp
    src/runtime/boundary/modules/module_catalog.cpp
    src/runtime/boundary/facade/android_boundary_hle.cpp
    src/runtime/integration/android_link_preflight.cpp
    ${OGPLAY_DEXVM_ANDROID_SOURCES}
    src/runtime/integration/bitmap_pixels.cpp
    src/runtime/integration/apk_assets.cpp
    src/runtime/integration/dexvm_bridge.cpp
    src/runtime/integration/dexvm_io_vfs.cpp
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
    src/runtime/dexvm/class_name_codec.cpp
    src/runtime/dexvm/class_loader_facade.cpp
    src/runtime/dexvm/big_int_runtime.cpp
    src/runtime/dexvm/io_runtime.cpp
    src/runtime/dexvm/network_runtime.cpp
    src/runtime/dexvm/unsafe_runtime.cpp
    src/runtime/dexvm/nio_runtime.cpp
    src/runtime/dexvm/zip_runtime.cpp
    src/runtime/dexvm/reflection.cpp
    src/runtime/dexvm/reflection_codec.cpp
    src/runtime/dexvm/annotation_runtime.cpp
    src/runtime/dexvm/class_linker.cpp
    src/runtime/dexvm/class_linker_resolve.cpp
    src/runtime/dexvm/diagnostics.cpp
    src/runtime/dexvm/fault_invoke_diagnostics.cpp
    src/runtime/dexvm/fast_code.cpp
    ${OGPLAY_DEXVM_INTRINSIC_SOURCES}
    src/runtime/dexvm/gap_survey.cpp
    src/runtime/dexvm/method_precheck.cpp
    src/runtime/dexvm/object_model.cpp
    src/runtime/dexvm/interpreter.cpp
    src/runtime/dexvm/interpreter_context.cpp
    src/runtime/dexvm/vm_monitors.cpp
    src/runtime/dexvm/vm_threads.cpp
    src/runtime/dexvm/interp_exec.cpp
    src/runtime/dexvm/interp_threaded.cpp
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
    src/runtime/vfs/host_file_backing.cpp
    src/runtime/vfs/vfs_sandbox.cpp
    ${OGPLAY_BOOST_URL_CODEC_SOURCES}
)
target_link_libraries(ogplay_runtime PUBLIC
    OGPlay::Audio OGPlay::Core OGPlay::Cpu OGPlay::Hal OGPlay::Loader OGPlay::Memory
    OGPlay::Video
)
target_link_libraries(ogplay_runtime PRIVATE OGPlay::Gles ogplay_sqlite)
target_include_directories(ogplay_runtime PRIVATE
    "${OGPLAY_GENERATED_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
add_dependencies(ogplay_runtime ogplay_dexvm_codegen)
target_include_directories(ogplay_runtime SYSTEM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ext-boost
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb)
target_compile_definitions(ogplay_runtime PRIVATE BOOST_URL_NO_LIB=1)
ogplay_suppress_source_warnings(src/runtime/integration/host_image_decode.cpp
    ${OGPLAY_BOOST_URL_CODEC_SOURCES})

ogplay_add_module(ogplay_session Session
    src/session/android_input_mapping.cpp
    src/session/android_app_process.cpp
    src/session/audio_output_pump.cpp
    src/session/dex_activity_lifecycle.cpp
    src/session/ui_compositor.cpp
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
    src/session/control_config.cpp
)
target_link_libraries(ogplay_session PUBLIC
    OGPlay::Audio OGPlay::Core OGPlay::Hal OGPlay::Input OGPlay::Memory
    OGPlay::Runtime
)

ogplay_add_module(ogplay_agent Agent
    src/agent/dashboard.cpp
    src/agent/control_service.cpp
    src/agent/control_editor.cpp
    src/agent/coordinate_overlay.cpp
    src/agent/mcp_input.cpp
    src/agent/mcp_protocol.cpp
    src/agent/mcp_session_control.cpp
    src/agent/stb_image_write.cpp
)
target_link_libraries(ogplay_agent PUBLIC OGPlay::Core OGPlay::Session)
target_include_directories(ogplay_agent SYSTEM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb)
ogplay_suppress_source_warnings(src/agent/stb_image_write.cpp)

add_library(ogplay_library STATIC
    src/frontend/library/model.cpp src/frontend/library/import.cpp
    src/frontend/library/visuals.cpp src/frontend/library/settings.cpp
    src/frontend/library/game_settings.cpp src/frontend/library/profiles.cpp
    src/frontend/library/resolve_launch.cpp src/frontend/library/locks.cpp)
target_link_libraries(ogplay_library PRIVATE OGPlay::Runtime OGPlay::Session OGPlay::Agent)
target_include_directories(ogplay_library PRIVATE "${OGPLAY_GENERATED_DIR}")
target_include_directories(ogplay_library SYSTEM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb")
target_compile_definitions(ogplay_library PRIVATE ogplaycore_EXPORTS)
set_target_properties(ogplay_library PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
ogplay_set_project_warnings(ogplay_library)

ogplay_add_module(ogplay_frontend Frontend
    src/frontend/data_directory.cpp
    src/frontend/control_overlay.cpp
    src/frontend/control_requests.cpp
    src/frontend/host_shortcuts.cpp
    src/frontend/mcp_http_server.cpp
    src/frontend/mcp_input_dispatch.cpp
    src/frontend/run_apk_progress.cpp
    src/frontend/game/run_game_vfs.cpp
    src/frontend/user_data_dir.cpp
)
target_link_libraries(ogplay_frontend PUBLIC
    OGPlay::Agent OGPlay::Input OGPlay::Runtime)
target_include_directories(ogplay_frontend SYSTEM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ext-boost
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb)
target_compile_definitions(ogplay_frontend PRIVATE
    OGPLAY_VERSION="${PROJECT_VERSION}"
    OGPLAY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
if(WIN32)
    target_link_libraries(ogplay_frontend PRIVATE ws2_32)
endif()

# Application facade aggregates runtime and shared library domain only.
add_library(ogplaycore SHARED
    src/frontend/app/json.cpp src/frontend/app/logging.cpp src/frontend/app/encoding.cpp
    src/frontend/app/game_run.cpp src/frontend/app/services.cpp src/frontend/game/run_options.cpp
    src/frontend/game/run_game.cpp)
target_link_libraries(ogplaycore PRIVATE
    OGPlay::Frontend "$<LINK_LIBRARY:WHOLE_ARCHIVE,ogplay_library>" OGPlay::Hal OGPlay::Loader OGPlay::Runtime)
target_compile_definitions(ogplaycore PRIVATE
    OGPLAY_VERSION="${PROJECT_VERSION}"
    OGPLAY_HAS_SDL3=$<BOOL:${OGPLAY_ENABLE_SDL3}>)
if(OGPLAY_ENABLE_SDL3)
    target_link_libraries(ogplaycore PRIVATE SDL3::SDL3)
endif()
if(WIN32)
    set(OGPLAY_NATIVE_ANGLE_RENDERER d3d11)
elseif(APPLE)
    set(OGPLAY_NATIVE_ANGLE_RENDERER metal)
elseif(UNIX)
    set(OGPLAY_NATIVE_ANGLE_RENDERER vulkan)
else()
    message(FATAL_ERROR "OGPlay has no ANGLE renderer default for this host platform")
endif()
target_compile_definitions(ogplaycore PRIVATE
    OGPLAY_NATIVE_ANGLE_RENDERER="${OGPLAY_NATIVE_ANGLE_RENDERER}")
include(GenerateExportHeader)
generate_export_header(ogplaycore BASE_NAME OGPLAYCORE
    EXPORT_FILE_NAME "${OGPLAY_GENERATED_DIR}/ogplay/app/export.h"
    EXPORT_MACRO_NAME OGPLAYCORE_API)
if(MSVC)
    # Same-build /MD facade deliberately carries STL/Pimpl and std exceptions.
    target_compile_options(ogplaycore PUBLIC /wd4251 /wd4275)
    target_compile_options(ogplay_library PRIVATE /wd4251 /wd4275)
endif()

target_include_directories(ogplaycore PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${OGPLAY_GENERATED_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
set_target_properties(ogplaycore PROPERTIES
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
ogplay_set_project_warnings(ogplaycore)
ogplay_stage_angle_runtime(ogplaycore)
ogplay_stage_ffmpeg_runtime(ogplaycore)

if(OGPLAY_ENABLE_SDL3)
    add_custom_command(TARGET ogplaycore POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:ogplaycore>)
    install(TARGETS SDL3-shared RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Runtime)
endif()

include(cmake/WebUi.cmake)
if(OGPLAY_BUILD_GUI OR OGPLAY_BUILD_TESTS)
    add_library(ogplay_desktop_app STATIC
        src/frontend/gui/launch.cpp src/frontend/gui/dashboard.cpp
        src/frontend/gui/rpc.cpp src/frontend/gui/rpc_import.cpp
        src/frontend/gui/rpc_settings.cpp src/frontend/gui/view_model.cpp)
    target_link_libraries(ogplay_desktop_app PUBLIC ogplaycore PRIVATE ogplay_desktop_webview)
    target_include_directories(ogplay_desktop_app PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_include_directories(ogplay_desktop_app SYSTEM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ext-boost")
    target_compile_definitions(ogplay_desktop_app PRIVATE OGPLAY_VERSION="${PROJECT_VERSION}")
    if(OGPLAY_ENABLE_SDL3)
        target_sources(ogplay_desktop_app PRIVATE src/frontend/gui/process_manager.cpp src/frontend/gui/webview_shell.cpp)
        target_link_libraries(ogplay_desktop_app PRIVATE SDL3::SDL3)
    endif()
    if(OGPLAY_BUILD_GUI)
        add_dependencies(ogplay_desktop_app ogplay_check_webui)
    endif()
    if(WIN32)
        target_link_libraries(ogplay_desktop_app PRIVATE ws2_32)
    endif()
    ogplay_set_project_warnings(ogplay_desktop_app)
endif()

add_library(ogplay_cli_app STATIC src/frontend/cli/commands.cpp src/frontend/cli/library_commands.cpp)
target_link_libraries(ogplay_cli_app PUBLIC ogplaycore)
target_compile_definitions(ogplay_cli_app PRIVATE OGPLAY_VERSION="${PROJECT_VERSION}")
ogplay_set_project_warnings(ogplay_cli_app)

if(WIN32)
    add_executable(ogplay-cli src/frontend/cli/main.cpp src/hal/windows/cli_entry.cpp ${_ogplay_icon_resource})
else()
    add_executable(ogplay-cli src/frontend/cli/main.cpp)
endif()
target_link_libraries(ogplay-cli PRIVATE ogplay_cli_app ogplaycore)
target_compile_definitions(ogplay-cli PRIVATE OGPLAY_NATIVE_ENTRY_ADAPTER=$<BOOL:${WIN32}>)
ogplay_set_project_warnings(ogplay-cli)

if(OGPLAY_BUILD_GUI)
    if(WIN32)
        add_executable(ogplay WIN32 src/frontend/desktop/main.cpp src/hal/windows/desktop_entry.cpp ${_ogplay_icon_resource})
    else()
        # OGPlay.app: executable in Contents/MacOS, shared runtime in Contents/Frameworks.
        add_executable(ogplay MACOSX_BUNDLE src/frontend/desktop/main.cpp)
        target_sources(ogplay PRIVATE assets/icons/ogplay.icns)
        set_source_files_properties(assets/icons/ogplay.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
        set_target_properties(ogplay PROPERTIES
            OUTPUT_NAME OGPlay
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/macos/Info.plist.in"
            MACOSX_BUNDLE_BUNDLE_NAME OGPlay
            MACOSX_BUNDLE_ICON_FILE ogplay.icns
            MACOSX_BUNDLE_GUI_IDENTIFIER org.ogplay.OGPlay
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
            BUILD_WITH_INSTALL_RPATH ON
            INSTALL_RPATH "@executable_path/../Frameworks")
    endif()
    target_link_libraries(ogplay PRIVATE ogplay_desktop_app ogplaycore)
    target_include_directories(ogplay PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(ogplay PRIVATE OGPLAY_VERSION="${PROJECT_VERSION}"
        OGPLAY_NATIVE_ENTRY_ADAPTER=$<BOOL:${WIN32}>)
    ogplay_set_project_warnings(ogplay)
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT ogplay)
endif()
if(WIN32)
    target_link_libraries(ogplay-cli PRIVATE shell32)
    if(TARGET ogplay)
        target_link_libraries(ogplay PRIVATE shell32)
    endif()
elseif(APPLE)
    set_target_properties(ogplaycore PROPERTIES INSTALL_NAME_DIR "@rpath")
    target_link_options(ogplaycore PRIVATE
        "LINKER:-exported_symbol,__ZN6ogplay3app*"
        "LINKER:-exported_symbol,__ZNK6ogplay3app*"
        "LINKER:-exported_symbol,__ZT?N6ogplay3app*")
    # ogplaycore and its runtime dependencies always sit in one directory (build tree,
    # lib/ install or Contents/Frameworks), so it carries no absolute build paths.
    set_target_properties(ogplaycore PROPERTIES
        BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "@loader_path")
    set_target_properties(ogplay-cli PROPERTIES
        BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
else()
    file(WRITE "${OGPLAY_GENERATED_DIR}/ogplaycore.exports"
        "{ global: extern \"C++\" { ogplay::app::*; }; _ZT?N6ogplay3app*; local: *; };\n")
    target_link_options(ogplaycore PRIVATE
        "LINKER:--version-script,${OGPLAY_GENERATED_DIR}/ogplaycore.exports")
    set_target_properties(ogplaycore ogplay-cli PROPERTIES
        BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
endif()

include(cmake/RuntimePackaging.cmake)

if(OGPLAY_BUILD_TESTS)
    include(cmake/Tests.cmake)
endif()
