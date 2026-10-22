/* SPDX-License-Identifier: Apache-2.0
 * Standalone Dalvik JNI probes. Oracles cite vm/Jni.cpp (Android 4.4).
 */
#include "probe.h"
static unsigned char storage[16];
static int holds(JNIEnv *env, jobject obj) {
    jclass thread = (*env)->FindClass(env, "java/lang/Thread");
    jmethodID m = (*env)->GetStaticMethodID(env, thread, "holdsLock", "(Ljava/lang/Object;)Z");
    return (*env)->CallStaticBooleanMethod(env, thread, m, obj);
}
JNIEXPORT jstring JNICALL Java_probe_MonitorNioCases_monitor(JNIEnv *env, jclass unused, jint mode,
                                                             jobject obj) {
    /* Jni.cpp:2525-2549: explicit ownership, reentrancy, non-owner IMS exception. */
    if (mode == 2) {
        jint rc = (*env)->MonitorExit(env, obj);
        if (rc != JNI_ERR)
            return failure(env, "non-owner MonitorExit did not return JNI_ERR");
        return expect_exception(env, "java/lang/IllegalMonitorStateException");
    }
    if ((*env)->MonitorEnter(env, obj) != JNI_OK)
        return failure(env, "MonitorEnter status");
    int ok = holds(env, obj);
    if (mode == 1) {
        if ((*env)->MonitorEnter(env, obj) != JNI_OK)
            return failure(env, "reentrant enter");
        int one_exit = (*env)->MonitorExit(env, obj) == JNI_OK;
        int still_held = holds(env, obj);
        ok = ok && one_exit && still_held;
    }
    int exited = (*env)->MonitorExit(env, obj) == JNI_OK;
    return ok && exited && !holds(env, obj) ? NULL : failure(env, "monitor ownership/depth");
}
JNIEXPORT jobject JNICALL Java_probe_MonitorNioCases_create(JNIEnv *env, jclass unused) {
    /* Jni.cpp:2691-2726: DirectByteBuffer wraps caller-owned native memory. */
    storage[0] = 11;
    storage[1] = 22;
    storage[2] = 33;
    return (*env)->NewDirectByteBuffer(env, storage, sizeof(storage));
}
JNIEXPORT jstring JNICALL Java_probe_MonitorNioCases_buffer(JNIEnv *env, jclass unused, jobject obj,
                                                            jint mode) {
    /* Jni.cpp:2733-2738: effectiveDirectAddress, NULL for heap buffers. */
    void *address = (*env)->GetDirectBufferAddress(env, obj);
    if (mode == 1)
        return address == NULL ? NULL : failure(env, "heap buffer address not NULL");
    /* Jni.cpp:2742-2759: NON-CHECK Dalvik deliberately returns Buffer.capacity even for heap.
     * This differs from the -1 mentioned in the function's own comment and JNI spec. */
    jlong capacity = (*env)->GetDirectBufferCapacity(env, obj);
    if (mode == 2)
        return capacity == 7 ? NULL : failure(env, "Dalvik heap buffer capacity expected 7");
    if (address != storage || capacity != 16)
        return failure(env, "direct buffer address/capacity");
    if (storage[1] != 73)
        return failure(env, "Java write not visible in native storage");
    storage[2] = (unsigned char)-55;
    return NULL;
}
