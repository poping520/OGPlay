cmake_minimum_required(VERSION 3.10)

project(ogplay_sweetshop LANGUAGES C CXX)

if(NOT ANDROID)
    message(FATAL_ERROR "The sweetshop sample must use the Android NDK toolchain")
endif()

set(NATIVE_APP_GLUE_DIR "${CMAKE_ANDROID_NDK}/sources/android/native_app_glue")
set(STB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/stb")

add_library(sweetshop SHARED
    src/main/cpp/main.cpp
    src/main/cpp/gfx.cpp
    src/main/cpp/audio.cpp
    src/main/cpp/board.cpp
    src/main/cpp/game.cpp
    src/main/cpp/stb_image_impl.cpp
    "${NATIVE_APP_GLUE_DIR}/android_native_app_glue.c"
)
target_include_directories(sweetshop PRIVATE "${NATIVE_APP_GLUE_DIR}")
target_include_directories(sweetshop SYSTEM PRIVATE "${STB_DIR}")
target_compile_features(sweetshop PRIVATE cxx_std_17)
target_compile_options(sweetshop PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Werror;-fno-exceptions;-fno-rtti>
)
set_source_files_properties(src/main/cpp/stb_image_impl.cpp PROPERTIES COMPILE_OPTIONS "-w")
target_link_libraries(sweetshop PRIVATE android EGL GLESv2 OpenSLES log m)
# NativeActivity looks up ANativeActivity_onCreate in the glue object.
set_target_properties(sweetshop PROPERTIES
    CXX_EXTENSIONS OFF
    LINK_FLAGS "-u ANativeActivity_onCreate"
)
