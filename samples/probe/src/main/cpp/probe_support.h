#pragma once

#include <jni.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

jlongArray probe_longs(JNIEnv* env, const jlong* values, size_t length);
int64_t probe_clock(clockid_t clock);

#ifdef __cplusplus
}
#endif
