# Included from the project root; source/binary paths use that directory scope.
include(CTest)
set(_ogplay_gui_test_path "")
if(TARGET ogplay)
    set(_ogplay_gui_test_path "$<TARGET_FILE:ogplay>")
endif()
add_test(NAME frontend.app_entrypoints
    COMMAND ${CMAKE_COMMAND}
        -DOGPLAY_CLI=$<TARGET_FILE:ogplay-cli>
        -DOGPLAY_CORE=$<TARGET_FILE:ogplaycore>
        -DOGPLAY_GUI=${_ogplay_gui_test_path}
        -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckAppEntrypoints.cmake)
set_tests_properties(frontend.app_entrypoints PROPERTIES LABELS "integration;frontend" TIMEOUT 30)
include(cmake/TestDependencies.cmake)

# Test fixtures intentionally use partial aggregate initialization for
# request structs; production targets keep the warning enabled.
function(ogplay_add_test_executable target)
    add_executable(${target} tests/test_main.cpp ${ARGN})
    target_link_libraries(${target} PRIVATE doctest::doctest)
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/src")
    ogplay_set_project_warnings(${target})
    if(MSVC)
        target_compile_options(${target} PRIVATE /wd5285)
    else()
        target_compile_options(${target} PRIVATE -Wno-missing-field-initializers)
    endif()
endfunction()

