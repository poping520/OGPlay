#include "probe_support.h"

#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define COUNT(values) (sizeof(values) / sizeof((values)[0]))

JNIEXPORT jlongArray JNICALL Java_org_ogplay_probe_NativeProbe_processIds(JNIEnv* env, jclass type) {
    (void)type;
    jlong result[] = {getpid(), gettid(), sysconf(_SC_PAGESIZE)};
    return probe_longs(env, result, COUNT(result));
}

struct Count {
    int value;
    int lock_error;
    pthread_mutex_t mutex;
};

static void* count_worker(void* opaque) {
    struct Count* count = opaque;
    for (int index = 0; index < 53; index++) {
        count->lock_error = pthread_mutex_lock(&count->mutex);
        if (count->lock_error) return NULL;
        count->value++;
        count->lock_error = pthread_mutex_unlock(&count->mutex);
        if (count->lock_error) return NULL;
    }
    return count;
}

JNIEXPORT jlongArray JNICALL Java_org_ogplay_probe_NativeProbe_threads(JNIEnv* env, jclass type) {
    (void)type;
    pthread_t child;
    struct Count* count = calloc(1, sizeof(*count));
    if (count == NULL) {
        const jlong failed[] = {ENOMEM, -1, -1, -1, 0, 0};
        return probe_longs(env, failed, COUNT(failed));
    }
    int initialized = pthread_mutex_init(&count->mutex, NULL);
    int created = -1;
    int joined = -1;
    void* returned = NULL;
    if (initialized == 0) {
        created = pthread_create(&child, NULL, count_worker, count);
        if (created == 0) {
            for (int i = 0; i < 81; i++) {
                int error = pthread_mutex_lock(&count->mutex);
                if (error != 0) break;
                count->value++;
                if (pthread_mutex_unlock(&count->mutex) != 0) break;
            }
            joined = pthread_join(child, &returned);
            if (joined != 0) {
                // Without a join, the child may still reference count and its mutex.
                const jlong failed[] = {0, 0, joined, -1, 0, 0};
                return probe_longs(env, failed, COUNT(failed));
            }
        }
        pthread_mutex_destroy(&count->mutex);
    }
    jlong result[] = {initialized, created, joined, count->lock_error, count->value, returned == count};
    free(count);
    return probe_longs(env, result, COUNT(result));
}

JNIEXPORT jlongArray JNICALL Java_org_ogplay_probe_NativeProbe_condWait(JNIEnv* env, jclass type) {
    (void)type;
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
    int outcome = pthread_mutex_lock(&mutex);
    int64_t before = probe_clock(CLOCK_MONOTONIC);
    int64_t realtime_before = -1;
    int64_t deadline_ns = -1;
    int64_t realtime_at_return = -1;
    int64_t monotonic_at_return = -1;
    int zero_returns = 0;
    if (outcome == 0) {
        struct timespec deadline;
        if (clock_gettime(CLOCK_REALTIME, &deadline) == 0) {
            realtime_before = (int64_t)deadline.tv_sec * 1000000000 + deadline.tv_nsec;
            deadline.tv_nsec += 120000000;
            if (deadline.tv_nsec >= 1000000000) {
                deadline.tv_sec++;
                deadline.tv_nsec -= 1000000000;
            }
            deadline_ns = (int64_t)deadline.tv_sec * 1000000000 + deadline.tv_nsec;
            while (zero_returns < 8) {
                outcome = pthread_cond_timedwait(&cond, &mutex, &deadline);
                if (outcome != 0) break;
                zero_returns++;
            }
            monotonic_at_return = probe_clock(CLOCK_MONOTONIC);
            realtime_at_return = probe_clock(CLOCK_REALTIME);
        } else {
            outcome = errno;
        }
        pthread_mutex_unlock(&mutex);
    }
    int64_t after = probe_clock(CLOCK_MONOTONIC);
    pthread_cond_destroy(&cond);
    pthread_mutex_destroy(&mutex);
    jlong result[] = {
        outcome,     ETIMEDOUT, before, after, realtime_before, deadline_ns, realtime_at_return, monotonic_at_return,
        zero_returns};
    return probe_longs(env, result, COUNT(result));
}

JNIEXPORT jlongArray JNICALL Java_org_ogplay_probe_NativeProbe_sleep(JNIEnv* env, jclass type) {
    (void)type;
    struct timespec delay = {.tv_sec = 0, .tv_nsec = 120000000};
    int64_t before = probe_clock(CLOCK_MONOTONIC);
    int rc = nanosleep(&delay, NULL);
    int error = rc == 0 ? 0 : errno;
    int64_t after = probe_clock(CLOCK_MONOTONIC);
    jlong result[] = {rc, error, before, after};
    return probe_longs(env, result, COUNT(result));
}

