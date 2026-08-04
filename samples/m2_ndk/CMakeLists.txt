cmake_minimum_required(VERSION 3.10)

project(ogplay_m2_ndk C)

if(NOT ANDROID)
    message(FATAL_ERROR "The M2 NDK sample must use the Android NDK toolchain")
endif()

add_library(ogplay_m2_ndk SHARED src/sample.c)
target_compile_options(ogplay_m2_ndk PRIVATE
    -Wall -Wextra -Werror -fno-builtin
)
target_link_options(ogplay_m2_ndk PRIVATE
    -Wl,--hash-style=sysv -Wl,--no-undefined
)
set_target_properties(ogplay_m2_ndk PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    OUTPUT_NAME ogplay_m2_ndk
)