set(OGPLAY_TEST_SOURCES
    tests/core/capability_ledger_tests.cpp
    tests/core/encoding_tests.cpp
    tests/core/json_tests.cpp
    tests/core/logger_tests.cpp
    tests/agent/control_service_tests.cpp
    tests/agent/dashboard_sources_tests.cpp
    tests/agent/dashboard_tests.cpp
    tests/agent/mcp_protocol_tests.cpp
    tests/frontend/mcp_input_dispatch_tests.cpp
    tests/frontend/run_apk_progress_tests.cpp
    tests/frontend/data_directory_tests.cpp
    tests/golden/golden_frame_tests.cpp
    tests/gles/angle_backend_tests.cpp
    tests/gles/angle_frame_tests.cpp
    tests/runtime/java_payload_provider_tests.cpp
    tests/runtime/surface_stream_tests.cpp
    tests/gles/egl_lifecycle_tests.cpp
    tests/gles/atc_tests.cpp
    tests/gles/etc1_tests.cpp
    tests/gles/gles_call_preparation_tests.cpp
    tests/gles/gles_dispatch_tests.cpp
    tests/gles/gles_transfer_state_tests.cpp
    tests/gles/guest_transfer_tests.cpp
    tests/gles/supersample_tests.cpp
    tests/hal/clock_tests.cpp
    tests/hal/from_chars_tests.cpp
    tests/hal/audio_output_tests.cpp
    tests/hal/host_environment_tests.cpp
    tests/hal/service_contract_tests.cpp
    tests/hal/thread_tests.cpp
    tests/hal/window_input_tests.cpp
    tests/hal/virtual_memory_tests.cpp
    tests/input/mouse_touch_mapper_tests.cpp
    tests/input/touch_mapper_tests.cpp
    tests/input/control_mapper_tests.cpp
    tests/session/control_config_tests.cpp
    tests/agent/control_editor_tests.cpp
    tests/frontend/control_requests_tests.cpp
    tests/frontend/key_hints_tests.cpp
    tests/memory/address_tests.cpp
    tests/memory/address_space_tests.cpp
    tests/memory/bus_tests.cpp
    tests/memory/snapshot_tests.cpp
    tests/runtime/syscall_tests.cpp
    tests/runtime/stall_diagnostics_tests.cpp
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
    tests/runtime/guest_cpu_environment_tests.cpp
    tests/runtime/guest_looper_tests.cpp
    tests/runtime/android_guest_framework_platform_tests.cpp
    tests/runtime/android_guest_java_media_tests.cpp
    tests/runtime/boundary/integration/android_boundary_hle_tests.cpp
    tests/runtime/boundary/integration/android_boundary_liblog_tests.cpp
    tests/runtime/boundary/integration/android_egl_lifecycle_tests.cpp
    tests/runtime/boundary/integration/android_boundary_libc_libdl_tests.cpp
    tests/runtime/boundary/integration/android_boundary_opensles_tests.cpp
    tests/runtime/boundary/integration/android_gles2_shaders_tests.cpp
    tests/runtime/boundary/integration/android_gles1_exports_tests.cpp
    tests/runtime/boundary/integration/android_gles_shared_resources_tests.cpp
    tests/runtime/boundary/integration/android_gles1_state_tests.cpp
    tests/runtime/boundary/integration/android_gles1_rendering_tests.cpp
    tests/runtime/boundary/integration/android_boundary_looper_tests.cpp
    tests/runtime/boundary/integration/android_boundary_sensor_tests.cpp
    tests/runtime/boundary/integration/android_egl_sharing_tests.cpp
    tests/runtime/boundary/integration/android_frame_presentation_tests.cpp
    tests/runtime/boundary/integration/android_native_activity_io_tests.cpp
    tests/runtime/boundary/integration/android_gles_texture_formats_tests.cpp
    tests/runtime/boundary/integration/android_boundary_fixture.cpp
    tests/runtime/boundary/modules/gles1_fixed_tests.cpp
    tests/runtime/android_link_preflight_tests.cpp
    tests/runtime/native_activity_runner_tests.cpp
    tests/runtime/application_startup_tests.cpp
    tests/runtime/native_library_loader_tests.cpp
    tests/runtime/jni_bridge_framework_tests.cpp
    tests/runtime/jni_bridge_objects_tests.cpp
    tests/runtime/jni_bridge_threads_tests.cpp
    tests/runtime/activity_window_lifecycle_tests.cpp
    tests/runtime/jni_android_platform_tests.cpp
    tests/runtime/guest_native_values_tests.cpp
    tests/runtime/activity_task_stack_tests.cpp
    tests/runtime/guest_zlib_tests.cpp
    tests/runtime/guest_icu_tests.cpp
    tests/runtime/guest_crypto_bks_tests.cpp
    tests/runtime/guest_crypto_certificates_tests.cpp
    tests/runtime/application_exit_tests.cpp
    tests/runtime/native_activity_process_tests.cpp
    tests/runtime/native_dlopen_tests.cpp
    tests/runtime/native_library_fixture.cpp
    tests/runtime/tls_trust_tests.cpp
    tests/runtime/preferences_xml_tests.cpp
    tests/runtime/ui_tree_tests.cpp
    tests/runtime/ui_renderer_tests.cpp
    tests/runtime/sandbox_store_tests.cpp
    tests/runtime/vfs_sandbox_tests.cpp
    tests/runtime/vfs_tests.cpp
    tests/frontend/run_apk_vfs_tests.cpp
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
    tests/cpu/execution_budget_tests.cpp
    tests/cpu/futex_tests.cpp
    tests/cpu/interpreter_tests.cpp
    tests/cpu/m1_guest_sample_tests.cpp
    tests/cpu/thread_group_tests.cpp
    tests/gles/pvrtc_tests.cpp
    tests/audio/ogg_vorbis_tests.cpp
    tests/audio/mp3_tests.cpp
    tests/audio/wav_tests.cpp
    tests/audio/encoded_audio_tests.cpp
    tests/audio/encoded_music_tests.cpp
    tests/audio/java_sound_pool_mixer_tests.cpp
    tests/audio/open_sles_pcm_mixer_tests.cpp
    tests/session/audio_output_pump_tests.cpp
    tests/video/video_player_tests.cpp
    tests/video/ffmpeg_video_player_tests.cpp
    tests/session/quirk_registry_tests.cpp
    tests/session/android_input_mapping_tests.cpp
    tests/session/lifecycle_tests.cpp
    tests/session/ui_compositor_tests.cpp
    tests/session/profile_apk_tests.cpp
    tests/session/profile_entry_scope_tests.cpp
    tests/session/session_tests.cpp
    tests/session/title_profile_tests.cpp
    tests/dexvm/class_loader_tests.cpp
    tests/dexvm/class_name_codec_tests.cpp
    tests/dexvm/reflection_tests.cpp
    tests/dexvm/dexasm_readback_tests.cpp
    tests/dexvm/file_vfs_tests.cpp
    tests/dexvm/file_streams_tests.cpp
    tests/dexvm/file_application_storage_tests.cpp
    tests/dexvm/file_assets_resources_tests.cpp
    tests/dexvm/file_media_descriptors_tests.cpp
    tests/dexvm/file_serialization_tests.cpp
    tests/dexvm/file_vfs_fixture.cpp
    tests/dexvm/fast_code_tests.cpp
    tests/dexvm/egl_facade_tests.cpp
    tests/dexvm/audio_track_tests.cpp
    tests/dexvm/scheduler_tests.cpp
    tests/dexvm/android_context_services_tests.cpp
    tests/dexvm/android_intent_uri_tests.cpp
    tests/dexvm/android_parcel_bundle_tests.cpp
    tests/dexvm/android_resources_bitmap_tests.cpp
    tests/dexvm/android_view_state_tests.cpp
    tests/dexvm/viewgroup_traversal_tests.cpp
    tests/dexvm/framelayout_traversal_tests.cpp
    tests/dexvm/container_traversal_fixture.cpp
    tests/dexvm/linearlayout_traversal_tests.cpp
    tests/dexvm/relativelayout_traversal_tests.cpp
    tests/dexvm/xml_block_tests.cpp
    tests/dexvm/android_pending_notifications_tests.cpp
    tests/dexvm/android_value_tests.cpp
    tests/dexvm/android_logging_tests.cpp
    tests/dexvm/android_component_services_tests.cpp
    tests/dexvm/android_package_metadata_tests.cpp
    tests/dexvm/android_text_drawable_tests.cpp
    tests/dexvm/android_preferences_tests.cpp
    tests/dexvm/android_webview_tests.cpp
    tests/dexvm/android_input_values_tests.cpp
    tests/dexvm/android_wifi_locks_tests.cpp
    tests/dexvm/android_value_fixture.cpp
    tests/dexvm/skeleton_surface_tests.cpp
    tests/dexvm/gap_survey_tests.cpp
    tests/dexvm/gc_tests.cpp
    tests/dexvm/dex_code_tests.cpp
    tests/dexvm/intrinsic_contract_tests.cpp
    tests/dexvm/core_catalog_tests.cpp
    tests/dexvm/throwable_contract_tests.cpp
    tests/dexvm/interpreter_tests.cpp
    tests/dexvm/interpreter_backend_tests.cpp
    tests/dexvm/interpreter_diagnostics_tests.cpp
    tests/dexvm/interpreter_fixture.cpp
    tests/dexvm/nio_runtime_tests.cpp
    tests/dexvm/unsafe_tests.cpp
    tests/dexvm/bootdex_concurrency_tests.cpp
    tests/dexvm/bootdex_library_tests.cpp
    tests/dexvm/bootdex_datetime_tests.cpp
    tests/dexvm/bootdex_security_tests.cpp
    tests/dexvm/bootdex_collections_io_tests.cpp
    tests/dexvm/bootdex_shutdown_tests.cpp
    tests/dexvm/bootdex_library_fixture.cpp
    tests/dexvm/boot_dex.cpp
    tests/dexvm/observable_tests.cpp
    tests/dexvm/network_sqlite_tests.cpp
    tests/dexvm/session_exit_tests.cpp
    tests/dexvm/intrinsics_p1_tests.cpp
    tests/dexvm/thread_intrinsic_tests.cpp
    tests/dexvm/vm_monitor_tests.cpp
    tests/dexvm/vm_thread_tests.cpp
    tests/dexvm/videoview_tests.cpp
    tests/dexvm/widget_click_tests.cpp
    tests/dexvm/widget_hierarchy_tests.cpp
    tests/dexvm/widget_inflater_tests.cpp
    tests/dexvm/widget_resources_tests.cpp
    tests/dexvm/widget_focus_input_tests.cpp
    tests/dexvm/widget_window_dialog_tests.cpp
    tests/dexvm/widget_fixture.cpp
    tests/dexvm/ui_include_tests.cpp
    tests/dexvm/arsc_tests.cpp
)
if(OGPLAY_ENABLE_DYNARMIC)
    list(APPEND OGPLAY_TEST_SOURCES
        tests/cpu/dynarmic_tests.cpp
        tests/runtime/arm_kernel_helpers_tests.cpp
    )
