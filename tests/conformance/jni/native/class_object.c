/* SPDX-License-Identifier: Apache-2.0
 * Independent probes derived from Dalvik vm/Jni.cpp, Android 4.4.
 * Every oracle below cites the corresponding implementation, not HotSpot.
 */
#include "probe.h"
#include <stdarg.h>
/* Jni.cpp:1363-1380,1396: inspect then clear; compare class identity, not message. */
jstring expect_exception(JNIEnv *env, const char *type) {
    jthrowable exception = (*env)->ExceptionOccurred(env);
    if (!exception)
        return failure(env, "expected exception, none pending");
    (*env)->ExceptionClear(env);
    jclass actual = (*env)->GetObjectClass(env, exception);
    jclass expected = (*env)->FindClass(env, type);
    return (*env)->IsSameObject(env, actual, expected) ? NULL : failure(env, type);
}
JNIEXPORT jclass JNICALL Java_probe_ClassObjectCases_superclass(JNIEnv *env, jclass unused,
                                                                jclass type) {
    /* Jni.cpp:1264-1267: returns clazz->super. */
    return (*env)->GetSuperclass(env, type);
}
JNIEXPORT jboolean JNICALL Java_probe_ClassObjectCases_assignable(JNIEnv *env, jclass unused,
                                                                  jclass a, jclass b) {
    /* Jni.cpp:1275-1279: dvmInstanceof(clazz1,clazz2), in this order. */
    return (*env)->IsAssignableFrom(env, a, b);
}
JNIEXPORT jboolean JNICALL Java_probe_ClassObjectCases_instance(JNIEnv *env, jclass unused,
                                                                jobject obj, jclass type) {
    /* Jni.cpp:1600-1610: null obj returns true; otherwise actual class ancestry. */
    return (*env)->IsInstanceOf(env, obj, type);
}
static jobject construct_v(JNIEnv *env, jclass type, jmethodID ctor, ...) {
    va_list args;
    va_start(args, ctor);
    jobject result = (*env)->NewObjectV(env, type, ctor, args);
    va_end(args);
    return result;
}
JNIEXPORT jobject JNICALL Java_probe_ClassObjectCases_construct(JNIEnv *env, jclass unused,
                                                                jclass type, jint mode) {
    /* Jni.cpp:1508-1520;1535-1542;1556-1560: allocation vs constructor dispatch. */
    if (mode == 0)
        return (*env)->AllocObject(env, type);
    jmethodID ctor = (*env)->GetMethodID(env, type, "<init>", "(JDLjava/lang/String;I)V");
    if (!ctor)
        return NULL;
    jstring marker = (*env)->NewStringUTF(env, "ctor");
    if (mode == 1)
        return (*env)->NewObject(env, type, ctor, (jlong)-0x123456789abcdefLL, -7.25, marker, -73);
    return construct_v(env, type, ctor, (jlong)-0x123456789abcdefLL, -7.25, marker, -73);
}
