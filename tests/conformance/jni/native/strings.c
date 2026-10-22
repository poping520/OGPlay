/* SPDX-License-Identifier: Apache-2.0
 * Independent probes derived from Dalvik vm/Jni.cpp, Android 4.4.
 * Every oracle below cites the corresponding implementation, not HotSpot.
 */
#include "probe.h"
static const jchar expected[] = {0x41, 0, 0x7ff, 0x800, 0xd83d, 0xde00, 0x5a};
JNIEXPORT jstring JNICALL Java_probe_StringsCases_create(JNIEnv *env, jclass unused,
                                                         jboolean empty) {
    /* Jni.cpp:2101-2111: zero length accepts NULL; otherwise copies UTF-16. */
    return (*env)->NewString(env, empty ? NULL : expected, empty ? 0 : 7);
}
JNIEXPORT jstring JNICALL Java_probe_StringsCases_probe(JNIEnv *env, jclass unused, jint test,
                                                        jstring value) {
    switch (test) {
    /* Jni.cpp:2117-2120. */
    case 2: {
        return (*env)->GetStringLength(env, value) == 7 ? NULL
                                                        : failure(env, "UTF16 length expected 7");
    }
    /* Jni.cpp:2176-2182. */
    case 3: {
        return (*env)->GetStringUTFLength(env, value) == 15
                   ? NULL
                   : failure(env, "MUTF8 length expected 15");
    }
    /* Jni.cpp:2178-2180. */
    case 4: {
        return (*env)->GetStringUTFLength(env, NULL) == 0 && !(*env)->ExceptionCheck(env)
                   ? NULL
                   : failure(env, "NULL UTF length");
    }
    /* Jni.cpp:2130-2152. */
    case 5: {
        const jchar *p = (*env)->GetStringChars(env, value, NULL);
        if (!p)
            return failure(env, "NULL chars");
        int ok = memcmp(p, expected, sizeof(expected)) == 0;
        (*env)->ReleaseStringChars(env, value, p);
        return ok ? NULL : failure(env, "UTF16 chars mismatch");
    }
    /* Jni.cpp:2130-2152. */
    case 6: {
        const jchar *p = (*env)->GetStringChars(env, value, NULL);
        if (!p)
            return failure(env, "NULL chars");
        int ok = memcmp(p, expected, sizeof(expected)) == 0;
        (*env)->ReleaseStringChars(env, value, p);
        return ok ? NULL : failure(env, "UTF16 chars mismatch");
    }
    /* Jni.cpp:2130-2142. */
    case 7: {
        jboolean copy = JNI_TRUE;
        const jchar *p = (*env)->GetStringChars(env, value, &copy);
        if (!p)
            return failure(env, "NULL chars");
        (*env)->ReleaseStringChars(env, value, p);
        return copy == JNI_FALSE ? NULL : failure(env, "GetStringChars isCopy expected false");
    }
    /* Jni.cpp:2199-2214. */
    case 8: {
        jboolean copy = JNI_FALSE;
        const char *p = (*env)->GetStringUTFChars(env, value, &copy);
        if (!p)
            return failure(env, "NULL UTF chars");
        (*env)->ReleaseStringUTFChars(env, value, p);
        return copy == JNI_TRUE ? NULL : failure(env, "GetStringUTFChars isCopy expected true");
    }
    /* Jni.cpp:2201-2203. */
    case 9: {
        return (*env)->GetStringUTFChars(env, NULL, NULL) == NULL && !(*env)->ExceptionCheck(env)
                   ? NULL
                   : failure(env, "NULL UTF chars");
    }
    /* Jni.cpp:2564-2572. */
    case 10: {
        jchar buf[7] = {0xaaaa, 0, 0, 0, 0, 0, 0xbbbb};
        (*env)->GetStringRegion(env, value, 1, 5, buf + 1);
        return !(*env)->ExceptionCheck(env) && buf[0] == 0xaaaa && buf[6] == 0xbbbb &&
                       memcmp(buf + 1, expected + 1, 10) == 0
                   ? NULL
                   : failure(env, "UTF16 region mismatch");
    }
    /* Jni.cpp:2579-2587. */
    case 11: {
        unsigned char buf[16];
        memset(buf, 0x55, sizeof(buf));
        const unsigned char utf[] = {0xc0, 0x80, 0xdf, 0xbf, 0xe0, 0xa0, 0x80,
                                     0xed, 0xa0, 0xbd, 0xed, 0xb8, 0x80};
        (*env)->GetStringUTFRegion(env, value, 1, 5, (char *)buf + 1);
        return !(*env)->ExceptionCheck(env) && buf[0] == 0x55 && memcmp(buf + 1, utf, 13) == 0 &&
                       buf[15] == 0x55
                   ? NULL
                   : failure(env, "MUTF8 region mismatch");
    }
    /* Jni.cpp:2568-2570;2583-2585. */
    case 12: {
        jchar buf[32];
        (*env)->GetStringRegion(env, value, -1, 1, buf);
        return expect_exception(env, "java/lang/StringIndexOutOfBoundsException");
    }
    /* Jni.cpp:2568-2570;2583-2585. */
    case 13: {
        jchar buf[32];
        (*env)->GetStringRegion(env, value, 0, -1, buf);
        return expect_exception(env, "java/lang/StringIndexOutOfBoundsException");
    }
    /* Jni.cpp:2568-2570;2583-2585. */
    case 14: {
        jchar buf[32];
        (*env)->GetStringRegion(env, value, 6, 2, buf);
        return expect_exception(env, "java/lang/StringIndexOutOfBoundsException");
    }
    /* Jni.cpp:2568-2570;2583-2585. */
    case 15: {
        char buf[32];
        (*env)->GetStringUTFRegion(env, value, -1, 1, buf);
        return expect_exception(env, "java/lang/StringIndexOutOfBoundsException");
    }
    /* Jni.cpp:2568-2570;2583-2585. */
    case 16: {
        char buf[32];
        (*env)->GetStringUTFRegion(env, value, 0, -1, buf);
        return expect_exception(env, "java/lang/StringIndexOutOfBoundsException");
    }
    /* Jni.cpp:2568-2570;2583-2585. */
    case 17: {
        char buf[32];
        (*env)->GetStringUTFRegion(env, value, 6, 2, buf);
        return expect_exception(env, "java/lang/StringIndexOutOfBoundsException");
    }
    /* Jni.cpp:2623-2645. */
    case 18: {
        jboolean copy = JNI_TRUE;
        const jchar *p = (*env)->GetStringCritical(env, value, &copy);
        if (!p)
            return failure(env, "NULL critical chars");
        int ok = memcmp(p, expected, sizeof(expected)) == 0;
        (*env)->ReleaseStringCritical(env, value, p);
        (void)ok;
        return ok ? NULL : failure(env, "critical string content/isCopy");
    }
    /* Jni.cpp:2623-2645. */
    case 19: {
        jboolean copy = JNI_TRUE;
        const jchar *p = (*env)->GetStringCritical(env, value, &copy);
        if (!p)
            return failure(env, "NULL critical chars");
        int ok = memcmp(p, expected, sizeof(expected)) == 0;
        (*env)->ReleaseStringCritical(env, value, p);
        (void)ok;
        return ok ? NULL : failure(env, "critical string content/isCopy");
    }
    /* Jni.cpp:2623-2645. */
    case 20: {
        jboolean copy = JNI_TRUE;
        const jchar *p = (*env)->GetStringCritical(env, value, &copy);
        if (!p)
            return failure(env, "NULL critical chars");
        int ok = memcmp(p, expected, sizeof(expected)) == 0;
        (*env)->ReleaseStringCritical(env, value, p);
        (void)ok;
        return copy == JNI_FALSE ? NULL : failure(env, "critical string content/isCopy");
    }
    default:
        return failure(env, "unknown string case");
    }
}
