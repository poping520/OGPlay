/* API 19 CRC32 and Adler32 native boundary. Java owns the accumulator; guest zlib computes it. */
#include <jni.h>
#include <stdint.h>
#include <zlib.h>

static void fail(JNIEnv* env, const char* type, const char* message) {
    jclass cls = (*env)->FindClass(env, type);
    if (cls) {
        (*env)->ThrowNew(env, cls, message);
        (*env)->DeleteLocalRef(env, cls);
    }
}

typedef uLong (*ChecksumUpdate)(uLong, const Bytef*, uInt);

static jlong update_array(JNIEnv* env, jbyteArray array, jint offset, jint count, jlong previous,
                          ChecksumUpdate update) {
    if (!array) {
        fail(env, "java/lang/NullPointerException", "array == null");
        return 0; /* Pending exception, never a successful checksum. */
    }
    jsize length = (*env)->GetArrayLength(env, array);
    if ((*env)->ExceptionCheck(env)) return 0;
    if (offset < 0 || count < 0 || offset > length || count > length - offset) {
        fail(env, "java/lang/ArrayIndexOutOfBoundsException", "invalid checksum range");
        return 0;
    }
    uint32_t value = (uint32_t)previous;
    jbyte buffer[4096];
    while (count > 0) {
        jint chunk = count < (jint)sizeof(buffer) ? count : (jint)sizeof(buffer);
        (*env)->GetByteArrayRegion(env, array, offset, chunk, buffer);
        if ((*env)->ExceptionCheck(env)) return 0;
        value = (uint32_t)update(value, (const Bytef*)buffer, (uInt)chunk);
        offset += chunk;
        count -= chunk;
    }
    /* zlib interprets a null buffer as initialization, including for empty input. */
    return (jlong)value;
}

JNIEXPORT jlong JNICALL Java_java_util_zip_CRC32_updateImpl(JNIEnv* env, jobject self, jbyteArray array, jint offset,
                                                            jint count, jlong previous) {
    (void)self;
    return update_array(env, array, offset, count, previous, crc32);
}

JNIEXPORT jlong JNICALL Java_java_util_zip_CRC32_updateByteImpl(JNIEnv* env, jobject self, jbyte byte, jlong previous) {
    (void)env;
    (void)self;
    const Bytef value = (Bytef)byte;
    return (jlong)(uint32_t)crc32((uint32_t)previous, &value, 1);
}

JNIEXPORT jlong JNICALL Java_java_util_zip_Adler32_updateImpl(JNIEnv* env, jobject self, jbyteArray array, jint offset,
                                                              jint count, jlong previous) {
    (void)self;
    return update_array(env, array, offset, count, previous, adler32);
}

JNIEXPORT jlong JNICALL Java_java_util_zip_Adler32_updateByteImpl(JNIEnv* env, jobject self, jint byte,
                                                                  jlong previous) {
    (void)env;
    (void)self;
    const Bytef value = (Bytef)byte;
    return (jlong)(uint32_t)adler32((uint32_t)previous, &value, 1);
}
