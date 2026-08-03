include(FetchContent)

# doctest 2.4.11 still declares a pre-3.5 policy baseline. CMake 4 removed the
# implicit compatibility mode, so keep the exception local to test dependencies.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

if(OGPLAY_DOCTEST_SOURCE_DIR)
    set(doctest_SOURCE_DIR "${OGPLAY_DOCTEST_SOURCE_DIR}")
    add_subdirectory("${doctest_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/_deps/doctest-build")
    set(OGPLAY_DOCTEST_CMAKE_MODULE "${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake")
else()
    find_package(doctest 2.4.11 CONFIG QUIET)
    if(doctest_FOUND)
        set(OGPLAY_DOCTEST_CMAKE_MODULE doctest)
    elseif(OGPLAY_FETCH_TEST_DEPENDENCIES)
        FetchContent_Declare(doctest
            GIT_REPOSITORY https://github.com/doctest/doctest.git
            GIT_TAG v2.4.11
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(doctest)
        set(OGPLAY_DOCTEST_CMAKE_MODULE "${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake")
    else()
        message(FATAL_ERROR
            "doctest 2.4.11 not found. Set OGPLAY_DOCTEST_SOURCE_DIR or enable fetching.")
    endif()
endif()
