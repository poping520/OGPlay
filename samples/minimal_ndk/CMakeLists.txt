cmake_minimum_required(VERSION 3.10)

project(ogplay_minimal_ndk LANGUAGES C CXX)

if(NOT ANDROID)
    message(FATAL_ERROR "The minimal NDK sample must use the Android NDK toolchain")
endif()

set(NATIVE_APP_GLUE_DIR
    "${CMAKE_ANDROID_NDK}/sources/android/native_app_glue"
)
if(NOT EXISTS "${NATIVE_APP_GLUE_DIR}/android_native_app_glue.c")
    message(FATAL_ERROR "android_native_app_glue was not found in the selected NDK")
endif()

add_library(ogplay_minimal_ndk SHARED
    src/main/cpp/main.cpp
    "${NATIVE_APP_GLUE_DIR}/android_native_app_glue.c"
)
target_include_directories(ogplay_minimal_ndk PRIVATE "${NATIVE_APP_GLUE_DIR}")
target_compile_features(ogplay_minimal_ndk PRIVATE cxx_std_17)
target_compile_options(ogplay_minimal_ndk PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Werror;-fno-exceptions;-fno-rtti>
)
target_link_libraries(ogplay_minimal_ndk PRIVATE android EGL GLESv2 log)
set_target_properties(ogplay_minimal_ndk PROPERTIES
    CXX_EXTENSIONS OFF
    OUTPUT_NAME ogplay_minimal_ndk
)
