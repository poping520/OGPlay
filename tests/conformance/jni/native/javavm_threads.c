/* SPDX-License-Identifier: Apache-2.0
 * Standalone Dalvik JNI probes; pthread shape also checked against ART
 * test/JniTest/jni_test.cc (d5e4ac0). Oracles are Dalvik vm/Jni.cpp.
 */
#include "probe.h"
#include <pthread.h>
#include <stdint.h>

typedef struct {
    int mode;
    const char *error;
} ThreadProbe;

static const char *thread_identity(JNIEnv *env, int named, int daemon) {
    /* Jni.cpp:2813-2838 passes name/daemon into Thread.cpp:1867-1899;
     * libdvm Thread.java:355-375 supplies Thread-<id> for a NULL name. */
    jclass cls = (*env)->FindClass(env, "java/lang/Thread");
    if (!cls)
        return "Thread class unavailable";
    jmethodID current =
        (*env)->GetStaticMethodID(env, cls, "currentThread", "()Ljava/lang/Thread;");
    jmethodID get_name = (*env)->GetMethodID(env, cls, "getName", "()Ljava/lang/String;");
    jmethodID is_daemon = (*env)->GetMethodID(env, cls, "isDaemon", "()Z");
    if (!current || !get_name || !is_daemon)
        return "Thread identity methods unavailable";
    jobject thread = (*env)->CallStaticObjectMethod(env, cls, current);
    if ((*env)->ExceptionCheck(env) || !thread)
        return "currentThread callback failed";
    jstring name = (*env)->CallObjectMethod(env, thread, get_name);
    if ((*env)->ExceptionCheck(env) || !name)
        return "getName callback failed";
    const char *utf = (*env)->GetStringUTFChars(env, name, NULL);
    if (!utf)
        return "getName UTF conversion failed";
    int correct_name = named ? strcmp(utf, "OGPlayJNIProbe") == 0
                             : strncmp(utf, "Thread-", 7) == 0 && utf[7] != '\0';
    (*env)->ReleaseStringUTFChars(env, name, utf);
    jboolean actual_daemon = (*env)->CallBooleanMethod(env, thread, is_daemon);
    if ((*env)->ExceptionCheck(env))
        return "isDaemon callback failed";
    if (!correct_name)
        return "attached thread name mismatch";
    return actual_daemon == (daemon ? JNI_TRUE : JNI_FALSE) ? NULL
                                                            : "attached daemon flag mismatch";
}

static void *worker(void *arg) {
    ThreadProbe *probe = arg;
    JavaVM *vm = probe_vm();
    JNIEnv *env = (JNIEnv *)(uintptr_t)1;
    /* Jni.cpp:2904-2925: detached means EDETACHED and a NULL output pointer. */
    jint initial_status = (*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6);
    if (initial_status != JNI_EDETACHED || env != NULL) {
        probe->error = "unattached GetEnv expected EDETACHED and NULL";
        if (initial_status == JNI_OK && env)
            (*vm)->DetachCurrentThread(vm);
        return NULL;
    }
    if (probe->mode == 0)
        return NULL;
    JavaVMAttachArgs args = {JNI_VERSION_1_6, "OGPlayJNIProbe", NULL};
    /* Jni.cpp:2785-2790,2813-2849; NULL args and explicit name both defined. */
    jint status = probe->mode == 3
                      ? (*vm)->AttachCurrentThreadAsDaemon(vm, &env, NULL)
                      : (*vm)->AttachCurrentThread(vm, &env, probe->mode == 2 ? &args : NULL);
    if (status != JNI_OK || !env) {
        probe->error = "AttachCurrentThread failed";
        return NULL;
    }
    if (probe->mode == 4) {
        JNIEnv *again = NULL;
        /* Jni.cpp:2785-2790: re-attach returns immediately, preserving env and daemon status. */
        status = (*vm)->AttachCurrentThread(vm, &again, NULL);
        if (status != JNI_OK || again != env)
            probe->error = "repeat attach changed env";
        status = (*vm)->AttachCurrentThreadAsDaemon(vm, &again, NULL);
        if (status != JNI_OK || again != env)
            probe->error = "repeat daemon attach changed env";
    }
    if (!probe->error)
        probe->error = thread_identity(env, probe->mode == 2, probe->mode == 3);
    if (!probe->error && probe->mode == 6) {
        /* Jni.cpp:1225-1247: NativeStart frame chooses system loader; run-dex is -cp style.
         * APK process system loader is NOT the application's loader. */
        jclass app = (*env)->FindClass(env, "probe/JavaVmCases");
        if (!app)
            probe->error = "attached FindClass could not find -cp application class";
        else {
            jmethodID m = (*env)->GetStaticMethodID(env, app, "appMarker", "()Z");
            if (!m || !(*env)->CallStaticBooleanMethod(env, app, m))
                probe->error = "attached application class callback failed";
        }
    }
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        if (!probe->error)
            probe->error = "attached worker pending exception";
    }
    /* Jni.cpp:2880-2893: fully detach before joining; never leave non-daemon workers alive. */
    if ((*vm)->DetachCurrentThread(vm) != JNI_OK)
        probe->error = "DetachCurrentThread failed";
    env = (JNIEnv *)(uintptr_t)1;
    if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_EDETACHED || env != NULL)
        probe->error = "GetEnv after detach expected EDETACHED and NULL";
    return NULL;
}

JNIEXPORT jstring JNICALL Java_probe_JavaVmCases_probe(JNIEnv *env, jclass unused, jint mode) {
    JavaVM *vm = probe_vm();
    if (mode == 7) {
        /* Jni.cpp:2555-2558 returns the same VM recorded by JNI_OnLoad. */
        JavaVM *actual = NULL;
        return (*env)->GetJavaVM(env, &actual) == JNI_OK && actual == vm
                   ? NULL
                   : failure(env, "GetJavaVM identity differs from JNI_OnLoad");
    }
    if (mode == 8) {
        /* Jni.cpp:2880-2891 -> Thread.cpp:2013-2029: Java frames cause abort.
         * This is an isolated expected-death probe, not a return-code assumption. */
        (void)(*vm)->DetachCurrentThread(vm);
        return failure(env, "Java-thread detach returned instead of aborting");
    }
    ThreadProbe probe = {mode, NULL};
    pthread_t thread;
    if (pthread_create(&thread, NULL, worker, &probe) != 0)
        return failure(env, "pthread_create failed");
    if (pthread_join(thread, NULL) != 0)
        return failure(env, "pthread_join failed");
    return probe.error ? failure(env, probe.error) : NULL;
}
