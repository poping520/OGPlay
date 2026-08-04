function(ogplay_require_submodule relative_path display_name)
    if(NOT EXISTS "${PROJECT_SOURCE_DIR}/${relative_path}/CMakeLists.txt")
        message(FATAL_ERROR
            "${display_name} submodule is missing. "
            "Run: git submodule update --init --recursive")
    endif()
endfunction()

ogplay_require_submodule("third_party/SDL" "SDL3")
ogplay_require_submodule("third_party/dynarmic" "Dynarmic")

if(OGPLAY_ENABLE_SDL3)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    set(SDL_UNINSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory("${PROJECT_SOURCE_DIR}/third_party/SDL"
        "${CMAKE_BINARY_DIR}/_deps/sdl-build" EXCLUDE_FROM_ALL)
endif()

if(OGPLAY_ENABLE_DYNARMIC)
    if(NOT EXISTS "${PROJECT_SOURCE_DIR}/third_party/ext-boost/boost/version.hpp")
        message(FATAL_ERROR
            "ext-boost submodule is missing. "
            "Run: git submodule update --init --recursive")
    endif()
    if(NOT EXISTS
       "${PROJECT_SOURCE_DIR}/third_party/boost-pool/include/boost/pool/pool_alloc.hpp")
        message(FATAL_ERROR
            "Boost.Pool submodule is missing. "
            "Run: git submodule update --init --recursive")
    endif()
    set(Boost_INCLUDE_DIR
        "${PROJECT_SOURCE_DIR}/third_party/ext-boost"
        CACHE PATH "Bundled ext-boost include directory" FORCE)
    set(Boost_NO_SYSTEM_PATHS ON CACHE BOOL "Use bundled ext-boost only" FORCE)
    set(DYNARMIC_FRONTENDS "A32" CACHE STRING "" FORCE)
    set(DYNARMIC_TESTS OFF CACHE BOOL "" FORCE)
    set(DYNARMIC_USE_LLVM OFF CACHE BOOL "" FORCE)
    set(DYNARMIC_USE_BUNDLED_EXTERNALS ON CACHE BOOL "" FORCE)
    set(DYNARMIC_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
    add_subdirectory("${PROJECT_SOURCE_DIR}/third_party/dynarmic"
        "${CMAKE_BINARY_DIR}/_deps/dynarmic-build" EXCLUDE_FROM_ALL)
    target_include_directories(dynarmic SYSTEM PRIVATE
        "${PROJECT_SOURCE_DIR}/third_party/boost-pool/include")
endif()

if(OGPLAY_ENABLE_ANGLE)
    set(_angle_source "${PROJECT_SOURCE_DIR}/third_party/angle")
    if(NOT EXISTS "${_angle_source}/BUILD.gn" OR
       NOT EXISTS "${_angle_source}/include/EGL/egl.h")
        message(FATAL_ERROR
            "ANGLE source submodule is missing or incomplete. "
            "Run: git submodule update --init --depth 1 third_party/angle")
    endif()
    if(NOT IS_ABSOLUTE "${OGPLAY_ANGLE_BUILD_DIR}")
        message(FATAL_ERROR "OGPLAY_ANGLE_BUILD_DIR must be an absolute path")
    endif()

    find_library(_angle_egl_library NAMES libEGL EGL
        PATHS "${OGPLAY_ANGLE_BUILD_DIR}" NO_DEFAULT_PATH NO_CACHE)
    find_library(_angle_glesv2_library NAMES libGLESv2 GLESv2
        PATHS "${OGPLAY_ANGLE_BUILD_DIR}" NO_DEFAULT_PATH NO_CACHE)
    if(WIN32)
        find_file(_angle_egl_runtime NAMES libEGL.dll EGL.dll
            PATHS "${OGPLAY_ANGLE_BUILD_DIR}" NO_DEFAULT_PATH NO_CACHE)
        find_file(_angle_glesv2_runtime NAMES libGLESv2.dll GLESv2.dll
            PATHS "${OGPLAY_ANGLE_BUILD_DIR}" NO_DEFAULT_PATH NO_CACHE)
    endif()

    if(NOT _angle_egl_library OR NOT _angle_glesv2_library OR
       (WIN32 AND (NOT _angle_egl_runtime OR NOT _angle_glesv2_runtime)))
        message(FATAL_ERROR
            "ANGLE GN output is incomplete at ${OGPLAY_ANGLE_BUILD_DIR}. "
            "Expected link and runtime artifacts for libEGL and libGLESv2; "
            "prepare the pinned submodule with depot_tools/gclient, then build "
            "the GN targets libEGL and libGLESv2.")
    endif()

    if(WIN32)
        add_library(ANGLE::EGL SHARED IMPORTED GLOBAL)
        set_target_properties(ANGLE::EGL PROPERTIES
            IMPORTED_IMPLIB "${_angle_egl_library}"
            IMPORTED_LOCATION "${_angle_egl_runtime}")
        add_library(ANGLE::GLESv2 SHARED IMPORTED GLOBAL)
        set_target_properties(ANGLE::GLESv2 PROPERTIES
            IMPORTED_IMPLIB "${_angle_glesv2_library}"
            IMPORTED_LOCATION "${_angle_glesv2_runtime}")
    else()
        add_library(ANGLE::EGL UNKNOWN IMPORTED GLOBAL)
        set_target_properties(ANGLE::EGL PROPERTIES
            IMPORTED_LOCATION "${_angle_egl_library}")
        add_library(ANGLE::GLESv2 UNKNOWN IMPORTED GLOBAL)
        set_target_properties(ANGLE::GLESv2 PROPERTIES
            IMPORTED_LOCATION "${_angle_glesv2_library}")
    endif()
    set_target_properties(ANGLE::EGL ANGLE::GLESv2 PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${_angle_source}/include")
endif()
