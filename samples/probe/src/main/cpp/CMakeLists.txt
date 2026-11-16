cmake_minimum_required(VERSION 3.10)
project(ogprobe LANGUAGES C CXX)
add_library(ogprobe SHARED probe.c bionic.c ndk.c cpp.cpp)
target_compile_features(ogprobe PRIVATE c_std_11 cxx_std_11)
target_link_libraries(ogprobe PRIVATE android OpenSLES dl)
target_compile_options(ogprobe PRIVATE -Wall -Wextra -Werror -fvisibility=hidden)
# The NDK may link against API 21 stubs. ARMv7 API 19 has no GNU hash loader support.
target_link_options(ogprobe PRIVATE -Wl,--hash-style=sysv -Wl,--wrap=android_set_abort_message)
