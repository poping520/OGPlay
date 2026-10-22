/* SPDX-License-Identifier: Apache-2.0
 * Standalone Dalvik JNI probes. Oracles cite vm/Jni.cpp (Android 4.4).
 */
#include "probe.h"
JNIEXPORT jobject JNICALL Java_probe_ReflectionCases_to(JNIEnv *env, jclass unused, jint mode,
                                                        jclass type) {
    /* Jni.cpp:1307-1327: reflection objects derive from the member ID; static flag passed
     * accurately. */
    if (mode < 3) {
        jmethodID m = mode == 1 ? (*env)->GetStaticMethodID(env, type, "twice", "(I)I")
                                : (*env)->GetMethodID(env, type, mode == 2 ? "<init>" : "add",
                                                      mode == 2 ? "(I)V" : "(I)I");
        return m ? (*env)->ToReflectedMethod(env, type, m, mode == 1) : NULL;
    }
    jfieldID f = mode == 4 ? (*env)->GetStaticFieldID(env, type, "shared", "I")
                           : (*env)->GetFieldID(env, type, "number", "I");
    return f ? (*env)->ToReflectedField(env, type, f, mode == 4) : NULL;
}
JNIEXPORT jobject JNICALL Java_probe_ReflectionCases_fromConstructor(JNIEnv *env, jclass unused,
                                                                     jobject reflected,
                                                                     jclass type) {
    /* Jni.cpp:1285-1288,1565-1578: Constructor reflection resolves the same constructor ID. */
    jmethodID m = (*env)->FromReflectedMethod(env, reflected);
    jvalue args[] = {{.i = 73}};
    return (*env)->NewObjectA(env, type, m, args);
}
JNIEXPORT jstring JNICALL Java_probe_ReflectionCases_from(JNIEnv *env, jclass unused, jint mode,
                                                          jobject reflected, jobject obj,
                                                          jclass type) {
    if (mode < 2) {
        /* Jni.cpp:1285-1288: Method -> ID; dispatch according to its actual static flag. */
        jmethodID m = (*env)->FromReflectedMethod(env, reflected);
        jint result = mode == 1 ? (*env)->CallStaticIntMethod(env, type, m, 9)
                                : (*env)->CallIntMethod(env, obj, m, 9);
        return !(*env)->ExceptionCheck(env) && result == (mode == 1 ? 18 : 16)
                   ? NULL
                   : failure(env, "reflected method invocation");
    }
    /* Jni.cpp:1294-1297: Field -> ID; separate static and instance access. */
    jfieldID f = (*env)->FromReflectedField(env, reflected);
    jint before =
        mode == 3 ? (*env)->GetStaticIntField(env, type, f) : (*env)->GetIntField(env, obj, f);
    if (mode == 3)
        (*env)->SetStaticIntField(env, type, f, 41);
    else
        (*env)->SetIntField(env, obj, f, 41);
    return !(*env)->ExceptionCheck(env) && before == (mode == 3 ? 11 : 7)
               ? NULL
               : failure(env, "reflected field access");
}
