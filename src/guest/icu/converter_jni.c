/* JNI protocol adapted from API 19 libcore_icu_NativeConverter.cpp.
 * Copyright (C) 1996-2006 International Business Machines Corporation
 * and others. All Rights Reserved.
 */
#include <jni.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "icu51_capi.h"

enum { kReplacementLimit = 32, kReport = 0, kIgnore = 1, kReplace = 2 };

enum { kUnassigned = 0, kIrregular = 2 };

typedef struct Converter {
    UConverter* icu;
    jlong token;
    struct Converter* next;
    int encode_actions[2], decode_actions[2];
    char replacement_bytes[kReplacementLimit];
    UChar replacement_chars[kReplacementLimit];
    jsize byte_count, char_count;
} Converter;

static pthread_mutex_t converters_mutex = PTHREAD_MUTEX_INITIALIZER;
static Converter* converters;
static uint64_t next_token = 1;

static void throw_new(JNIEnv* env, const char* type, const char* message) {
    if ((*env)->ExceptionCheck(env)) return;
    jclass cls = (*env)->FindClass(env, type);
    if (cls != NULL) (*env)->ThrowNew(env, cls, message);
}

static void check_status(JNIEnv* env, UErrorCode status) {
    if (status <= U_ZERO_ERROR) return;
    const char* type = "java/lang/RuntimeException";
    if (status == U_ILLEGAL_ARGUMENT_ERROR) type = "java/lang/IllegalArgumentException";
    if (status == U_MEMORY_ALLOCATION_ERROR) type = "java/lang/OutOfMemoryError";
    throw_new(env, type, ICU(u_errorName)(status));
}

static Converter* find_converter(JNIEnv* env, jlong token) {
    pthread_mutex_lock(&converters_mutex);
    Converter* entry = converters;
    while (entry != NULL && entry->token != token)
        entry = entry->next;
    pthread_mutex_unlock(&converters_mutex);
    /* ROM confines each converter to its own encoder or decoder. */
    if (entry == NULL) check_status(env, U_ILLEGAL_ARGUMENT_ERROR);
    return entry;
}

static jlong open_converter(JNIEnv* env, jclass cls, jstring name) {
    (void)cls;
    const char* text = (*env)->GetStringUTFChars(env, name, NULL);
    if (text == NULL) return 0;
    UErrorCode status = U_ZERO_ERROR;
    UConverter* icu = ICU(ucnv_open)(text, &status);
    (*env)->ReleaseStringUTFChars(env, name, text);
    if (status > U_ZERO_ERROR) {
        check_status(env, status);
        return 0;
    }
    Converter* entry = (Converter*)calloc(1, sizeof(*entry));
    if (entry == NULL) {
        ICU(ucnv_close)(icu);
        check_status(env, U_MEMORY_ALLOCATION_ERROR);
        return 0;
    }
    entry->icu = icu;
    pthread_mutex_lock(&converters_mutex);
    entry->token = (jlong)next_token++;
    entry->next = converters;
    converters = entry;
    pthread_mutex_unlock(&converters_mutex);
    return entry->token;
}

static void free_converter(Converter* entry) {
    ICU(ucnv_close)(entry->icu);
    free(entry);
}

static void close_converter(JNIEnv* env, jclass cls, jlong token) {
    (void)env;
    (void)cls;
    pthread_mutex_lock(&converters_mutex);
    Converter** current = &converters;
    while (*current != NULL && (*current)->token != token)
        current = &(*current)->next;
    Converter* entry = *current;
    if (entry != NULL) *current = entry->next;
    pthread_mutex_unlock(&converters_mutex);
    if (entry != NULL) free_converter(entry);
}

void ogplay_converter_release(void) {
    while (converters != NULL) {
        Converter* entry = converters;
        converters = entry->next;
        free_converter(entry);
    }
}