JNIEXPORT jlongArray JNICALL Java_org_ogplay_probe_NativeProbe_memory(JNIEnv* env, jclass type) {
    (void)type;
    size_t length = 4096;
    volatile unsigned char* map = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    jlong fields[] = {map != MAP_FAILED, 0, 0, -1, -1, -1, 0, -1};
    if (map == MAP_FAILED) {
        fields[1] = errno;
        return probe_longs(env, fields, COUNT(fields));
    }

    fields[2] = map[17] == 0;
    map[17] = 19;
    fields[3] = mprotect((void*)map, length, PROT_READ) == 0 ? 0 : errno;
    if (fields[3] == 0) fields[4] = map[17];
    fields[5] = mprotect((void*)map, length, PROT_READ | PROT_WRITE) == 0 ? 0 : errno;
    if (fields[5] == 0) {
        map[17] = 29;
        fields[6] = map[17];
    }
    fields[7] = munmap((void*)map, length) == 0 ? 0 : errno;
    return probe_longs(env, fields, COUNT(fields));
}

JNIEXPORT jlongArray JNICALL Java_org_ogplay_probe_NativeProbe_maps(JNIEnv* env, jclass type, jstring library_dir) {
    (void)type;
    char path[1024];
    const char* directory = (*env)->GetStringUTFChars(env, library_dir, NULL);
    if (directory == NULL) return NULL;
    const int length = snprintf(path, sizeof(path), "%s/libogprobe.so", directory);
    (*env)->ReleaseStringUTFChars(env, library_dir, directory);

    Dl_info info = {0};
    const jlong same_path = length > 0 && (size_t)length < sizeof(path) &&
                            dladdr((const void*)&Java_org_ogplay_probe_NativeProbe_maps, &info) != 0 &&
                            info.dli_fname != NULL && strcmp(path, info.dli_fname) == 0;
    FILE* file = fopen("/proc/self/maps", "r");
    if (file == NULL) {
        const jlong failed[] = {0, errno, 0, 0, same_path};
        return probe_longs(env, failed, COUNT(failed));
    }
    char line[1024];
    jlong lines = 0;
    jlong found = 0;
    while (fgets(line, sizeof(line), file) != NULL) {
        lines++;
        if (length > 0 && (size_t)length < sizeof(path) && strstr(line, path) != NULL) {
            found = 1;
            break;
        }
    }
    jlong result[] = {1, ferror(file) ? errno : 0, lines, found, same_path};
    fclose(file);
    return probe_longs(env, result, COUNT(result));
}

JNIEXPORT jlongArray JNICALL Java_org_ogplay_probe_NativeProbe_readCpuInfo(JNIEnv* env, jclass type) {
    (void)type;
    FILE* file = fopen("/proc/cpuinfo", "r");
    if (file == NULL) {
        jlong failed[] = {0, 0, errno};
        return probe_longs(env, failed, COUNT(failed));
    }
    uint32_t hash = UINT32_C(2166136261);
    unsigned char bytes[256];
    size_t read_count;
    size_t total = 0;
    while ((read_count = fread(bytes, 1, sizeof(bytes), file)) != 0 && total <= 1024 * 1024) {
        for (size_t i = 0; i < read_count; i++)
            hash = (hash ^ bytes[i]) * UINT32_C(16777619);
        total += read_count;
    }
    int error = ferror(file);
    fclose(file);
    jlong result[] = {(jlong)total, hash, error};
    return probe_longs(env, result, COUNT(result));
}

JNIEXPORT jstring JNICALL Java_org_ogplay_probe_NativeProbe_environmentValue(JNIEnv* env, jclass type) {
    (void)type;
    const char* value = getenv("ANDROID_ROOT");
    return value == NULL ? NULL : (*env)->NewStringUTF(env, value);
}

JNIEXPORT jint JNICALL Java_org_ogplay_probe_NativeProbe_jump(JNIEnv* env, jclass type) {
    (void)env;
    (void)type;
    jmp_buf point;
    int code = setjmp(point);
    if (code == 0) longjmp(point, 19);
    return code;
}

JNIEXPORT jlongArray JNICALL Java_org_ogplay_probe_NativeProbe_dynamicLibraries(JNIEnv* env, jclass type) {
    (void)type;
    void* graphics = dlopen("libGLESv2.so", RTLD_NOW | RTLD_LOCAL);
    void* clear = graphics == NULL ? NULL : dlsym(graphics, "glClear");
    dlerror();
    void* missing = dlopen("lib_ogplay_probe_fixture_missing.so", RTLD_NOW | RTLD_LOCAL);
    const char* last_error = dlerror();
    jlong has_error = last_error != NULL && last_error[0] != '\0';
    void* self = dlopen(NULL, RTLD_NOW);
    jlong result[] = {graphics != NULL, clear != NULL, missing == NULL, has_error, self != NULL};
    if (self != NULL) dlclose(self);
    if (missing != NULL) dlclose(missing);
    if (graphics != NULL) dlclose(graphics);
    return probe_longs(env, result, COUNT(result));
}

JNIEXPORT jlongArray JNICALL Java_org_ogplay_probe_NativeProbe_tryFork(JNIEnv* env, jclass type) {
    (void)type;
    errno = 0;
    pid_t child = fork();
    if (child == 0) _exit(0);
    jlong result[] = {child, child == -1 ? errno : 0};
    return probe_longs(env, result, COUNT(result));
}
