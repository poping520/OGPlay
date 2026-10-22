/* SPDX-License-Identifier: Apache-2.0
 * Standalone Dalvik JNI probes. Oracles cite vm/Jni.cpp (Android 4.4).
 */
#include "probe.h"
JNIEXPORT jstring JNICALL Java_probe_ReferencesCases_probe(JNIEnv *env, jclass unused, jint mode,
                                                           jobject obj) {
    if (mode == 0 || mode == 1) {
        /* Jni.cpp:1449-1460,2683-2685: global identity/type; delete while Java still owns obj. */
        jobject g = (*env)->NewGlobalRef(env, obj);
        int ok = g && (*env)->IsSameObject(env, g, obj);
        (*env)->DeleteGlobalRef(env, g);
        return ok && (*env)->IsSameObject(env, obj, obj) ? NULL
                                                         : failure(env, "global identity/type");
    }
    if (mode == 2 || mode == 3) {
        /* Jni.cpp:1467-1478: fresh local identity; deleting it leaves original reference valid. */
        jobject local = (*env)->NewLocalRef(env, obj);
        int ok = local && (*env)->IsSameObject(env, local, obj);
        (*env)->DeleteLocalRef(env, local);
        return ok && (*env)->IsSameObject(env, obj, obj) ? NULL
                                                         : failure(env, "local identity/type");
    }
    if (mode == 4 || mode == 5) {
        /* Jni.cpp:2651-2663: weak global creation/deletion; strong Java obj prevents clearing. */
        jweak weak = (*env)->NewWeakGlobalRef(env, obj);
        int ok =
            weak && !(*env)->IsSameObject(env, weak, NULL) && (*env)->IsSameObject(env, weak, obj);
        (*env)->DeleteWeakGlobalRef(env, weak);
        return ok ? NULL : failure(env, "reachable weak global identity/type");
    }
    if (mode == 6) {
        /* Jni.cpp:2683-2685: type observations are a separate probe so missing
         * GetObjectRefType cannot hide creation/deletion semantics. */
        jobject local = (*env)->NewLocalRef(env, obj);
        jobject global = (*env)->NewGlobalRef(env, obj);
        jweak weak = (*env)->NewWeakGlobalRef(env, obj);
        int ok = (*env)->GetObjectRefType(env, local) == JNILocalRefType &&
                 (*env)->GetObjectRefType(env, global) == JNIGlobalRefType &&
                 (*env)->GetObjectRefType(env, weak) == JNIWeakGlobalRefType;
        (*env)->DeleteLocalRef(env, local);
        (*env)->DeleteGlobalRef(env, global);
        (*env)->DeleteWeakGlobalRef(env, weak);
        return ok ? NULL : failure(env, "local/global/weak reference type mismatch");
    }
    if (mode == 7 || mode == 8) {
        /* Jni.cpp:409-412,1485-1491; Thread.h:74 kJniLocalRefMax=512. No allocation overflow. */
        jint rc = (*env)->EnsureLocalCapacity(env, mode == 7 ? 16 : 513);
        if (mode == 8)
            return rc == JNI_ERR ? expect_exception(env, "java/lang/OutOfMemoryError")
                                 : failure(env, "capacity above 512 accepted");
        return rc == JNI_OK ? NULL : failure(env, "EnsureLocalCapacity(16)");
    }
    if (mode == 9 || mode == 10 || mode == 11 || mode == 12) {
        /* Jni.cpp:1418-1428: rejecting capacity limit throws OOME without pushing a frame. */
        jint rc = (*env)->PushLocalFrame(env, mode == 11 ? 513 : 16);
        if (mode == 11) {
            if (rc == JNI_OK) {
                (*env)->PopLocalFrame(env, NULL);
                return failure(env, "oversize frame accepted");
            }
            return expect_exception(env, "java/lang/OutOfMemoryError");
        }
        if (rc != JNI_OK)
            return failure(env, "PushLocalFrame(16)");
        jobject result = (*env)->NewLocalRef(env, obj);
        jobject discarded = (*env)->NewStringUTF(env, "frame-only");
        /* Jni.cpp:1435-1443: transfer result into the parent frame. */
        jobject outer = (*env)->PopLocalFrame(env, result);
        if (mode == 12) {
            /* Jni.cpp:663-678 invokes decode at 324-329: deleted local -> ReportJniError.
             * Isolated death probe; never dereference this invalid handle ourselves. */
            (void)(*env)->GetObjectRefType(env, discarded);
            return failure(env, "GetObjectRefType on popped local returned instead of aborting");
        }
        return outer && (*env)->IsSameObject(env, outer, obj)
                   ? NULL
                   : failure(env, "PopLocalFrame result was not transferred");
    }
    return failure(env, "unknown reference case");
}