static void encode_callback(const void* context, UConverterFromUnicodeArgs* args, const UChar* units, int32_t length,
                            int32_t cp, int32_t reason, UErrorCode* status) {
    (void)units;
    (void)length;
    (void)cp;
    if (reason < kUnassigned || reason > kIrregular) return;
    const Converter* entry = context;
    const int action = entry->encode_actions[reason == kUnassigned];
    if (action != kReport) *status = U_ZERO_ERROR;
    if (action == kReplace) ICU(ucnv_cbFromUWriteBytes)(args, entry->replacement_bytes, entry->byte_count, 0, status);
}

static void decode_callback(const void* context, UConverterToUnicodeArgs* args, const char* units, int32_t length,
                            int32_t reason, UErrorCode* status) {
    (void)units;
    (void)length;
    if (reason < kUnassigned || reason > kIrregular) return;
    const Converter* entry = context;
    const int action = entry->decode_actions[reason == kUnassigned];
    if (action != kReport) *status = U_ZERO_ERROR;
    if (action == kReplace) ICU(ucnv_cbToUWriteUChars)(args, entry->replacement_chars, entry->char_count, 0, status);
}

static int valid_actions(int malformed, int unmappable, jsize length) {
    return malformed >= kReport && malformed <= kReplace && unmappable >= kReport && unmappable <= kReplace &&
           length <= kReplacementLimit;
}

static void set_encode(JNIEnv* env, jclass cls, jlong token, jint malformed, jint unmappable, jbyteArray replacement) {
    (void)cls;
    Converter* entry = find_converter(env, token);
    if (entry == NULL) return;
    const jsize length = (*env)->GetArrayLength(env, replacement);
    if (!valid_actions(malformed, unmappable, length)) {
        check_status(env, U_ILLEGAL_ARGUMENT_ERROR);
        return;
    }
    (*env)->GetByteArrayRegion(env, replacement, 0, length, (jbyte*)entry->replacement_bytes);
    if ((*env)->ExceptionCheck(env)) return;
    entry->encode_actions[0] = malformed;
    entry->encode_actions[1] = unmappable;
    entry->byte_count = length;
    UErrorCode status = U_ZERO_ERROR;
    ICU(ucnv_setFromUCallBack)(entry->icu, encode_callback, entry, NULL, NULL, &status);
    check_status(env, status);
}

static void set_decode(JNIEnv* env, jclass cls, jlong token, jint malformed, jint unmappable, jstring replacement) {
    (void)cls;
    Converter* entry = find_converter(env, token);
    if (entry == NULL) return;
    const jsize length = (*env)->GetStringLength(env, replacement);
    if (!valid_actions(malformed, unmappable, length)) {
        check_status(env, U_ILLEGAL_ARGUMENT_ERROR);
        return;
    }
    (*env)->GetStringRegion(env, replacement, 0, length, entry->replacement_chars);
    if ((*env)->ExceptionCheck(env)) return;
    entry->decode_actions[0] = malformed;
    entry->decode_actions[1] = unmappable;
    entry->char_count = length;
    UErrorCode status = U_ZERO_ERROR;
    ICU(ucnv_setToUCallBack)(entry->icu, decode_callback, entry, NULL, NULL, &status);
    check_status(env, status);
}

static int conversion_bounds(JNIEnv* env, jarray source, jint source_end, jarray target, jint target_end,
                             jintArray offsets, jint data[3]) {
    (*env)->GetIntArrayRegion(env, offsets, 0, 3, data);
    if ((*env)->ExceptionCheck(env)) return 0;
    if (data[0] >= 0 && data[0] <= source_end && source_end <= (*env)->GetArrayLength(env, source) && data[1] >= 0 &&
        data[1] <= target_end && target_end <= (*env)->GetArrayLength(env, target))
        return 1;
    check_status(env, U_ILLEGAL_ARGUMENT_ERROR);
    return 0;
}

