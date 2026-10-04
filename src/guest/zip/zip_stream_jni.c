/* API19 byte-array Deflater/Inflater ABI. Algorithms execute in guest libz;
 * Java owns public semantics. Tokens never expose native addresses to Java. */
#include <jni.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <zlib.h>

typedef struct ZipStream {
    struct ZipStream *next;
    jlong token;
    int inflate, references, closed;
    pthread_mutex_t mutex;
    z_stream z;
    unsigned char *input;
} ZipStream;
static pthread_mutex_t registry_mutex = PTHREAD_MUTEX_INITIALIZER;
static ZipStream *streams;
static jlong next_token = 1;

static void fail(JNIEnv *env, const char *type, const char *message) {
    jclass cls = (*env)->FindClass(env, type);
    if (cls) {
        (*env)->ThrowNew(env, cls, message);
        (*env)->DeleteLocalRef(env, cls);
    }
}
static void zfailure(JNIEnv *env, int error, const char *type) {
    fail(env, error == Z_MEM_ERROR ? "java/lang/OutOfMemoryError" : type, zError(error));
}
static int range(JNIEnv *env, jbyteArray array, jint offset, jint count) {
    if (!array) {
        fail(env, "java/lang/NullPointerException", "array == null");
        return 0;
    }
    jsize length = (*env)->GetArrayLength(env, array);
    if ((*env)->ExceptionCheck(env)) return 0;
    if (offset < 0 || count < 0 || offset > length || count > length - offset) {
        fail(env, "java/lang/ArrayIndexOutOfBoundsException", "invalid ZIP buffer range");
        return 0;
    }
    return 1;
}
static void destroy(ZipStream *s) {
    if (s->inflate) inflateEnd(&s->z); else deflateEnd(&s->z);
    free(s->input);
    pthread_mutex_destroy(&s->mutex);
    free(s);
}
/* Registry references keep end/GC from freeing an executing native call. */
static ZipStream *acquire(JNIEnv *env, jlong token, int inflate) {
    pthread_mutex_lock(&registry_mutex);
    ZipStream *s = streams;
    while (s && s->token != token) s = s->next;
    if (s && s->inflate == inflate) ++s->references; else s = NULL;
    pthread_mutex_unlock(&registry_mutex);
    if (!s) {
        fail(env, "java/lang/IllegalStateException", "invalid or closed ZIP stream token");
        return NULL;
    }
    pthread_mutex_lock(&s->mutex);
    return s;
}
static void release(ZipStream **address) {
    ZipStream *s = *address;
    if (!s) return;
    pthread_mutex_unlock(&s->mutex);
    pthread_mutex_lock(&registry_mutex);
    int dispose = --s->references == 0 && s->closed;
    pthread_mutex_unlock(&registry_mutex);
    if (dispose) destroy(s);
}
#define STREAM(kind) ZipStream *s __attribute__((cleanup(release))) = acquire(env, token, kind); if (!s) return
static void close_stream(JNIEnv *env, jlong token, int kind, int strict) {
    pthread_mutex_lock(&registry_mutex);
    ZipStream **p = &streams;
    while (*p && (*p)->token != token) p = &(*p)->next;
    ZipStream *s = *p;
    if (s && (kind < 0 || s->inflate == kind)) {
        *p = s->next;
        s->closed = 1;
    } else s = NULL;
    int dispose = s && s->references == 0;
    pthread_mutex_unlock(&registry_mutex);
    if (dispose) destroy(s);
    if (!s && strict) fail(env, "java/lang/IllegalStateException", "invalid or closed ZIP stream token");
}
__attribute__((destructor)) static void release_streams(void) {
    /* The guest library is unloaded only after guest execution has stopped. */
    while (streams) close_stream(NULL, streams->token, -1, 0);
}
static jlong create(JNIEnv *env, int inflate, int level, int strategy, jboolean raw) {
    ZipStream *s = calloc(1, sizeof(*s));
    if (!s) { fail(env, "java/lang/OutOfMemoryError", "ZIP stream allocation"); return -1; }
    s->inflate = inflate;
    int error = inflate ? inflateInit2(&s->z, raw ? -15 : 15)
                        : deflateInit2(&s->z, level, Z_DEFLATED, raw ? -15 : 15, 8, strategy);
    if (error != Z_OK) {
        free(s);
        zfailure(env, error, "java/lang/IllegalArgumentException");
        return -1;
    }
    if (pthread_mutex_init(&s->mutex, NULL) != 0) {
        if (inflate) inflateEnd(&s->z); else deflateEnd(&s->z);
        free(s);
        fail(env, "java/lang/OutOfMemoryError", "ZIP stream mutex allocation");
        return -1;
    }
    pthread_mutex_lock(&registry_mutex);
    if (next_token == INT64_MAX) {
        pthread_mutex_unlock(&registry_mutex);
        destroy(s);
        fail(env, "java/lang/OutOfMemoryError", "ZIP stream token exhaustion");
        return -1;
    }
    s->token = next_token++;
    s->next = streams;
    streams = s;
    pthread_mutex_unlock(&registry_mutex);
    return s->token;
}
static void set_input(JNIEnv *env, jbyteArray array, jint offset, jint count, jlong token, int kind) {
    if (!range(env, array, offset, count)) return;
    STREAM(kind);
    unsigned char *input = malloc(count ? (size_t)count : 1);
    if (!input) { fail(env, "java/lang/OutOfMemoryError", "ZIP input allocation"); return; }
    (*env)->GetByteArrayRegion(env, array, offset, count, (jbyte *)input);
    if ((*env)->ExceptionCheck(env)) { free(input); return; }
    free(s->input);
    s->input = input;
    s->z.next_in = input;
    s->z.avail_in = (uInt)count;
}
static void dictionary(JNIEnv *env, jbyteArray array, jint offset, jint count, jlong token, int kind) {
    if (!range(env, array, offset, count)) return;
    STREAM(kind);
    unsigned char *bytes = malloc(count ? (size_t)count : 1);
    if (!bytes) { fail(env, "java/lang/OutOfMemoryError", "ZIP dictionary allocation"); return; }
    (*env)->GetByteArrayRegion(env, array, offset, count, (jbyte *)bytes);
    if (!(*env)->ExceptionCheck(env)) {
        int error = kind ? inflateSetDictionary(&s->z, bytes, (uInt)count)
                         : deflateSetDictionary(&s->z, bytes, (uInt)count);
        if (error != Z_OK) zfailure(env, error, "java/lang/IllegalArgumentException");
    }
    free(bytes);
}
static jint process(JNIEnv *env, jobject self, jbyteArray array, jint offset, jint count,
                    jlong token, int kind, int flush) {
    if (!range(env, array, offset, count)) return 0;
    if (!kind && flush != Z_NO_FLUSH && flush != Z_SYNC_FLUSH && flush != Z_FULL_FLUSH && flush != Z_FINISH) {
        fail(env, "java/lang/IllegalArgumentException", "invalid ZIP flush mode"); return 0;
    }
    ZipStream *s __attribute__((cleanup(release))) = acquire(env, token, kind);
    if (!s) return 0;
    /* Match API19 JniConstants: fields belong to the base class even if a
     * subclass declares private fields with the same names. */
    jclass cls = (*env)->FindClass(env, kind ? "java/util/zip/Inflater" : "java/util/zip/Deflater");
    if (!cls) return 0;
    jfieldID in_read = (*env)->GetFieldID(env, cls, "inRead", "I");
    jfieldID finished = (*env)->GetFieldID(env, cls, "finished", "Z");
    jfieldID needs_dict = kind ? (*env)->GetFieldID(env, cls, "needsDictionary", "Z") : NULL;
    (*env)->DeleteLocalRef(env, cls);
    if ((*env)->ExceptionCheck(env)) return 0;
    jint previous = (*env)->GetIntField(env, self, in_read);
    if ((*env)->ExceptionCheck(env)) return 0;
    uInt available = s->z.avail_in;
    jint written = 0;
    int error = Z_OK;
    unsigned char buffer[65536];
    /* Bounded JNI scratch; preserve one Java output range across zlib chunks. */
    do {
        uInt capacity = (uInt)(count - written);
        if (capacity > sizeof(buffer)) capacity = sizeof(buffer);
        s->z.next_out = buffer;
        s->z.avail_out = capacity;
        error = kind ? inflate(&s->z, Z_SYNC_FLUSH) : deflate(&s->z, flush);
        uInt produced = capacity - s->z.avail_out;
        s->z.next_out = NULL;
        s->z.avail_out = 0;
        if (error != Z_OK && error != Z_STREAM_END && error != Z_BUF_ERROR && !(kind && error == Z_NEED_DICT)) {
            zfailure(env, error, "java/util/zip/DataFormatException"); return 0;
        }
        (*env)->SetByteArrayRegion(env, array, offset + written, (jsize)produced, (const jbyte *)buffer);
        if ((*env)->ExceptionCheck(env)) return 0;
        written += (jint)produced;
        if (error != Z_OK || produced < capacity || capacity == 0) break;
    } while (written < count);
    (*env)->SetIntField(env, self, in_read, previous + (jint)(available - s->z.avail_in));
    if ((*env)->ExceptionCheck(env)) return 0;
    if (error == Z_STREAM_END) (*env)->SetBooleanField(env, self, finished, JNI_TRUE);
    if (kind && error == Z_NEED_DICT) (*env)->SetBooleanField(env, self, needs_dict, JNI_TRUE);
    return written;
}

