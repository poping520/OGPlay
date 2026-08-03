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
endif()