static void finish_conversion(JNIEnv* env, jintArray offsets, const jint data[3], UErrorCode status, jboolean flush) {
    (*env)->SetIntArrayRegion(env, offsets, 0, 3, data);
    /* ROM's loop handles overflow and malformed/unmappable input, and
       the flush handles overflow and truncated trailing input. */
    if (status != U_BUFFER_OVERFLOW_ERROR &&
        (flush ? status != U_TRUNCATED_CHAR_FOUND : status != U_INVALID_CHAR_FOUND && status != U_ILLEGAL_CHAR_FOUND))
        check_status(env, status);
}

static jint encode(JNIEnv* env, jclass cls, jlong token, jcharArray input, jint input_end, jbyteArray output,
                   jint output_end, jintArray offsets, jboolean flush) {
    (void)cls;
    Converter* entry = find_converter(env, token);
    jint data[3];
    if (entry == NULL || !conversion_bounds(env, input, input_end, output, output_end, offsets, data))
        return U_ILLEGAL_ARGUMENT_ERROR;
    jchar* in = (*env)->GetCharArrayElements(env, input, NULL);
    if (in == NULL) return U_MEMORY_ALLOCATION_ERROR;
    jbyte* out = (*env)->GetByteArrayElements(env, output, NULL);
    UErrorCode status = U_MEMORY_ALLOCATION_ERROR;
    if (out != NULL) {
        const UChar* from = in + data[0];
        char* to = (char*)out + data[1];
        status = U_ZERO_ERROR;
        ICU(ucnv_fromUnicode)(entry->icu, &to, (char*)out + output_end, &from, in + input_end, NULL, flush, &status);
        data[0] = (jint)(from - (in + data[0]));
        data[1] = (jint)(to - ((char*)out + data[1]));
        data[2] = 0;
        if (status == U_INVALID_CHAR_FOUND || status == U_ILLEGAL_CHAR_FOUND) {
            UChar invalid[kReplacementLimit];
            int8_t length = kReplacementLimit;
            UErrorCode detail = U_ZERO_ERROR;
            ICU(ucnv_getInvalidUChars)(entry->icu, invalid, &length, &detail);
            if (detail <= U_ZERO_ERROR) data[2] = length;
        }
        (*env)->ReleaseByteArrayElements(env, output, out, 0);
    }
    (*env)->ReleaseCharArrayElements(env, input, in, JNI_ABORT);
    if (out != NULL) finish_conversion(env, offsets, data, status, flush);
    return status;
}

static jint decode(JNIEnv* env, jclass cls, jlong token, jbyteArray input, jint input_end, jcharArray output,
                   jint output_end, jintArray offsets, jboolean flush) {
    (void)cls;
    Converter* entry = find_converter(env, token);
    jint data[3];
    if (entry == NULL || !conversion_bounds(env, input, input_end, output, output_end, offsets, data))
        return U_ILLEGAL_ARGUMENT_ERROR;
    jbyte* in = (*env)->GetByteArrayElements(env, input, NULL);
    if (in == NULL) return U_MEMORY_ALLOCATION_ERROR;
    jchar* out = (*env)->GetCharArrayElements(env, output, NULL);
    UErrorCode status = U_MEMORY_ALLOCATION_ERROR;
    if (out != NULL) {
        const char* from = (const char*)in + data[0];
        UChar* to = out + data[1];
        status = U_ZERO_ERROR;
        ICU(ucnv_toUnicode)(entry->icu, &to, out + output_end, &from, (const char*)in + input_end, NULL, flush,
                            &status);
        data[0] = (jint)(from - ((const char*)in + data[0]));
        data[1] = (jint)(to - (out + data[1]));
        data[2] = 0;
        if (status == U_INVALID_CHAR_FOUND || status == U_ILLEGAL_CHAR_FOUND) {
            char invalid[kReplacementLimit];
            int8_t length = kReplacementLimit;
            UErrorCode detail = U_ZERO_ERROR;
            ICU(ucnv_getInvalidChars)(entry->icu, invalid, &length, &detail);
            if (detail <= U_ZERO_ERROR) data[2] = length;
        }
        (*env)->ReleaseCharArrayElements(env, output, out, 0);
    }
    (*env)->ReleaseByteArrayElements(env, input, in, JNI_ABORT);
    if (out != NULL) finish_conversion(env, offsets, data, status, flush);
    return status;
}