#define DEFLATER(name) Java_java_util_zip_Deflater_##name
#define INFLATER(name) Java_java_util_zip_Inflater_##name
JNIEXPORT jlong JNICALL DEFLATER(createStream)(JNIEnv *env, jobject self, jint level, jint strategy, jboolean raw) {
    (void)self; return create(env, 0, level, strategy, raw);
}
JNIEXPORT jlong JNICALL INFLATER(createStream)(JNIEnv *env, jobject self, jboolean raw) {
    (void)self; return create(env, 1, 0, 0, raw);
}
JNIEXPORT jint JNICALL DEFLATER(deflateImpl)(JNIEnv *env, jobject self, jbyteArray array,
        jint offset, jint count, jlong token, jint flush) {
    return process(env, self, array, offset, count, token, 0, flush);
}
JNIEXPORT jint JNICALL INFLATER(inflateImpl)(JNIEnv *env, jobject self, jbyteArray array,
        jint offset, jint count, jlong token) {
    return process(env, self, array, offset, count, token, 1, Z_SYNC_FLUSH);
}
#define COMMON(owner, kind) \
JNIEXPORT void JNICALL owner(setInputImpl)(JNIEnv *env, jobject self, jbyteArray array, jint off, jint n, jlong token) { \
    (void)self; set_input(env, array, off, n, token, kind); \
} \
JNIEXPORT void JNICALL owner(setDictionaryImpl)(JNIEnv *env, jobject self, jbyteArray array, jint off, jint n, jlong token) { \
    (void)self; dictionary(env, array, off, n, token, kind); \
} \
JNIEXPORT void JNICALL owner(endImpl)(JNIEnv *env, jobject self, jlong token) { \
    (void)self; close_stream(env, token, kind, 1); \
} \
JNIEXPORT void JNICALL owner(resetImpl)(JNIEnv *env, jobject self, jlong token) { \
    (void)self; STREAM(kind); \
    int error = kind ? inflateReset(&s->z) : deflateReset(&s->z); \
    if (error != Z_OK) zfailure(env, error, "java/lang/IllegalArgumentException"); \
    else { free(s->input); s->input = NULL; s->z.next_in = NULL; s->z.avail_in = 0; } \
} \
JNIEXPORT jint JNICALL owner(getAdlerImpl)(JNIEnv *env, jobject self, jlong token) { \
    (void)self; ZipStream *s __attribute__((cleanup(release))) = acquire(env, token, kind); \
    return s ? (jint)s->z.adler : 0; \
} \
JNIEXPORT jlong JNICALL owner(getTotalInImpl)(JNIEnv *env, jobject self, jlong token) { \
    (void)self; ZipStream *s __attribute__((cleanup(release))) = acquire(env, token, kind); \
    return s ? (jlong)s->z.total_in : 0; \
} \
JNIEXPORT jlong JNICALL owner(getTotalOutImpl)(JNIEnv *env, jobject self, jlong token) { \
    (void)self; ZipStream *s __attribute__((cleanup(release))) = acquire(env, token, kind); \
    return s ? (jlong)s->z.total_out : 0; \
}
COMMON(DEFLATER, 0)
COMMON(INFLATER, 1)
JNIEXPORT void JNICALL DEFLATER(setLevelsImpl)(JNIEnv *env, jobject self, jint level, jint strategy, jlong token) {
    (void)self; STREAM(0);
    s->z.next_out = NULL;
    s->z.avail_out = 0;
    int error = deflateParams(&s->z, level, strategy);
    if (error != Z_OK) zfailure(env, error, "java/lang/IllegalStateException");
}
JNIEXPORT void JNICALL Java_org_ogplay_zip_NativeZip_release(JNIEnv *env, jclass cls, jlong token) {
    (void)cls; close_stream(env, token, -1, 0);
}
