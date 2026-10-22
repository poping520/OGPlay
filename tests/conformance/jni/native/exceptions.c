/* SPDX-License-Identifier: Apache-2.0
 * Standalone Dalvik JNI probes. Oracles cite vm/Jni.cpp (Android 4.4).
 */
#include "probe.h"
JNIEXPORT void JNICALL Java_probe_ExceptionsCases_throwSame(JNIEnv *env, jclass unused,
                                                            jthrowable original) {
    /* Jni.cpp:1333-1338: sets this same object as pending. */
    (*env)->Throw(env, original);
}
JNIEXPORT jstring JNICALL Java_probe_ExceptionsCases_probe(JNIEnv *env, jclass owner, jint mode,
                                                           jthrowable original, jobject receiver) {
    jclass target = (*env)->GetObjectClass(env, receiver);
    jmethodID callback = (*env)->GetMethodID(env, target, "callback", "()V");
    if (!callback)
        return failure(env, "callback setup");
    /* Jni.cpp:1333-1338: install original without constructing a replacement. */
    if ((*env)->Throw(env, original) != JNI_OK)
        return failure(env, "Throw status");
    jobject produced = NULL;
    jmethodID method = NULL;
    if (mode == 1) {
        /* Jni.cpp:1383-1387 delegates to Exception.cpp:564-592 which RESTORES pending. */
        (*env)->ExceptionDescribe(env);
    } else if (mode == 2) {
        /* Jni.cpp:1625-1668 has no pending guard. Class already initialized. */
        method = (*env)->GetMethodID(env, target, "callback", "()V");
    } else if (mode == 3) {
        /* Jni.cpp:2161-2170 has no pending guard. */
        produced = (*env)->NewStringUTF(env, "pending-created");
    } else if (mode == 4) {
        /* Jni.cpp:1909-1973 CALL_VIRTUAL has no pending guard. */
        (*env)->CallVoidMethod(env, receiver, callback);
    }
    /* Jni.cpp:1363-1380: returns local reference and leaves original pending. */
    jthrowable first = (*env)->ExceptionOccurred(env);
    int pending = (*env)->ExceptionCheck(env);
    jthrowable second = (*env)->ExceptionOccurred(env);
    (*env)->ExceptionClear(env);
    int same = first && second && pending && (*env)->IsSameObject(env, first, original) &&
               (*env)->IsSameObject(env, second, original);
    int local = mode != 5 || (first && (*env)->GetObjectRefType(env, first) == JNILocalRefType);
    if (!same || !local)
        return failure(env, "pending identity/local-reference mismatch");
    if (mode == 2 && !method)
        return failure(env, "GetMethodID blocked under pending");
    if (mode == 3 && !text_equal(env, (jstring)produced, "pending-created"))
        return failure(env, "NewStringUTF blocked under pending");
    return NULL;
}
JNIEXPORT void JNICALL Java_probe_ExceptionsCases_fatal(JNIEnv *env, jclass unused) {
    /* Jni.cpp:1406-1409: message then ReportJniError; never returns. */
    (*env)->FatalError(env, "OGPLAY_JNI_FATAL_WU8_1B");
}
