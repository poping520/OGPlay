#include <fcntl.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct worker_context {
    uint8_t* bytes;
    size_t size;
    int result;
};

static void* worker_entry(void* opaque) {
    struct worker_context* context = (struct worker_context*)opaque;
    uint8_t* scratch = (uint8_t*)malloc(context->size);
    if (scratch == NULL) {
        context->result = -1;
        return NULL;
    }
    for (size_t index = 0; index < context->size; ++index) {
        scratch[index] = (uint8_t)(0x31U + index * 7U);
    }
    memcpy(context->bytes, scratch, context->size);
    free(scratch);
    context->result = 0;
    return NULL;
}

__attribute__((visibility("default")))
int ogplay_m2_entry(const char* path) {
    if (path == NULL) return -1;
    const size_t size = 32;
    uint8_t* bytes = (uint8_t*)malloc(size);
    uint8_t* actual = (uint8_t*)malloc(size);
    if (bytes == NULL || actual == NULL) {
        free(actual);
        free(bytes);
        return -2;
    }
    memset(bytes, 0, size);
    memset(actual, 0, size);
    struct worker_context context = {bytes, size, -1};
    pthread_t thread;
    if (pthread_create(&thread, NULL, worker_entry, &context) != 0) {
        free(actual);
        free(bytes);
        return -3;
    }
    if (pthread_join(thread, NULL) != 0 || context.result != 0) {
        free(actual);
        free(bytes);
        return -4;
    }
    const int descriptor = open(path, O_CREAT | O_TRUNC | O_RDWR, 0600);
    if (descriptor < 0) {
        free(actual);
        free(bytes);
        return -5;
    }
    int result = 0;
    if (write(descriptor, bytes, size) != (ssize_t)size) {
        result = -6;
    } else if (lseek(descriptor, 0, SEEK_SET) != 0) {
        result = -7;
    } else if (read(descriptor, actual, size) != (ssize_t)size) {
        result = -8;
    } else if (memcmp(bytes, actual, size) != 0) {
        result = -9;
    }
    if (close(descriptor) != 0 && result == 0) result = -10;
    free(actual);
    free(bytes);
    return result;
}
