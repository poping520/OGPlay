/* SPDX-License-Identifier: Apache-2.0
 * Test points rewritten from CTS tests/tests/jni (libjnitest and Java peers),
 * Sources: libjnitest/android_jni_cts_{Static,Instance}Nonce.c;
 * src/android/jni/cts/Jni{Static,Instance}Test.java. Additional ABI shapes
 * from ART test/MyClassNatives, d5e4ac0; synchronized dispatch verified at
 * dalvik/vm/Jni.cpp:1146.
 * android-4.4.4_r2.0.1, commit b8e2daba969c1d0d9eb3feec9559e12db378681b.
 * Copyright (C) 2009 The Android Open Source Project
 * Licensed under Apache-2.0: https://www.apache.org/licenses/LICENSE-2.0
 */

#include "probe.h"
static void BoundaryInstance_nop(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
}
static jboolean BoundaryInstance_returnBoolean(JNIEnv *env, jobject self) { return JNI_TRUE; }
static jboolean BoundaryInstance_takeBoolean(JNIEnv *env, jobject self, jboolean v) {
    return v == JNI_TRUE;
}
static jbyte BoundaryInstance_returnByte(JNIEnv *env, jobject self) { return 123; }
static jboolean BoundaryInstance_takeByte(JNIEnv *env, jobject self, jbyte v) { return v == -99; }
static jshort BoundaryInstance_returnShort(JNIEnv *env, jobject self) { return -12345; }
static jboolean BoundaryInstance_takeShort(JNIEnv *env, jobject self, jshort v) {
    return v == 19991;
}
static jchar BoundaryInstance_returnChar(JNIEnv *env, jobject self) { return 34567; }
static jboolean BoundaryInstance_takeChar(JNIEnv *env, jobject self, jchar v) { return v == 999; }
static jint BoundaryInstance_returnInt(JNIEnv *env, jobject self) { return 12345678; }
static jboolean BoundaryInstance_takeInt(JNIEnv *env, jobject self, jint v) {
    return v == -999888777;
}
static jlong BoundaryInstance_returnLong(JNIEnv *env, jobject self) {
    return -1098765432109876543LL;
}
static jboolean BoundaryInstance_takeLong(JNIEnv *env, jobject self, jlong v) {
    return v == 999888777666555444LL;
}
static jfloat BoundaryInstance_returnFloat(JNIEnv *env, jobject self) { return -98765.4321F; }
static jboolean BoundaryInstance_takeFloat(JNIEnv *env, jobject self, jfloat v) {
    return v == -9988.7766F;
}
static jdouble BoundaryInstance_returnDouble(JNIEnv *env, jobject self) { return 12345678.9; }
static jboolean BoundaryInstance_takeDouble(JNIEnv *env, jobject self, jdouble v) {
    return v == 999888777.666555;
}
static jobject BoundaryInstance_returnNull(JNIEnv *env, jobject self) { return NULL; }
static jboolean BoundaryInstance_takeNull(JNIEnv *env, jobject self, jobject v) {
    return v == NULL;
}
static jstring BoundaryInstance_returnString(JNIEnv *env, jobject self) {
    return (*env)->NewStringUTF(env, "blort");
}
static jboolean BoundaryInstance_takeString(JNIEnv *env, jobject self, jstring v) {
    return text_equal(env, v, "fuzzbot");
}
static jshortArray BoundaryInstance_returnShortArray(JNIEnv *env, jobject self) {
    jshort v[] = {10, 20, 30};
    jshortArray a = (*env)->NewShortArray(env, 3);
    (*env)->SetShortArrayRegion(env, a, 0, 3, v);
    return a;
}
static jobjectArray BoundaryInstance_returnStringArray(JNIEnv *env, jobject self) {
    jclass k = (*env)->FindClass(env, "java/lang/String");
    jobjectArray a = (*env)->NewObjectArray(env, 100, k, NULL);
    int ix[] = {0, 50, 99};
    const char *s[] = {"blort", "zorch", "fizmo"};
    for (int i = 0; i < 3; i++)
        (*env)->SetObjectArrayElement(env, a, ix[i], (*env)->NewStringUTF(env, s[i]));
    return a;
}
static jclass BoundaryInstance_returnThisClass(JNIEnv *env, jobject self) {
    return (*env)->GetObjectClass(env, self);
}
static jboolean BoundaryInstance_takeThisClass(JNIEnv *env, jobject self, jclass v) {
    return (*env)->IsSameObject(env, (*env)->GetObjectClass(env, self), v);
}
static jobject BoundaryInstance_returnInstance(JNIEnv *env, jobject self) {
    jclass k = (*env)->GetObjectClass(env, self);
    jmethodID m = (*env)->GetMethodID(env, k, "<init>", "()V");
    return (*env)->NewObjectA(env, k, m, NULL);
}
static jboolean BoundaryInstance_takeIntLong(JNIEnv *env, jobject self, jint a, jlong b) {
    return a == 914 && b == 9140914091409140914LL;
}
static jboolean BoundaryInstance_takeLongInt(JNIEnv *env, jobject self, jlong a, jint b) {
    return a == -4321LL && b == 12341234;
}
static jboolean BoundaryInstance_takeOneOfEach(JNIEnv *env, jobject self, jboolean a, jbyte b,
                                               jshort c, jchar d, jint e, jlong f, jstring g,
                                               jfloat h, jdouble i, jintArray j) {
    jint v[2];
    if ((*env)->GetArrayLength(env, j) != 2)
        return JNI_FALSE;
    (*env)->GetIntArrayRegion(env, j, 0, 2, v);
    return !a && b == 1 && c == 2 && d == 3 && e == 4 && f == 5 && text_equal(env, g, "six") &&
           h == 7 && i == 8 && v[0] == 9 && v[1] == 10;
}
static jboolean BoundaryInstance_takeCoolHandLuke(
    JNIEnv *env, jobject self, jint a1, jint a2, jint a3, jint a4, jint a5, jint a6, jint a7,
    jint a8, jint a9, jint a10, jint a11, jint a12, jint a13, jint a14, jint a15, jint a16,
    jint a17, jint a18, jint a19, jint a20, jint a21, jint a22, jint a23, jint a24, jint a25,
    jint a26, jint a27, jint a28, jint a29, jint a30, jint a31, jint a32, jint a33, jint a34,
    jint a35, jint a36, jint a37, jint a38, jint a39, jint a40, jint a41, jint a42, jint a43,
    jint a44, jint a45, jint a46, jint a47, jint a48, jint a49, jint a50) {
    return a1 == 1 && a2 == 2 && a3 == 3 && a4 == 4 && a5 == 5 && a6 == 6 && a7 == 7 && a8 == 8 &&
           a9 == 9 && a10 == 10 && a11 == 11 && a12 == 12 && a13 == 13 && a14 == 14 && a15 == 15 &&
           a16 == 16 && a17 == 17 && a18 == 18 && a19 == 19 && a20 == 20 && a21 == 21 &&
           a22 == 22 && a23 == 23 && a24 == 24 && a25 == 25 && a26 == 26 && a27 == 27 &&
           a28 == 28 && a29 == 29 && a30 == 30 && a31 == 31 && a32 == 32 && a33 == 33 &&
           a34 == 34 && a35 == 35 && a36 == 36 && a37 == 37 && a38 == 38 && a39 == 39 &&
           a40 == 40 && a41 == 41 && a42 == 42 && a43 == 43 && a44 == 44 && a45 == 45 &&
           a46 == 46 && a47 == 47 && a48 == 48 && a49 == 49 && a50 == 50;
}
static jboolean BoundaryInstance_holdsLock(JNIEnv *env, jobject self) {
    jclass k = (*env)->FindClass(env, "java/lang/Thread");
    jmethodID m = (*env)->GetStaticMethodID(env, k, "holdsLock", "(Ljava/lang/Object;)Z");
    return (*env)->CallStaticBooleanMethod(env, k, m, self);
}
static jboolean BoundaryInstance_mixedAlignment(JNIEnv *env, jobject self, jlong a, jobject b,
                                                jlong c, jobject d) {
    return a == 0x123456789LL && c == -0x234567891LL && b != NULL &&
           (*env)->IsSameObject(env, b, d);
}
static jdouble BoundaryInstance_doublePair(JNIEnv *env, jobject self, jdouble a, jdouble b) {
    return a + b;
}
static void BoundaryInstance_throwException(JNIEnv *env, jobject self) {
    (*env)->ThrowNew(env, (*env)->FindClass(env, "java/lang/IllegalStateException"), "probe");
}
int register_BoundaryInstance(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/BoundaryInstance");
    JNINativeMethod methods[] = {
        {"nop", "()V", (void *)BoundaryInstance_nop},
        {"returnBoolean", "()Z", (void *)BoundaryInstance_returnBoolean},
        {"takeBoolean", "(Z)Z", (void *)BoundaryInstance_takeBoolean},
        {"returnByte", "()B", (void *)BoundaryInstance_returnByte},
        {"takeByte", "(B)Z", (void *)BoundaryInstance_takeByte},
        {"returnShort", "()S", (void *)BoundaryInstance_returnShort},
        {"takeShort", "(S)Z", (void *)BoundaryInstance_takeShort},
        {"returnChar", "()C", (void *)BoundaryInstance_returnChar},
        {"takeChar", "(C)Z", (void *)BoundaryInstance_takeChar},
        {"returnInt", "()I", (void *)BoundaryInstance_returnInt},
        {"takeInt", "(I)Z", (void *)BoundaryInstance_takeInt},
        {"returnLong", "()J", (void *)BoundaryInstance_returnLong},
        {"takeLong", "(J)Z", (void *)BoundaryInstance_takeLong},
        {"returnFloat", "()F", (void *)BoundaryInstance_returnFloat},
        {"takeFloat", "(F)Z", (void *)BoundaryInstance_takeFloat},
        {"returnDouble", "()D", (void *)BoundaryInstance_returnDouble},
        {"takeDouble", "(D)Z", (void *)BoundaryInstance_takeDouble},
        {"returnNull", "()Ljava/lang/Object;", (void *)BoundaryInstance_returnNull},
        {"takeNull", "(Ljava/lang/Object;)Z", (void *)BoundaryInstance_takeNull},
        {"returnString", "()Ljava/lang/String;", (void *)BoundaryInstance_returnString},
        {"takeString", "(Ljava/lang/String;)Z", (void *)BoundaryInstance_takeString},
        {"returnShortArray", "()[S", (void *)BoundaryInstance_returnShortArray},
        {"returnStringArray", "()[Ljava/lang/String;", (void *)BoundaryInstance_returnStringArray},
        {"returnThisClass", "()Ljava/lang/Class;", (void *)BoundaryInstance_returnThisClass},
        {"takeThisClass", "(Ljava/lang/Class;)Z", (void *)BoundaryInstance_takeThisClass},
        {"returnInstance", "()Lprobe/BoundaryInstance;", (void *)BoundaryInstance_returnInstance},
        {"takeIntLong", "(IJ)Z", (void *)BoundaryInstance_takeIntLong},
        {"takeLongInt", "(JI)Z", (void *)BoundaryInstance_takeLongInt},
        {"takeOneOfEach", "(ZBSCIJLjava/lang/String;FD[I)Z",
         (void *)BoundaryInstance_takeOneOfEach},
        {"takeCoolHandLuke", "(IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII)Z",
         (void *)BoundaryInstance_takeCoolHandLuke},
        {"holdsLock", "()Z", (void *)BoundaryInstance_holdsLock},
        {"mixedAlignment", "(JLjava/lang/Object;JLjava/lang/Object;)Z",
         (void *)BoundaryInstance_mixedAlignment},
        {"doublePair", "(DD)D", (void *)BoundaryInstance_doublePair},
        {"throwException", "()V", (void *)BoundaryInstance_throwException}};
    return (*env)->RegisterNatives(env, k, methods, sizeof(methods) / sizeof(methods[0]));
}