static void reset_encode(JNIEnv* env, jclass cls, jlong token) {
    (void)cls;
    Converter* entry = find_converter(env, token);
    if (entry != NULL) ICU(ucnv_resetFromUnicode)(entry->icu);
}

static void reset_decode(JNIEnv* env, jclass cls, jlong token) {
    (void)cls;
    Converter* entry = find_converter(env, token);
    if (entry != NULL) ICU(ucnv_resetToUnicode)(entry->icu);
}

static jint max_bytes(JNIEnv* env, jclass cls, jlong token) {
    (void)cls;
    Converter* entry = find_converter(env, token);
    return entry != NULL ? ICU(ucnv_getMaxCharSize)(entry->icu) : -1;
}

static jint min_bytes(JNIEnv* env, jclass cls, jlong token) {
    (void)cls;
    Converter* entry = find_converter(env, token);
    return entry != NULL ? ICU(ucnv_getMinCharSize)(entry->icu) : -1;
}

static jfloat average_bytes(JNIEnv* env, jclass cls, jlong token) {
    return ((jfloat)max_bytes(env, cls, token) + (jfloat)min_bytes(env, cls, token)) * 0.5f;
}

static jfloat average_chars(JNIEnv* env, jclass cls, jlong token) {
    return 1.0f / (jfloat)max_bytes(env, cls, token);
}

static jbyteArray substitution(JNIEnv* env, jclass cls, jlong token) {
    (void)cls;
    Converter* entry = find_converter(env, token);
    if (entry == NULL) return NULL;
    char bytes[kReplacementLimit];
    int8_t length = sizeof(bytes);
    UErrorCode status = U_ZERO_ERROR;
    ICU(ucnv_getSubstChars)(entry->icu, bytes, &length, &status);
    if (status > U_ZERO_ERROR) length = 0;
    jbyteArray result = (*env)->NewByteArray(env, length);
    if (result != NULL) (*env)->SetByteArrayRegion(env, result, 0, length, (const jbyte*)bytes);
    return result;
}

static const char* canonical_icu_name(const char* name) {
    const char* standards[] = {"MIME", "IANA", ""};
    for (size_t i = 0; i < sizeof(standards) / sizeof(standards[0]); ++i) {
        UErrorCode status = U_ZERO_ERROR;
        const char* canonical = ICU(ucnv_getCanonicalName)(name, standards[i], &status);
        if (canonical != NULL) return canonical;
    }
    UErrorCode status = U_ZERO_ERROR;
    const char* alias = ICU(ucnv_getAlias)(name, 0, &status);
    if (alias != NULL) return alias;
    return strncmp(name, "x-", 2) == 0 ? name + 2 : NULL;
}

static jstring canonical_java_name(JNIEnv* env, const char* name) {
    UErrorCode status = U_ZERO_ERROR;
    const char* standard = ICU(ucnv_getStandardName)(name, "MIME", &status);
    if (standard == NULL) {
        status = U_ZERO_ERROR;
        standard = ICU(ucnv_getStandardName)(name, "IANA", &status);
    }
    if (standard != NULL) return (*env)->NewStringUTF(env, standard);
    status = U_ZERO_ERROR;
    const uint16_t count = ICU(ucnv_countAliases)(name, &status);
    for (uint16_t i = 0; i < count; ++i) {
        const char* alias = ICU(ucnv_getAlias)(name, i, &status);
        if (alias != NULL && strncmp(alias, "x-", 2) == 0) return (*env)->NewStringUTF(env, alias);
    }
    status = U_ZERO_ERROR;
    standard = ICU(ucnv_getStandardName)(name, "UTR22", &status);
    if (standard == NULL && strchr(name, ',') != NULL) standard = ICU(ucnv_getAlias)(name, 1, &status);
    if (standard == NULL) standard = name;
    size_t length = strlen(standard);
    char* prefixed = (char*)malloc(length + 3);
    if (prefixed == NULL) {
        check_status(env, U_MEMORY_ALLOCATION_ERROR);
        return NULL;
    }
    memcpy(prefixed, "x-", 2);
    memcpy(prefixed + 2, standard, length + 1);
    jstring result = (*env)->NewStringUTF(env, prefixed);
    free(prefixed);
    return result;
}

