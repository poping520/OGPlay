function(ogplay_require_submodule relative_path display_name)
    if(NOT EXISTS "${PROJECT_SOURCE_DIR}/${relative_path}/CMakeLists.txt")
        message(FATAL_ERROR
            "${display_name} submodule is missing. "
            "Run: git submodule update --init --recursive")
    endif()
endfunction()

function(ogplay_verify_manifest_files manifest root label)
    string(JSON _file_count ERROR_VARIABLE _json_error
        LENGTH "${manifest}" files)
    if(_json_error OR _file_count LESS 1)
        message(FATAL_ERROR "${label} manifest has no valid file list")
    endif()
    math(EXPR _last_file "${_file_count} - 1")
    foreach(_index RANGE 0 ${_last_file})
        string(JSON _relative GET "${manifest}" files ${_index} path)
        string(JSON _expected_size GET "${manifest}" files ${_index} size)
        string(JSON _expected_hash GET "${manifest}" files ${_index} sha256)
        if(_relative MATCHES "^[A-Za-z]:" OR
           _relative MATCHES "^/" OR
           _relative MATCHES "(^|/)\\.\\.(/|$)")
            message(FATAL_ERROR "${label} manifest contains unsafe path: ${_relative}")
        endif()
        set(_file "${root}/${_relative}")
        if(NOT EXISTS "${_file}")
            message(FATAL_ERROR "${label} file is missing: ${_relative}")
        endif()
        file(SIZE "${_file}" _actual_size)
        file(SHA256 "${_file}" _actual_hash)
        if(NOT _actual_size EQUAL _expected_size OR
           NOT _actual_hash STREQUAL _expected_hash)
            message(FATAL_ERROR "${label} file failed integrity check: ${_relative}")
        endif()
    endforeach()
endfunction()

ogplay_require_submodule("third_party/SDL" "SDL3")
ogplay_require_submodule("third_party/dynarmic" "Dynarmic")
ogplay_require_submodule("third_party/yyjson" "yyjson")
if(NOT EXISTS "${PROJECT_SOURCE_DIR}/third_party/ext-boost/boost/url/encode.hpp" OR
   NOT EXISTS "${PROJECT_SOURCE_DIR}/third_party/ext-boost/libs/url/src/pct_string_view.cpp")
    message(FATAL_ERROR
        "ext-boost Boost.URL sources are missing. "
        "Run: git submodule update --init --recursive")
endif()


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
    include("${CMAKE_CURRENT_LIST_DIR}/DynarmicCoprocessor.cmake")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|ARM64)$")
        include("${CMAKE_CURRENT_LIST_DIR}/DynarmicReadPageTable.cmake")
    endif()
    if(WIN32 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64)$")
        include("${CMAKE_CURRENT_LIST_DIR}/DynarmicDiagnostics.cmake")
    endif()
    target_include_directories(dynarmic SYSTEM PRIVATE
        "${PROJECT_SOURCE_DIR}/third_party/boost-pool/include")
endif()

