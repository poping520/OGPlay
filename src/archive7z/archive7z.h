#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#if defined(ogplay_7z_EXPORTS)
#define OGPLAY_7Z_API __declspec(dllexport)
#else
#define OGPLAY_7Z_API __declspec(dllimport)
#endif
#else
#define OGPLAY_7Z_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ogplay_7z_archive ogplay_7z_archive;

typedef enum ogplay_7z_status {
    OGPLAY_7Z_OK,
    OGPLAY_7Z_INVALID,
    OGPLAY_7Z_IO,
    OGPLAY_7Z_ENCRYPTED,
    OGPLAY_7Z_MULTIVOLUME,
    OGPLAY_7Z_UNSUPPORTED,
    OGPLAY_7Z_CANCELLED,
    OGPLAY_7Z_NO_MEMORY
} ogplay_7z_status;

typedef struct ogplay_7z_error {
    ogplay_7z_status code;
    char message[512];
} ogplay_7z_error;

typedef struct ogplay_7z_name {
    const char* bytes;
    size_t size;
} ogplay_7z_name;

typedef struct ogplay_7z_entry {
    ogplay_7z_name name;
    ogplay_7z_name unicode_name; /* optional UTF-8 name supplied separately by the archive */
    uint64_t size;
    uint8_t name_is_utf8;
    uint8_t directory;
    uint8_t encrypted;
    uint8_t split;
    uint8_t special;
} ogplay_7z_entry;

/* A status callback returns 0 on success. Callbacks must handle their own exceptions.
 * Reads may return short counts. Extraction callbacks can run on a decoder thread;
 * cancelled can overlap other callbacks. */
typedef int (*ogplay_7z_read)(void* context, uint64_t offset, void* data, size_t capacity, size_t* read);

typedef struct ogplay_7z_input {
    void* context;
    uint64_t size;
    ogplay_7z_read read;
} ogplay_7z_input;

/* Selected entries are streamed in index order. Begin/end are called for empty files too.
 * End acknowledges a fully decoded entry. A failed extraction may leave a begun entry without an end. */
typedef struct ogplay_7z_output {
    void* context;
    int (*begin)(void* context, uint32_t index);
    int (*write)(void* context, const void* data, size_t size);
    int (*end)(void* context);
    void (*progress)(void* context, uint64_t written);
    int (*cancelled)(void* context);
} ogplay_7z_output;

/* Input context and callbacks are borrowed until close. Use a handle from one thread at a time.
 * An error argument may be NULL. A successful open owns no destination files. */
OGPLAY_7Z_API ogplay_7z_status ogplay_7z_open(const ogplay_7z_input* input, ogplay_7z_archive** archive,
                                              ogplay_7z_error* error);
OGPLAY_7Z_API void ogplay_7z_close(ogplay_7z_archive* archive);
OGPLAY_7Z_API uint32_t ogplay_7z_count(const ogplay_7z_archive* archive);
OGPLAY_7Z_API int ogplay_7z_is_volume(const ogplay_7z_archive* archive);
/* Both names are borrowed until close. */
OGPLAY_7Z_API ogplay_7z_status ogplay_7z_get(const ogplay_7z_archive* archive, uint32_t index, ogplay_7z_entry* entry,
                                             ogplay_7z_error* error);
/* Indices must be unique and strictly increasing. Progress counts bytes delivered to write. */
OGPLAY_7Z_API ogplay_7z_status ogplay_7z_extract(ogplay_7z_archive* archive, const uint32_t* indices, uint32_t count,
                                                 const ogplay_7z_output* output, ogplay_7z_error* error);

#ifdef __cplusplus
}
#endif