static jobjectArray converter_aliases(JNIEnv* env, const char* name) {
    const char* standards[] = {"IANA", "MIME", "JAVA", "WINDOWS"};
    UEnumeration* lists[4] = {NULL};
    const char* names[128];
    jsize count = 0;
    jobjectArray result = NULL;
    UErrorCode status = U_ZERO_ERROR;
    for (size_t i = 0; i < sizeof(lists) / sizeof(lists[0]); ++i) {
        lists[i] = ICU(ucnv_openStandardNames)(name, standards[i], &status);
        if (status > U_ZERO_ERROR) goto done;
        if (lists[i] == NULL) continue;
        const char* alias;
        while ((alias = ICU(uenum_next)(lists[i], NULL, &status)) != NULL && status <= U_ZERO_ERROR) {
            if (strpbrk(alias, "+,") != NULL) continue;
            if (count == (jsize)(sizeof(names) / sizeof(names[0]))) {
                status = U_BUFFER_OVERFLOW_ERROR;
                goto done;
            }
            names[count++] = alias;
        }
        if (status > U_ZERO_ERROR) goto done;
    }
    jclass string = (*env)->FindClass(env, "java/lang/String");
    if (string == NULL) goto done;
    result = (*env)->NewObjectArray(env, count, string, NULL);
    for (jsize i = 0; result != NULL && i < count; ++i) {
        jstring alias = (*env)->NewStringUTF(env, names[i]);
        if (alias == NULL) break;
        (*env)->SetObjectArrayElement(env, result, i, alias);
        (*env)->DeleteLocalRef(env, alias);
        if ((*env)->ExceptionCheck(env)) break;
    }
done:
    for (size_t i = 0; i < sizeof(lists) / sizeof(lists[0]); ++i)
        if (lists[i] != NULL) ICU(uenum_close)(lists[i]);
    check_status(env, status);
    return result;
}

static jobject charset_for_name(JNIEnv* env, jclass cls, jstring requested) {
    (void)cls;
    const char* text = (*env)->GetStringUTFChars(env, requested, NULL);
    if (text == NULL) return NULL;
    const char* name = canonical_icu_name(text);
    UErrorCode status = U_ZERO_ERROR;
    UConverter* converter = name != NULL ? ICU(ucnv_open)(name, &status) : NULL;
    jobject result = NULL;
    if (converter != NULL && status <= U_ZERO_ERROR) {
        ICU(ucnv_close)(converter);
        jstring canonical = canonical_java_name(env, name);
        jobjectArray aliases = canonical != NULL ? converter_aliases(env, name) : NULL;
        jstring icu_name = aliases != NULL ? (*env)->NewStringUTF(env, name) : NULL;
        jclass type = icu_name != NULL ? (*env)->FindClass(env, "java/nio/charset/CharsetICU") : NULL;
        jmethodID ctor =
            type != NULL
                ? (*env)->GetMethodID(env, type, "<init>", "(Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;)V")
                : NULL;
        if (ctor != NULL) result = (*env)->NewObject(env, type, ctor, canonical, icu_name, aliases);
    }
    (*env)->ReleaseStringUTFChars(env, requested, text);
    return result;
}

