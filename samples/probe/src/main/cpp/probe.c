#include "probe_support.h"

#include <errno.h>
#include <sys/utsname.h>
#include <stdlib.h>
#include <unistd.h>

JNIEXPORT jint JNICALL Java_org_ogplay_probe_NativeProbe_ping(JNIEnv* env, jclass type) {
    (void)env;
    (void)type;
    return 19;
}

JNIEXPORT jlong JNICALL Java_org_ogplay_probe_NativeProbe_cpuCount(JNIEnv* env, jclass type, jboolean online) {
    (void)env;
    (void)type;
    return sysconf(online ? _SC_NPROCESSORS_ONLN : _SC_NPROCESSORS_CONF);
}

JNIEXPORT void JNICALL Java_org_ogplay_probe_NativeProbe_abortProcess(JNIEnv* env, jclass type) {
    (void)env;
    (void)type;
    abort();
}

JNIEXPORT jstring JNICALL Java_org_ogplay_probe_NativeProbe_machine(JNIEnv* env, jclass type) {
    (void)type;
    struct utsname value;
    if (uname(&value) != 0) return NULL;
    return (*env)->NewStringUTF(env, value.machine);
}

JNIEXPORT jlongArray JNICALL Java_org_ogplay_probe_NativeProbe_clocks(JNIEnv* env, jclass type) {
    (void)type;
    const clockid_t ids[] = {CLOCK_MONOTONIC, CLOCK_REALTIME, CLOCK_BOOTTIME};
    jlong samples[3];
    for (size_t index = 0; index < sizeof(ids) / sizeof(ids[0]); ++index) {
        samples[index] = probe_clock(ids[index]);
    }
    return probe_longs(env, samples, 3);
}

jlongArray probe_longs(JNIEnv* env, const jlong* values, size_t length) {
    jlongArray output = (*env)->NewLongArray(env, (jsize)length);
    if (output != NULL) (*env)->SetLongArrayRegion(env, output, 0, (jsize)length, values);
    return output;
}

int64_t probe_clock(clockid_t clock) {
    struct timespec now;
    if (clock_gettime(clock, &now) != 0) return -errno;
    return (int64_t)now.tv_sec * INT64_C(1000000000) + now.tv_nsec;
}
