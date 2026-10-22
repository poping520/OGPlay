#pragma once
#include <jni.h>
#include <string.h>
jstring failure(JNIEnv *env, const char *message);
jboolean text_equal(JNIEnv *env, jstring value, const char *expected);
jstring run_call(JNIEnv *env, const char *id);
int register_BoundaryStatic(JNIEnv *env);
int register_BoundaryInstance(JNIEnv *env);
jstring run_version(JNIEnv *env, const char *id);