static jobjectArray available(JNIEnv* env, jclass cls) {
    (void)cls;
    const int32_t count = ICU(ucnv_countAvailable)();
    jclass string = (*env)->FindClass(env, "java/lang/String");
    if (string == NULL) return NULL;
    jobjectArray result = (*env)->NewObjectArray(env, count, string, NULL);
    for (int32_t i = 0; result != NULL && i < count; ++i) {
        jstring name = canonical_java_name(env, ICU(ucnv_getAvailableName)(i));
        if (name == NULL) return NULL;
        (*env)->SetObjectArrayElement(env, result, i, name);
        (*env)->DeleteLocalRef(env, name);
        if ((*env)->ExceptionCheck(env)) return NULL;
    }
    return result;
}

static jboolean contains(JNIEnv* env, jclass cls, jstring left, jstring right) {
    (void)cls;
    const char* first = (*env)->GetStringUTFChars(env, left, NULL);
    if (first == NULL) return JNI_FALSE;
    const char* second = (*env)->GetStringUTFChars(env, right, NULL);
    UErrorCode status = U_ZERO_ERROR;
    UConverter* a = second != NULL ? ICU(ucnv_open)(first, &status) : NULL;
    UConverter* b = a != NULL ? ICU(ucnv_open)(second, &status) : NULL;
    USet* set_a = b != NULL ? ICU(uset_openEmpty)() : NULL;
    USet* set_b = set_a != NULL ? ICU(uset_openEmpty)() : NULL;
    jboolean result = JNI_FALSE;
    if (set_b != NULL) {
        ICU(ucnv_getUnicodeSet)(a, set_a, 0, &status);
        ICU(ucnv_getUnicodeSet)(b, set_b, 0, &status);
        result = status <= U_ZERO_ERROR && ICU(uset_containsAll)(set_a, set_b);
        ICU(uset_close)(set_b);
    }
    if (set_a != NULL) ICU(uset_close)(set_a);
    if (b != NULL) ICU(ucnv_close)(b);
    if (a != NULL) ICU(ucnv_close)(a);
    if (second != NULL) (*env)->ReleaseStringUTFChars(env, right, second);
    (*env)->ReleaseStringUTFChars(env, left, first);
    return result;
}

int ogplay_converter_on_load(JNIEnv* env) {
    static const JNINativeMethod methods[] = {
        {"charsetForName", "(Ljava/lang/String;)Ljava/nio/charset/Charset;", (void*)charset_for_name},
        {"closeConverter", "(J)V", (void*)close_converter},
        {"contains", "(Ljava/lang/String;Ljava/lang/String;)Z", (void*)contains},
        {"decode", "(J[BI[CI[IZ)I", (void*)decode},
        {"encode", "(J[CI[BI[IZ)I", (void*)encode},
        {"getAvailableCharsetNames", "()[Ljava/lang/String;", (void*)available},
        {"getAveBytesPerChar", "(J)F", (void*)average_bytes},
        {"getAveCharsPerByte", "(J)F", (void*)average_chars},
        {"getMaxBytesPerChar", "(J)I", (void*)max_bytes},
        {"getMinBytesPerChar", "(J)I", (void*)min_bytes},
        {"getSubstitutionBytes", "(J)[B", (void*)substitution},
        {"openConverter", "(Ljava/lang/String;)J", (void*)open_converter},
        {"resetByteToChar", "(J)V", (void*)reset_decode},
        {"resetCharToByte", "(J)V", (void*)reset_encode},
        {"setCallbackDecode", "(JIILjava/lang/String;)V", (void*)set_decode},
        {"setCallbackEncode", "(JII[B)V", (void*)set_encode},
    };
    jclass owner = (*env)->FindClass(env, "libcore/icu/NativeConverter");
    return owner != NULL && (*env)->RegisterNatives(env, owner, methods, sizeof(methods) / sizeof(methods[0])) == 0;
}
