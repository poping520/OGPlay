cmake_minimum_required(VERSION 3.25)

project(OGPlay VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(OGPLAY_BUILD_TESTS "Build OGPlay unit and contract tests" ON)
option(OGPLAY_WARNINGS_AS_ERRORS "Treat OGPlay warnings as errors" OFF)
option(OGPLAY_ENABLE_SDL3 "Configure the bundled SDL3 platform dependency" ON)
option(OGPLAY_ENABLE_DYNARMIC "Configure the bundled Dynarmic CPU dependency" OFF)

include(GNUInstallDirs)
include(cmake/CompilerWarnings.cmake)
include(cmake/ThirdParty.cmake)

add_library(ogplay_core
    src/core/capability_ledger.cpp
    src/core/frame_compare.cpp
    src/core/logger.cpp
    src/core/software_surface.cpp
    src/hal/clock.cpp
)
add_library(OGPlay::Core ALIAS ogplay_core)
target_include_directories(ogplay_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_features(ogplay_core PUBLIC cxx_std_20)
ogplay_set_project_warnings(ogplay_core)

add_library(ogplay_session src/session/session.cpp)
add_library(OGPlay::Session ALIAS ogplay_session)
target_link_libraries(ogplay_session PUBLIC OGPlay::Core)
target_include_directories(ogplay_session PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
ogplay_set_project_warnings(ogplay_session)

add_library(ogplay_agent src/agent/control_service.cpp)
add_library(OGPlay::Agent ALIAS ogplay_agent)
target_link_libraries(ogplay_agent PUBLIC OGPlay::Core OGPlay::Session)
ogplay_set_project_warnings(ogplay_agent)

add_executable(ogplay src/frontend/cli/main.cpp)
target_link_libraries(ogplay PRIVATE OGPlay::Agent)
target_compile_definitions(ogplay PRIVATE OGPLAY_VERSION="${PROJECT_VERSION}")
target_compile_definitions(ogplay PRIVATE OGPLAY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
ogplay_set_project_warnings(ogplay)

install(TARGETS ogplay ogplay_core ogplay_agent ogplay_session
    EXPORT OGPlayTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(OGPLAY_BUILD_TESTS)
    include(CTest)
    include(cmake/TestDependencies.cmake)

    add_executable(ogplay_tests
        tests/test_main.cpp
        tests/core/capability_ledger_tests.cpp
        tests/core/logger_tests.cpp
        tests/agent/control_service_tests.cpp
        tests/golden/golden_frame_tests.cpp
        tests/hal/clock_tests.cpp
        tests/session/session_tests.cpp
    )
    target_link_libraries(ogplay_tests PRIVATE OGPlay::Agent doctest::doctest)
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
endif()