endif()
ogplay_add_test_executable(ogplay_desktop_integration_tests
    tests/frontend/mcp_http_server_tests.cpp tests/frontend/gui_dashboard_window_tests.cpp)
if(WIN32)
    target_sources(ogplay_desktop_integration_tests PRIVATE ${_ogplay_icon_resource})
endif()
target_link_libraries(ogplay_desktop_integration_tests PRIVATE OGPlay::Frontend ogplay_desktop_app ogplay_desktop_webview)
if(APPLE AND OGPLAY_BUILD_GUI)
    target_sources(ogplay_desktop_integration_tests PRIVATE tests/frontend/gui_window_test_support.mm)
    target_compile_options(ogplay_desktop_integration_tests PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    target_link_libraries(ogplay_desktop_integration_tests PRIVATE "-framework Cocoa")
endif()
target_include_directories(ogplay_desktop_integration_tests SYSTEM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ext-boost")
target_compile_definitions(ogplay_desktop_integration_tests PRIVATE OGPLAY_TEST_HAS_GUI=$<BOOL:${OGPLAY_BUILD_GUI}>
    OGPLAY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
if(MSVC)
    target_link_libraries(ogplay_desktop_integration_tests PRIVATE ws2_32)
endif()
if(OGPLAY_ENABLE_SDL3)
    target_link_libraries(ogplay_desktop_integration_tests PRIVATE SDL3::SDL3)
endif()
ogplay_add_test_executable(ogplay_file_lock_tests tests/hal/file_lock_tests.cpp
    tests/frontend/library_lock_contention_tests.cpp)
target_link_libraries(ogplay_file_lock_tests PRIVATE OGPlay::Hal ogplaycore ogplay_desktop_app)
ogplay_add_test_executable(ogplay_app_tests
    tests/frontend/surface_export_tests.cpp
    tests/frontend/run_dex_tests.cpp
    tests/frontend/game_run_args_tests.cpp
    tests/frontend/library_store_tests.cpp tests/frontend/library_lock_tests.cpp
    tests/frontend/cli_library_tests.cpp tests/frontend/apk_fixture.cpp tests/frontend/app_serialization_tests.cpp
    tests/frontend/gui_import_tests.cpp tests/frontend/gui_launch_tests.cpp
    tests/frontend/gui_rpc_tests.cpp tests/frontend/gui_view_model_tests.cpp
    tests/frontend/library_visuals_tests.cpp tests/frontend/data_hint_tests.cpp)
target_link_libraries(ogplay_app_tests PRIVATE ogplaycore ogplay_cli_app ogplay_desktop_app)
target_compile_definitions(ogplay_app_tests PRIVATE OGPLAY_TEST_HAS_GUI=$<BOOL:${OGPLAY_BUILD_GUI}>
    OGPLAY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    OGPLAY_DEXVM_FIXTURE_DIR="${OGPLAY_DEXVM_FIXTURE_DIR}")
add_dependencies(ogplay_app_tests ogplay_stage_data ogplay_dexvm_fixtures)

ogplay_add_test_executable(ogplay_tests ${OGPLAY_TEST_SOURCES})
target_link_libraries(ogplay_tests PRIVATE
    OGPlay::Frontend OGPlay::Cpu OGPlay::Gles OGPlay::Loader OGPlay::Runtime OGPlay::Video
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
    OGPLAY_TEST_HAS_GUI=$<BOOL:${OGPLAY_BUILD_GUI}>
    OGPLAY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    OGPLAY_DEXVM_FIXTURE_DIR="${OGPLAY_DEXVM_FIXTURE_DIR}"
    OGPLAY_ANGLE_HAS_SWIFTSHADER=$<BOOL:${_angle_has_swiftshader}>
)
add_dependencies(ogplay_tests ogplay_dexvm_fixtures)
add_dependencies(ogplay_tests ogplay_stage_data)
if(MSVC)
    target_link_libraries(ogplay_tests PRIVATE ws2_32)
endif()
if(CYGWIN)
    target_compile_definitions(ogplay_tests PRIVATE DOCTEST_CONFIG_NO_POSIX_SIGNALS)
endif()

ogplay_stage_angle_runtime(ogplay_tests)
ogplay_stage_ffmpeg_runtime(ogplay_tests)

include(${OGPLAY_DOCTEST_CMAKE_MODULE})
doctest_discover_tests(ogplay_desktop_integration_tests PROPERTIES LABELS "frontend;integration")
doctest_discover_tests(ogplay_file_lock_tests PROPERTIES LABELS "hal;contract")
doctest_discover_tests(ogplay_app_tests PROPERTIES LABELS "app;contract")
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
    NAME tools.capabilities_current
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/capabilities.py check
)
set_tests_properties(tools.capabilities_current PROPERTIES LABELS tools)

add_test(
    NAME architecture.platform_boundaries
    COMMAND ${CMAKE_COMMAND}
        -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckPlatformBoundaries.cmake
)
set_tests_properties(architecture.platform_boundaries PROPERTIES LABELS architecture)

add_test(
    NAME architecture.ui_traversal_entry
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_ui_traversal.py
)
set_tests_properties(architecture.ui_traversal_entry PROPERTIES LABELS architecture)

add_test(
    NAME architecture.documentation_layout
    COMMAND ${CMAKE_COMMAND}
        -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckDocumentationLayout.cmake
)
set_tests_properties(architecture.documentation_layout PROPERTIES LABELS architecture)

add_test(
    NAME architecture.dexvm_intrinsic_layout
    COMMAND ${CMAKE_COMMAND}
        -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckDexVmIntrinsicLayout.cmake
)
set_tests_properties(architecture.dexvm_intrinsic_layout PROPERTIES LABELS architecture)

add_test(
    NAME architecture.boundary_hot_path
    COMMAND ${CMAKE_COMMAND}
        -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckBoundaryHotPath.cmake
)
set_tests_properties(architecture.boundary_hot_path PROPERTIES LABELS architecture)

add_test(
    NAME frontend.run_apk_options
    COMMAND ${CMAKE_COMMAND}
        -DOGPLAY_CLI=$<TARGET_FILE:ogplay-cli>
        -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckRunApkOptions.cmake
)
set_tests_properties(frontend.run_apk_options PROPERTIES LABELS "unit;frontend")

if(OGPLAY_BUILD_GUI)
    add_test(NAME frontend.gui_dashboard_window
        COMMAND ogplay_desktop_integration_tests "--test-case=GUI Dashboard windows*" --no-skip=true)
    set_tests_properties(frontend.gui_dashboard_window PROPERTIES LABELS "integration;frontend" TIMEOUT 30)
    add_test(NAME frontend.dashboard_http
        COMMAND ogplay_desktop_integration_tests "--test-case=Dashboard HTTP*,MCP HTTP*")
    add_test(NAME frontend.dashboard_manifest
        COMMAND ${CMAKE_COMMAND} -DWEBUI_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/data/webui/dashboard
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckWebUi.cmake)
    add_test(NAME frontend.key_editor_manifest
        COMMAND ${CMAKE_COMMAND} -DWEBUI_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/data/webui/controls
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckWebUi.cmake)
    set_tests_properties(frontend.dashboard_http frontend.dashboard_manifest frontend.key_editor_manifest
        PROPERTIES LABELS "unit;frontend" TIMEOUT 30)
    add_test(
        NAME frontend.gui_webui_manifest
        COMMAND ${CMAKE_COMMAND} -DWEBUI_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/data/webui/gui
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckWebUi.cmake)
    add_test(
        NAME frontend.gui_options
        COMMAND ${CMAKE_COMMAND}
            -DOGPLAY_GUI=$<TARGET_FILE:ogplay>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckGuiOptions.cmake
    )
    set_tests_properties(frontend.gui_options PROPERTIES LABELS "unit;frontend")

    add_test(
        NAME frontend.gui_smoke
        COMMAND $<TARGET_FILE:ogplay>
            --library-root ${CMAKE_CURRENT_BINARY_DIR}/gui-smoke-library
            --smoke-frames 3
    )
    set_tests_properties(frontend.gui_smoke PROPERTIES
        LABELS "integration;frontend"
        TIMEOUT 30)

    add_test(
        NAME frontend.gui_library_smoke
        COMMAND ${CMAKE_COMMAND}
            -DOGPLAY_GUI=$<TARGET_FILE:ogplay>
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
    NAME tools.dexvm_api19_surface_self_test
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/dexvm_api19_surface.py
        --self-test
)
set_tests_properties(tools.dexvm_api19_surface_self_test PROPERTIES LABELS tools)

add_test(
    NAME tools.api19_skeleton_self_test
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/api19_skeleton.py --self-test
)
set_tests_properties(tools.api19_skeleton_self_test PROPERTIES LABELS tools)

add_test(
    NAME tools.framework_contract_self_test
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/conformance/framework_contract.py --self-test
)
set_tests_properties(tools.framework_contract_self_test PROPERTIES LABELS tools)

add_test(
    NAME tools.boundary_exports_self_test
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/conformance/boundary_exports.py --self-test
)
set_tests_properties(tools.boundary_exports_self_test PROPERTIES LABELS tools)

add_test(
    NAME tools.title_preflight_self_test
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/title_preflight.py --self-test
)
set_tests_properties(tools.title_preflight_self_test PROPERTIES LABELS tools)

add_test(
    NAME tools.dexvm_gap_report_self_test
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/dexvm_gap_report.py
        --repo ${CMAKE_CURRENT_SOURCE_DIR}
        --self-test
)
set_tests_properties(tools.dexvm_gap_report_self_test PROPERTIES LABELS tools)

add_test(
    NAME tools.bootdex_builder_self_test
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/bootdex/compose_bootdex.py
        --self-test
)
set_tests_properties(tools.bootdex_builder_self_test
    PROPERTIES LABELS "tools;dexvm;distribution")

add_test(
    NAME tools.diagnostics_align_host_threads
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/test_align_host_threads.py
)
set_tests_properties(tools.diagnostics_align_host_threads PROPERTIES LABELS tools)

add_test(
    NAME tools.gles2_extensions_catalog_current
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_gles_api.py
        --idl ${CMAKE_CURRENT_SOURCE_DIR}/data/gles/gles2_extensions.json
        --output ${OGPLAY_GLES2_EXTENSIONS_CATALOG}
        --check
        --verify-header ${OGPLAY_ANGLE_SDK_ROOT}/include/GLES2/gl2ext.h
)
set_tests_properties(tools.gles2_extensions_catalog_current PROPERTIES LABELS tools)

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
    NAME tools.gles3_catalog_current
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_gles_api.py
        --idl ${CMAKE_CURRENT_SOURCE_DIR}/data/gles/gles3.json
        --output ${OGPLAY_GLES3_CATALOG}
        --check
        --verify-header
            ${OGPLAY_ANGLE_SDK_ROOT}/include/GLES3/gl3.h
)
set_tests_properties(tools.gles3_catalog_current PROPERTIES LABELS tools)

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
    NAME tools.android_payload_current
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate_android_payload.py
        --root ${CMAKE_CURRENT_SOURCE_DIR}/data/android/19
)
set_tests_properties(tools.android_payload_current
    PROPERTIES LABELS "tools;bionic;distribution")

