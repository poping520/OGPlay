cmake_minimum_required(VERSION 3.10)

project(ogplay_m4_exit LANGUAGES C CXX)

if(NOT ANDROID)
    message(FATAL_ERROR "The M4 exit sample must use the Android NDK toolchain")
endif()

set(NATIVE_APP_GLUE_DIR
    "${CMAKE_ANDROID_NDK}/sources/android/native_app_glue"
)
if(NOT EXISTS "${NATIVE_APP_GLUE_DIR}/android_native_app_glue.c")
    message(FATAL_ERROR "android_native_app_glue was not found in the selected NDK")
endif()

add_library(ogplay_m4_exit SHARED
    src/main/cpp/main.cpp
    "${NATIVE_APP_GLUE_DIR}/android_native_app_glue.c"
)
target_include_directories(ogplay_m4_exit PRIVATE "${NATIVE_APP_GLUE_DIR}")
target_compile_features(ogplay_m4_exit PRIVATE cxx_std_17)
target_compile_options(ogplay_m4_exit PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Werror;-fno-exceptions;-fno-rtti>
)
target_link_libraries(ogplay_m4_exit PRIVATE android EGL GLESv2 log)
set_target_properties(ogplay_m4_exit PROPERTIES
    CXX_EXTENSIONS OFF
    OUTPUT_NAME ogplay_m4_exit
)
