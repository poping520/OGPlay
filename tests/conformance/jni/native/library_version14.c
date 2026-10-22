/* SPDX-License-Identifier: Apache-2.0
 * Test points rewritten from CTS tests/tests/jni (libjnitest and Java peers),
 * android-4.4.4_r2.0.1, commit b8e2daba969c1d0d9eb3feec9559e12db378681b.
 * Copyright (C) 2009 The Android Open Source Project
 * Licensed under Apache-2.0: https://www.apache.org/licenses/LICENSE-2.0
 */
#include <jni.h>
/* JavaVM accepts both JNI 1.4 and 1.6, independently loaded libraries. */
static jint loads;
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    (void)vm;
    (void)reserved;
    loads++;
    return JNI_VERSION_1_4;
}
JNIEXPORT jint JNICALL Java_probe_LibraryCases_loadCount14(JNIEnv *env, jclass k) {
    (void)env;
    (void)k;
    return loads;
}
