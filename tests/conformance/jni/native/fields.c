/* SPDX-License-Identifier: Apache-2.0
 * Independent probes derived from Dalvik vm/Jni.cpp, Android 4.4.
 * Every oracle below cites the corresponding implementation, not HotSpot.
 */
#include "probe.h"
#include <stdint.h>
/* Jni.cpp:1851,1889,1773,1810: storage values preserve typed bits. */
static jfloat float_value(void) {
    uint32_t b = 0x7fc12345;
    jfloat v;
    memcpy(&v, &b, 4);
    return v;
}
static jdouble double_value(void) {
    uint64_t b = 0xc123456789abcdefULL;
    jdouble v;
    memcpy(&v, &b, 8);
    return v;
}
static int float_equal(jfloat v) {
    uint32_t b;
    memcpy(&b, &v, 4);
    return b == 0x7fc12345;
}
static int double_equal(jdouble v) {
    uint64_t b;
    memcpy(&b, &v, 8);
    return b == 0xc123456789abcdefULL;
}
JNIEXPORT jstring JNICALL Java_probe_FieldsCases_probe(JNIEnv *env, jclass unused, jint test,
                                                       jobject obj, jobject marker) {
    jclass k = (*env)->GetObjectClass(env, obj);
    switch (test) {
    /* Jni.cpp:1829-1864; ID lookup 1674-1690. */
    case 0: {
        jfieldID fid = (*env)->GetFieldID(env, k, "iBoolean", "Z");
        if (!fid)
            return failure(env, "field lookup failed");
        return (*env)->GetBooleanField(env, obj, fid) == JNI_TRUE
                   ? NULL
                   : failure(env, "GetBooleanField value/bit mismatch");
    }
    /* Jni.cpp:1869-1902; ID lookup 1674-1690. */
    case 1: {
        jfieldID fid = (*env)->GetFieldID(env, k, "iBoolean", "Z");
        if (!fid)
            return failure(env, "field lookup failed");
        (*env)->SetBooleanField(env, obj, fid, JNI_TRUE);
        return (*env)->ExceptionCheck(env) ? failure(env, "SetBooleanField exception") : NULL;
    }
    /* Jni.cpp:1829-1864; ID lookup 1674-1690. */
    case 2: {
        jfieldID fid = (*env)->GetFieldID(env, k, "iByte", "B");
        if (!fid)
            return failure(env, "field lookup failed");
        return (*env)->GetByteField(env, obj, fid) == -99
                   ? NULL
                   : failure(env, "GetByteField value/bit mismatch");
    }
    /* Jni.cpp:1869-1902; ID lookup 1674-1690. */
    case 3: {
        jfieldID fid = (*env)->GetFieldID(env, k, "iByte", "B");
        if (!fid)
            return failure(env, "field lookup failed");
        (*env)->SetByteField(env, obj, fid, -99);
        return (*env)->ExceptionCheck(env) ? failure(env, "SetByteField exception") : NULL;
    }
    /* Jni.cpp:1829-1864; ID lookup 1674-1690. */
    case 4: {
        jfieldID fid = (*env)->GetFieldID(env, k, "iShort", "S");
        if (!fid)
            return failure(env, "field lookup failed");
        return (*env)->GetShortField(env, obj, fid) == -23456
                   ? NULL
                   : failure(env, "GetShortField value/bit mismatch");
    }
    /* Jni.cpp:1869-1902; ID lookup 1674-1690. */
    case 5: {
        jfieldID fid = (*env)->GetFieldID(env, k, "iShort", "S");
        if (!fid)
            return failure(env, "field lookup failed");
        (*env)->SetShortField(env, obj, fid, -23456);
        return (*env)->ExceptionCheck(env) ? failure(env, "SetShortField exception") : NULL;
    }
    /* Jni.cpp:1829-1864; ID lookup 1674-1690. */
    case 6: {
        jfieldID fid = (*env)->GetFieldID(env, k, "iChar", "C");
        if (!fid)
            return failure(env, "field lookup failed");
        return (*env)->GetCharField(env, obj, fid) == 0xffff
                   ? NULL
                   : failure(env, "GetCharField value/bit mismatch");
    }
    /* Jni.cpp:1869-1902; ID lookup 1674-1690. */
    case 7: {
        jfieldID fid = (*env)->GetFieldID(env, k, "iChar", "C");
        if (!fid)
            return failure(env, "field lookup failed");
        (*env)->SetCharField(env, obj, fid, 0xffff);
        return (*env)->ExceptionCheck(env) ? failure(env, "SetCharField exception") : NULL;
    }
    /* Jni.cpp:1829-1864; ID lookup 1674-1690. */
    case 8: {
        jfieldID fid = (*env)->GetFieldID(env, k, "iInt", "I");
        if (!fid)
            return failure(env, "field lookup failed");
        return (*env)->GetIntField(env, obj, fid) == (jint)0x89abcdef
                   ? NULL
                   : failure(env, "GetIntField value/bit mismatch");
    }
    /* Jni.cpp:1869-1902; ID lookup 1674-1690. */
    case 9: {
        jfieldID fid = (*env)->GetFieldID(env, k, "iInt", "I");
        if (!fid)
            return failure(env, "field lookup failed");
        (*env)->SetIntField(env, obj, fid, (jint)0x89abcdef);
        return (*env)->ExceptionCheck(env) ? failure(env, "SetIntField exception") : NULL;
    }
    /* Jni.cpp:1829-1864; ID lookup 1674-1690. */
    case 10: {
        jfieldID fid = (*env)->GetFieldID(env, k, "iLong", "J");
        if (!fid)
            return failure(env, "field lookup failed");
        return (*env)->GetLongField(env, obj, fid) == (jlong)0x8123456789abcdefULL
                   ? NULL
                   : failure(env, "GetLongField value/bit mismatch");
    }
    /* Jni.cpp:1869-1902; ID lookup 1674-1690. */
    case 11: {
        jfieldID fid = (*env)->GetFieldID(env, k, "iLong", "J");
        if (!fid)
            return failure(env, "field lookup failed");
        (*env)->SetLongField(env, obj, fid, (jlong)0x8123456789abcdefULL);
        return (*env)->ExceptionCheck(env) ? failure(env, "SetLongField exception") : NULL;
    }
    /* Jni.cpp:1829-1864; ID lookup 1674-1690. */
    case 12: {
        jfieldID fid = (*env)->GetFieldID(env, k, "iFloat", "F");
        if (!fid)
            return failure(env, "field lookup failed");
        return float_equal((*env)->GetFloatField(env, obj, fid))
                   ? NULL
                   : failure(env, "GetFloatField value/bit mismatch");
    }
    /* Jni.cpp:1869-1902; ID lookup 1674-1690. */
    case 13: {
        jfieldID fid = (*env)->GetFieldID(env, k, "iFloat", "F");
        if (!fid)
            return failure(env, "field lookup failed");
        (*env)->SetFloatField(env, obj, fid, float_value());
        return (*env)->ExceptionCheck(env) ? failure(env, "SetFloatField exception") : NULL;
    }
    /* Jni.cpp:1829-1864; ID lookup 1674-1690. */
    case 14: {
        jfieldID fid = (*env)->GetFieldID(env, k, "iDouble", "D");
        if (!fid)
            return failure(env, "field lookup failed");
        return double_equal((*env)->GetDoubleField(env, obj, fid))
                   ? NULL
                   : failure(env, "GetDoubleField value/bit mismatch");
    }
    /* Jni.cpp:1869-1902; ID lookup 1674-1690. */
    case 15: {
        jfieldID fid = (*env)->GetFieldID(env, k, "iDouble", "D");
        if (!fid)
            return failure(env, "field lookup failed");
        (*env)->SetDoubleField(env, obj, fid, double_value());
        return (*env)->ExceptionCheck(env) ? failure(env, "SetDoubleField exception") : NULL;
    }
    /* Jni.cpp:1829-1864; ID lookup 1674-1690. */
    case 16: {
        jfieldID fid = (*env)->GetFieldID(env, k, "iObject", "Ljava/lang/Object;");
        if (!fid)
            return failure(env, "field lookup failed");
        return (*env)->IsSameObject(env, (*env)->GetObjectField(env, obj, fid), marker)
                   ? NULL
                   : failure(env, "GetObjectField value/bit mismatch");
    }
    /* Jni.cpp:1869-1902; ID lookup 1674-1690. */
    case 17: {
        jfieldID fid = (*env)->GetFieldID(env, k, "iObject", "Ljava/lang/Object;");
        if (!fid)
            return failure(env, "field lookup failed");
        (*env)->SetObjectField(env, obj, fid, marker);
        return (*env)->ExceptionCheck(env) ? failure(env, "SetObjectField exception") : NULL;
    }
    /* Jni.cpp:1752-1786; ID lookup 1730-1745. */
    case 18: {
        jfieldID fid = (*env)->GetStaticFieldID(env, k, "sBoolean", "Z");
        if (!fid)
            return failure(env, "field lookup failed");
        return (*env)->GetStaticBooleanField(env, k, fid) == JNI_TRUE
                   ? NULL
                   : failure(env, "GetStaticBooleanField value/bit mismatch");
    }
    /* Jni.cpp:1791-1822; ID lookup 1730-1745. */
    case 19: {
        jfieldID fid = (*env)->GetStaticFieldID(env, k, "sBoolean", "Z");
        if (!fid)
            return failure(env, "field lookup failed");
        (*env)->SetStaticBooleanField(env, k, fid, JNI_TRUE);
        return (*env)->ExceptionCheck(env) ? failure(env, "SetStaticBooleanField exception") : NULL;
    }
    /* Jni.cpp:1752-1786; ID lookup 1730-1745. */
    case 20: {
        jfieldID fid = (*env)->GetStaticFieldID(env, k, "sByte", "B");
        if (!fid)
            return failure(env, "field lookup failed");
        return (*env)->GetStaticByteField(env, k, fid) == -99
                   ? NULL
                   : failure(env, "GetStaticByteField value/bit mismatch");
    }
    /* Jni.cpp:1791-1822; ID lookup 1730-1745. */
    case 21: {
        jfieldID fid = (*env)->GetStaticFieldID(env, k, "sByte", "B");
        if (!fid)
            return failure(env, "field lookup failed");
        (*env)->SetStaticByteField(env, k, fid, -99);
        return (*env)->ExceptionCheck(env) ? failure(env, "SetStaticByteField exception") : NULL;
    }
    /* Jni.cpp:1752-1786; ID lookup 1730-1745. */
    case 22: {
        jfieldID fid = (*env)->GetStaticFieldID(env, k, "sShort", "S");
        if (!fid)
            return failure(env, "field lookup failed");
        return (*env)->GetStaticShortField(env, k, fid) == -23456
                   ? NULL
                   : failure(env, "GetStaticShortField value/bit mismatch");
    }
    /* Jni.cpp:1791-1822; ID lookup 1730-1745. */
    case 23: {
        jfieldID fid = (*env)->GetStaticFieldID(env, k, "sShort", "S");
        if (!fid)
            return failure(env, "field lookup failed");
        (*env)->SetStaticShortField(env, k, fid, -23456);
        return (*env)->ExceptionCheck(env) ? failure(env, "SetStaticShortField exception") : NULL;
    }
    /* Jni.cpp:1752-1786; ID lookup 1730-1745. */
    case 24: {
        jfieldID fid = (*env)->GetStaticFieldID(env, k, "sChar", "C");
        if (!fid)
            return failure(env, "field lookup failed");
        return (*env)->GetStaticCharField(env, k, fid) == 0xffff
                   ? NULL
                   : failure(env, "GetStaticCharField value/bit mismatch");
    }
    /* Jni.cpp:1791-1822; ID lookup 1730-1745. */
    case 25: {
        jfieldID fid = (*env)->GetStaticFieldID(env, k, "sChar", "C");
        if (!fid)
            return failure(env, "field lookup failed");
        (*env)->SetStaticCharField(env, k, fid, 0xffff);
        return (*env)->ExceptionCheck(env) ? failure(env, "SetStaticCharField exception") : NULL;
    }
    /* Jni.cpp:1752-1786; ID lookup 1730-1745. */
    case 26: {
        jfieldID fid = (*env)->GetStaticFieldID(env, k, "sLong", "J");
        if (!fid)
            return failure(env, "field lookup failed");
        return (*env)->GetStaticLongField(env, k, fid) == (jlong)0x8123456789abcdefULL
                   ? NULL
                   : failure(env, "GetStaticLongField value/bit mismatch");
    }
    /* Jni.cpp:1791-1822; ID lookup 1730-1745. */
    case 27: {
        jfieldID fid = (*env)->GetStaticFieldID(env, k, "sLong", "J");
        if (!fid)
            return failure(env, "field lookup failed");
        (*env)->SetStaticLongField(env, k, fid, (jlong)0x8123456789abcdefULL);
        return (*env)->ExceptionCheck(env) ? failure(env, "SetStaticLongField exception") : NULL;
    }
    /* Jni.cpp:1752-1786; ID lookup 1730-1745. */
    case 28: {
        jfieldID fid = (*env)->GetStaticFieldID(env, k, "sFloat", "F");
        if (!fid)
            return failure(env, "field lookup failed");
        return float_equal((*env)->GetStaticFloatField(env, k, fid))
                   ? NULL
                   : failure(env, "GetStaticFloatField value/bit mismatch");
    }
    /* Jni.cpp:1791-1822; ID lookup 1730-1745. */
    case 29: {
        jfieldID fid = (*env)->GetStaticFieldID(env, k, "sFloat", "F");
        if (!fid)
            return failure(env, "field lookup failed");
        (*env)->SetStaticFloatField(env, k, fid, float_value());
        return (*env)->ExceptionCheck(env) ? failure(env, "SetStaticFloatField exception") : NULL;
    }
    /* Jni.cpp:1752-1786; ID lookup 1730-1745. */
    case 30: {
        jfieldID fid = (*env)->GetStaticFieldID(env, k, "sDouble", "D");
        if (!fid)
            return failure(env, "field lookup failed");
        return double_equal((*env)->GetStaticDoubleField(env, k, fid))
                   ? NULL
                   : failure(env, "GetStaticDoubleField value/bit mismatch");
    }
    /* Jni.cpp:1791-1822; ID lookup 1730-1745. */
    case 31: {
        jfieldID fid = (*env)->GetStaticFieldID(env, k, "sDouble", "D");
        if (!fid)
            return failure(env, "field lookup failed");
        (*env)->SetStaticDoubleField(env, k, fid, double_value());
        return (*env)->ExceptionCheck(env) ? failure(env, "SetStaticDoubleField exception") : NULL;
    }
    /* Jni.cpp:1752-1786; ID lookup 1730-1745. */
    case 32: {
        jfieldID fid = (*env)->GetStaticFieldID(env, k, "sObject", "Ljava/lang/Object;");
        if (!fid)
            return failure(env, "field lookup failed");
        return (*env)->IsSameObject(env, (*env)->GetStaticObjectField(env, k, fid), marker)
                   ? NULL
                   : failure(env, "GetStaticObjectField value/bit mismatch");
    }
    /* Jni.cpp:1791-1822; ID lookup 1730-1745. */
    case 33: {
        jfieldID fid = (*env)->GetStaticFieldID(env, k, "sObject", "Ljava/lang/Object;");
        if (!fid)
            return failure(env, "field lookup failed");
        (*env)->SetStaticObjectField(env, k, fid, marker);
        return (*env)->ExceptionCheck(env) ? failure(env, "SetStaticObjectField exception") : NULL;
    }
    case 34: { /* Jni.cpp:1684-1688. */
        jfieldID f = (*env)->GetFieldID(env, k, "iLong", "J");
        return f && !(*env)->ExceptionCheck(env) ? NULL : failure(env, "GetFieldID");
    }
    case 35: { /* Jni.cpp:1684-1688. */
        jfieldID f = (*env)->GetFieldID(env, k, "absent", "I");
        if (f)
            return failure(env, "unexpected field ID");
        return expect_exception(env, "java/lang/NoSuchFieldError");
    }
    case 36: { /* Jni.cpp:1684-1688. */
        jfieldID f = (*env)->GetFieldID(env, k, "iLong", "I");
        if (f)
            return failure(env, "unexpected field ID");
        return expect_exception(env, "java/lang/NoSuchFieldError");
    }
    default:
        return failure(env, "unknown field case");
    }
}