if(OGPLAY_ENABLE_ANGLE)
    if(NOT IS_ABSOLUTE "${OGPLAY_ANGLE_SDK_ROOT}")
        message(FATAL_ERROR "OGPLAY_ANGLE_SDK_ROOT must be an absolute path")
    endif()
    if(NOT OGPLAY_ANGLE_SDK_CONFIGURATION MATCHES "^(release|debug)$")
        message(FATAL_ERROR
            "OGPLAY_ANGLE_SDK_CONFIGURATION must be release or debug")
    endif()

    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _angle_processor)
    if(_angle_processor MATCHES "^(amd64|x86_64)$")
        set(_angle_cpu "x64")
    elseif(_angle_processor MATCHES "^(arm64|aarch64)$")
        set(_angle_cpu "arm64")
    else()
        message(FATAL_ERROR
            "ANGLE prebuilt SDK does not support CPU ${CMAKE_SYSTEM_PROCESSOR}")
    endif()
    if(WIN32)
        set(_angle_platform "windows")
    elseif(APPLE)
        set(_angle_platform "macos")
    elseif(UNIX)
        set(_angle_platform "linux")
    else()
        message(FATAL_ERROR "ANGLE prebuilt SDK does not support this platform")
    endif()

    set(_angle_sdk
        "${OGPLAY_ANGLE_SDK_ROOT}/${_angle_platform}-${_angle_cpu}/${OGPLAY_ANGLE_SDK_CONFIGURATION}")
    set(_angle_manifest_path "${_angle_sdk}/manifest.json")
    if(NOT EXISTS "${_angle_manifest_path}")
        message(FATAL_ERROR
            "ANGLE prebuilt SDK is missing at ${_angle_sdk}. "
            "Initialize third_party/angle-prebuilt or set OGPLAY_ANGLE_SDK_ROOT.")
    endif()
    file(READ "${_angle_manifest_path}" _angle_manifest)
    foreach(_angle_field schema_version angle_commit platform target_cpu configuration)
        string(JSON _angle_value ERROR_VARIABLE _angle_json_error
            GET "${_angle_manifest}" "${_angle_field}")
        if(_angle_json_error)
            message(FATAL_ERROR
                "ANGLE SDK manifest has no valid ${_angle_field}: ${_angle_json_error}")
        endif()
        set("_angle_manifest_${_angle_field}" "${_angle_value}")
    endforeach()
    if(NOT _angle_manifest_schema_version EQUAL 1 OR
       NOT _angle_manifest_platform STREQUAL _angle_platform OR
       NOT _angle_manifest_target_cpu STREQUAL _angle_cpu OR
       NOT _angle_manifest_configuration STREQUAL OGPLAY_ANGLE_SDK_CONFIGURATION)
        message(FATAL_ERROR
            "ANGLE SDK manifest does not match requested "
            "${_angle_platform}-${_angle_cpu}/${OGPLAY_ANGLE_SDK_CONFIGURATION}")
    endif()

    ogplay_verify_manifest_files("${_angle_manifest}" "${_angle_sdk}" "ANGLE SDK")

    set(_angle_include_dir "${_angle_sdk}/include")
    string(JSON _angle_shared_headers ERROR_VARIABLE _angle_shared_error
        GET "${_angle_manifest}" shared_headers)
    if(NOT _angle_shared_error)
        if(_angle_shared_headers MATCHES "^[A-Za-z]:" OR
           _angle_shared_headers MATCHES "^/" OR
           _angle_shared_headers MATCHES "(^|/)\\.\\.(/|$)")
            message(FATAL_ERROR
                "ANGLE SDK manifest contains unsafe shared_headers: ${_angle_shared_headers}")
        endif()
        set(_angle_include_dir "${OGPLAY_ANGLE_SDK_ROOT}/${_angle_shared_headers}")
        set(_angle_header_manifest_path "${_angle_include_dir}/manifest.json")
        if(NOT EXISTS "${_angle_header_manifest_path}")
            message(FATAL_ERROR "ANGLE shared header manifest is missing")
        endif()
        file(READ "${_angle_header_manifest_path}" _angle_header_manifest)
        foreach(_angle_header_field schema_version kind angle_commit)
            string(JSON _angle_header_value ERROR_VARIABLE _angle_json_error
                GET "${_angle_header_manifest}" "${_angle_header_field}")
            if(_angle_json_error)
                message(FATAL_ERROR
                    "ANGLE shared header manifest has no valid ${_angle_header_field}")
            endif()
            set("_angle_header_${_angle_header_field}" "${_angle_header_value}")
        endforeach()
        if(NOT _angle_header_schema_version EQUAL 1 OR
           NOT _angle_header_kind STREQUAL "headers" OR
           NOT _angle_header_angle_commit STREQUAL _angle_manifest_angle_commit)
            message(FATAL_ERROR
                "ANGLE shared headers do not match the platform SDK commit")
        endif()
        ogplay_verify_manifest_files(
            "${_angle_header_manifest}" "${_angle_include_dir}" "ANGLE headers")
    endif()

    if(NOT EXISTS "${_angle_include_dir}/EGL/egl.h")
        message(FATAL_ERROR "ANGLE SDK does not contain EGL headers")
    endif()

    set(_angle_has_swiftshader FALSE)
    set(_angle_has_swiftshader_declaration FALSE)
    string(JSON _angle_gn_arg_count ERROR_VARIABLE _angle_json_error
        LENGTH "${_angle_manifest}" gn_args)
    if(_angle_json_error OR _angle_gn_arg_count LESS 1)
        message(FATAL_ERROR "ANGLE SDK manifest has no valid GN arguments")
    endif()
    math(EXPR _angle_last_gn_arg "${_angle_gn_arg_count} - 1")
    foreach(_angle_index RANGE 0 ${_angle_last_gn_arg})
        string(JSON _angle_gn_arg GET "${_angle_manifest}" gn_args ${_angle_index})
        if(_angle_gn_arg STREQUAL "angle_enable_swiftshader=true")
            set(_angle_has_swiftshader TRUE)
            set(_angle_has_swiftshader_declaration TRUE)
        elseif(_angle_gn_arg STREQUAL "angle_enable_swiftshader=false")
            set(_angle_has_swiftshader FALSE)
            set(_angle_has_swiftshader_declaration TRUE)
        endif()
    endforeach()
    if(NOT _angle_has_swiftshader_declaration)
        message(FATAL_ERROR
            "ANGLE SDK manifest does not declare angle_enable_swiftshader")
    endif()
    if(WIN32)
        find_file(_angle_egl_library NAMES libEGL.dll.lib libEGL.lib
            PATHS "${_angle_sdk}/lib" NO_DEFAULT_PATH NO_CACHE)
        find_file(_angle_glesv2_library NAMES libGLESv2.dll.lib libGLESv2.lib
            PATHS "${_angle_sdk}/lib" NO_DEFAULT_PATH NO_CACHE)
        find_file(_angle_egl_runtime NAMES libEGL.dll EGL.dll
            PATHS "${_angle_sdk}/bin" NO_DEFAULT_PATH NO_CACHE)
        find_file(_angle_glesv2_runtime NAMES libGLESv2.dll GLESv2.dll
            PATHS "${_angle_sdk}/bin" NO_DEFAULT_PATH NO_CACHE)
    elseif(APPLE)
        find_library(_angle_egl_library NAMES EGL libEGL
            PATHS "${_angle_sdk}/lib" NO_DEFAULT_PATH NO_CACHE)
        find_library(_angle_glesv2_library NAMES GLESv2 libGLESv2
            PATHS "${_angle_sdk}/lib" NO_DEFAULT_PATH NO_CACHE)
    else()
        find_file(_angle_egl_library NAMES libEGL.so libEGL.so.1
            PATHS "${_angle_sdk}/lib" NO_DEFAULT_PATH NO_CACHE)
        find_file(_angle_glesv2_library NAMES libGLESv2.so libGLESv2.so.2
            PATHS "${_angle_sdk}/lib" NO_DEFAULT_PATH NO_CACHE)
    endif()
    if(NOT _angle_egl_library OR NOT _angle_glesv2_library OR
       (WIN32 AND (NOT _angle_egl_runtime OR NOT _angle_glesv2_runtime)))
        message(FATAL_ERROR "ANGLE SDK has no usable EGL/GLESv2 artifacts: ${_angle_sdk}")
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
        INTERFACE_INCLUDE_DIRECTORIES "${_angle_include_dir}")
