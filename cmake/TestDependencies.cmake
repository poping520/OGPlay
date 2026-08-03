# doctest 2.4.11 still declares a pre-3.5 policy baseline. CMake 4 removed the
# implicit compatibility mode, so keep the exception local to test dependencies.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

set(doctest_SOURCE_DIR "${PROJECT_SOURCE_DIR}/third_party/doctest")
if(NOT EXISTS "${doctest_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "doctest submodule is missing. Run: git submodule update --init --recursive")
endif()

set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${doctest_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/_deps/doctest-build"
    EXCLUDE_FROM_ALL)
set(OGPLAY_DOCTEST_CMAKE_MODULE "${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake")
