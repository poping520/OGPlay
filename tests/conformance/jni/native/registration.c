/* SPDX-License-Identifier: Apache-2.0
 * Standalone Dalvik JNI probes. Oracles cite vm/Jni.cpp (Android 4.4).
 */
#include "probe.h"
static jint dynamic_value(JNIEnv *env, jclass unused) { return 29; }
JNIEXPORT jint JNICALL Java_probe_RegistrationCases_00024ExportTarget_value(JNIEnv *env,
                                                                            jclass unused) {
    return 17;
}
JNIEXPORT jstring JNICALL Java_probe_RegistrationCases_registerTarget(JNIEnv *env, jclass unused,
                                                                      jclass target) {
    /* Jni.cpp:2459-2478: register replaces native entry; Native.cpp:534 resolves it again after
     * unregister. */
    JNINativeMethod method = {"value", "()I", (void *)dynamic_value};
    return (*env)->RegisterNatives(env, target, &method, 1) == JNI_OK
               ? NULL
               : failure(env, "RegisterNatives failed");
}
JNIEXPORT jstring JNICALL Java_probe_RegistrationCases_unregister(JNIEnv *env, jclass unused,
                                                                  jclass target) {
    /* Jni.cpp:2507-2516 -> Native.cpp:516-544 resets to dvmResolveNativeMethod. */
    return (*env)->UnregisterNatives(env, target) == JNI_OK
               ? NULL
               : failure(env, "UnregisterNatives status");
}