endif()

# ogplay_ffmpeg: trimmed FFmpeg merged into one shared library, built by the
# separate OGPlay-ffmpeg repository and consumed only through its prebuilt
# package (ADR-0136). AUTO builds without video decoding on platforms that
# have no package yet; FfmpegUnavailableReason() then reports why.
set(OGPLAY_FFMPEG_FOUND FALSE)
set(OGPLAY_FFMPEG_MISSING_REASON "")
if(NOT OGPLAY_ENABLE_FFMPEG MATCHES "^(AUTO|ON|OFF)$")
    message(FATAL_ERROR "OGPLAY_ENABLE_FFMPEG must be AUTO, ON or OFF")
endif()
if(OGPLAY_ENABLE_FFMPEG STREQUAL "OFF")
    set(OGPLAY_FFMPEG_MISSING_REASON "built with OGPLAY_ENABLE_FFMPEG=OFF")
else()
    if(NOT IS_ABSOLUTE "${OGPLAY_FFMPEG_SDK_ROOT}")
        message(FATAL_ERROR "OGPLAY_FFMPEG_SDK_ROOT must be an absolute path")
    endif()
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _ffmpeg_processor)
    if(_ffmpeg_processor MATCHES "^(amd64|x86_64)$")
        set(_ffmpeg_cpu "x64")
    elseif(_ffmpeg_processor MATCHES "^(arm64|aarch64)$")
        set(_ffmpeg_cpu "arm64")
    else()
        set(_ffmpeg_cpu "${_ffmpeg_processor}")
    endif()
    if(WIN32)
        set(_ffmpeg_os "windows")
    elseif(APPLE)
        set(_ffmpeg_os "macos")
    else()
        set(_ffmpeg_os "linux")
    endif()
    set(_ffmpeg_platform "${_ffmpeg_os}-${_ffmpeg_cpu}")
    set(_ffmpeg_sdk "${OGPLAY_FFMPEG_SDK_ROOT}/${_ffmpeg_platform}")

    if(NOT EXISTS "${_ffmpeg_sdk}/manifest.json")
        set(OGPLAY_FFMPEG_MISSING_REASON
            "no ogplay_ffmpeg package for ${_ffmpeg_platform} in third_party/ffmpeg-prebuilt")
        if(OGPLAY_ENABLE_FFMPEG STREQUAL "ON")
            message(FATAL_ERROR "OGPLAY_ENABLE_FFMPEG=ON but ${OGPLAY_FFMPEG_MISSING_REASON}. "
                "Run: git submodule update --init third_party/ffmpeg-prebuilt")
        endif()
        message(WARNING "Video decoding disabled: ${OGPLAY_FFMPEG_MISSING_REASON}")
    else()
        file(READ "${_ffmpeg_sdk}/manifest.json" _ffmpeg_manifest)
        foreach(_ffmpeg_field schema_version kind platform ffmpeg_commit library shared_headers)
            string(JSON _ffmpeg_value ERROR_VARIABLE _ffmpeg_json_error
                GET "${_ffmpeg_manifest}" "${_ffmpeg_field}")
            if(_ffmpeg_json_error)
                message(FATAL_ERROR
                    "ogplay_ffmpeg manifest has no valid ${_ffmpeg_field}: ${_ffmpeg_json_error}")
            endif()
            set("_ffmpeg_manifest_${_ffmpeg_field}" "${_ffmpeg_value}")
        endforeach()
        if(NOT _ffmpeg_manifest_schema_version EQUAL 1 OR
           NOT _ffmpeg_manifest_kind STREQUAL "platform" OR
           NOT _ffmpeg_manifest_platform STREQUAL _ffmpeg_platform)
            message(FATAL_ERROR "ogplay_ffmpeg manifest does not match ${_ffmpeg_platform}")
        endif()
        foreach(_ffmpeg_relative IN ITEMS
                "${_ffmpeg_manifest_library}" "${_ffmpeg_manifest_shared_headers}")
            if(_ffmpeg_relative MATCHES "^[A-Za-z]:" OR _ffmpeg_relative MATCHES "^/" OR
               _ffmpeg_relative MATCHES "(^|/)\\.\\.(/|$)")
                message(FATAL_ERROR "ogplay_ffmpeg manifest contains unsafe path: ${_ffmpeg_relative}")
            endif()
        endforeach()
        ogplay_verify_manifest_files("${_ffmpeg_manifest}" "${_ffmpeg_sdk}" "ogplay_ffmpeg SDK")

        set(_ffmpeg_include_dir "${OGPLAY_FFMPEG_SDK_ROOT}/${_ffmpeg_manifest_shared_headers}")
        if(NOT EXISTS "${_ffmpeg_include_dir}/manifest.json")
            message(FATAL_ERROR "ogplay_ffmpeg shared header manifest is missing")
        endif()
        file(READ "${_ffmpeg_include_dir}/manifest.json" _ffmpeg_header_manifest)
        string(JSON _ffmpeg_header_kind GET "${_ffmpeg_header_manifest}" kind)
        string(JSON _ffmpeg_header_commit GET "${_ffmpeg_header_manifest}" ffmpeg_commit)
        if(NOT _ffmpeg_header_kind STREQUAL "headers" OR
           NOT _ffmpeg_header_commit STREQUAL _ffmpeg_manifest_ffmpeg_commit)
            message(FATAL_ERROR "ogplay_ffmpeg headers do not match the platform package commit")
        endif()
        ogplay_verify_manifest_files(
            "${_ffmpeg_header_manifest}" "${_ffmpeg_include_dir}" "ogplay_ffmpeg headers")

        set(OGPLAY_FFMPEG_RUNTIME "${_ffmpeg_sdk}/${_ffmpeg_manifest_library}")
        add_library(FFmpeg::FFmpeg SHARED IMPORTED GLOBAL)
        set_target_properties(FFmpeg::FFmpeg PROPERTIES
            IMPORTED_LOCATION "${OGPLAY_FFMPEG_RUNTIME}"
            INTERFACE_INCLUDE_DIRECTORIES "${_ffmpeg_include_dir}")
        if(WIN32)
            string(JSON _ffmpeg_import_library GET "${_ffmpeg_manifest}" import_library)
            set_target_properties(FFmpeg::FFmpeg PROPERTIES
                IMPORTED_IMPLIB "${_ffmpeg_sdk}/${_ffmpeg_import_library}")
        elseif(APPLE)
            get_filename_component(_ffmpeg_library_name "${OGPLAY_FFMPEG_RUNTIME}" NAME)
            set_target_properties(FFmpeg::FFmpeg PROPERTIES
                IMPORTED_SONAME "@rpath/${_ffmpeg_library_name}")
        endif()
        set(OGPLAY_FFMPEG_FOUND TRUE)
        message(STATUS "ogplay_ffmpeg: ${_ffmpeg_platform} (FFmpeg ${_ffmpeg_manifest_ffmpeg_commit})")
    endif()
endif()
