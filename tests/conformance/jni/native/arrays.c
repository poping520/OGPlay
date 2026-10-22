/* SPDX-License-Identifier: Apache-2.0
 * Independent probes derived from Dalvik vm/Jni.cpp, Android 4.4.
 * Every oracle below cites the corresponding implementation, not HotSpot.
 */
#include "probe.h"
/* Jni.cpp:2355-2386: retained lease is typed; Java retains the array itself. */
static jboolean *lease_Boolean;
static jstring array_Boolean(JNIEnv *env, jbooleanArray array, int op, int action) {
    if (op == 5) {
        if (action == 0) {
            lease_Boolean = (*env)->GetBooleanArrayElements(env, array, NULL);
            if (!lease_Boolean)
                return failure(env, "GetBooleanArrayElements: NULL lease");
            /* Jni.cpp:2361-2366: direct data is visible to Java before Release. */
            lease_Boolean[0] = JNI_TRUE;
            return NULL;
        }
        if (!lease_Boolean)
            return failure(env, "missing array lease");
        if (action == 2) {
            /* Jni.cpp:2381: JNI_COMMIT keeps the pin/lease; write through it again. */
            (*env)->ReleaseBooleanArrayElements(env, array, lease_Boolean, JNI_COMMIT);
            lease_Boolean[0] = JNI_FALSE;
            return NULL;
        }
        /* Jni.cpp:2381-2384: JNI_ABORT only unpins; no undo of direct writes. */
        (*env)->ReleaseBooleanArrayElements(env, array, lease_Boolean, action == 1 ? JNI_ABORT : 0);
        lease_Boolean = NULL;
        return NULL;
    }
    if (op == 0 || op == 1 || op == 4) {
        jboolean copy = JNI_TRUE;
        jboolean *p = (*env)->GetBooleanArrayElements(env, array, &copy);
        if (!p)
            return failure(env, "GetBooleanArrayElements: NULL data");
        /* Jni.cpp:2362: typed array contents; 2376-2386: release only unpins. */
        int correct = p[1] == JNI_FALSE;
        if (op == 1)
            p[0] = JNI_TRUE;
        (*env)->ReleaseBooleanArrayElements(env, array, p, op == 1 ? 0 : JNI_ABORT);
        if (op == 4)
            return copy == JNI_FALSE
                       ? NULL
                       : failure(env, "GetBooleanArrayElements: isCopy expected false");
        return correct ? NULL : failure(env, "GetBooleanArrayElements: initial data mismatch");
    }
    if (op == 2) {
        jboolean buf[3] = {JNI_TRUE, JNI_TRUE, JNI_TRUE};
        /* Jni.cpp:2410: copies only len typed elements starting at start. */
        (*env)->GetBooleanArrayRegion(env, array, 1, 1, buf + 1);
        return !(*env)->ExceptionCheck(env) && buf[0] == JNI_TRUE && buf[1] == JNI_FALSE &&
                       buf[2] == JNI_TRUE
                   ? NULL
                   : failure(env, "GetBooleanArrayRegion: region/guard mismatch");
    }
    if (op == 3) {
        jboolean value = JNI_TRUE;
        /* Jni.cpp:2427: copies into the array; Java checks unchanged guards. */
        (*env)->SetBooleanArrayRegion(env, array, 1, 1, &value);
        return (*env)->ExceptionCheck(env)
                   ? failure(env, "SetBooleanArrayRegion: unexpected exception")
                   : NULL;
    }
    /* Jni.cpp:2388-2394,2407-2408,2424-2425: all three invalid regions throw AIOOBE. */
    const jsize starts[] = {-1, 0, 2}, lengths[] = {1, -1, 2};
    jboolean buf[4] = {0};
    for (int i = 0; i < 3; i++) {
        if (op == 6)
            (*env)->GetBooleanArrayRegion(env, array, starts[i], lengths[i], buf);
        else
            (*env)->SetBooleanArrayRegion(env, array, starts[i], lengths[i], buf);
        jstring error = expect_exception(env, "java/lang/ArrayIndexOutOfBoundsException");
        if (error)
            return error;
    }
    return NULL;
}
/* Jni.cpp:2355-2386: retained lease is typed; Java retains the array itself. */
static jbyte *lease_Byte;
static jstring array_Byte(JNIEnv *env, jbyteArray array, int op, int action) {
    if (op == 5) {
        if (action == 0) {
            lease_Byte = (*env)->GetByteArrayElements(env, array, NULL);
            if (!lease_Byte)
                return failure(env, "GetByteArrayElements: NULL lease");
            /* Jni.cpp:2361-2366: direct data is visible to Java before Release. */
            lease_Byte[0] = -73;
            return NULL;
        }
        if (!lease_Byte)
            return failure(env, "missing array lease");
        if (action == 2) {
            /* Jni.cpp:2381: JNI_COMMIT keeps the pin/lease; write through it again. */
            (*env)->ReleaseByteArrayElements(env, array, lease_Byte, JNI_COMMIT);
            lease_Byte[0] = 29;
            return NULL;
        }
        /* Jni.cpp:2381-2384: JNI_ABORT only unpins; no undo of direct writes. */
        (*env)->ReleaseByteArrayElements(env, array, lease_Byte, action == 1 ? JNI_ABORT : 0);
        lease_Byte = NULL;
        return NULL;
    }
    if (op == 0 || op == 1 || op == 4) {
        jboolean copy = JNI_TRUE;
        jbyte *p = (*env)->GetByteArrayElements(env, array, &copy);
        if (!p)
            return failure(env, "GetByteArrayElements: NULL data");
        /* Jni.cpp:2362: typed array contents; 2376-2386: release only unpins. */
        int correct = p[1] == -99;
        if (op == 1)
            p[0] = -73;
        (*env)->ReleaseByteArrayElements(env, array, p, op == 1 ? 0 : JNI_ABORT);
        if (op == 4)
            return copy == JNI_FALSE ? NULL
                                     : failure(env, "GetByteArrayElements: isCopy expected false");
        return correct ? NULL : failure(env, "GetByteArrayElements: initial data mismatch");
    }
    if (op == 2) {
        jbyte buf[3] = {29, 29, 29};
        /* Jni.cpp:2410: copies only len typed elements starting at start. */
        (*env)->GetByteArrayRegion(env, array, 1, 1, buf + 1);
        return !(*env)->ExceptionCheck(env) && buf[0] == 29 && buf[1] == -99 && buf[2] == 29
                   ? NULL
                   : failure(env, "GetByteArrayRegion: region/guard mismatch");
    }
    if (op == 3) {
        jbyte value = -73;
        /* Jni.cpp:2427: copies into the array; Java checks unchanged guards. */
        (*env)->SetByteArrayRegion(env, array, 1, 1, &value);
        return (*env)->ExceptionCheck(env)
                   ? failure(env, "SetByteArrayRegion: unexpected exception")
                   : NULL;
    }
    /* Jni.cpp:2388-2394,2407-2408,2424-2425: all three invalid regions throw AIOOBE. */
    const jsize starts[] = {-1, 0, 2}, lengths[] = {1, -1, 2};
    jbyte buf[4] = {0};
    for (int i = 0; i < 3; i++) {
        if (op == 6)
            (*env)->GetByteArrayRegion(env, array, starts[i], lengths[i], buf);
        else
            (*env)->SetByteArrayRegion(env, array, starts[i], lengths[i], buf);
        jstring error = expect_exception(env, "java/lang/ArrayIndexOutOfBoundsException");
        if (error)
            return error;
    }
    return NULL;
}
/* Jni.cpp:2355-2386: retained lease is typed; Java retains the array itself. */
static jshort *lease_Short;
static jstring array_Short(JNIEnv *env, jshortArray array, int op, int action) {
    if (op == 5) {
        if (action == 0) {
            lease_Short = (*env)->GetShortArrayElements(env, array, NULL);
            if (!lease_Short)
                return failure(env, "GetShortArrayElements: NULL lease");
            /* Jni.cpp:2361-2366: direct data is visible to Java before Release. */
            lease_Short[0] = -12345;
            return NULL;
        }
        if (!lease_Short)
            return failure(env, "missing array lease");
        if (action == 2) {
            /* Jni.cpp:2381: JNI_COMMIT keeps the pin/lease; write through it again. */
            (*env)->ReleaseShortArrayElements(env, array, lease_Short, JNI_COMMIT);
            lease_Short[0] = 23456;
            return NULL;
        }
        /* Jni.cpp:2381-2384: JNI_ABORT only unpins; no undo of direct writes. */
        (*env)->ReleaseShortArrayElements(env, array, lease_Short, action == 1 ? JNI_ABORT : 0);
        lease_Short = NULL;
        return NULL;
    }
    if (op == 0 || op == 1 || op == 4) {
        jboolean copy = JNI_TRUE;
        jshort *p = (*env)->GetShortArrayElements(env, array, &copy);
        if (!p)
            return failure(env, "GetShortArrayElements: NULL data");
        /* Jni.cpp:2362: typed array contents; 2376-2386: release only unpins. */
        int correct = p[1] == -23456;
        if (op == 1)
            p[0] = -12345;
        (*env)->ReleaseShortArrayElements(env, array, p, op == 1 ? 0 : JNI_ABORT);
        if (op == 4)
            return copy == JNI_FALSE ? NULL
                                     : failure(env, "GetShortArrayElements: isCopy expected false");
        return correct ? NULL : failure(env, "GetShortArrayElements: initial data mismatch");
    }
    if (op == 2) {
        jshort buf[3] = {23456, 23456, 23456};
        /* Jni.cpp:2410: copies only len typed elements starting at start. */
        (*env)->GetShortArrayRegion(env, array, 1, 1, buf + 1);
        return !(*env)->ExceptionCheck(env) && buf[0] == 23456 && buf[1] == -23456 &&
                       buf[2] == 23456
                   ? NULL
                   : failure(env, "GetShortArrayRegion: region/guard mismatch");
    }
    if (op == 3) {
        jshort value = -12345;
        /* Jni.cpp:2427: copies into the array; Java checks unchanged guards. */
        (*env)->SetShortArrayRegion(env, array, 1, 1, &value);
        return (*env)->ExceptionCheck(env)
                   ? failure(env, "SetShortArrayRegion: unexpected exception")
                   : NULL;
    }
    /* Jni.cpp:2388-2394,2407-2408,2424-2425: all three invalid regions throw AIOOBE. */
    const jsize starts[] = {-1, 0, 2}, lengths[] = {1, -1, 2};
    jshort buf[4] = {0};
    for (int i = 0; i < 3; i++) {
        if (op == 6)
            (*env)->GetShortArrayRegion(env, array, starts[i], lengths[i], buf);
        else
            (*env)->SetShortArrayRegion(env, array, starts[i], lengths[i], buf);
        jstring error = expect_exception(env, "java/lang/ArrayIndexOutOfBoundsException");
        if (error)
            return error;
    }
    return NULL;
}
/* Jni.cpp:2355-2386: retained lease is typed; Java retains the array itself. */
static jchar *lease_Char;
static jstring array_Char(JNIEnv *env, jcharArray array, int op, int action) {
    if (op == 5) {
        if (action == 0) {
            lease_Char = (*env)->GetCharArrayElements(env, array, NULL);
            if (!lease_Char)
                return failure(env, "GetCharArrayElements: NULL lease");
            /* Jni.cpp:2361-2366: direct data is visible to Java before Release. */
            lease_Char[0] = 0xabcd;
            return NULL;
        }
        if (!lease_Char)
            return failure(env, "missing array lease");
        if (action == 2) {
            /* Jni.cpp:2381: JNI_COMMIT keeps the pin/lease; write through it again. */
            (*env)->ReleaseCharArrayElements(env, array, lease_Char, JNI_COMMIT);
            lease_Char[0] = 0x9876;
            return NULL;
        }
        /* Jni.cpp:2381-2384: JNI_ABORT only unpins; no undo of direct writes. */
        (*env)->ReleaseCharArrayElements(env, array, lease_Char, action == 1 ? JNI_ABORT : 0);
        lease_Char = NULL;
        return NULL;
    }
    if (op == 0 || op == 1 || op == 4) {
        jboolean copy = JNI_TRUE;
        jchar *p = (*env)->GetCharArrayElements(env, array, &copy);
        if (!p)
            return failure(env, "GetCharArrayElements: NULL data");
        /* Jni.cpp:2362: typed array contents; 2376-2386: release only unpins. */
        int correct = p[1] == 0xffff;
        if (op == 1)
            p[0] = 0xabcd;
        (*env)->ReleaseCharArrayElements(env, array, p, op == 1 ? 0 : JNI_ABORT);
        if (op == 4)
            return copy == JNI_FALSE ? NULL
                                     : failure(env, "GetCharArrayElements: isCopy expected false");
        return correct ? NULL : failure(env, "GetCharArrayElements: initial data mismatch");
    }
    if (op == 2) {
        jchar buf[3] = {0x9876, 0x9876, 0x9876};
        /* Jni.cpp:2410: copies only len typed elements starting at start. */
        (*env)->GetCharArrayRegion(env, array, 1, 1, buf + 1);
        return !(*env)->ExceptionCheck(env) && buf[0] == 0x9876 && buf[1] == 0xffff &&
                       buf[2] == 0x9876
                   ? NULL
                   : failure(env, "GetCharArrayRegion: region/guard mismatch");
    }
    if (op == 3) {
        jchar value = 0xabcd;
        /* Jni.cpp:2427: copies into the array; Java checks unchanged guards. */
        (*env)->SetCharArrayRegion(env, array, 1, 1, &value);
        return (*env)->ExceptionCheck(env)
                   ? failure(env, "SetCharArrayRegion: unexpected exception")
                   : NULL;
    }
    /* Jni.cpp:2388-2394,2407-2408,2424-2425: all three invalid regions throw AIOOBE. */
    const jsize starts[] = {-1, 0, 2}, lengths[] = {1, -1, 2};
    jchar buf[4] = {0};
    for (int i = 0; i < 3; i++) {
        if (op == 6)
            (*env)->GetCharArrayRegion(env, array, starts[i], lengths[i], buf);
        else
            (*env)->SetCharArrayRegion(env, array, starts[i], lengths[i], buf);
        jstring error = expect_exception(env, "java/lang/ArrayIndexOutOfBoundsException");
        if (error)
            return error;
    }
    return NULL;
}
/* Jni.cpp:2355-2386: retained lease is typed; Java retains the array itself. */
static jint *lease_Int;
static jstring array_Int(JNIEnv *env, jintArray array, int op, int action) {
    if (op == 5) {
        if (action == 0) {
            lease_Int = (*env)->GetIntArrayElements(env, array, NULL);
            if (!lease_Int)
                return failure(env, "GetIntArrayElements: NULL lease");
            /* Jni.cpp:2361-2366: direct data is visible to Java before Release. */
            lease_Int[0] = (jint)0x87654321;
            return NULL;
        }
        if (!lease_Int)
            return failure(env, "missing array lease");
        if (action == 2) {
            /* Jni.cpp:2381: JNI_COMMIT keeps the pin/lease; write through it again. */
            (*env)->ReleaseIntArrayElements(env, array, lease_Int, JNI_COMMIT);
            lease_Int[0] = 0x76543210;
            return NULL;
        }
        /* Jni.cpp:2381-2384: JNI_ABORT only unpins; no undo of direct writes. */
        (*env)->ReleaseIntArrayElements(env, array, lease_Int, action == 1 ? JNI_ABORT : 0);
        lease_Int = NULL;
        return NULL;
    }
    if (op == 0 || op == 1 || op == 4) {
        jboolean copy = JNI_TRUE;
        jint *p = (*env)->GetIntArrayElements(env, array, &copy);
        if (!p)
            return failure(env, "GetIntArrayElements: NULL data");
        /* Jni.cpp:2362: typed array contents; 2376-2386: release only unpins. */
        int correct = p[1] == (jint)0x89abcdef;
        if (op == 1)
            p[0] = (jint)0x87654321;
        (*env)->ReleaseIntArrayElements(env, array, p, op == 1 ? 0 : JNI_ABORT);
        if (op == 4)
            return copy == JNI_FALSE ? NULL
                                     : failure(env, "GetIntArrayElements: isCopy expected false");
        return correct ? NULL : failure(env, "GetIntArrayElements: initial data mismatch");
    }
    if (op == 2) {
        jint buf[3] = {0x76543210, 0x76543210, 0x76543210};
        /* Jni.cpp:2410: copies only len typed elements starting at start. */
        (*env)->GetIntArrayRegion(env, array, 1, 1, buf + 1);
        return !(*env)->ExceptionCheck(env) && buf[0] == 0x76543210 && buf[1] == (jint)0x89abcdef &&
                       buf[2] == 0x76543210
                   ? NULL
                   : failure(env, "GetIntArrayRegion: region/guard mismatch");
    }
    if (op == 3) {
        jint value = (jint)0x87654321;
        /* Jni.cpp:2427: copies into the array; Java checks unchanged guards. */
        (*env)->SetIntArrayRegion(env, array, 1, 1, &value);
        return (*env)->ExceptionCheck(env) ? failure(env, "SetIntArrayRegion: unexpected exception")
                                           : NULL;
    }
    /* Jni.cpp:2388-2394,2407-2408,2424-2425: all three invalid regions throw AIOOBE. */
    const jsize starts[] = {-1, 0, 2}, lengths[] = {1, -1, 2};
    jint buf[4] = {0};
    for (int i = 0; i < 3; i++) {
        if (op == 6)
            (*env)->GetIntArrayRegion(env, array, starts[i], lengths[i], buf);
        else
            (*env)->SetIntArrayRegion(env, array, starts[i], lengths[i], buf);
        jstring error = expect_exception(env, "java/lang/ArrayIndexOutOfBoundsException");
        if (error)
            return error;
    }
    return NULL;
}
/* Jni.cpp:2355-2386: retained lease is typed; Java retains the array itself. */
static jlong *lease_Long;
static jstring array_Long(JNIEnv *env, jlongArray array, int op, int action) {
    if (op == 5) {
        if (action == 0) {
            lease_Long = (*env)->GetLongArrayElements(env, array, NULL);
            if (!lease_Long)
                return failure(env, "GetLongArrayElements: NULL lease");
            /* Jni.cpp:2361-2366: direct data is visible to Java before Release. */
            lease_Long[0] = (jlong)0x823456789abcdef1ULL;
            return NULL;
        }
        if (!lease_Long)
            return failure(env, "missing array lease");
        if (action == 2) {
            /* Jni.cpp:2381: JNI_COMMIT keeps the pin/lease; write through it again. */
            (*env)->ReleaseLongArrayElements(env, array, lease_Long, JNI_COMMIT);
            lease_Long[0] = (jlong)0x73456789abcdef12ULL;
            return NULL;
        }
        /* Jni.cpp:2381-2384: JNI_ABORT only unpins; no undo of direct writes. */
        (*env)->ReleaseLongArrayElements(env, array, lease_Long, action == 1 ? JNI_ABORT : 0);
        lease_Long = NULL;
        return NULL;
    }
    if (op == 0 || op == 1 || op == 4) {
        jboolean copy = JNI_TRUE;
        jlong *p = (*env)->GetLongArrayElements(env, array, &copy);
        if (!p)
            return failure(env, "GetLongArrayElements: NULL data");
        /* Jni.cpp:2362: typed array contents; 2376-2386: release only unpins. */
        int correct = p[1] == (jlong)0x8123456789abcdefULL;
        if (op == 1)
            p[0] = (jlong)0x823456789abcdef1ULL;
        (*env)->ReleaseLongArrayElements(env, array, p, op == 1 ? 0 : JNI_ABORT);
        if (op == 4)
            return copy == JNI_FALSE ? NULL
                                     : failure(env, "GetLongArrayElements: isCopy expected false");
        return correct ? NULL : failure(env, "GetLongArrayElements: initial data mismatch");
    }
    if (op == 2) {
        jlong buf[3] = {(jlong)0x73456789abcdef12ULL, (jlong)0x73456789abcdef12ULL,
                        (jlong)0x73456789abcdef12ULL};
        /* Jni.cpp:2410: copies only len typed elements starting at start. */
        (*env)->GetLongArrayRegion(env, array, 1, 1, buf + 1);
        return !(*env)->ExceptionCheck(env) && buf[0] == (jlong)0x73456789abcdef12ULL &&
                       buf[1] == (jlong)0x8123456789abcdefULL &&
                       buf[2] == (jlong)0x73456789abcdef12ULL
                   ? NULL
                   : failure(env, "GetLongArrayRegion: region/guard mismatch");
    }
    if (op == 3) {
        jlong value = (jlong)0x823456789abcdef1ULL;
        /* Jni.cpp:2427: copies into the array; Java checks unchanged guards. */
        (*env)->SetLongArrayRegion(env, array, 1, 1, &value);
        return (*env)->ExceptionCheck(env)
                   ? failure(env, "SetLongArrayRegion: unexpected exception")
                   : NULL;
    }
    /* Jni.cpp:2388-2394,2407-2408,2424-2425: all three invalid regions throw AIOOBE. */
    const jsize starts[] = {-1, 0, 2}, lengths[] = {1, -1, 2};
    jlong buf[4] = {0};
    for (int i = 0; i < 3; i++) {
        if (op == 6)
            (*env)->GetLongArrayRegion(env, array, starts[i], lengths[i], buf);
        else
            (*env)->SetLongArrayRegion(env, array, starts[i], lengths[i], buf);
        jstring error = expect_exception(env, "java/lang/ArrayIndexOutOfBoundsException");
        if (error)
            return error;
    }
    return NULL;
}
/* Jni.cpp:2355-2386: retained lease is typed; Java retains the array itself. */
static jfloat *lease_Float;
static jstring array_Float(JNIEnv *env, jfloatArray array, int op, int action) {
    if (op == 5) {
        if (action == 0) {
            lease_Float = (*env)->GetFloatArrayElements(env, array, NULL);
            if (!lease_Float)
                return failure(env, "GetFloatArrayElements: NULL lease");
            /* Jni.cpp:2361-2366: direct data is visible to Java before Release. */
            lease_Float[0] = 13.25f;
            return NULL;
        }
        if (!lease_Float)
            return failure(env, "missing array lease");
        if (action == 2) {
            /* Jni.cpp:2381: JNI_COMMIT keeps the pin/lease; write through it again. */
            (*env)->ReleaseFloatArrayElements(env, array, lease_Float, JNI_COMMIT);
            lease_Float[0] = -27.5f;
            return NULL;
        }
        /* Jni.cpp:2381-2384: JNI_ABORT only unpins; no undo of direct writes. */
        (*env)->ReleaseFloatArrayElements(env, array, lease_Float, action == 1 ? JNI_ABORT : 0);
        lease_Float = NULL;
        return NULL;
    }
    if (op == 0 || op == 1 || op == 4) {
        jboolean copy = JNI_TRUE;
        jfloat *p = (*env)->GetFloatArrayElements(env, array, &copy);
        if (!p)
            return failure(env, "GetFloatArrayElements: NULL data");
        /* Jni.cpp:2362: typed array contents; 2376-2386: release only unpins. */
        int correct = p[1] == -1.25f;
        if (op == 1)
            p[0] = 13.25f;
        (*env)->ReleaseFloatArrayElements(env, array, p, op == 1 ? 0 : JNI_ABORT);
        if (op == 4)
            return copy == JNI_FALSE ? NULL
                                     : failure(env, "GetFloatArrayElements: isCopy expected false");
        return correct ? NULL : failure(env, "GetFloatArrayElements: initial data mismatch");
    }
    if (op == 2) {
        jfloat buf[3] = {-27.5f, -27.5f, -27.5f};
        /* Jni.cpp:2410: copies only len typed elements starting at start. */
        (*env)->GetFloatArrayRegion(env, array, 1, 1, buf + 1);
        return !(*env)->ExceptionCheck(env) && buf[0] == -27.5f && buf[1] == -1.25f &&
                       buf[2] == -27.5f
                   ? NULL
                   : failure(env, "GetFloatArrayRegion: region/guard mismatch");
    }
    if (op == 3) {
        jfloat value = 13.25f;
        /* Jni.cpp:2427: copies into the array; Java checks unchanged guards. */
        (*env)->SetFloatArrayRegion(env, array, 1, 1, &value);
        return (*env)->ExceptionCheck(env)
                   ? failure(env, "SetFloatArrayRegion: unexpected exception")
                   : NULL;
    }
    /* Jni.cpp:2388-2394,2407-2408,2424-2425: all three invalid regions throw AIOOBE. */
    const jsize starts[] = {-1, 0, 2}, lengths[] = {1, -1, 2};
    jfloat buf[4] = {0};
    for (int i = 0; i < 3; i++) {
        if (op == 6)
            (*env)->GetFloatArrayRegion(env, array, starts[i], lengths[i], buf);
        else
            (*env)->SetFloatArrayRegion(env, array, starts[i], lengths[i], buf);
        jstring error = expect_exception(env, "java/lang/ArrayIndexOutOfBoundsException");
        if (error)
            return error;
    }
    return NULL;
}
/* Jni.cpp:2355-2386: retained lease is typed; Java retains the array itself. */
static jdouble *lease_Double;
static jstring array_Double(JNIEnv *env, jdoubleArray array, int op, int action) {
    if (op == 5) {
        if (action == 0) {
            lease_Double = (*env)->GetDoubleArrayElements(env, array, NULL);
            if (!lease_Double)
                return failure(env, "GetDoubleArrayElements: NULL lease");
            /* Jni.cpp:2361-2366: direct data is visible to Java before Release. */
            lease_Double[0] = 3.125e110;
            return NULL;
        }
        if (!lease_Double)
            return failure(env, "missing array lease");
        if (action == 2) {
            /* Jni.cpp:2381: JNI_COMMIT keeps the pin/lease; write through it again. */
            (*env)->ReleaseDoubleArrayElements(env, array, lease_Double, JNI_COMMIT);
            lease_Double[0] = -6.25e120;
            return NULL;
        }
        /* Jni.cpp:2381-2384: JNI_ABORT only unpins; no undo of direct writes. */
        (*env)->ReleaseDoubleArrayElements(env, array, lease_Double, action == 1 ? JNI_ABORT : 0);
        lease_Double = NULL;
        return NULL;
    }
    if (op == 0 || op == 1 || op == 4) {
        jboolean copy = JNI_TRUE;
        jdouble *p = (*env)->GetDoubleArrayElements(env, array, &copy);
        if (!p)
            return failure(env, "GetDoubleArrayElements: NULL data");
        /* Jni.cpp:2362: typed array contents; 2376-2386: release only unpins. */
        int correct = p[1] == -7.503e100;
        if (op == 1)
            p[0] = 3.125e110;
        (*env)->ReleaseDoubleArrayElements(env, array, p, op == 1 ? 0 : JNI_ABORT);
        if (op == 4)
            return copy == JNI_FALSE
                       ? NULL
                       : failure(env, "GetDoubleArrayElements: isCopy expected false");
        return correct ? NULL : failure(env, "GetDoubleArrayElements: initial data mismatch");
    }
    if (op == 2) {
        jdouble buf[3] = {-6.25e120, -6.25e120, -6.25e120};
        /* Jni.cpp:2410: copies only len typed elements starting at start. */
        (*env)->GetDoubleArrayRegion(env, array, 1, 1, buf + 1);
        return !(*env)->ExceptionCheck(env) && buf[0] == -6.25e120 && buf[1] == -7.503e100 &&
                       buf[2] == -6.25e120
                   ? NULL
                   : failure(env, "GetDoubleArrayRegion: region/guard mismatch");
    }
    if (op == 3) {
        jdouble value = 3.125e110;
        /* Jni.cpp:2427: copies into the array; Java checks unchanged guards. */
        (*env)->SetDoubleArrayRegion(env, array, 1, 1, &value);
        return (*env)->ExceptionCheck(env)
                   ? failure(env, "SetDoubleArrayRegion: unexpected exception")
                   : NULL;
    }
    /* Jni.cpp:2388-2394,2407-2408,2424-2425: all three invalid regions throw AIOOBE. */
    const jsize starts[] = {-1, 0, 2}, lengths[] = {1, -1, 2};
    jdouble buf[4] = {0};
    for (int i = 0; i < 3; i++) {
        if (op == 6)
            (*env)->GetDoubleArrayRegion(env, array, starts[i], lengths[i], buf);
        else
            (*env)->SetDoubleArrayRegion(env, array, starts[i], lengths[i], buf);
        jstring error = expect_exception(env, "java/lang/ArrayIndexOutOfBoundsException");
        if (error)
            return error;
    }
    return NULL;
}
static jstring primitive_critical(JNIEnv *env, jintArray array, int mode) {
    jboolean copy = JNI_TRUE;
    /* Jni.cpp:2598-2606: direct pointer, isCopy false. */
    jint *p = (*env)->GetPrimitiveArrayCritical(env, array, &copy);
    if (!p)
        return failure(env, "NULL primitive critical");
    int initial = p[1] == -23;
    p[0] = 914;
    if (mode == 2) {
        /* Jni.cpp:2612-2617: COMMIT retains pin until final release. */
        (*env)->ReleasePrimitiveArrayCritical(env, array, p, JNI_COMMIT);
        p[0] = 4321;
    }
    (*env)->ReleasePrimitiveArrayCritical(env, array, p, mode == 1 ? JNI_ABORT : 0);
    if (mode == 3 && copy != JNI_FALSE)
        return failure(env, "GetPrimitiveArrayCritical: isCopy expected false");
    return initial ? NULL : failure(env, "critical array content mismatch");
}
static jstring object_element(JNIEnv *env, jobjectArray array, int mode) {
    /* Jni.cpp:2285-2294: selected value is returned as a local reference. */
    jobject value = (*env)->GetObjectArrayElement(env, array, mode == 2 ? 3 : mode);
    if (mode == 2)
        return expect_exception(env, "java/lang/ArrayIndexOutOfBoundsException");
    if ((*env)->ExceptionCheck(env))
        return failure(env, "object element unexpected exception");
    return mode == 1 ? (value == NULL ? NULL : failure(env, "null element expected"))
                     : (text_equal(env, (jstring)value, "object-marker")
                            ? NULL
                            : failure(env, "wrong object element"));
}
JNIEXPORT jobject JNICALL Java_probe_ArraysCases_create(JNIEnv *env, jclass unused, jint type) {
    /* Jni.cpp:2326-2344: allocate zero-initialized primitive array. */
    switch (type) {
    case 0:
        return (*env)->NewBooleanArray(env, 3);
    case 1:
        return (*env)->NewByteArray(env, 3);
    case 2:
        return (*env)->NewShortArray(env, 3);
    case 3:
        return (*env)->NewCharArray(env, 3);
    case 4:
        return (*env)->NewIntArray(env, 3);
    case 5:
        return (*env)->NewLongArray(env, 3);
    case 6:
        return (*env)->NewFloatArray(env, 3);
    case 7:
        return (*env)->NewDoubleArray(env, 3);
    default:
        return NULL;
    }
}
JNIEXPORT jstring JNICALL Java_probe_ArraysCases_probe(JNIEnv *env, jclass unused, jint test,
                                                       jobject array, jint action) {
    switch (test) {
    case 0:
        return failure(env, "create uses separate entry");
    case 1:
        return array_Boolean(env, (jbooleanArray)array, 0, action);
    case 2:
        return array_Boolean(env, (jbooleanArray)array, 1, action);
    case 3:
        return array_Boolean(env, (jbooleanArray)array, 2, action);
    case 4:
        return array_Boolean(env, (jbooleanArray)array, 3, action);
    case 5:
        return array_Boolean(env, (jbooleanArray)array, 4, action);
    case 6:
        return array_Boolean(env, (jbooleanArray)array, 5, action);
    case 7:
        return array_Boolean(env, (jbooleanArray)array, 5, action);
    case 8:
        return array_Boolean(env, (jbooleanArray)array, 5, action);
    case 9:
        return array_Boolean(env, (jbooleanArray)array, 6, action);
    case 10:
        return array_Boolean(env, (jbooleanArray)array, 7, action);
    case 11:
        return failure(env, "create uses separate entry");
    case 12:
        return array_Byte(env, (jbyteArray)array, 0, action);
    case 13:
        return array_Byte(env, (jbyteArray)array, 1, action);
    case 14:
        return array_Byte(env, (jbyteArray)array, 2, action);
    case 15:
        return array_Byte(env, (jbyteArray)array, 3, action);
    case 16:
        return array_Byte(env, (jbyteArray)array, 4, action);
    case 17:
        return array_Byte(env, (jbyteArray)array, 5, action);
    case 18:
        return array_Byte(env, (jbyteArray)array, 5, action);
    case 19:
        return array_Byte(env, (jbyteArray)array, 5, action);
    case 20:
        return array_Byte(env, (jbyteArray)array, 6, action);
    case 21:
        return array_Byte(env, (jbyteArray)array, 7, action);
    case 22:
        return failure(env, "create uses separate entry");
    case 23:
        return array_Short(env, (jshortArray)array, 0, action);
    case 24:
        return array_Short(env, (jshortArray)array, 1, action);
    case 25:
        return array_Short(env, (jshortArray)array, 2, action);
    case 26:
        return array_Short(env, (jshortArray)array, 3, action);
    case 27:
        return array_Short(env, (jshortArray)array, 4, action);
    case 28:
        return array_Short(env, (jshortArray)array, 5, action);
    case 29:
        return array_Short(env, (jshortArray)array, 5, action);
    case 30:
        return array_Short(env, (jshortArray)array, 5, action);
    case 31:
        return array_Short(env, (jshortArray)array, 6, action);
    case 32:
        return array_Short(env, (jshortArray)array, 7, action);
    case 33:
        return failure(env, "create uses separate entry");
    case 34:
        return array_Char(env, (jcharArray)array, 0, action);
    case 35:
        return array_Char(env, (jcharArray)array, 1, action);
    case 36:
        return array_Char(env, (jcharArray)array, 2, action);
    case 37:
        return array_Char(env, (jcharArray)array, 3, action);
    case 38:
        return array_Char(env, (jcharArray)array, 4, action);
    case 39:
        return array_Char(env, (jcharArray)array, 5, action);
    case 40:
        return array_Char(env, (jcharArray)array, 5, action);
    case 41:
        return array_Char(env, (jcharArray)array, 5, action);
    case 42:
        return array_Char(env, (jcharArray)array, 6, action);
    case 43:
        return array_Char(env, (jcharArray)array, 7, action);
    case 44:
        return failure(env, "create uses separate entry");
    case 45:
        return array_Int(env, (jintArray)array, 0, action);
    case 46:
        return array_Int(env, (jintArray)array, 1, action);
    case 47:
        return array_Int(env, (jintArray)array, 2, action);
    case 48:
        return array_Int(env, (jintArray)array, 3, action);
    case 49:
        return array_Int(env, (jintArray)array, 4, action);
    case 50:
        return array_Int(env, (jintArray)array, 5, action);
    case 51:
        return array_Int(env, (jintArray)array, 5, action);
    case 52:
        return array_Int(env, (jintArray)array, 5, action);
    case 53:
        return array_Int(env, (jintArray)array, 6, action);
    case 54:
        return array_Int(env, (jintArray)array, 7, action);
    case 55:
        return failure(env, "create uses separate entry");
    case 56:
        return array_Long(env, (jlongArray)array, 0, action);
    case 57:
        return array_Long(env, (jlongArray)array, 1, action);
    case 58:
        return array_Long(env, (jlongArray)array, 2, action);
    case 59:
        return array_Long(env, (jlongArray)array, 3, action);
    case 60:
        return array_Long(env, (jlongArray)array, 4, action);
    case 61:
        return array_Long(env, (jlongArray)array, 5, action);
    case 62:
        return array_Long(env, (jlongArray)array, 5, action);
    case 63:
        return array_Long(env, (jlongArray)array, 5, action);
    case 64:
        return array_Long(env, (jlongArray)array, 6, action);
    case 65:
        return array_Long(env, (jlongArray)array, 7, action);
    case 66:
        return failure(env, "create uses separate entry");
    case 67:
        return array_Float(env, (jfloatArray)array, 0, action);
    case 68:
        return array_Float(env, (jfloatArray)array, 1, action);
    case 69:
        return array_Float(env, (jfloatArray)array, 2, action);
    case 70:
        return array_Float(env, (jfloatArray)array, 3, action);
    case 71:
        return array_Float(env, (jfloatArray)array, 4, action);
    case 72:
        return array_Float(env, (jfloatArray)array, 5, action);
    case 73:
        return array_Float(env, (jfloatArray)array, 5, action);
    case 74:
        return array_Float(env, (jfloatArray)array, 5, action);
    case 75:
        return array_Float(env, (jfloatArray)array, 6, action);
    case 76:
        return array_Float(env, (jfloatArray)array, 7, action);
    case 77:
        return failure(env, "create uses separate entry");
    case 78:
        return array_Double(env, (jdoubleArray)array, 0, action);
    case 79:
        return array_Double(env, (jdoubleArray)array, 1, action);
    case 80:
        return array_Double(env, (jdoubleArray)array, 2, action);
    case 81:
        return array_Double(env, (jdoubleArray)array, 3, action);
    case 82:
        return array_Double(env, (jdoubleArray)array, 4, action);
    case 83:
        return array_Double(env, (jdoubleArray)array, 5, action);
    case 84:
        return array_Double(env, (jdoubleArray)array, 5, action);
    case 85:
        return array_Double(env, (jdoubleArray)array, 5, action);
    case 86:
        return array_Double(env, (jdoubleArray)array, 6, action);
    case 87:
        return array_Double(env, (jdoubleArray)array, 7, action);
    case 88:
        return primitive_critical(env, (jintArray)array, 0);
    case 89:
        return primitive_critical(env, (jintArray)array, 0);
    case 90:
        return primitive_critical(env, (jintArray)array, 3);
    case 91:
        return primitive_critical(env, (jintArray)array, 1);
    case 92:
        return primitive_critical(env, (jintArray)array, 2);
    case 93:
        return object_element(env, (jobjectArray)array, 0);
    case 94:
        return object_element(env, (jobjectArray)array, 1);
    case 95:
        return object_element(env, (jobjectArray)array, 2);
    default:
        return failure(env, "unknown array case");
    }
}
