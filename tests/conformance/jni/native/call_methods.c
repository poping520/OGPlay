/* SPDX-License-Identifier: Apache-2.0
 * Test points rewritten from CTS tests/tests/jni (libjnitest and Java peers),
 * Sources: libjnitest/macroized_tests.c and
 * src/android/jni/cts/{Static,Instance}FromNative.java.
 * android-4.4.4_r2.0.1, commit b8e2daba969c1d0d9eb3feec9559e12db378681b.
 * Copyright (C) 2009 The Android Open Source Project
 * Licensed under Apache-2.0: https://www.apache.org/licenses/LICENSE-2.0
 */

#include "probe.h"
#include <stdarg.h>
static jstring probe_CallBooleanMethod(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Parent");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueBoolean", "()Z");
    if (!m)
        return failure(env, "method lookup");
    jboolean result = (*env)->CallBooleanMethod(env, obj, m);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == JNI_TRUE ? NULL : failure(env, "CallBooleanMethod returned wrong value");
}
static jstring probe_CallBooleanMethodA(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Parent");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueBoolean", "()Z");
    if (!m)
        return failure(env, "method lookup");
    jvalue aargs[] = {{.i = 0}};
    jboolean result = (*env)->CallBooleanMethodA(env, obj, m, aargs);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == JNI_TRUE ? NULL : failure(env, "CallBooleanMethodA returned wrong value");
}
static jstring probe_CallBooleanMethodV(JNIEnv *env, ...) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Parent");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueBoolean", "()Z");
    if (!m)
        return failure(env, "method lookup");
    va_list ap;
    va_start(ap, env);
    jboolean result = (*env)->CallBooleanMethodV(env, obj, m, ap);
    va_end(ap);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == JNI_TRUE ? NULL : failure(env, "CallBooleanMethodV returned wrong value");
}
static jstring probe_CallByteMethod(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Parent");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueByte", "()B");
    if (!m)
        return failure(env, "method lookup");
    jbyte result = (*env)->CallByteMethod(env, obj, m);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 14 ? NULL : failure(env, "CallByteMethod returned wrong value");
}
static jstring probe_CallByteMethodA(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Parent");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueByte", "()B");
    if (!m)
        return failure(env, "method lookup");
    jvalue aargs[] = {{.i = 0}};
    jbyte result = (*env)->CallByteMethodA(env, obj, m, aargs);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 14 ? NULL : failure(env, "CallByteMethodA returned wrong value");
}
static jstring probe_CallByteMethodV(JNIEnv *env, ...) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Parent");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueByte", "()B");
    if (!m)
        return failure(env, "method lookup");
    va_list ap;
    va_start(ap, env);
    jbyte result = (*env)->CallByteMethodV(env, obj, m, ap);
    va_end(ap);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 14 ? NULL : failure(env, "CallByteMethodV returned wrong value");
}
static jstring probe_CallShortMethod(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Parent");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueShort", "()S");
    if (!m)
        return failure(env, "method lookup");
    jshort result = (*env)->CallShortMethod(env, obj, m);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == -608 ? NULL : failure(env, "CallShortMethod returned wrong value");
}
static jstring probe_CallShortMethodA(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Parent");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueShort", "()S");
    if (!m)
        return failure(env, "method lookup");
    jvalue aargs[] = {{.i = 0}};
    jshort result = (*env)->CallShortMethodA(env, obj, m, aargs);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == -608 ? NULL : failure(env, "CallShortMethodA returned wrong value");
}
static jstring probe_CallShortMethodV(JNIEnv *env, ...) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Parent");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueShort", "()S");
    if (!m)
        return failure(env, "method lookup");
    va_list ap;
    va_start(ap, env);
    jshort result = (*env)->CallShortMethodV(env, obj, m, ap);
    va_end(ap);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == -608 ? NULL : failure(env, "CallShortMethodV returned wrong value");
}
static jstring probe_CallCharMethod(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Parent");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueChar", "()C");
    if (!m)
        return failure(env, "method lookup");
    jchar result = (*env)->CallCharMethod(env, obj, m);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 9000 ? NULL : failure(env, "CallCharMethod returned wrong value");
}
static jstring probe_CallCharMethodA(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Parent");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueChar", "()C");
    if (!m)
        return failure(env, "method lookup");
    jvalue aargs[] = {{.i = 0}};
    jchar result = (*env)->CallCharMethodA(env, obj, m, aargs);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 9000 ? NULL : failure(env, "CallCharMethodA returned wrong value");
}
static jstring probe_CallCharMethodV(JNIEnv *env, ...) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Parent");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueChar", "()C");
    if (!m)
        return failure(env, "method lookup");
    va_list ap;
    va_start(ap, env);
    jchar result = (*env)->CallCharMethodV(env, obj, m, ap);
    va_end(ap);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 9000 ? NULL : failure(env, "CallCharMethodV returned wrong value");
}
static jstring probe_CallIntMethod(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Parent");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueInt", "()I");
    if (!m)
        return failure(env, "method lookup");
    jint result = (*env)->CallIntMethod(env, obj, m);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 4004004 ? NULL : failure(env, "CallIntMethod returned wrong value");
}
static jstring probe_CallIntMethodA(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Parent");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueInt", "()I");
    if (!m)
        return failure(env, "method lookup");
    jvalue aargs[] = {{.i = 0}};
    jint result = (*env)->CallIntMethodA(env, obj, m, aargs);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 4004004 ? NULL : failure(env, "CallIntMethodA returned wrong value");
}
static jstring probe_CallIntMethodV(JNIEnv *env, ...) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Parent");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueInt", "()I");
    if (!m)
        return failure(env, "method lookup");
    va_list ap;
    va_start(ap, env);
    jint result = (*env)->CallIntMethodV(env, obj, m, ap);
    va_end(ap);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 4004004 ? NULL : failure(env, "CallIntMethodV returned wrong value");
}
static jstring probe_CallLongMethod(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Parent");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueLong", "()J");
    if (!m)
        return failure(env, "method lookup");
    jlong result = (*env)->CallLongMethod(env, obj, m);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == -80080080087LL ? NULL : failure(env, "CallLongMethod returned wrong value");
}
static jstring probe_CallLongMethodA(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Parent");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueLong", "()J");
    if (!m)
        return failure(env, "method lookup");
    jvalue aargs[] = {{.i = 0}};
    jlong result = (*env)->CallLongMethodA(env, obj, m, aargs);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == -80080080087LL ? NULL : failure(env, "CallLongMethodA returned wrong value");
}
static jstring probe_CallLongMethodV(JNIEnv *env, ...) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Parent");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueLong", "()J");
    if (!m)
        return failure(env, "method lookup");
    va_list ap;
    va_start(ap, env);
    jlong result = (*env)->CallLongMethodV(env, obj, m, ap);
    va_end(ap);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == -80080080087LL ? NULL : failure(env, "CallLongMethodV returned wrong value");
}
static jstring probe_CallFloatMethod(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Parent");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueFloat", "()F");
    if (!m)
        return failure(env, "method lookup");
    jfloat result = (*env)->CallFloatMethod(env, obj, m);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 2.5e22f ? NULL : failure(env, "CallFloatMethod returned wrong value");
}
static jstring probe_CallFloatMethodA(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Parent");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueFloat", "()F");
    if (!m)
        return failure(env, "method lookup");
    jvalue aargs[] = {{.i = 0}};
    jfloat result = (*env)->CallFloatMethodA(env, obj, m, aargs);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 2.5e22f ? NULL : failure(env, "CallFloatMethodA returned wrong value");
}
static jstring probe_CallFloatMethodV(JNIEnv *env, ...) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Parent");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueFloat", "()F");
    if (!m)
        return failure(env, "method lookup");
    va_list ap;
    va_start(ap, env);
    jfloat result = (*env)->CallFloatMethodV(env, obj, m, ap);
    va_end(ap);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 2.5e22f ? NULL : failure(env, "CallFloatMethodV returned wrong value");
}
static jstring probe_CallDoubleMethod(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Parent");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueDouble", "()D");
    if (!m)
        return failure(env, "method lookup");
    jdouble result = (*env)->CallDoubleMethod(env, obj, m);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 7.503e100 ? NULL : failure(env, "CallDoubleMethod returned wrong value");
}
static jstring probe_CallDoubleMethodA(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Parent");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueDouble", "()D");
    if (!m)
        return failure(env, "method lookup");
    jvalue aargs[] = {{.i = 0}};
    jdouble result = (*env)->CallDoubleMethodA(env, obj, m, aargs);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 7.503e100 ? NULL : failure(env, "CallDoubleMethodA returned wrong value");
}
static jstring probe_CallDoubleMethodV(JNIEnv *env, ...) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Parent");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueDouble", "()D");
    if (!m)
        return failure(env, "method lookup");
    va_list ap;
    va_start(ap, env);
    jdouble result = (*env)->CallDoubleMethodV(env, obj, m, ap);
    va_end(ap);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 7.503e100 ? NULL : failure(env, "CallDoubleMethodV returned wrong value");
}
static jstring probe_CallObjectMethod(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Parent");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueObject", "()Ljava/lang/Object;");
    if (!m)
        return failure(env, "method lookup");
    jobject result = (*env)->CallObjectMethod(env, obj, m);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return text_equal(env, (jstring)result, "muffins")
               ? NULL
               : failure(env, "CallObjectMethod returned wrong value");
}
static jstring probe_CallObjectMethodA(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Parent");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueObject", "()Ljava/lang/Object;");
    if (!m)
        return failure(env, "method lookup");
    jvalue aargs[] = {{.i = 0}};
    jobject result = (*env)->CallObjectMethodA(env, obj, m, aargs);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return text_equal(env, (jstring)result, "muffins")
               ? NULL
               : failure(env, "CallObjectMethodA returned wrong value");
}
static jstring probe_CallObjectMethodV(JNIEnv *env, ...) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Parent");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueObject", "()Ljava/lang/Object;");
    if (!m)
        return failure(env, "method lookup");
    va_list ap;
    va_start(ap, env);
    jobject result = (*env)->CallObjectMethodV(env, obj, m, ap);
    va_end(ap);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return text_equal(env, (jstring)result, "muffins")
               ? NULL
               : failure(env, "CallObjectMethodV returned wrong value");
}
static jstring probe_CallVoidMethod(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Parent");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueVoid", "()V");
    if (!m)
        return failure(env, "method lookup");
    jclass main = (*env)->FindClass(env, "probe/Main");
    (*env)->SetStaticIntField(env, main, (*env)->GetStaticFieldID(env, main, "touched", "I"), 0);
    (*env)->CallVoidMethod(env, obj, m);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return (*env)->GetStaticIntField(env, (*env)->FindClass(env, "probe/Main"),
                                     (*env)->GetStaticFieldID(env,
                                                              (*env)->FindClass(env, "probe/Main"),
                                                              "touched", "I")) == 42
               ? NULL
               : failure(env, "CallVoidMethod returned wrong value");
}
static jstring probe_CallVoidMethodA(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Parent");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueVoid", "()V");
    if (!m)
        return failure(env, "method lookup");
    jvalue aargs[] = {{.i = 0}};
    jclass main = (*env)->FindClass(env, "probe/Main");
    (*env)->SetStaticIntField(env, main, (*env)->GetStaticFieldID(env, main, "touched", "I"), 0);
    (*env)->CallVoidMethodA(env, obj, m, aargs);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return (*env)->GetStaticIntField(env, (*env)->FindClass(env, "probe/Main"),
                                     (*env)->GetStaticFieldID(env,
                                                              (*env)->FindClass(env, "probe/Main"),
                                                              "touched", "I")) == 42
               ? NULL
               : failure(env, "CallVoidMethodA returned wrong value");
}
static jstring probe_CallVoidMethodV(JNIEnv *env, ...) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Parent");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueVoid", "()V");
    if (!m)
        return failure(env, "method lookup");
    va_list ap;
    va_start(ap, env);
    jclass main = (*env)->FindClass(env, "probe/Main");
    (*env)->SetStaticIntField(env, main, (*env)->GetStaticFieldID(env, main, "touched", "I"), 0);
    (*env)->CallVoidMethodV(env, obj, m, ap);
    va_end(ap);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return (*env)->GetStaticIntField(env, (*env)->FindClass(env, "probe/Main"),
                                     (*env)->GetStaticFieldID(env,
                                                              (*env)->FindClass(env, "probe/Main"),
                                                              "touched", "I")) == 42
               ? NULL
               : failure(env, "CallVoidMethodV returned wrong value");
}
static jstring probe_CallBooleanMethod_takeOneOfEach(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Parent");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "takeOneOfEach", "(DFJICSBZLjava/lang/String;)Z");
    if (!m)
        return failure(env, "method lookup");
    jboolean result = (*env)->CallBooleanMethod(env, obj, m, 0.0, 1.0, (jlong)2, 3, 4, 5, 6, 1,
                                                (*env)->NewStringUTF(env, "biscuits"));
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == JNI_TRUE ? NULL : failure(env, "CallBooleanMethod returned wrong value");
}
static jstring probe_CallBooleanMethodA_takeOneOfEach(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Parent");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "takeOneOfEach", "(DFJICSBZLjava/lang/String;)Z");
    if (!m)
        return failure(env, "method lookup");
    jvalue aargs[] = {{.d = 0}, {.f = 1}, {.j = 2},
                      {.i = 3}, {.c = 4}, {.s = 5},
                      {.b = 6}, {.z = 1}, {.l = (*env)->NewStringUTF(env, "biscuits")}};
    jboolean result = (*env)->CallBooleanMethodA(env, obj, m, aargs);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == JNI_TRUE ? NULL : failure(env, "CallBooleanMethodA returned wrong value");
}
static jstring probe_CallBooleanMethodV_takeOneOfEach(JNIEnv *env, ...) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Parent");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "takeOneOfEach", "(DFJICSBZLjava/lang/String;)Z");
    if (!m)
        return failure(env, "method lookup");
    va_list ap;
    va_start(ap, env);
    jboolean result = (*env)->CallBooleanMethodV(env, obj, m, ap);
    va_end(ap);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == JNI_TRUE ? NULL : failure(env, "CallBooleanMethodV returned wrong value");
}
static jstring probe_CallBooleanMethod_takeCoolHandLuke(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Parent");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "takeCoolHandLuke",
                                      "(IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII)Z");
    if (!m)
        return failure(env, "method lookup");
    jboolean result = (*env)->CallBooleanMethod(env, obj, m, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
                                                13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
                                                26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38,
                                                39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == JNI_TRUE ? NULL : failure(env, "CallBooleanMethod returned wrong value");
}
static jstring probe_CallBooleanMethodA_takeCoolHandLuke(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Parent");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "takeCoolHandLuke",
                                      "(IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII)Z");
    if (!m)
        return failure(env, "method lookup");
    jvalue aargs[] = {{.i = 1},  {.i = 2},  {.i = 3},  {.i = 4},  {.i = 5},  {.i = 6},  {.i = 7},
                      {.i = 8},  {.i = 9},  {.i = 10}, {.i = 11}, {.i = 12}, {.i = 13}, {.i = 14},
                      {.i = 15}, {.i = 16}, {.i = 17}, {.i = 18}, {.i = 19}, {.i = 20}, {.i = 21},
                      {.i = 22}, {.i = 23}, {.i = 24}, {.i = 25}, {.i = 26}, {.i = 27}, {.i = 28},
                      {.i = 29}, {.i = 30}, {.i = 31}, {.i = 32}, {.i = 33}, {.i = 34}, {.i = 35},
                      {.i = 36}, {.i = 37}, {.i = 38}, {.i = 39}, {.i = 40}, {.i = 41}, {.i = 42},
                      {.i = 43}, {.i = 44}, {.i = 45}, {.i = 46}, {.i = 47}, {.i = 48}, {.i = 49},
                      {.i = 50}};
    jboolean result = (*env)->CallBooleanMethodA(env, obj, m, aargs);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == JNI_TRUE ? NULL : failure(env, "CallBooleanMethodA returned wrong value");
}
static jstring probe_CallBooleanMethodV_takeCoolHandLuke(JNIEnv *env, ...) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Parent");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "takeCoolHandLuke",
                                      "(IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII)Z");
    if (!m)
        return failure(env, "method lookup");
    va_list ap;
    va_start(ap, env);
    jboolean result = (*env)->CallBooleanMethodV(env, obj, m, ap);
    va_end(ap);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == JNI_TRUE ? NULL : failure(env, "CallBooleanMethodV returned wrong value");
}
static jstring probe_CallNonvirtualBooleanMethod(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Child");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueBoolean", "()Z");
    if (!m)
        return failure(env, "method lookup");
    jboolean result = (*env)->CallNonvirtualBooleanMethod(env, obj, k, m);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == JNI_TRUE ? NULL
                              : failure(env, "CallNonvirtualBooleanMethod returned wrong value");
}
static jstring probe_CallNonvirtualBooleanMethodA(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Child");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueBoolean", "()Z");
    if (!m)
        return failure(env, "method lookup");
    jvalue aargs[] = {{.i = 0}};
    jboolean result = (*env)->CallNonvirtualBooleanMethodA(env, obj, k, m, aargs);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == JNI_TRUE ? NULL
                              : failure(env, "CallNonvirtualBooleanMethodA returned wrong value");
}
static jstring probe_CallNonvirtualBooleanMethodV(JNIEnv *env, ...) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Child");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueBoolean", "()Z");
    if (!m)
        return failure(env, "method lookup");
    va_list ap;
    va_start(ap, env);
    jboolean result = (*env)->CallNonvirtualBooleanMethodV(env, obj, k, m, ap);
    va_end(ap);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == JNI_TRUE ? NULL
                              : failure(env, "CallNonvirtualBooleanMethodV returned wrong value");
}
static jstring probe_CallNonvirtualByteMethod(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Child");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueByte", "()B");
    if (!m)
        return failure(env, "method lookup");
    jbyte result = (*env)->CallNonvirtualByteMethod(env, obj, k, m);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 14 ? NULL : failure(env, "CallNonvirtualByteMethod returned wrong value");
}
static jstring probe_CallNonvirtualByteMethodA(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Child");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueByte", "()B");
    if (!m)
        return failure(env, "method lookup");
    jvalue aargs[] = {{.i = 0}};
    jbyte result = (*env)->CallNonvirtualByteMethodA(env, obj, k, m, aargs);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 14 ? NULL : failure(env, "CallNonvirtualByteMethodA returned wrong value");
}
static jstring probe_CallNonvirtualByteMethodV(JNIEnv *env, ...) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Child");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueByte", "()B");
    if (!m)
        return failure(env, "method lookup");
    va_list ap;
    va_start(ap, env);
    jbyte result = (*env)->CallNonvirtualByteMethodV(env, obj, k, m, ap);
    va_end(ap);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 14 ? NULL : failure(env, "CallNonvirtualByteMethodV returned wrong value");
}
static jstring probe_CallNonvirtualShortMethod(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Child");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueShort", "()S");
    if (!m)
        return failure(env, "method lookup");
    jshort result = (*env)->CallNonvirtualShortMethod(env, obj, k, m);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == -608 ? NULL : failure(env, "CallNonvirtualShortMethod returned wrong value");
}
static jstring probe_CallNonvirtualShortMethodA(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Child");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueShort", "()S");
    if (!m)
        return failure(env, "method lookup");
    jvalue aargs[] = {{.i = 0}};
    jshort result = (*env)->CallNonvirtualShortMethodA(env, obj, k, m, aargs);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == -608 ? NULL : failure(env, "CallNonvirtualShortMethodA returned wrong value");
}
static jstring probe_CallNonvirtualShortMethodV(JNIEnv *env, ...) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Child");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueShort", "()S");
    if (!m)
        return failure(env, "method lookup");
    va_list ap;
    va_start(ap, env);
    jshort result = (*env)->CallNonvirtualShortMethodV(env, obj, k, m, ap);
    va_end(ap);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == -608 ? NULL : failure(env, "CallNonvirtualShortMethodV returned wrong value");
}
static jstring probe_CallNonvirtualCharMethod(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Child");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueChar", "()C");
    if (!m)
        return failure(env, "method lookup");
    jchar result = (*env)->CallNonvirtualCharMethod(env, obj, k, m);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 9000 ? NULL : failure(env, "CallNonvirtualCharMethod returned wrong value");
}
static jstring probe_CallNonvirtualCharMethodA(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Child");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueChar", "()C");
    if (!m)
        return failure(env, "method lookup");
    jvalue aargs[] = {{.i = 0}};
    jchar result = (*env)->CallNonvirtualCharMethodA(env, obj, k, m, aargs);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 9000 ? NULL : failure(env, "CallNonvirtualCharMethodA returned wrong value");
}
static jstring probe_CallNonvirtualCharMethodV(JNIEnv *env, ...) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Child");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueChar", "()C");
    if (!m)
        return failure(env, "method lookup");
    va_list ap;
    va_start(ap, env);
    jchar result = (*env)->CallNonvirtualCharMethodV(env, obj, k, m, ap);
    va_end(ap);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 9000 ? NULL : failure(env, "CallNonvirtualCharMethodV returned wrong value");
}
static jstring probe_CallNonvirtualIntMethod(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Child");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueInt", "()I");
    if (!m)
        return failure(env, "method lookup");
    jint result = (*env)->CallNonvirtualIntMethod(env, obj, k, m);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 4004004 ? NULL : failure(env, "CallNonvirtualIntMethod returned wrong value");
}
static jstring probe_CallNonvirtualIntMethodA(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Child");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueInt", "()I");
    if (!m)
        return failure(env, "method lookup");
    jvalue aargs[] = {{.i = 0}};
    jint result = (*env)->CallNonvirtualIntMethodA(env, obj, k, m, aargs);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 4004004 ? NULL : failure(env, "CallNonvirtualIntMethodA returned wrong value");
}
static jstring probe_CallNonvirtualIntMethodV(JNIEnv *env, ...) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Child");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueInt", "()I");
    if (!m)
        return failure(env, "method lookup");
    va_list ap;
    va_start(ap, env);
    jint result = (*env)->CallNonvirtualIntMethodV(env, obj, k, m, ap);
    va_end(ap);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 4004004 ? NULL : failure(env, "CallNonvirtualIntMethodV returned wrong value");
}
static jstring probe_CallNonvirtualLongMethod(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Child");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueLong", "()J");
    if (!m)
        return failure(env, "method lookup");
    jlong result = (*env)->CallNonvirtualLongMethod(env, obj, k, m);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == -80080080087LL ? NULL
                                    : failure(env, "CallNonvirtualLongMethod returned wrong value");
}
static jstring probe_CallNonvirtualLongMethodA(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Child");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueLong", "()J");
    if (!m)
        return failure(env, "method lookup");
    jvalue aargs[] = {{.i = 0}};
    jlong result = (*env)->CallNonvirtualLongMethodA(env, obj, k, m, aargs);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == -80080080087LL
               ? NULL
               : failure(env, "CallNonvirtualLongMethodA returned wrong value");
}
static jstring probe_CallNonvirtualLongMethodV(JNIEnv *env, ...) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Child");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueLong", "()J");
    if (!m)
        return failure(env, "method lookup");
    va_list ap;
    va_start(ap, env);
    jlong result = (*env)->CallNonvirtualLongMethodV(env, obj, k, m, ap);
    va_end(ap);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == -80080080087LL
               ? NULL
               : failure(env, "CallNonvirtualLongMethodV returned wrong value");
}
static jstring probe_CallNonvirtualFloatMethod(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Child");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueFloat", "()F");
    if (!m)
        return failure(env, "method lookup");
    jfloat result = (*env)->CallNonvirtualFloatMethod(env, obj, k, m);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 2.5e22f ? NULL
                             : failure(env, "CallNonvirtualFloatMethod returned wrong value");
}
static jstring probe_CallNonvirtualFloatMethodA(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Child");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueFloat", "()F");
    if (!m)
        return failure(env, "method lookup");
    jvalue aargs[] = {{.i = 0}};
    jfloat result = (*env)->CallNonvirtualFloatMethodA(env, obj, k, m, aargs);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 2.5e22f ? NULL
                             : failure(env, "CallNonvirtualFloatMethodA returned wrong value");
}
static jstring probe_CallNonvirtualFloatMethodV(JNIEnv *env, ...) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Child");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueFloat", "()F");
    if (!m)
        return failure(env, "method lookup");
    va_list ap;
    va_start(ap, env);
    jfloat result = (*env)->CallNonvirtualFloatMethodV(env, obj, k, m, ap);
    va_end(ap);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 2.5e22f ? NULL
                             : failure(env, "CallNonvirtualFloatMethodV returned wrong value");
}
static jstring probe_CallNonvirtualDoubleMethod(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Child");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueDouble", "()D");
    if (!m)
        return failure(env, "method lookup");
    jdouble result = (*env)->CallNonvirtualDoubleMethod(env, obj, k, m);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 7.503e100 ? NULL
                               : failure(env, "CallNonvirtualDoubleMethod returned wrong value");
}
static jstring probe_CallNonvirtualDoubleMethodA(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Child");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueDouble", "()D");
    if (!m)
        return failure(env, "method lookup");
    jvalue aargs[] = {{.i = 0}};
    jdouble result = (*env)->CallNonvirtualDoubleMethodA(env, obj, k, m, aargs);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 7.503e100 ? NULL
                               : failure(env, "CallNonvirtualDoubleMethodA returned wrong value");
}
static jstring probe_CallNonvirtualDoubleMethodV(JNIEnv *env, ...) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Child");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueDouble", "()D");
    if (!m)
        return failure(env, "method lookup");
    va_list ap;
    va_start(ap, env);
    jdouble result = (*env)->CallNonvirtualDoubleMethodV(env, obj, k, m, ap);
    va_end(ap);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 7.503e100 ? NULL
                               : failure(env, "CallNonvirtualDoubleMethodV returned wrong value");
}
static jstring probe_CallNonvirtualObjectMethod(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Child");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueObject", "()Ljava/lang/Object;");
    if (!m)
        return failure(env, "method lookup");
    jobject result = (*env)->CallNonvirtualObjectMethod(env, obj, k, m);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return text_equal(env, (jstring)result, "muffins")
               ? NULL
               : failure(env, "CallNonvirtualObjectMethod returned wrong value");
}
static jstring probe_CallNonvirtualObjectMethodA(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Child");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueObject", "()Ljava/lang/Object;");
    if (!m)
        return failure(env, "method lookup");
    jvalue aargs[] = {{.i = 0}};
    jobject result = (*env)->CallNonvirtualObjectMethodA(env, obj, k, m, aargs);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return text_equal(env, (jstring)result, "muffins")
               ? NULL
               : failure(env, "CallNonvirtualObjectMethodA returned wrong value");
}
static jstring probe_CallNonvirtualObjectMethodV(JNIEnv *env, ...) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Child");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueObject", "()Ljava/lang/Object;");
    if (!m)
        return failure(env, "method lookup");
    va_list ap;
    va_start(ap, env);
    jobject result = (*env)->CallNonvirtualObjectMethodV(env, obj, k, m, ap);
    va_end(ap);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return text_equal(env, (jstring)result, "muffins")
               ? NULL
               : failure(env, "CallNonvirtualObjectMethodV returned wrong value");
}
static jstring probe_CallNonvirtualVoidMethod(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Child");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueVoid", "()V");
    if (!m)
        return failure(env, "method lookup");
    jclass main = (*env)->FindClass(env, "probe/Main");
    (*env)->SetStaticIntField(env, main, (*env)->GetStaticFieldID(env, main, "touched", "I"), 0);
    (*env)->CallNonvirtualVoidMethod(env, obj, k, m);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return (*env)->GetStaticIntField(env, (*env)->FindClass(env, "probe/Main"),
                                     (*env)->GetStaticFieldID(env,
                                                              (*env)->FindClass(env, "probe/Main"),
                                                              "touched", "I")) == 42
               ? NULL
               : failure(env, "CallNonvirtualVoidMethod returned wrong value");
}
static jstring probe_CallNonvirtualVoidMethodA(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Child");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueVoid", "()V");
    if (!m)
        return failure(env, "method lookup");
    jvalue aargs[] = {{.i = 0}};
    jclass main = (*env)->FindClass(env, "probe/Main");
    (*env)->SetStaticIntField(env, main, (*env)->GetStaticFieldID(env, main, "touched", "I"), 0);
    (*env)->CallNonvirtualVoidMethodA(env, obj, k, m, aargs);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return (*env)->GetStaticIntField(env, (*env)->FindClass(env, "probe/Main"),
                                     (*env)->GetStaticFieldID(env,
                                                              (*env)->FindClass(env, "probe/Main"),
                                                              "touched", "I")) == 42
               ? NULL
               : failure(env, "CallNonvirtualVoidMethodA returned wrong value");
}
static jstring probe_CallNonvirtualVoidMethodV(JNIEnv *env, ...) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Child");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "valueVoid", "()V");
    if (!m)
        return failure(env, "method lookup");
    va_list ap;
    va_start(ap, env);
    jclass main = (*env)->FindClass(env, "probe/Main");
    (*env)->SetStaticIntField(env, main, (*env)->GetStaticFieldID(env, main, "touched", "I"), 0);
    (*env)->CallNonvirtualVoidMethodV(env, obj, k, m, ap);
    va_end(ap);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return (*env)->GetStaticIntField(env, (*env)->FindClass(env, "probe/Main"),
                                     (*env)->GetStaticFieldID(env,
                                                              (*env)->FindClass(env, "probe/Main"),
                                                              "touched", "I")) == 42
               ? NULL
               : failure(env, "CallNonvirtualVoidMethodV returned wrong value");
}
static jstring probe_CallNonvirtualBooleanMethod_takeOneOfEach(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Child");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "takeOneOfEach", "(DFJICSBZLjava/lang/String;)Z");
    if (!m)
        return failure(env, "method lookup");
    jboolean result = (*env)->CallNonvirtualBooleanMethod(
        env, obj, k, m, 0.0, 1.0, (jlong)2, 3, 4, 5, 6, 1, (*env)->NewStringUTF(env, "biscuits"));
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == JNI_TRUE ? NULL
                              : failure(env, "CallNonvirtualBooleanMethod returned wrong value");
}
static jstring probe_CallNonvirtualBooleanMethodA_takeOneOfEach(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Child");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "takeOneOfEach", "(DFJICSBZLjava/lang/String;)Z");
    if (!m)
        return failure(env, "method lookup");
    jvalue aargs[] = {{.d = 0}, {.f = 1}, {.j = 2},
                      {.i = 3}, {.c = 4}, {.s = 5},
                      {.b = 6}, {.z = 1}, {.l = (*env)->NewStringUTF(env, "biscuits")}};
    jboolean result = (*env)->CallNonvirtualBooleanMethodA(env, obj, k, m, aargs);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == JNI_TRUE ? NULL
                              : failure(env, "CallNonvirtualBooleanMethodA returned wrong value");
}
static jstring probe_CallNonvirtualBooleanMethodV_takeOneOfEach(JNIEnv *env, ...) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Child");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "takeOneOfEach", "(DFJICSBZLjava/lang/String;)Z");
    if (!m)
        return failure(env, "method lookup");
    va_list ap;
    va_start(ap, env);
    jboolean result = (*env)->CallNonvirtualBooleanMethodV(env, obj, k, m, ap);
    va_end(ap);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == JNI_TRUE ? NULL
                              : failure(env, "CallNonvirtualBooleanMethodV returned wrong value");
}
static jstring probe_CallNonvirtualBooleanMethod_takeCoolHandLuke(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Child");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "takeCoolHandLuke",
                                      "(IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII)Z");
    if (!m)
        return failure(env, "method lookup");
    jboolean result = (*env)->CallNonvirtualBooleanMethod(
        env, obj, k, m, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
        22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44,
        45, 46, 47, 48, 49, 50);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == JNI_TRUE ? NULL
                              : failure(env, "CallNonvirtualBooleanMethod returned wrong value");
}
static jstring probe_CallNonvirtualBooleanMethodA_takeCoolHandLuke(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Child");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "takeCoolHandLuke",
                                      "(IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII)Z");
    if (!m)
        return failure(env, "method lookup");
    jvalue aargs[] = {{.i = 1},  {.i = 2},  {.i = 3},  {.i = 4},  {.i = 5},  {.i = 6},  {.i = 7},
                      {.i = 8},  {.i = 9},  {.i = 10}, {.i = 11}, {.i = 12}, {.i = 13}, {.i = 14},
                      {.i = 15}, {.i = 16}, {.i = 17}, {.i = 18}, {.i = 19}, {.i = 20}, {.i = 21},
                      {.i = 22}, {.i = 23}, {.i = 24}, {.i = 25}, {.i = 26}, {.i = 27}, {.i = 28},
                      {.i = 29}, {.i = 30}, {.i = 31}, {.i = 32}, {.i = 33}, {.i = 34}, {.i = 35},
                      {.i = 36}, {.i = 37}, {.i = 38}, {.i = 39}, {.i = 40}, {.i = 41}, {.i = 42},
                      {.i = 43}, {.i = 44}, {.i = 45}, {.i = 46}, {.i = 47}, {.i = 48}, {.i = 49},
                      {.i = 50}};
    jboolean result = (*env)->CallNonvirtualBooleanMethodA(env, obj, k, m, aargs);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == JNI_TRUE ? NULL
                              : failure(env, "CallNonvirtualBooleanMethodA returned wrong value");
}
static jstring probe_CallNonvirtualBooleanMethodV_takeCoolHandLuke(JNIEnv *env, ...) {
    jclass k = (*env)->FindClass(env, "probe/Parent");
    jclass a = (*env)->FindClass(env, "probe/Child");
    jobject obj = (*env)->NewObjectA(env, a, (*env)->GetMethodID(env, a, "<init>", "()V"), NULL);
    jmethodID m = (*env)->GetMethodID(env, k, "takeCoolHandLuke",
                                      "(IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII)Z");
    if (!m)
        return failure(env, "method lookup");
    va_list ap;
    va_start(ap, env);
    jboolean result = (*env)->CallNonvirtualBooleanMethodV(env, obj, k, m, ap);
    va_end(ap);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == JNI_TRUE ? NULL
                              : failure(env, "CallNonvirtualBooleanMethodV returned wrong value");
}
static jstring probe_CallStaticBooleanMethod(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/StaticTarget");
    jmethodID m = (*env)->GetStaticMethodID(env, k, "valueBoolean", "()Z");
    if (!m)
        return failure(env, "method lookup");
    jboolean result = (*env)->CallStaticBooleanMethod(env, k, m);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == JNI_TRUE ? NULL : failure(env, "CallStaticBooleanMethod returned wrong value");
}
static jstring probe_CallStaticBooleanMethodA(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/StaticTarget");
    jmethodID m = (*env)->GetStaticMethodID(env, k, "valueBoolean", "()Z");
    if (!m)
        return failure(env, "method lookup");
    jvalue aargs[] = {{.i = 0}};
    jboolean result = (*env)->CallStaticBooleanMethodA(env, k, m, aargs);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == JNI_TRUE ? NULL
                              : failure(env, "CallStaticBooleanMethodA returned wrong value");
}
static jstring probe_CallStaticBooleanMethodV(JNIEnv *env, ...) {
    jclass k = (*env)->FindClass(env, "probe/StaticTarget");
    jmethodID m = (*env)->GetStaticMethodID(env, k, "valueBoolean", "()Z");
    if (!m)
        return failure(env, "method lookup");
    va_list ap;
    va_start(ap, env);
    jboolean result = (*env)->CallStaticBooleanMethodV(env, k, m, ap);
    va_end(ap);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == JNI_TRUE ? NULL
                              : failure(env, "CallStaticBooleanMethodV returned wrong value");
}
static jstring probe_CallStaticByteMethod(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/StaticTarget");
    jmethodID m = (*env)->GetStaticMethodID(env, k, "valueByte", "()B");
    if (!m)
        return failure(env, "method lookup");
    jbyte result = (*env)->CallStaticByteMethod(env, k, m);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 14 ? NULL : failure(env, "CallStaticByteMethod returned wrong value");
}
static jstring probe_CallStaticByteMethodA(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/StaticTarget");
    jmethodID m = (*env)->GetStaticMethodID(env, k, "valueByte", "()B");
    if (!m)
        return failure(env, "method lookup");
    jvalue aargs[] = {{.i = 0}};
    jbyte result = (*env)->CallStaticByteMethodA(env, k, m, aargs);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 14 ? NULL : failure(env, "CallStaticByteMethodA returned wrong value");
}
static jstring probe_CallStaticByteMethodV(JNIEnv *env, ...) {
    jclass k = (*env)->FindClass(env, "probe/StaticTarget");
    jmethodID m = (*env)->GetStaticMethodID(env, k, "valueByte", "()B");
    if (!m)
        return failure(env, "method lookup");
    va_list ap;
    va_start(ap, env);
    jbyte result = (*env)->CallStaticByteMethodV(env, k, m, ap);
    va_end(ap);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 14 ? NULL : failure(env, "CallStaticByteMethodV returned wrong value");
}
static jstring probe_CallStaticShortMethod(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/StaticTarget");
    jmethodID m = (*env)->GetStaticMethodID(env, k, "valueShort", "()S");
    if (!m)
        return failure(env, "method lookup");
    jshort result = (*env)->CallStaticShortMethod(env, k, m);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == -608 ? NULL : failure(env, "CallStaticShortMethod returned wrong value");
}
static jstring probe_CallStaticShortMethodA(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/StaticTarget");
    jmethodID m = (*env)->GetStaticMethodID(env, k, "valueShort", "()S");
    if (!m)
        return failure(env, "method lookup");
    jvalue aargs[] = {{.i = 0}};
    jshort result = (*env)->CallStaticShortMethodA(env, k, m, aargs);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == -608 ? NULL : failure(env, "CallStaticShortMethodA returned wrong value");
}
static jstring probe_CallStaticShortMethodV(JNIEnv *env, ...) {
    jclass k = (*env)->FindClass(env, "probe/StaticTarget");
    jmethodID m = (*env)->GetStaticMethodID(env, k, "valueShort", "()S");
    if (!m)
        return failure(env, "method lookup");
    va_list ap;
    va_start(ap, env);
    jshort result = (*env)->CallStaticShortMethodV(env, k, m, ap);
    va_end(ap);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == -608 ? NULL : failure(env, "CallStaticShortMethodV returned wrong value");
}
static jstring probe_CallStaticCharMethod(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/StaticTarget");
    jmethodID m = (*env)->GetStaticMethodID(env, k, "valueChar", "()C");
    if (!m)
        return failure(env, "method lookup");
    jchar result = (*env)->CallStaticCharMethod(env, k, m);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 9000 ? NULL : failure(env, "CallStaticCharMethod returned wrong value");
}
static jstring probe_CallStaticCharMethodA(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/StaticTarget");
    jmethodID m = (*env)->GetStaticMethodID(env, k, "valueChar", "()C");
    if (!m)
        return failure(env, "method lookup");
    jvalue aargs[] = {{.i = 0}};
    jchar result = (*env)->CallStaticCharMethodA(env, k, m, aargs);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 9000 ? NULL : failure(env, "CallStaticCharMethodA returned wrong value");
}
static jstring probe_CallStaticCharMethodV(JNIEnv *env, ...) {
    jclass k = (*env)->FindClass(env, "probe/StaticTarget");
    jmethodID m = (*env)->GetStaticMethodID(env, k, "valueChar", "()C");
    if (!m)
        return failure(env, "method lookup");
    va_list ap;
    va_start(ap, env);
    jchar result = (*env)->CallStaticCharMethodV(env, k, m, ap);
    va_end(ap);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 9000 ? NULL : failure(env, "CallStaticCharMethodV returned wrong value");
}
static jstring probe_CallStaticIntMethod(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/StaticTarget");
    jmethodID m = (*env)->GetStaticMethodID(env, k, "valueInt", "()I");
    if (!m)
        return failure(env, "method lookup");
    jint result = (*env)->CallStaticIntMethod(env, k, m);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 4004004 ? NULL : failure(env, "CallStaticIntMethod returned wrong value");
}
static jstring probe_CallStaticIntMethodA(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/StaticTarget");
    jmethodID m = (*env)->GetStaticMethodID(env, k, "valueInt", "()I");
    if (!m)
        return failure(env, "method lookup");
    jvalue aargs[] = {{.i = 0}};
    jint result = (*env)->CallStaticIntMethodA(env, k, m, aargs);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 4004004 ? NULL : failure(env, "CallStaticIntMethodA returned wrong value");
}
static jstring probe_CallStaticIntMethodV(JNIEnv *env, ...) {
    jclass k = (*env)->FindClass(env, "probe/StaticTarget");
    jmethodID m = (*env)->GetStaticMethodID(env, k, "valueInt", "()I");
    if (!m)
        return failure(env, "method lookup");
    va_list ap;
    va_start(ap, env);
    jint result = (*env)->CallStaticIntMethodV(env, k, m, ap);
    va_end(ap);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 4004004 ? NULL : failure(env, "CallStaticIntMethodV returned wrong value");
}
static jstring probe_CallStaticLongMethod(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/StaticTarget");
    jmethodID m = (*env)->GetStaticMethodID(env, k, "valueLong", "()J");
    if (!m)
        return failure(env, "method lookup");
    jlong result = (*env)->CallStaticLongMethod(env, k, m);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == -80080080087LL ? NULL
                                    : failure(env, "CallStaticLongMethod returned wrong value");
}
static jstring probe_CallStaticLongMethodA(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/StaticTarget");
    jmethodID m = (*env)->GetStaticMethodID(env, k, "valueLong", "()J");
    if (!m)
        return failure(env, "method lookup");
    jvalue aargs[] = {{.i = 0}};
    jlong result = (*env)->CallStaticLongMethodA(env, k, m, aargs);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == -80080080087LL ? NULL
                                    : failure(env, "CallStaticLongMethodA returned wrong value");
}
static jstring probe_CallStaticLongMethodV(JNIEnv *env, ...) {
    jclass k = (*env)->FindClass(env, "probe/StaticTarget");
    jmethodID m = (*env)->GetStaticMethodID(env, k, "valueLong", "()J");
    if (!m)
        return failure(env, "method lookup");
    va_list ap;
    va_start(ap, env);
    jlong result = (*env)->CallStaticLongMethodV(env, k, m, ap);
    va_end(ap);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == -80080080087LL ? NULL
                                    : failure(env, "CallStaticLongMethodV returned wrong value");
}
static jstring probe_CallStaticFloatMethod(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/StaticTarget");
    jmethodID m = (*env)->GetStaticMethodID(env, k, "valueFloat", "()F");
    if (!m)
        return failure(env, "method lookup");
    jfloat result = (*env)->CallStaticFloatMethod(env, k, m);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 2.5e22f ? NULL : failure(env, "CallStaticFloatMethod returned wrong value");
}
static jstring probe_CallStaticFloatMethodA(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/StaticTarget");
    jmethodID m = (*env)->GetStaticMethodID(env, k, "valueFloat", "()F");
    if (!m)
        return failure(env, "method lookup");
    jvalue aargs[] = {{.i = 0}};
    jfloat result = (*env)->CallStaticFloatMethodA(env, k, m, aargs);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 2.5e22f ? NULL : failure(env, "CallStaticFloatMethodA returned wrong value");
}
static jstring probe_CallStaticFloatMethodV(JNIEnv *env, ...) {
    jclass k = (*env)->FindClass(env, "probe/StaticTarget");
    jmethodID m = (*env)->GetStaticMethodID(env, k, "valueFloat", "()F");
    if (!m)
        return failure(env, "method lookup");
    va_list ap;
    va_start(ap, env);
    jfloat result = (*env)->CallStaticFloatMethodV(env, k, m, ap);
    va_end(ap);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 2.5e22f ? NULL : failure(env, "CallStaticFloatMethodV returned wrong value");
}
static jstring probe_CallStaticDoubleMethod(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/StaticTarget");
    jmethodID m = (*env)->GetStaticMethodID(env, k, "valueDouble", "()D");
    if (!m)
        return failure(env, "method lookup");
    jdouble result = (*env)->CallStaticDoubleMethod(env, k, m);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 7.503e100 ? NULL : failure(env, "CallStaticDoubleMethod returned wrong value");
}
static jstring probe_CallStaticDoubleMethodA(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/StaticTarget");
    jmethodID m = (*env)->GetStaticMethodID(env, k, "valueDouble", "()D");
    if (!m)
        return failure(env, "method lookup");
    jvalue aargs[] = {{.i = 0}};
    jdouble result = (*env)->CallStaticDoubleMethodA(env, k, m, aargs);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 7.503e100 ? NULL
                               : failure(env, "CallStaticDoubleMethodA returned wrong value");
}
static jstring probe_CallStaticDoubleMethodV(JNIEnv *env, ...) {
    jclass k = (*env)->FindClass(env, "probe/StaticTarget");
    jmethodID m = (*env)->GetStaticMethodID(env, k, "valueDouble", "()D");
    if (!m)
        return failure(env, "method lookup");
    va_list ap;
    va_start(ap, env);
    jdouble result = (*env)->CallStaticDoubleMethodV(env, k, m, ap);
    va_end(ap);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == 7.503e100 ? NULL
                               : failure(env, "CallStaticDoubleMethodV returned wrong value");
}
static jstring probe_CallStaticObjectMethod(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/StaticTarget");
    jmethodID m = (*env)->GetStaticMethodID(env, k, "valueObject", "()Ljava/lang/Object;");
    if (!m)
        return failure(env, "method lookup");
    jobject result = (*env)->CallStaticObjectMethod(env, k, m);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return text_equal(env, (jstring)result, "muffins")
               ? NULL
               : failure(env, "CallStaticObjectMethod returned wrong value");
}
static jstring probe_CallStaticObjectMethodA(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/StaticTarget");
    jmethodID m = (*env)->GetStaticMethodID(env, k, "valueObject", "()Ljava/lang/Object;");
    if (!m)
        return failure(env, "method lookup");
    jvalue aargs[] = {{.i = 0}};
    jobject result = (*env)->CallStaticObjectMethodA(env, k, m, aargs);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return text_equal(env, (jstring)result, "muffins")
               ? NULL
               : failure(env, "CallStaticObjectMethodA returned wrong value");
}
static jstring probe_CallStaticObjectMethodV(JNIEnv *env, ...) {
    jclass k = (*env)->FindClass(env, "probe/StaticTarget");
    jmethodID m = (*env)->GetStaticMethodID(env, k, "valueObject", "()Ljava/lang/Object;");
    if (!m)
        return failure(env, "method lookup");
    va_list ap;
    va_start(ap, env);
    jobject result = (*env)->CallStaticObjectMethodV(env, k, m, ap);
    va_end(ap);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return text_equal(env, (jstring)result, "muffins")
               ? NULL
               : failure(env, "CallStaticObjectMethodV returned wrong value");
}
static jstring probe_CallStaticVoidMethod(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/StaticTarget");
    jmethodID m = (*env)->GetStaticMethodID(env, k, "valueVoid", "()V");
    if (!m)
        return failure(env, "method lookup");
    jclass main = (*env)->FindClass(env, "probe/Main");
    (*env)->SetStaticIntField(env, main, (*env)->GetStaticFieldID(env, main, "touched", "I"), 0);
    (*env)->CallStaticVoidMethod(env, k, m);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return (*env)->GetStaticIntField(env, (*env)->FindClass(env, "probe/Main"),
                                     (*env)->GetStaticFieldID(env,
                                                              (*env)->FindClass(env, "probe/Main"),
                                                              "touched", "I")) == 42
               ? NULL
               : failure(env, "CallStaticVoidMethod returned wrong value");
}
static jstring probe_CallStaticVoidMethodA(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/StaticTarget");
    jmethodID m = (*env)->GetStaticMethodID(env, k, "valueVoid", "()V");
    if (!m)
        return failure(env, "method lookup");
    jvalue aargs[] = {{.i = 0}};
    jclass main = (*env)->FindClass(env, "probe/Main");
    (*env)->SetStaticIntField(env, main, (*env)->GetStaticFieldID(env, main, "touched", "I"), 0);
    (*env)->CallStaticVoidMethodA(env, k, m, aargs);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return (*env)->GetStaticIntField(env, (*env)->FindClass(env, "probe/Main"),
                                     (*env)->GetStaticFieldID(env,
                                                              (*env)->FindClass(env, "probe/Main"),
                                                              "touched", "I")) == 42
               ? NULL
               : failure(env, "CallStaticVoidMethodA returned wrong value");
}
static jstring probe_CallStaticVoidMethodV(JNIEnv *env, ...) {
    jclass k = (*env)->FindClass(env, "probe/StaticTarget");
    jmethodID m = (*env)->GetStaticMethodID(env, k, "valueVoid", "()V");
    if (!m)
        return failure(env, "method lookup");
    va_list ap;
    va_start(ap, env);
    jclass main = (*env)->FindClass(env, "probe/Main");
    (*env)->SetStaticIntField(env, main, (*env)->GetStaticFieldID(env, main, "touched", "I"), 0);
    (*env)->CallStaticVoidMethodV(env, k, m, ap);
    va_end(ap);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return (*env)->GetStaticIntField(env, (*env)->FindClass(env, "probe/Main"),
                                     (*env)->GetStaticFieldID(env,
                                                              (*env)->FindClass(env, "probe/Main"),
                                                              "touched", "I")) == 42
               ? NULL
               : failure(env, "CallStaticVoidMethodV returned wrong value");
}
static jstring probe_CallStaticBooleanMethod_takeOneOfEach(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/StaticTarget");
    jmethodID m =
        (*env)->GetStaticMethodID(env, k, "takeOneOfEach", "(DFJICSBZLjava/lang/String;)Z");
    if (!m)
        return failure(env, "method lookup");
    jboolean result = (*env)->CallStaticBooleanMethod(env, k, m, 0.0, 1.0, (jlong)2, 3, 4, 5, 6, 1,
                                                      (*env)->NewStringUTF(env, "biscuits"));
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == JNI_TRUE ? NULL : failure(env, "CallStaticBooleanMethod returned wrong value");
}
static jstring probe_CallStaticBooleanMethodA_takeOneOfEach(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/StaticTarget");
    jmethodID m =
        (*env)->GetStaticMethodID(env, k, "takeOneOfEach", "(DFJICSBZLjava/lang/String;)Z");
    if (!m)
        return failure(env, "method lookup");
    jvalue aargs[] = {{.d = 0}, {.f = 1}, {.j = 2},
                      {.i = 3}, {.c = 4}, {.s = 5},
                      {.b = 6}, {.z = 1}, {.l = (*env)->NewStringUTF(env, "biscuits")}};
    jboolean result = (*env)->CallStaticBooleanMethodA(env, k, m, aargs);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == JNI_TRUE ? NULL
                              : failure(env, "CallStaticBooleanMethodA returned wrong value");
}
static jstring probe_CallStaticBooleanMethodV_takeOneOfEach(JNIEnv *env, ...) {
    jclass k = (*env)->FindClass(env, "probe/StaticTarget");
    jmethodID m =
        (*env)->GetStaticMethodID(env, k, "takeOneOfEach", "(DFJICSBZLjava/lang/String;)Z");
    if (!m)
        return failure(env, "method lookup");
    va_list ap;
    va_start(ap, env);
    jboolean result = (*env)->CallStaticBooleanMethodV(env, k, m, ap);
    va_end(ap);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == JNI_TRUE ? NULL
                              : failure(env, "CallStaticBooleanMethodV returned wrong value");
}
static jstring probe_CallStaticBooleanMethod_takeCoolHandLuke(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/StaticTarget");
    jmethodID m = (*env)->GetStaticMethodID(
        env, k, "takeCoolHandLuke", "(IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII)Z");
    if (!m)
        return failure(env, "method lookup");
    jboolean result = (*env)->CallStaticBooleanMethod(
        env, k, m, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
        23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45,
        46, 47, 48, 49, 50);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == JNI_TRUE ? NULL : failure(env, "CallStaticBooleanMethod returned wrong value");
}
static jstring probe_CallStaticBooleanMethodA_takeCoolHandLuke(JNIEnv *env) {
    jclass k = (*env)->FindClass(env, "probe/StaticTarget");
    jmethodID m = (*env)->GetStaticMethodID(
        env, k, "takeCoolHandLuke", "(IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII)Z");
    if (!m)
        return failure(env, "method lookup");
    jvalue aargs[] = {{.i = 1},  {.i = 2},  {.i = 3},  {.i = 4},  {.i = 5},  {.i = 6},  {.i = 7},
                      {.i = 8},  {.i = 9},  {.i = 10}, {.i = 11}, {.i = 12}, {.i = 13}, {.i = 14},
                      {.i = 15}, {.i = 16}, {.i = 17}, {.i = 18}, {.i = 19}, {.i = 20}, {.i = 21},
                      {.i = 22}, {.i = 23}, {.i = 24}, {.i = 25}, {.i = 26}, {.i = 27}, {.i = 28},
                      {.i = 29}, {.i = 30}, {.i = 31}, {.i = 32}, {.i = 33}, {.i = 34}, {.i = 35},
                      {.i = 36}, {.i = 37}, {.i = 38}, {.i = 39}, {.i = 40}, {.i = 41}, {.i = 42},
                      {.i = 43}, {.i = 44}, {.i = 45}, {.i = 46}, {.i = 47}, {.i = 48}, {.i = 49},
                      {.i = 50}};
    jboolean result = (*env)->CallStaticBooleanMethodA(env, k, m, aargs);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == JNI_TRUE ? NULL
                              : failure(env, "CallStaticBooleanMethodA returned wrong value");
}
static jstring probe_CallStaticBooleanMethodV_takeCoolHandLuke(JNIEnv *env, ...) {
    jclass k = (*env)->FindClass(env, "probe/StaticTarget");
    jmethodID m = (*env)->GetStaticMethodID(
        env, k, "takeCoolHandLuke", "(IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII)Z");
    if (!m)
        return failure(env, "method lookup");
    va_list ap;
    va_start(ap, env);
    jboolean result = (*env)->CallStaticBooleanMethodV(env, k, m, ap);
    va_end(ap);
    if ((*env)->ExceptionCheck(env))
        return failure(env, "callback exception");
    return result == JNI_TRUE ? NULL
                              : failure(env, "CallStaticBooleanMethodV returned wrong value");
}
jstring run_call(JNIEnv *env, const char *id) {
    if (strcmp(id, "call.CallBooleanMethod") == 0)
        return probe_CallBooleanMethod(env);
    if (strcmp(id, "call.CallBooleanMethodA") == 0)
        return probe_CallBooleanMethodA(env);
    if (strcmp(id, "call.CallBooleanMethodV") == 0)
        return probe_CallBooleanMethodV(env);
    if (strcmp(id, "call.CallByteMethod") == 0)
        return probe_CallByteMethod(env);
    if (strcmp(id, "call.CallByteMethodA") == 0)
        return probe_CallByteMethodA(env);
    if (strcmp(id, "call.CallByteMethodV") == 0)
        return probe_CallByteMethodV(env);
    if (strcmp(id, "call.CallShortMethod") == 0)
        return probe_CallShortMethod(env);
    if (strcmp(id, "call.CallShortMethodA") == 0)
        return probe_CallShortMethodA(env);
    if (strcmp(id, "call.CallShortMethodV") == 0)
        return probe_CallShortMethodV(env);
    if (strcmp(id, "call.CallCharMethod") == 0)
        return probe_CallCharMethod(env);
    if (strcmp(id, "call.CallCharMethodA") == 0)
        return probe_CallCharMethodA(env);
    if (strcmp(id, "call.CallCharMethodV") == 0)
        return probe_CallCharMethodV(env);
    if (strcmp(id, "call.CallIntMethod") == 0)
        return probe_CallIntMethod(env);
    if (strcmp(id, "call.CallIntMethodA") == 0)
        return probe_CallIntMethodA(env);
    if (strcmp(id, "call.CallIntMethodV") == 0)
        return probe_CallIntMethodV(env);
    if (strcmp(id, "call.CallLongMethod") == 0)
        return probe_CallLongMethod(env);
    if (strcmp(id, "call.CallLongMethodA") == 0)
        return probe_CallLongMethodA(env);
    if (strcmp(id, "call.CallLongMethodV") == 0)
        return probe_CallLongMethodV(env);
    if (strcmp(id, "call.CallFloatMethod") == 0)
        return probe_CallFloatMethod(env);
    if (strcmp(id, "call.CallFloatMethodA") == 0)
        return probe_CallFloatMethodA(env);
    if (strcmp(id, "call.CallFloatMethodV") == 0)
        return probe_CallFloatMethodV(env);
    if (strcmp(id, "call.CallDoubleMethod") == 0)
        return probe_CallDoubleMethod(env);
    if (strcmp(id, "call.CallDoubleMethodA") == 0)
        return probe_CallDoubleMethodA(env);
    if (strcmp(id, "call.CallDoubleMethodV") == 0)
        return probe_CallDoubleMethodV(env);
    if (strcmp(id, "call.CallObjectMethod") == 0)
        return probe_CallObjectMethod(env);
    if (strcmp(id, "call.CallObjectMethodA") == 0)
        return probe_CallObjectMethodA(env);
    if (strcmp(id, "call.CallObjectMethodV") == 0)
        return probe_CallObjectMethodV(env);
    if (strcmp(id, "call.CallVoidMethod") == 0)
        return probe_CallVoidMethod(env);
    if (strcmp(id, "call.CallVoidMethodA") == 0)
        return probe_CallVoidMethodA(env);
    if (strcmp(id, "call.CallVoidMethodV") == 0)
        return probe_CallVoidMethodV(env);
    if (strcmp(id, "call.CallBooleanMethod.takeOneOfEach") == 0)
        return probe_CallBooleanMethod_takeOneOfEach(env);
    if (strcmp(id, "call.CallBooleanMethodA.takeOneOfEach") == 0)
        return probe_CallBooleanMethodA_takeOneOfEach(env);
    if (strcmp(id, "call.CallBooleanMethodV.takeOneOfEach") == 0)
        return probe_CallBooleanMethodV_takeOneOfEach(env, 0.0, 1.0, (jlong)2, 3, 4, 5, 6, 1,
                                                      (*env)->NewStringUTF(env, "biscuits"));
    if (strcmp(id, "call.CallBooleanMethod.takeCoolHandLuke") == 0)
        return probe_CallBooleanMethod_takeCoolHandLuke(env);
    if (strcmp(id, "call.CallBooleanMethodA.takeCoolHandLuke") == 0)
        return probe_CallBooleanMethodA_takeCoolHandLuke(env);
    if (strcmp(id, "call.CallBooleanMethodV.takeCoolHandLuke") == 0)
        return probe_CallBooleanMethodV_takeCoolHandLuke(
            env, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
            24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45,
            46, 47, 48, 49, 50);
    if (strcmp(id, "call.CallNonvirtualBooleanMethod") == 0)
        return probe_CallNonvirtualBooleanMethod(env);
    if (strcmp(id, "call.CallNonvirtualBooleanMethodA") == 0)
        return probe_CallNonvirtualBooleanMethodA(env);
    if (strcmp(id, "call.CallNonvirtualBooleanMethodV") == 0)
        return probe_CallNonvirtualBooleanMethodV(env);
    if (strcmp(id, "call.CallNonvirtualByteMethod") == 0)
        return probe_CallNonvirtualByteMethod(env);
    if (strcmp(id, "call.CallNonvirtualByteMethodA") == 0)
        return probe_CallNonvirtualByteMethodA(env);
    if (strcmp(id, "call.CallNonvirtualByteMethodV") == 0)
        return probe_CallNonvirtualByteMethodV(env);
    if (strcmp(id, "call.CallNonvirtualShortMethod") == 0)
        return probe_CallNonvirtualShortMethod(env);
    if (strcmp(id, "call.CallNonvirtualShortMethodA") == 0)
        return probe_CallNonvirtualShortMethodA(env);
    if (strcmp(id, "call.CallNonvirtualShortMethodV") == 0)
        return probe_CallNonvirtualShortMethodV(env);
    if (strcmp(id, "call.CallNonvirtualCharMethod") == 0)
        return probe_CallNonvirtualCharMethod(env);
    if (strcmp(id, "call.CallNonvirtualCharMethodA") == 0)
        return probe_CallNonvirtualCharMethodA(env);
    if (strcmp(id, "call.CallNonvirtualCharMethodV") == 0)
        return probe_CallNonvirtualCharMethodV(env);
    if (strcmp(id, "call.CallNonvirtualIntMethod") == 0)
        return probe_CallNonvirtualIntMethod(env);
    if (strcmp(id, "call.CallNonvirtualIntMethodA") == 0)
        return probe_CallNonvirtualIntMethodA(env);
    if (strcmp(id, "call.CallNonvirtualIntMethodV") == 0)
        return probe_CallNonvirtualIntMethodV(env);
    if (strcmp(id, "call.CallNonvirtualLongMethod") == 0)
        return probe_CallNonvirtualLongMethod(env);
    if (strcmp(id, "call.CallNonvirtualLongMethodA") == 0)
        return probe_CallNonvirtualLongMethodA(env);
    if (strcmp(id, "call.CallNonvirtualLongMethodV") == 0)
        return probe_CallNonvirtualLongMethodV(env);
    if (strcmp(id, "call.CallNonvirtualFloatMethod") == 0)
        return probe_CallNonvirtualFloatMethod(env);
    if (strcmp(id, "call.CallNonvirtualFloatMethodA") == 0)
        return probe_CallNonvirtualFloatMethodA(env);
    if (strcmp(id, "call.CallNonvirtualFloatMethodV") == 0)
        return probe_CallNonvirtualFloatMethodV(env);
    if (strcmp(id, "call.CallNonvirtualDoubleMethod") == 0)
        return probe_CallNonvirtualDoubleMethod(env);
    if (strcmp(id, "call.CallNonvirtualDoubleMethodA") == 0)
        return probe_CallNonvirtualDoubleMethodA(env);
    if (strcmp(id, "call.CallNonvirtualDoubleMethodV") == 0)
        return probe_CallNonvirtualDoubleMethodV(env);
    if (strcmp(id, "call.CallNonvirtualObjectMethod") == 0)
        return probe_CallNonvirtualObjectMethod(env);
    if (strcmp(id, "call.CallNonvirtualObjectMethodA") == 0)
        return probe_CallNonvirtualObjectMethodA(env);
    if (strcmp(id, "call.CallNonvirtualObjectMethodV") == 0)
        return probe_CallNonvirtualObjectMethodV(env);
    if (strcmp(id, "call.CallNonvirtualVoidMethod") == 0)
        return probe_CallNonvirtualVoidMethod(env);
    if (strcmp(id, "call.CallNonvirtualVoidMethodA") == 0)
        return probe_CallNonvirtualVoidMethodA(env);
    if (strcmp(id, "call.CallNonvirtualVoidMethodV") == 0)
        return probe_CallNonvirtualVoidMethodV(env);
    if (strcmp(id, "call.CallNonvirtualBooleanMethod.takeOneOfEach") == 0)
        return probe_CallNonvirtualBooleanMethod_takeOneOfEach(env);
    if (strcmp(id, "call.CallNonvirtualBooleanMethodA.takeOneOfEach") == 0)
        return probe_CallNonvirtualBooleanMethodA_takeOneOfEach(env);
    if (strcmp(id, "call.CallNonvirtualBooleanMethodV.takeOneOfEach") == 0)
        return probe_CallNonvirtualBooleanMethodV_takeOneOfEach(
            env, 0.0, 1.0, (jlong)2, 3, 4, 5, 6, 1, (*env)->NewStringUTF(env, "biscuits"));
    if (strcmp(id, "call.CallNonvirtualBooleanMethod.takeCoolHandLuke") == 0)
        return probe_CallNonvirtualBooleanMethod_takeCoolHandLuke(env);
    if (strcmp(id, "call.CallNonvirtualBooleanMethodA.takeCoolHandLuke") == 0)
        return probe_CallNonvirtualBooleanMethodA_takeCoolHandLuke(env);
    if (strcmp(id, "call.CallNonvirtualBooleanMethodV.takeCoolHandLuke") == 0)
        return probe_CallNonvirtualBooleanMethodV_takeCoolHandLuke(
            env, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
            24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45,
            46, 47, 48, 49, 50);
    if (strcmp(id, "call.CallStaticBooleanMethod") == 0)
        return probe_CallStaticBooleanMethod(env);
    if (strcmp(id, "call.CallStaticBooleanMethodA") == 0)
        return probe_CallStaticBooleanMethodA(env);
    if (strcmp(id, "call.CallStaticBooleanMethodV") == 0)
        return probe_CallStaticBooleanMethodV(env);
    if (strcmp(id, "call.CallStaticByteMethod") == 0)
        return probe_CallStaticByteMethod(env);
    if (strcmp(id, "call.CallStaticByteMethodA") == 0)
        return probe_CallStaticByteMethodA(env);
    if (strcmp(id, "call.CallStaticByteMethodV") == 0)
        return probe_CallStaticByteMethodV(env);
    if (strcmp(id, "call.CallStaticShortMethod") == 0)
        return probe_CallStaticShortMethod(env);
    if (strcmp(id, "call.CallStaticShortMethodA") == 0)
        return probe_CallStaticShortMethodA(env);
    if (strcmp(id, "call.CallStaticShortMethodV") == 0)
        return probe_CallStaticShortMethodV(env);
    if (strcmp(id, "call.CallStaticCharMethod") == 0)
        return probe_CallStaticCharMethod(env);
    if (strcmp(id, "call.CallStaticCharMethodA") == 0)
        return probe_CallStaticCharMethodA(env);
    if (strcmp(id, "call.CallStaticCharMethodV") == 0)
        return probe_CallStaticCharMethodV(env);
    if (strcmp(id, "call.CallStaticIntMethod") == 0)
        return probe_CallStaticIntMethod(env);
    if (strcmp(id, "call.CallStaticIntMethodA") == 0)
        return probe_CallStaticIntMethodA(env);
    if (strcmp(id, "call.CallStaticIntMethodV") == 0)
        return probe_CallStaticIntMethodV(env);
    if (strcmp(id, "call.CallStaticLongMethod") == 0)
        return probe_CallStaticLongMethod(env);
    if (strcmp(id, "call.CallStaticLongMethodA") == 0)
        return probe_CallStaticLongMethodA(env);
    if (strcmp(id, "call.CallStaticLongMethodV") == 0)
        return probe_CallStaticLongMethodV(env);
    if (strcmp(id, "call.CallStaticFloatMethod") == 0)
        return probe_CallStaticFloatMethod(env);
    if (strcmp(id, "call.CallStaticFloatMethodA") == 0)
        return probe_CallStaticFloatMethodA(env);
    if (strcmp(id, "call.CallStaticFloatMethodV") == 0)
        return probe_CallStaticFloatMethodV(env);
    if (strcmp(id, "call.CallStaticDoubleMethod") == 0)
        return probe_CallStaticDoubleMethod(env);
    if (strcmp(id, "call.CallStaticDoubleMethodA") == 0)
        return probe_CallStaticDoubleMethodA(env);
    if (strcmp(id, "call.CallStaticDoubleMethodV") == 0)
        return probe_CallStaticDoubleMethodV(env);
    if (strcmp(id, "call.CallStaticObjectMethod") == 0)
        return probe_CallStaticObjectMethod(env);
    if (strcmp(id, "call.CallStaticObjectMethodA") == 0)
        return probe_CallStaticObjectMethodA(env);
    if (strcmp(id, "call.CallStaticObjectMethodV") == 0)
        return probe_CallStaticObjectMethodV(env);
    if (strcmp(id, "call.CallStaticVoidMethod") == 0)
        return probe_CallStaticVoidMethod(env);
    if (strcmp(id, "call.CallStaticVoidMethodA") == 0)
        return probe_CallStaticVoidMethodA(env);
    if (strcmp(id, "call.CallStaticVoidMethodV") == 0)
        return probe_CallStaticVoidMethodV(env);
    if (strcmp(id, "call.CallStaticBooleanMethod.takeOneOfEach") == 0)
        return probe_CallStaticBooleanMethod_takeOneOfEach(env);
    if (strcmp(id, "call.CallStaticBooleanMethodA.takeOneOfEach") == 0)
        return probe_CallStaticBooleanMethodA_takeOneOfEach(env);
    if (strcmp(id, "call.CallStaticBooleanMethodV.takeOneOfEach") == 0)
        return probe_CallStaticBooleanMethodV_takeOneOfEach(env, 0.0, 1.0, (jlong)2, 3, 4, 5, 6, 1,
                                                            (*env)->NewStringUTF(env, "biscuits"));
    if (strcmp(id, "call.CallStaticBooleanMethod.takeCoolHandLuke") == 0)
        return probe_CallStaticBooleanMethod_takeCoolHandLuke(env);
    if (strcmp(id, "call.CallStaticBooleanMethodA.takeCoolHandLuke") == 0)
        return probe_CallStaticBooleanMethodA_takeCoolHandLuke(env);
    if (strcmp(id, "call.CallStaticBooleanMethodV.takeCoolHandLuke") == 0)
        return probe_CallStaticBooleanMethodV_takeCoolHandLuke(
            env, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
            24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45,
            46, 47, 48, 49, 50);
    return failure(env, "unknown call case");
}
