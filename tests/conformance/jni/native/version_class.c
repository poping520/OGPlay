/* SPDX-License-Identifier: Apache-2.0
 * Test points rewritten from CTS tests/tests/jni (libjnitest and Java peers),
 * android-4.4.4_r2.0.1, commit b8e2daba969c1d0d9eb3feec9559e12db378681b.
 * Copyright (C) 2009 The Android Open Source Project
 * Licensed under Apache-2.0: https://www.apache.org/licenses/LICENSE-2.0
 */
#include "probe.h"
jstring run_version(JNIEnv *env, const char *id) {
    if (strcmp(id, "version.GetVersion") == 0)
        return (*env)->GetVersion(env) == JNI_VERSION_1_6 ? NULL
                                                          : failure(env, "GetVersion expected 1.6");
    if (strcmp(id, "version.DefineClass") == 0) {
        /* dalvik/vm/Jni.cpp:1198: unsupported, NULL without throwing. */
        jclass c = (*env)->DefineClass(env, "probe/Defined", NULL, NULL, 0);
        return c == NULL && !(*env)->ExceptionCheck(env)
                   ? NULL
                   : failure(env, "DefineClass expected NULL without exception");
    }
    return failure(env, "unknown version case");
}
