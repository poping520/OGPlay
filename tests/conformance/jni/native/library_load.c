/* SPDX-License-Identifier: Apache-2.0
 * Test points rewritten from CTS tests/tests/jni (libjnitest and Java peers),
 * android-4.4.4_r2.0.1, commit b8e2daba969c1d0d9eb3feec9559e12db378681b.
 * Copyright (C) 2009 The Android Open Source Project
 * Licensed under Apache-2.0: https://www.apache.org/licenses/LICENSE-2.0
 */
#include "probe.h"
static jint loads;
jstring failure(JNIEnv *env, const char *message) {
    /* A pending callback exception is a failed probe, not permission to call
     * NewStringUTF under a pending exception. Retain the named failure. */
    if ((*env)->ExceptionCheck(env))
        (*env)->ExceptionClear(env);
    return (*env)->NewStringUTF(env, message);
}
jboolean text_equal(JNIEnv *env, jstring value, const char *expected) {
    if (!value)
        return JNI_FALSE;
    const char *text = (*env)->GetStringUTFChars(env, value, NULL);
    if (!text)
        return JNI_FALSE;
    jboolean equal = strcmp(text, expected) == 0;
    (*env)->ReleaseStringUTFChars(env, value, text);
    return equal;
}
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    (void)reserved;
    JNIEnv *env = NULL;
    loads++;
    if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK)
        return JNI_ERR;
    if (register_BoundaryStatic(env) != 0 || register_BoundaryInstance(env) != 0)
        return JNI_ERR;
    return JNI_VERSION_1_6;
}
JNIEXPORT jint JNICALL Java_probe_Main_loadCount(JNIEnv *env, jclass k) {
    (void)env;
    (void)k;
    return loads;
}
JNIEXPORT jint JNICALL Java_probe_Main_shortExport(JNIEnv *env, jclass k) {
    (void)env;
    (void)k;
    return 73;
}
JNIEXPORT jint JNICALL Java_probe_Main_overloaded__I(JNIEnv *env, jclass k, jint v) {
    (void)env;
    (void)k;
    return v + 1;
}
JNIEXPORT jlong JNICALL Java_probe_Main_overloaded__J(JNIEnv *env, jclass k, jlong v) {
    (void)env;
    (void)k;
    return v + 2;
}
JNIEXPORT jstring JNICALL Java_probe_Main_runNative(JNIEnv *env, jclass k, jstring name) {
    (void)k;
    const char *s = (*env)->GetStringUTFChars(env, name, NULL);
    char id[160];
    if (!s)
        return NULL;
    if (strlen(s) >= sizeof(id)) {
        (*env)->ReleaseStringUTFChars(env, name, s);
        return failure(env, "case id too long");
    }
    strcpy(id, s);
    (*env)->ReleaseStringUTFChars(env, name, s);
    if (strncmp(id, "version.", 8) == 0)
        return run_version(env, id);
    return run_call(env, id);
}
