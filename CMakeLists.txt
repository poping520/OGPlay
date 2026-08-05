cmake_minimum_required(VERSION 3.25)

project(OGPlay VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(OGPLAY_BUILD_TESTS "Build OGPlay unit and contract tests" ON)
option(OGPLAY_WARNINGS_AS_ERRORS "Treat OGPlay warnings as errors" OFF)
option(OGPLAY_ENABLE_SDL3 "Configure the bundled SDL3 platform dependency" ON)
option(OGPLAY_ENABLE_DYNARMIC "Configure the bundled Dynarmic CPU dependency" ON)
option(OGPLAY_ENABLE_ANGLE "Link an externally GN-built ANGLE SDK" OFF)
set(OGPLAY_ANGLE_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/angle-prebuilt"
    CACHE PATH "Root of the platform-partitioned ANGLE prebuilt SDK")
set(OGPLAY_ANGLE_SDK_CONFIGURATION "release" CACHE STRING
    "ANGLE SDK configuration to consume (release or debug)")
set_property(CACHE OGPLAY_ANGLE_SDK_CONFIGURATION PROPERTY STRINGS release debug)

include(GNUInstallDirs)
include(cmake/CompilerWarnings.cmake)
include(cmake/ThirdParty.cmake)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_library(ogplay_core
    src/core/capability_ledger.cpp
    src/core/frame_compare.cpp
    src/core/logger.cpp
    src/core/software_surface.cpp
)
add_library(OGPlay::Core ALIAS ogplay_core)
target_include_directories(ogplay_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_features(ogplay_core PUBLIC cxx_std_20)
ogplay_set_project_warnings(ogplay_core)

add_library(ogplay_hal
    src/hal/clock.cpp
    src/hal/fs.cpp
    src/hal/sdl_window_input.cpp
    src/hal/thread.cpp
)
if(WIN32)
    target_sources(ogplay_hal PRIVATE src/hal/windows/virtual_memory.cpp)
elseif(APPLE)
    target_sources(ogplay_hal PRIVATE src/hal/macos/virtual_memory.cpp)
elseif(UNIX)
    target_sources(ogplay_hal PRIVATE src/hal/linux/virtual_memory.cpp)
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
ogplay_set_project_warnings(ogplay_hal)

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
    src/loader/dex.cpp
    src/loader/dex_analysis.cpp
    src/loader/dex_class_data.cpp
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

add_library(ogplay_gles
    src/gles/angle_backend.cpp
    src/gles/angle_frame.cpp
    src/gles/egl_lifecycle.cpp
    src/gles/gles_call_preparation.cpp
    src/gles/gles_dispatch.cpp
    src/gles/gles_transfer_state.cpp
    src/gles/guest_transfer.cpp
    src/gles/supersample.cpp
)
set(OGPLAY_GLES_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(OGPLAY_GLES2_CATALOG
    "${OGPLAY_GLES_GENERATED_DIR}/ogplay/gles/generated/gles2_catalog.h")
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
add_dependencies(ogplay_gles ogplay_gles_codegen)
add_library(OGPlay::Gles ALIAS ogplay_gles)
target_include_directories(ogplay_gles PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE "${OGPLAY_GLES_GENERATED_DIR}"
)
ogplay_set_project_warnings(ogplay_gles)
target_link_libraries(ogplay_gles PRIVATE OGPlay::Memory)
if(OGPLAY_ENABLE_ANGLE)
    target_compile_definitions(ogplay_gles PRIVATE OGPLAY_HAS_ANGLE=1)
    target_link_libraries(ogplay_gles PRIVATE ANGLE::EGL ANGLE::GLESv2)
else()
    target_compile_definitions(ogplay_gles PRIVATE OGPLAY_HAS_ANGLE=0)
endif()

add_library(ogplay_runtime
    src/runtime/syscall/arm_kernel_helpers.cpp
    src/runtime/bionic/bionic_profile.cpp
    src/runtime/bionic/bionic_selfcheck.cpp
    src/runtime/bionic/bionic_tls.cpp
    src/runtime/framework/framework_asset.cpp
    src/runtime/framework/framework_lifecycle.cpp
    src/runtime/framework/framework_locale.cpp
    src/runtime/framework/framework_package.cpp
    src/runtime/framework/framework_preferences.cpp
    src/runtime/execution/guest_lifecycle.cpp
    src/runtime/integration/headless_bionic_runner.cpp
    src/runtime/integration/android_boundary_hle.cpp
    src/runtime/integration/native_activity_runner.cpp
    src/runtime/integration/headless_jni_contract.cpp
    src/runtime/jni/jni.cpp
    src/runtime/jni/jni_exception.cpp
    src/runtime/jni/jni_field_store.cpp
    src/runtime/jni/jni_environment.cpp
    src/runtime/jni/jni_java_vm.cpp
    src/runtime/jni/jni_array.cpp
    src/runtime/jni/jni_class_registry.cpp
    src/runtime/jni/jni_invocation.cpp
    src/runtime/jni/jni_native_registry.cpp
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
    src/runtime/syscall/syscall_process.cpp
    src/runtime/syscall/syscall_signal.cpp
    src/runtime/syscall/syscall_thread_lifecycle.cpp
    src/runtime/vfs/vfs.cpp
)
add_library(OGPlay::Runtime ALIAS ogplay_runtime)
target_link_libraries(ogplay_runtime PUBLIC
    OGPlay::Core OGPlay::Cpu OGPlay::Loader OGPlay::Memory
)
target_link_libraries(ogplay_runtime PRIVATE OGPlay::Gles)
target_include_directories(ogplay_runtime PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
ogplay_set_project_warnings(ogplay_runtime)

add_library(ogplay_session src/session/session.cpp)
add_library(OGPlay::Session ALIAS ogplay_session)
target_link_libraries(ogplay_session PUBLIC OGPlay::Core OGPlay::Hal OGPlay::Memory)
target_include_directories(ogplay_session PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
ogplay_set_project_warnings(ogplay_session)

add_library(ogplay_agent src/agent/control_service.cpp)
add_library(OGPlay::Agent ALIAS ogplay_agent)
target_link_libraries(ogplay_agent PUBLIC OGPlay::Core OGPlay::Session)
ogplay_set_project_warnings(ogplay_agent)

add_executable(ogplay src/frontend/cli/main.cpp src/frontend/cli/run_apk.cpp)
target_link_libraries(ogplay PRIVATE OGPlay::Agent OGPlay::Hal OGPlay::Loader OGPlay::Runtime)
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

install(TARGETS ogplay ogplay_core ogplay_hal ogplay_memory ogplay_cpu ogplay_loader ogplay_gles ogplay_runtime ogplay_agent ogplay_session
    EXPORT OGPlayTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(OGPLAY_BUILD_TESTS)
    include(CTest)
    include(cmake/TestDependencies.cmake)

    set(OGPLAY_TEST_SOURCES
        tests/test_main.cpp
        tests/core/capability_ledger_tests.cpp
        tests/core/logger_tests.cpp
        tests/agent/control_service_tests.cpp
        tests/golden/golden_frame_tests.cpp
        tests/gles/angle_backend_tests.cpp
        tests/gles/angle_frame_tests.cpp
        tests/gles/egl_lifecycle_tests.cpp
        tests/gles/gles_call_preparation_tests.cpp
        tests/gles/gles_dispatch_tests.cpp
        tests/gles/gles_transfer_state_tests.cpp
        tests/gles/guest_transfer_tests.cpp
        tests/gles/supersample_tests.cpp
        tests/hal/clock_tests.cpp
        tests/hal/service_contract_tests.cpp
        tests/hal/thread_tests.cpp
        tests/hal/window_input_tests.cpp
        tests/hal/virtual_memory_tests.cpp
        tests/memory/address_tests.cpp
        tests/memory/address_space_tests.cpp
        tests/memory/bus_tests.cpp
        tests/memory/snapshot_tests.cpp
        tests/runtime/syscall_tests.cpp
        tests/runtime/syscall_bridge_tests.cpp
        tests/runtime/syscall_process_tests.cpp
        tests/runtime/syscall_signal_tests.cpp
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
        tests/runtime/jni_java_vm_tests.cpp
        tests/runtime/jni_array_tests.cpp
        tests/runtime/jni_class_registry_tests.cpp
        tests/runtime/jni_invocation_tests.cpp
        tests/runtime/jni_native_registry_tests.cpp
        tests/runtime/jni_object_tests.cpp
        tests/runtime/jni_object_array_tests.cpp
        tests/runtime/jni_signature_tests.cpp
        tests/runtime/jni_utf_tests.cpp
        tests/runtime/guest_thread_lifecycle_tests.cpp
        tests/runtime/guest_clone_thread_runtime_tests.cpp
        tests/runtime/guest_thread_runner_tests.cpp
        tests/runtime/android_boundary_hle_tests.cpp
        tests/runtime/native_activity_runner_tests.cpp
        tests/runtime/vfs_tests.cpp
        tests/loader/apk_tests.cpp
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
        tests/session/session_tests.cpp
    )
    if(OGPLAY_ENABLE_DYNARMIC)
        list(APPEND OGPLAY_TEST_SOURCES
            tests/cpu/dynarmic_tests.cpp
            tests/runtime/arm_kernel_helpers_tests.cpp
        )
    endif()
    add_executable(ogplay_tests ${OGPLAY_TEST_SOURCES})
    target_link_libraries(ogplay_tests PRIVATE
        OGPlay::Agent OGPlay::Cpu OGPlay::Gles OGPlay::Loader OGPlay::Runtime doctest::doctest
    )
    target_include_directories(ogplay_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/samples/m1_guest/include
    )
    if(OGPLAY_ENABLE_SDL3)
        target_compile_definitions(ogplay_tests PRIVATE OGPLAY_TEST_HAS_SDL3=1)
        target_link_libraries(ogplay_tests PRIVATE SDL3::SDL3)
    else()
        target_compile_definitions(ogplay_tests PRIVATE OGPLAY_TEST_HAS_SDL3=0)
    endif()
    target_compile_definitions(ogplay_tests PRIVATE
        OGPLAY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    if(MSVC)
        target_compile_options(ogplay_tests PRIVATE /wd5285)
    endif()
    if(CYGWIN)
        target_compile_definitions(ogplay_tests PRIVATE DOCTEST_CONFIG_NO_POSIX_SIGNALS)
    endif()
    ogplay_set_project_warnings(ogplay_tests)

    if(OGPLAY_ENABLE_ANGLE AND WIN32)
        add_custom_command(TARGET ogplay_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${_angle_sdk}/bin" $<TARGET_FILE_DIR:ogplay_tests>
        )
    endif()

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
        NAME tools.gles_idl_generator_self_test
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_gles_api.py
            --self-test
    )
    set_tests_properties(tools.gles_idl_generator_self_test PROPERTIES LABELS tools)

    add_test(
        NAME tools.gles2_catalog_current
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_gles_api.py
            --idl ${CMAKE_CURRENT_SOURCE_DIR}/data/gles/gles2.json
            --output ${OGPLAY_GLES2_CATALOG}
            --check
            --verify-header
                ${CMAKE_CURRENT_SOURCE_DIR}/third_party/angle-prebuilt/windows-x64/release/include/GLES2/gl2.h
    )
    set_tests_properties(tools.gles2_catalog_current PROPERTIES LABELS tools)

    add_test(
        NAME tools.remote_incremental_self_test
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/remote_incremental.py
            --self-test
    )
    set_tests_properties(tools.remote_incremental_self_test PROPERTIES LABELS tools)

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
