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
static void BoundaryStatic_nop(JNIEnv *env, jclass self) {
    (void)env;
    (void)self;
}
static jboolean BoundaryStatic_returnBoolean(JNIEnv *env, jclass self) { return JNI_TRUE; }
static jboolean BoundaryStatic_takeBoolean(JNIEnv *env, jclass self, jboolean v) {
    return v == JNI_TRUE;
}
static jbyte BoundaryStatic_returnByte(JNIEnv *env, jclass self) { return 123; }
static jboolean BoundaryStatic_takeByte(JNIEnv *env, jclass self, jbyte v) { return v == -99; }
static jshort BoundaryStatic_returnShort(JNIEnv *env, jclass self) { return -12345; }
static jboolean BoundaryStatic_takeShort(JNIEnv *env, jclass self, jshort v) { return v == 19991; }
static jchar BoundaryStatic_returnChar(JNIEnv *env, jclass self) { return 34567; }
static jboolean BoundaryStatic_takeChar(JNIEnv *env, jclass self, jchar v) { return v == 999; }
static jint BoundaryStatic_returnInt(JNIEnv *env, jclass self) { return 12345678; }
static jboolean BoundaryStatic_takeInt(JNIEnv *env, jclass self, jint v) { return v == -999888777; }
static jlong BoundaryStatic_returnLong(JNIEnv *env, jclass self) { return -1098765432109876543LL; }
static jboolean BoundaryStatic_takeLong(JNIEnv *env, jclass self, jlong v) {
    return v == 999888777666555444LL;
}
static jfloat BoundaryStatic_returnFloat(JNIEnv *env, jclass self) { return -98765.4321F; }
static jboolean BoundaryStatic_takeFloat(JNIEnv *env, jclass self, jfloat v) {
    return v == -9988.7766F;
}
static jdouble BoundaryStatic_returnDouble(JNIEnv *env, jclass self) { return 12345678.9; }
static jboolean BoundaryStatic_takeDouble(JNIEnv *env, jclass self, jdouble v) {
    return v == 999888777.666555;
}
static jobject BoundaryStatic_returnNull(JNIEnv *env, jclass self) { return NULL; }
static jboolean BoundaryStatic_takeNull(JNIEnv *env, jclass self, jobject v) { return v == NULL; }
static jstring BoundaryStatic_returnString(JNIEnv *env, jclass self) {
    return (*env)->NewStringUTF(env, "blort");
}
static jboolean BoundaryStatic_takeString(JNIEnv *env, jclass self, jstring v) {
    return text_equal(env, v, "fuzzbot");
}
static jshortArray BoundaryStatic_returnShortArray(JNIEnv *env, jclass self) {
    jshort v[] = {10, 20, 30};
    jshortArray a = (*env)->NewShortArray(env, 3);
    (*env)->SetShortArrayRegion(env, a, 0, 3, v);
    return a;
}
static jobjectArray BoundaryStatic_returnStringArray(JNIEnv *env, jclass self) {
    jclass k = (*env)->FindClass(env, "java/lang/String");
    jobjectArray a = (*env)->NewObjectArray(env, 100, k, NULL);
    int ix[] = {0, 50, 99};
    const char *s[] = {"blort", "zorch", "fizmo"};
    for (int i = 0; i < 3; i++)
        (*env)->SetObjectArrayElement(env, a, ix[i], (*env)->NewStringUTF(env, s[i]));
    return a;
}
static jclass BoundaryStatic_returnThisClass(JNIEnv *env, jclass self) { return self; }
static jboolean BoundaryStatic_takeThisClass(JNIEnv *env, jclass self, jclass v) {
    return (*env)->IsSameObject(env, self, v);
}
static jobject BoundaryStatic_returnInstance(JNIEnv *env, jclass self) {
    jclass k = self;
    jmethodID m = (*env)->GetMethodID(env, k, "<init>", "()V");
    return (*env)->NewObjectA(env, k, m, NULL);
}
static jboolean BoundaryStatic_takeIntLong(JNIEnv *env, jclass self, jint a, jlong b) {
    return a == 914 && b == 9140914091409140914LL;
}
static jboolean BoundaryStatic_takeLongInt(JNIEnv *env, jclass self, jlong a, jint b) {
    return a == -4321LL && b == 12341234;
}
static jboolean BoundaryStatic_takeOneOfEach(JNIEnv *env, jclass self, jboolean a, jbyte b,
                                             jshort c, jchar d, jint e, jlong f, jstring g,
                                             jfloat h, jdouble i, jintArray j) {
    jint v[2];
    if ((*env)->GetArrayLength(env, j) != 2)
        return JNI_FALSE;
    (*env)->GetIntArrayRegion(env, j, 0, 2, v);
    return !a && b == 1 && c == 2 && d == 3 && e == 4 && f == 5 && text_equal(env, g, "six") &&
           h == 7 && i == 8 && v[0] == 9 && v[1] == 10;
}
static jboolean BoundaryStatic_takeCoolHandLuke(
    JNIEnv *env, jclass self, jint a1, jint a2, jint a3, jint a4, jint a5, jint a6, jint a7,
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
static jboolean BoundaryStatic_holdsLock(JNIEnv *env, jclass self) {
    jclass k = (*env)->FindClass(env, "java/lang/Thread");
    jmethodID m = (*env)->GetStaticMethodID(env, k, "holdsLock", "(Ljava/lang/Object;)Z");
    return (*env)->CallStaticBooleanMethod(env, k, m, self);
}
static jboolean BoundaryStatic_mixedAlignment(JNIEnv *env, jclass self, jlong a, jobject b, jlong c,
                                              jobject d) {
    return a == 0x123456789LL && c == -0x234567891LL && b != NULL &&
           (*env)->IsSameObject(env, b, d);
}
static jdouble BoundaryStatic_doublePair(JNIEnv *env, jclass self, jdouble a, jdouble b) {
    return a + b;
}
static void BoundaryStatic_throwException(JNIEnv *env, jclass self) {
    (*env)->ThrowNew(env, (*env)->FindClass(env, "java/lang/IllegalStateException"), "probe");
}
int register_BoundaryStatic(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/BoundaryStatic");
    JNINativeMethod methods[] = {
        {"nop", "()V", (void *)BoundaryStatic_nop},
        {"returnBoolean", "()Z", (void *)BoundaryStatic_returnBoolean},
        {"takeBoolean", "(Z)Z", (void *)BoundaryStatic_takeBoolean},
        {"returnByte", "()B", (void *)BoundaryStatic_returnByte},
        {"takeByte", "(B)Z", (void *)BoundaryStatic_takeByte},
        {"returnShort", "()S", (void *)BoundaryStatic_returnShort},
        {"takeShort", "(S)Z", (void *)BoundaryStatic_takeShort},
        {"returnChar", "()C", (void *)BoundaryStatic_returnChar},
        {"takeChar", "(C)Z", (void *)BoundaryStatic_takeChar},
        {"returnInt", "()I", (void *)BoundaryStatic_returnInt},
        {"takeInt", "(I)Z", (void *)BoundaryStatic_takeInt},
        {"returnLong", "()J", (void *)BoundaryStatic_returnLong},
        {"takeLong", "(J)Z", (void *)BoundaryStatic_takeLong},
        {"returnFloat", "()F", (void *)BoundaryStatic_returnFloat},
        {"takeFloat", "(F)Z", (void *)BoundaryStatic_takeFloat},
        {"returnDouble", "()D", (void *)BoundaryStatic_returnDouble},
        {"takeDouble", "(D)Z", (void *)BoundaryStatic_takeDouble},
        {"returnNull", "()Ljava/lang/Object;", (void *)BoundaryStatic_returnNull},
        {"takeNull", "(Ljava/lang/Object;)Z", (void *)BoundaryStatic_takeNull},
        {"returnString", "()Ljava/lang/String;", (void *)BoundaryStatic_returnString},
        {"takeString", "(Ljava/lang/String;)Z", (void *)BoundaryStatic_takeString},
        {"returnShortArray", "()[S", (void *)BoundaryStatic_returnShortArray},
        {"returnStringArray", "()[Ljava/lang/String;", (void *)BoundaryStatic_returnStringArray},
        {"returnThisClass", "()Ljava/lang/Class;", (void *)BoundaryStatic_returnThisClass},
        {"takeThisClass", "(Ljava/lang/Class;)Z", (void *)BoundaryStatic_takeThisClass},
        {"returnInstance", "()Lprobe/BoundaryStatic;", (void *)BoundaryStatic_returnInstance},
        {"takeIntLong", "(IJ)Z", (void *)BoundaryStatic_takeIntLong},
        {"takeLongInt", "(JI)Z", (void *)BoundaryStatic_takeLongInt},
        {"takeOneOfEach", "(ZBSCIJLjava/lang/String;FD[I)Z", (void *)BoundaryStatic_takeOneOfEach},
        {"takeCoolHandLuke", "(IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII)Z",
         (void *)BoundaryStatic_takeCoolHandLuke},
        {"holdsLock", "()Z", (void *)BoundaryStatic_holdsLock},
        {"mixedAlignment", "(JLjava/lang/Object;JLjava/lang/Object;)Z",
         (void *)BoundaryStatic_mixedAlignment},
        {"doublePair", "(DD)D", (void *)BoundaryStatic_doublePair},
        {"throwException", "()V", (void *)BoundaryStatic_throwException}};
    return (*env)->RegisterNatives(env, k, methods, sizeof(methods) / sizeof(methods[0]));
}