add_test(
    NAME tools.title_profile_validator_self_test
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate_title_profiles.py
        --schema
            ${CMAKE_CURRENT_SOURCE_DIR}/data/profiles/title-profile-v4.schema.json
        --self-test
)
set_tests_properties(tools.title_profile_validator_self_test
    PROPERTIES LABELS "tools;profiles")

add_test(
    NAME tools.title_profiles_current
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate_title_profiles.py
        --schema
            ${CMAKE_CURRENT_SOURCE_DIR}/data/profiles/title-profile-v4.schema.json
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
    NAME tools.device_presets_current
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate_devices.py
        --devices ${CMAKE_CURRENT_SOURCE_DIR}/data/devices
)
add_test(
    NAME tools.device_presets_validator
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/test_validate_devices.py
)
set_tests_properties(tools.device_presets_current tools.device_presets_validator
    PROPERTIES LABELS "tools;devices")

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
        --output ${OGPLAY_DEXVM_OPCODE_TABLE}
        --check
)
set_tests_properties(tools.dexvm_opcode_catalog_current
    PROPERTIES LABELS "tools;dexvm")

add_test(
    NAME tools.dalvik_tests_self_test
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/conformance/dalvik_tests.py --self-test
)
set_tests_properties(tools.dalvik_tests_self_test
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
