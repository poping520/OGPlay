#include <jni.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "icu51_capi.h"

#define ICU_DATA_PATH "/system/usr/icu/icudt51l.dat"
#define ICU_DATA_SIZE 14146832u
#define ICU_CURRENCY_PACKAGE "icudt51l-curr"
#define ICU_DATE_MIN (-8.64e15)
#define ICU_DATE_MAX (8.64e15)

static unsigned char *icu_data;

static void throw_new(JNIEnv *env, const char *type, const char *message) {
  jclass cls = (*env)->FindClass(env, type);
  if (cls != NULL)
    (*env)->ThrowNew(env, cls, message);
}

static int icu_ok(JNIEnv *env, const char *operation, UErrorCode status) {
  if (status <= U_ZERO_ERROR)
    return 1;
  char message[160];
  snprintf(message, sizeof(message), "%s failed: %s (%d)", operation,
           ICU(u_errorName)(status), status);
  const char *type = "java/lang/RuntimeException";
  if (status == U_ILLEGAL_ARGUMENT_ERROR)
    type = "java/lang/IllegalArgumentException";
  else if (status == U_INDEX_OUTOFBOUNDS_ERROR ||
           status == U_BUFFER_OVERFLOW_ERROR)
    type = "java/lang/ArrayIndexOutOfBoundsException";
  else if (status == U_UNSUPPORTED_ERROR)
    type = "java/lang/UnsupportedOperationException";
  else if (status == U_MEMORY_ALLOCATION_ERROR)
    type = "java/lang/OutOfMemoryError";
  throw_new(env, type, message);
  return 0;
}

static int load_icu_data(JNIEnv *env) {
  FILE *file = fopen(ICU_DATA_PATH, "rb");
  if (file == NULL) {
    throw_new(env, "java/lang/UnsatisfiedLinkError",
              "missing pinned ICU data " ICU_DATA_PATH);
    return 0;
  }
  unsigned char *bytes = (unsigned char *)malloc(ICU_DATA_SIZE);
  if (bytes == NULL) {
    fclose(file);
    throw_new(env, "java/lang/OutOfMemoryError", "ICU data buffer");
    return 0;
  }
  const size_t count = fread(bytes, 1, ICU_DATA_SIZE, file);
  const int extra = fgetc(file);
  fclose(file);
  if (count != ICU_DATA_SIZE || extra != EOF) {
    free(bytes);
    throw_new(env, "java/lang/UnsatisfiedLinkError",
              "unexpected pinned ICU data size");
    return 0;
  }
  UErrorCode status = U_ZERO_ERROR;
  ICU(udata_setCommonData)(bytes, &status);
  if (!icu_ok(env, "udata_setCommonData", status)) {
    ICU(u_cleanup)();
    free(bytes);
    return 0;
  }
  status = U_ZERO_ERROR;
  ICU(udata_setFileAccess)(UDATA_NO_FILES, &status);
  if (!icu_ok(env, "udata_setFileAccess", status)) {
    ICU(u_cleanup)();
    free(bytes);
    return 0;
  }
  status = U_ZERO_ERROR;
  ICU(u_init)(&status);
  if (!icu_ok(env, "u_init", status)) {
    ICU(u_cleanup)();
    free(bytes);
    return 0;
  }
  icu_data = bytes;
  return 1;
}

static jstring new_ustring(JNIEnv *env, const UChar *value, int32_t length) {
  return (*env)->NewString(env, (const jchar *)value, length);
}

static UChar *string_chars(JNIEnv *env, jstring value, jsize *length) {
  if (value == NULL) {
    throw_new(env, "java/lang/NullPointerException", "string == null");
    return NULL;
  }
  *length = (*env)->GetStringLength(env, value);
  const jchar *chars = (*env)->GetStringChars(env, value, NULL);
  if (chars == NULL)
    return NULL;
  UChar *copy = (UChar *)malloc(((size_t)*length + 1u) * sizeof(UChar));
  if (copy == NULL) {
    (*env)->ReleaseStringChars(env, value, chars);
    throw_new(env, "java/lang/OutOfMemoryError", "UTF-16 buffer");
    return NULL;
  }
  memcpy(copy, chars, (size_t)*length * sizeof(UChar));
  copy[*length] = 0;
  (*env)->ReleaseStringChars(env, value, chars);
  return copy;
}

static char *string_utf8(JNIEnv *env, jstring value) {
  if (value == NULL) {
    throw_new(env, "java/lang/NullPointerException", "string == null");
    return NULL;
  }
  const char *chars = (*env)->GetStringUTFChars(env, value, NULL);
  if (chars == NULL)
    return NULL;
  const size_t length = strlen(chars);
  char *copy = (char *)malloc(length + 1u);
  if (copy != NULL)
    memcpy(copy, chars, length + 1u);
  (*env)->ReleaseStringUTFChars(env, value, chars);
  if (copy == NULL)
    throw_new(env, "java/lang/OutOfMemoryError", "UTF-8 buffer");
  return copy;
}

static jstring buffered_string(JNIEnv *env,
                               int32_t (*read)(void *, UChar *, int32_t,
                                               UErrorCode *),
                               void *owner, const char *operation) {
  UErrorCode status = U_ZERO_ERROR;
  int32_t length = read(owner, NULL, 0, &status);
  if (status != U_BUFFER_OVERFLOW_ERROR && !icu_ok(env, operation, status))
    return NULL;
  status = U_ZERO_ERROR;
  UChar *buffer = (UChar *)malloc(((size_t)length + 1u) * sizeof(UChar));
  if (buffer == NULL) {
    throw_new(env, "java/lang/OutOfMemoryError", operation);
    return NULL;
  }
  length = read(owner, buffer, length + 1, &status);
  jstring result =
      icu_ok(env, operation, status) ? new_ustring(env, buffer, length) : NULL;
  free(buffer);
  return result;
}

static jobjectArray iso_array(JNIEnv *env, const char *const *values) {
  jclass string_class = (*env)->FindClass(env, "java/lang/String");
  if (string_class == NULL)
    return NULL;
  jsize count = 0;
  while (values[count] != NULL)
    ++count;
  jobjectArray result = (*env)->NewObjectArray(env, count, string_class, NULL);
  if (result == NULL)
    return NULL;
  for (jsize i = 0; i < count; ++i) {
    jstring value = (*env)->NewStringUTF(env, values[i]);
    if (value == NULL)
      return NULL;
    (*env)->SetObjectArrayElement(env, result, i, value);
    (*env)->DeleteLocalRef(env, value);
  }
  return result;
}

static jobjectArray ICU_getISOLanguagesNative(JNIEnv *env, jclass cls) {
  (void)cls;
  return iso_array(env, ICU(uloc_getISOLanguages)());
}

static jobjectArray ICU_getISOCountriesNative(JNIEnv *env, jclass cls) {
  (void)cls;
  return iso_array(env, ICU(uloc_getISOCountries)());
}

static jstring map_case(JNIEnv *env, jstring value, jstring locale_value,
                        int upper) {
  jsize source_length = 0;
  UChar *source = string_chars(env, value, &source_length);
  char *locale = string_utf8(env, locale_value);
  if (source == NULL || locale == NULL) {
    free(source);
    free(locale);
    return NULL;
  }
  UErrorCode status = U_ZERO_ERROR;
  int32_t length =
      upper
          ? ICU(u_strToUpper)(NULL, 0, source, source_length, locale, &status)
          : ICU(u_strToLower)(NULL, 0, source, source_length, locale, &status);
  if (status != U_BUFFER_OVERFLOW_ERROR &&
      !icu_ok(env, "ICU case mapping", status)) {
    free(source);
    free(locale);
    return NULL;
  }
  status = U_ZERO_ERROR;
  UChar *result = (UChar *)malloc(((size_t)length + 1u) * sizeof(UChar));
  if (result == NULL) {
    free(source);
    free(locale);
    throw_new(env, "java/lang/OutOfMemoryError", "ICU case mapping");
    return NULL;
  }
  length = upper ? ICU(u_strToUpper)(result, length + 1, source, source_length,
                                     locale, &status)
                 : ICU(u_strToLower)(result, length + 1, source, source_length,
                                     locale, &status);
  jstring mapped = NULL;
  if (icu_ok(env, "ICU case mapping", status)) {
    mapped = length == source_length &&
                     memcmp(result, source, (size_t)length * sizeof(UChar)) == 0
                 ? value
                 : new_ustring(env, result, length);
  }
  free(result);
  free(source);
  free(locale);
  return mapped;
}

static jstring ICU_toLowerCase(JNIEnv *env, jclass cls, jstring value,
                               jstring locale) {
  (void)cls;
  return map_case(env, value, locale, 0);
}

static jstring ICU_toUpperCase(JNIEnv *env, jclass cls, jstring value,
                               jstring locale) {
  (void)cls;
  return map_case(env, value, locale, 1);
}

struct PatternRead {
  UDateTimePatternGenerator *generator;
  const UChar *skeleton;
  int32_t length;
};
static int32_t read_best_pattern(void *raw, UChar *out, int32_t capacity,
                                 UErrorCode *status) {
  struct PatternRead *p = (struct PatternRead *)raw;
  return ICU(udatpg_getBestPattern)(p->generator, p->skeleton, p->length, out,
                                    capacity, status);
}

static jstring ICU_getBestDateTimePatternNative(JNIEnv *env, jclass cls,
                                                jstring skeleton_value,
                                                jstring locale_value) {
  (void)cls;
  jsize length = 0;
  UChar *skeleton = string_chars(env, skeleton_value, &length);
  char *locale = string_utf8(env, locale_value);
  if (skeleton == NULL || locale == NULL) {
    free(skeleton);
    free(locale);
    return NULL;
  }
  UErrorCode status = U_ZERO_ERROR;
  UDateTimePatternGenerator *generator = ICU(udatpg_open)(locale, &status);
  struct PatternRead read = {generator, skeleton, length};
  jstring result = NULL;
  if (generator != NULL && icu_ok(env, "udatpg_open", status))
    result =
        buffered_string(env, read_best_pattern, &read, "udatpg_getBestPattern");
  if (generator != NULL)
    ICU(udatpg_close)(generator);
  free(skeleton);
  free(locale);
  return result;
}

static int currency_code_for_country(const char *country, UChar code[4]) {
  if (country[0] == 0)
    return 0;
  UErrorCode status = U_ZERO_ERROR;
  UResourceBundle *supplemental =
      ICU(ures_openDirect)(ICU_CURRENCY_PACKAGE, "supplementalData", &status);
  UResourceBundle *map =
      supplemental == NULL
          ? NULL
          : ICU(ures_getByKey)(supplemental, "CurrencyMap", NULL, &status);
  UResourceBundle *currencies =
      map == NULL ? NULL : ICU(ures_getByKey)(map, country, NULL, &status);
  UResourceBundle *current =
      currencies == NULL ? NULL
                         : ICU(ures_getByIndex)(currencies, 0, NULL, &status);
  int found = 0;
  if (current != NULL && status <= U_ZERO_ERROR) {
    UErrorCode to_status = U_ZERO_ERROR;
    UResourceBundle *to = ICU(ures_getByKey)(current, "to", NULL, &to_status);
    if (to != NULL)
      ICU(ures_close)(to);
    if (to_status > U_ZERO_ERROR) {
      status = U_ZERO_ERROR;
      int32_t length = 0;
      const UChar *id =
          ICU(ures_getStringByKey)(current, "id", &length, &status);
      if (id != NULL && status <= U_ZERO_ERROR && length == 3) {
        memcpy(code, id, 3u * sizeof(UChar));
        code[3] = 0;
        found = 1;
      }
    }
  }
  if (current != NULL)
    ICU(ures_close)(current);
  if (currencies != NULL)
    ICU(ures_close)(currencies);
  if (map != NULL)
    ICU(ures_close)(map);
  if (supplemental != NULL)
    ICU(ures_close)(supplemental);
  return found;
}

static jstring ICU_getCurrencyCode(JNIEnv *env, jclass cls,
                                   jstring country_value) {
  (void)cls;
  char *country = string_utf8(env, country_value);
  if (country == NULL)
    return NULL;
  UChar code[4] = {0};
  const int found = currency_code_for_country(country, code);
  free(country);
  return found ? new_ustring(env, code, 3) : NULL;
}

static jstring currency_name(JNIEnv *env, jstring locale_value,
                             jstring code_value, int style) {
  char *locale = string_utf8(env, locale_value);
  jsize code_length = 0;
  UChar *code = string_chars(env, code_value, &code_length);
  if (locale == NULL || code == NULL) {
    free(locale);
    free(code);
    return NULL;
  }
  UErrorCode status = U_ZERO_ERROR;
  UBool choice = 0;
  int32_t length = 0;
  const UChar *result =
      ICU(ucurr_getName)(code, locale, style, &choice, &length, &status);
  jstring value = NULL;
  if (status == U_USING_DEFAULT_WARNING) {
    if (style == UCURR_SYMBOL_NAME) {
      UErrorCode available_status = U_ZERO_ERROR;
      if (ICU(ucurr_isAvailable)(code, ICU_DATE_MIN, ICU_DATE_MAX,
                                 &available_status))
        value = new_ustring(env, result, length);
      else if (available_status > U_ZERO_ERROR)
        icu_ok(env, "ucurr_isAvailable", available_status);
    } else if (style == UCURR_LONG_NAME) {
      value = code_value;
    }
  } else if (result != NULL && icu_ok(env, "ucurr_getName", status)) {
    value = new_ustring(env, result, length);
  }
  free(locale);
  free(code);
  return value;
}

static jstring ICU_getCurrencyDisplayName(JNIEnv *env, jclass cls,
                                          jstring locale, jstring code) {
  (void)cls;
  return currency_name(env, locale, code, UCURR_LONG_NAME);
}
static jstring ICU_getCurrencySymbol(JNIEnv *env, jclass cls, jstring locale,
                                     jstring code) {
  (void)cls;
  return currency_name(env, locale, code, UCURR_SYMBOL_NAME);
}
static jint ICU_getCurrencyFractionDigits(JNIEnv *env, jclass cls,
                                          jstring code_value) {
  (void)cls;
  jsize length = 0;
  UChar *code = string_chars(env, code_value, &length);
  if (code == NULL)
    return 0;
  if (length == 3 && code[0] == 'X' && code[1] == 'X' && code[2] == 'X') {
    free(code);
    return -1;
  }
  UErrorCode status = U_ZERO_ERROR;
  const int result = ICU(ucurr_getDefaultFractionDigits)(code, &status);
  free(code);
  return icu_ok(env, "ucurr_getDefaultFractionDigits", status) ? result : 0;
}

static int set_string_field(JNIEnv *env, jobject object, const char *name,
                            const UChar *value, int32_t length) {
  jclass cls = (*env)->GetObjectClass(env, object);
  jfieldID field = (*env)->GetFieldID(env, cls, name, "Ljava/lang/String;");
  if (field == NULL)
    return 0;
  jstring text = new_ustring(env, value, length);
  if (text == NULL)
    return 0;
  (*env)->SetObjectField(env, object, field, text);
  (*env)->DeleteLocalRef(env, text);
  return !(*env)->ExceptionCheck(env);
}

static int set_char_field(JNIEnv *env, jobject object, const char *name,
                          jchar value) {
  jclass cls = (*env)->GetObjectClass(env, object);
  jfieldID field = (*env)->GetFieldID(env, cls, name, "C");
  if (field == NULL)
    return 0;
  (*env)->SetCharField(env, object, field, value);
  return !(*env)->ExceptionCheck(env);
}

static int set_integer_field(JNIEnv *env, jobject object, const char *name,
                             jint value) {
  jclass integer_class = (*env)->FindClass(env, "java/lang/Integer");
  jmethodID value_of = (*env)->GetStaticMethodID(env, integer_class, "valueOf",
                                                 "(I)Ljava/lang/Integer;");
  jobject boxed =
      value_of == NULL
          ? NULL
          : (*env)->CallStaticObjectMethod(env, integer_class, value_of, value);
  jclass cls = (*env)->GetObjectClass(env, object);
  jfieldID field = (*env)->GetFieldID(env, cls, name, "Ljava/lang/Integer;");
  if (field == NULL || boxed == NULL)
    return 0;
  (*env)->SetObjectField(env, object, field, boxed);
  (*env)->DeleteLocalRef(env, boxed);
  return !(*env)->ExceptionCheck(env);
}

static int set_symbol_array(JNIEnv *env, jobject object, const char *field_name,
                            UDateFormat *format, int symbol_type) {
  jclass string_class = (*env)->FindClass(env, "java/lang/String");
  const int32_t count = ICU(udat_countSymbols)(format, symbol_type);
  jobjectArray array = (*env)->NewObjectArray(env, count, string_class, NULL);
  if (array == NULL)
    return 0;
  for (int32_t i = 0; i < count; ++i) {
    UErrorCode status = U_ZERO_ERROR;
    int32_t length =
        ICU(udat_getSymbols)(format, symbol_type, i, NULL, 0, &status);
    if (status != U_BUFFER_OVERFLOW_ERROR &&
        !icu_ok(env, "udat_getSymbols", status))
      return 0;
    status = U_ZERO_ERROR;
    UChar *buffer = (UChar *)malloc(((size_t)length + 1u) * sizeof(UChar));
    if (buffer == NULL) {
      throw_new(env, "java/lang/OutOfMemoryError", "date symbols");
      return 0;
    }
    length = ICU(udat_getSymbols)(format, symbol_type, i, buffer, length + 1,
                                  &status);
    jstring value = icu_ok(env, "udat_getSymbols", status)
                        ? new_ustring(env, buffer, length)
                        : NULL;
    free(buffer);
    if (value == NULL)
      return 0;
    (*env)->SetObjectArrayElement(env, array, i, value);
    (*env)->DeleteLocalRef(env, value);
  }
  jclass cls = (*env)->GetObjectClass(env, object);
  jfieldID field =
      (*env)->GetFieldID(env, cls, field_name, "[Ljava/lang/String;");
  if (field == NULL)
    return 0;
  (*env)->SetObjectField(env, object, field, array);
  (*env)->DeleteLocalRef(env, array);
  return !(*env)->ExceptionCheck(env);
}

static int set_date_pattern(JNIEnv *env, jobject object, const char *field_name,
                            const char *locale, int date_style,
                            int time_style) {
  UErrorCode status = U_ZERO_ERROR;
  UResourceBundle *root = ICU(ures_open)(NULL, locale, &status);
  UResourceBundle *calendar =
      root == NULL ? NULL : ICU(ures_getByKey)(root, "calendar", NULL, &status);
  UResourceBundle *gregorian =
      calendar == NULL
          ? NULL
          : ICU(ures_getByKey)(calendar, "gregorian", NULL, &status);
  UResourceBundle *patterns =
      gregorian == NULL
          ? NULL
          : ICU(ures_getByKey)(gregorian, "DateTimePatterns", NULL, &status);
  const int32_t index = time_style != UDAT_NONE ? time_style : 4 + date_style;
  int32_t length = 0;
  const UChar *value =
      patterns == NULL
          ? NULL
          : ICU(ures_getStringByIndex)(patterns, index, &length, &status);
  const int ok = value != NULL && icu_ok(env, "DateTimePatterns", status) &&
                 set_string_field(env, object, field_name, value, length);
  if (patterns != NULL)
    ICU(ures_close)(patterns);
  if (gregorian != NULL)
    ICU(ures_close)(gregorian);
  if (calendar != NULL)
    ICU(ures_close)(calendar);
  if (root != NULL)
    ICU(ures_close)(root);
  return ok;
}

static int set_relative_day_field(JNIEnv *env, jobject object,
                                  const char *field_name, const char *locale,
                                  const char *key) {
  char current[128];
  if (strlen(locale) >= sizeof(current))
    return 0;
  strcpy(current, locale);
  for (;;) {
    UErrorCode status = U_ZERO_ERROR;
    UResourceBundle *root = ICU(ures_open)(NULL, current, &status);
    UResourceBundle *fields =
        root == NULL ? NULL : ICU(ures_getByKey)(root, "fields", NULL, &status);
    UResourceBundle *day =
        fields == NULL ? NULL
                       : ICU(ures_getByKey)(fields, "day", NULL, &status);
    UResourceBundle *relative =
        day == NULL ? NULL : ICU(ures_getByKey)(day, "relative", NULL, &status);
    int32_t length = 0;
    const UChar *value =
        relative == NULL
            ? NULL
            : ICU(ures_getStringByKey)(relative, key, &length, &status);
    const int found = value != NULL && status <= U_ZERO_ERROR &&
                      set_string_field(env, object, field_name, value, length);
    if (relative != NULL)
      ICU(ures_close)(relative);
    if (day != NULL)
      ICU(ures_close)(day);
    if (fields != NULL)
      ICU(ures_close)(fields);
    if (root != NULL)
      ICU(ures_close)(root);
    if (found)
      return 1;
    if (current[0] == 0)
      return 0;
    status = U_ZERO_ERROR;
    if (ICU(uloc_getParent)(current, current, (int32_t)sizeof(current),
                            &status) < 0 ||
        status > U_ZERO_ERROR)
      return 0;
  }
}

static int set_number_pattern(JNIEnv *env, jobject object,
                              const char *field_name, const char *locale,
                              int style) {
  UErrorCode status = U_ZERO_ERROR;
  UNumberFormat *format = ICU(unum_open)(style, NULL, 0, locale, NULL, &status);
  if (format == NULL || !icu_ok(env, "unum_open", status))
    return 0;
  status = U_ZERO_ERROR;
  int32_t length = ICU(unum_toPattern)(format, 0, NULL, 0, &status);
  if (status != U_BUFFER_OVERFLOW_ERROR &&
      !icu_ok(env, "unum_toPattern", status)) {
    ICU(unum_close)(format);
    return 0;
  }
  status = U_ZERO_ERROR;
  UChar *buffer = (UChar *)malloc(((size_t)length + 1u) * sizeof(UChar));
  if (buffer == NULL) {
    ICU(unum_close)(format);
    throw_new(env, "java/lang/OutOfMemoryError", "number pattern");
    return 0;
  }
  length = ICU(unum_toPattern)(format, 0, buffer, length + 1, &status);
  const int ok = icu_ok(env, "unum_toPattern", status) &&
                 set_string_field(env, object, field_name, buffer, length);
  free(buffer);
  ICU(unum_close)(format);
  return ok;
}

static int set_number_symbol(JNIEnv *env, jobject object,
                             const char *field_name, UNumberFormat *format,
                             int symbol, int character) {
  UErrorCode status = U_ZERO_ERROR;
  UChar buffer[32];
  const int32_t length =
      ICU(unum_getSymbol)(format, symbol, buffer, 32, &status);
  if (!icu_ok(env, "unum_getSymbol", status) || length <= 0)
    return 0;
  return character ? set_char_field(env, object, field_name, buffer[0])
                   : set_string_field(env, object, field_name, buffer, length);
}

static jboolean ICU_initLocaleDataNative(JNIEnv *env, jclass cls,
                                         jstring locale_value, jobject data) {
  (void)cls;
  if (data == NULL) {
    throw_new(env, "java/lang/NullPointerException", "localeData == null");
    return JNI_FALSE;
  }
  char *locale = string_utf8(env, locale_value);
  if (locale == NULL)
    return JNI_FALSE;
  if (!(locale[0] == 0 || strcmp(locale, "en") == 0 ||
        strcmp(locale, "en_US") == 0 || strcmp(locale, "zh") == 0 ||
        strcmp(locale, "zh_CN") == 0)) {
    free(locale);
    throw_new(env, "java/lang/UnsupportedOperationException",
              "locale is outside the audited ICU set");
    return JNI_FALSE;
  }
  const struct {
    const char *field;
    int date_style;
    int time_style;
  } patterns[] = {{"fullTimeFormat", UDAT_NONE, UDAT_FULL},
                  {"longTimeFormat", UDAT_NONE, UDAT_LONG},
                  {"mediumTimeFormat", UDAT_NONE, UDAT_MEDIUM},
                  {"shortTimeFormat", UDAT_NONE, UDAT_SHORT},
                  {"fullDateFormat", UDAT_FULL, UDAT_NONE},
                  {"longDateFormat", UDAT_LONG, UDAT_NONE},
                  {"mediumDateFormat", UDAT_MEDIUM, UDAT_NONE},
                  {"shortDateFormat", UDAT_SHORT, UDAT_NONE}};
  for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); ++i)
    if (!set_date_pattern(env, data, patterns[i].field, locale,
                          patterns[i].date_style, patterns[i].time_style))
      goto fail;
  if (!set_number_pattern(env, data, "numberPattern", locale, UNUM_DECIMAL) ||
      !set_number_pattern(env, data, "currencyPattern", locale,
                          UNUM_CURRENCY) ||
      !set_number_pattern(env, data, "percentPattern", locale, UNUM_PERCENT))
    goto fail;

  UErrorCode status = U_ZERO_ERROR;
  UDateFormat *symbols =
      ICU(udat_open)(UDAT_NONE, UDAT_MEDIUM, locale, NULL, 0, NULL, 0, &status);
  if (symbols == NULL || !icu_ok(env, "udat_open symbols", status))
    goto fail;
  const struct {
    const char *field;
    int type;
  } arrays[] = {
      {"amPm", UDAT_AM_PMS},
      {"eras", UDAT_ERAS},
      {"longMonthNames", UDAT_MONTHS},
      {"shortMonthNames", UDAT_SHORT_MONTHS},
      {"tinyMonthNames", UDAT_NARROW_MONTHS},
      {"longWeekdayNames", UDAT_WEEKDAYS},
      {"shortWeekdayNames", UDAT_SHORT_WEEKDAYS},
      {"tinyWeekdayNames", UDAT_NARROW_WEEKDAYS},
      {"longStandAloneMonthNames", UDAT_STANDALONE_MONTHS},
      {"shortStandAloneMonthNames", UDAT_STANDALONE_SHORT_MONTHS},
      {"tinyStandAloneMonthNames", UDAT_STANDALONE_NARROW_MONTHS},
      {"longStandAloneWeekdayNames", UDAT_STANDALONE_WEEKDAYS},
      {"shortStandAloneWeekdayNames", UDAT_STANDALONE_SHORT_WEEKDAYS},
      {"tinyStandAloneWeekdayNames", UDAT_STANDALONE_NARROW_WEEKDAYS}};
  for (size_t i = 0; i < sizeof(arrays) / sizeof(arrays[0]); ++i)
    if (!set_symbol_array(env, data, arrays[i].field, symbols,
                          arrays[i].type)) {
      ICU(udat_close)(symbols);
      goto fail;
    }
  ICU(udat_close)(symbols);

  status = U_ZERO_ERROR;
  UNumberFormat *numbers =
      ICU(unum_open)(UNUM_DECIMAL, NULL, 0, locale, NULL, &status);
  if (numbers == NULL || !icu_ok(env, "unum_open symbols", status))
    goto fail;
  const struct {
    const char *field;
    int symbol;
    int character;
  } number_symbols[] = {{"zeroDigit", 4, 1},
                        {"decimalSeparator", 0, 1},
                        {"groupingSeparator", 1, 1},
                        {"patternSeparator", 2, 1},
                        {"percent", 3, 1},
                        {"perMill", 12, 1},
                        {"monetarySeparator", 10, 1},
                        {"minusSign", 6, 1},
                        {"exponentSeparator", 11, 0},
                        {"infinity", 14, 0},
                        {"NaN", 15, 0}};
  for (size_t i = 0; i < sizeof(number_symbols) / sizeof(number_symbols[0]);
       ++i)
    if (!set_number_symbol(env, data, number_symbols[i].field, numbers,
                           number_symbols[i].symbol,
                           number_symbols[i].character)) {
      ICU(unum_close)(numbers);
      goto fail;
    }
  ICU(unum_close)(numbers);

  status = U_ZERO_ERROR;
  UCalendar *calendar = ICU(ucal_open)(NULL, 0, locale, UCAL_DEFAULT, &status);
  if (calendar == NULL || !icu_ok(env, "ucal_open", status))
    goto fail;
  if (!set_integer_field(
          env, data, "firstDayOfWeek",
          ICU(ucal_getAttribute)(calendar, UCAL_FIRST_DAY_OF_WEEK)) ||
      !set_integer_field(
          env, data, "minimalDaysInFirstWeek",
          ICU(ucal_getAttribute)(calendar, UCAL_MINIMAL_DAYS_IN_FIRST_WEEK))) {
    ICU(ucal_close)(calendar);
    goto fail;
  }
  ICU(ucal_close)(calendar);

  UChar currency[4] = {'X', 'X', 'X', 0};
  char country[16] = {0};
  status = U_ZERO_ERROR;
  const int32_t country_length =
      ICU(uloc_getCountry)(locale, country, (int32_t)sizeof(country), &status);
  if (status > U_ZERO_ERROR)
    goto fail;
  if (country_length > 0)
    currency_code_for_country(country, currency);
  if (!set_string_field(env, data, "internationalCurrencySymbol", currency, 3))
    goto fail;
  UBool choice = 0;
  int32_t name_length = 0;
  status = U_ZERO_ERROR;
  const UChar *currency_symbol = ICU(ucurr_getName)(
      currency, locale, UCURR_SYMBOL_NAME, &choice, &name_length, &status);
  if (currency_symbol == NULL || !icu_ok(env, "ucurr_getName", status) ||
      !set_string_field(env, data, "currencySymbol", currency_symbol,
                        name_length))
    goto fail;

  if (!set_relative_day_field(env, data, "yesterday", locale, "-1") ||
      !set_relative_day_field(env, data, "today", locale, "0") ||
      !set_relative_day_field(env, data, "tomorrow", locale, "1"))
    goto fail;
  free(locale);
  return JNI_TRUE;
fail:
  free(locale);
  return JNI_FALSE;
}

typedef struct DecimalEntry {
  struct DecimalEntry *next;
  jlong token;
  UNumberFormat *format;
} DecimalEntry;
static DecimalEntry *decimal_entries;
static jlong next_decimal_token = 1;

static DecimalEntry *decimal_entry(JNIEnv *env, jlong token) {
  for (DecimalEntry *p = decimal_entries; p != NULL; p = p->next)
    if (p->token == token)
      return p;
  throw_new(env, "java/lang/IllegalStateException",
            "invalid decimal formatter token");
  return NULL;
}

static int apply_symbols(JNIEnv *env, UNumberFormat *format,
                         jstring currency_symbol, jchar decimal_separator,
                         jchar digit, jstring exponent_separator,
                         jchar grouping_separator, jstring infinity,
                         jstring intl_currency, jchar minus_sign,
                         jchar monetary_separator, jstring nan,
                         jchar pattern_separator, jchar percent, jchar per_mill,
                         jchar zero_digit) {
  struct TextSymbol {
    int symbol;
    jstring value;
  } text[] = {{8, currency_symbol},
              {11, exponent_separator},
              {14, infinity},
              {9, intl_currency},
              {15, nan}};
  for (size_t i = 0; i < sizeof(text) / sizeof(text[0]); ++i) {
    jsize length = 0;
    UChar *value = string_chars(env, text[i].value, &length);
    if (value == NULL)
      return 0;
    UErrorCode status = U_ZERO_ERROR;
    ICU(unum_setSymbol)(format, text[i].symbol, value, length, &status);
    free(value);
    if (!icu_ok(env, "unum_setSymbol", status))
      return 0;
  }
  const struct CharSymbol {
    int symbol;
    jchar value;
  } chars[] = {
      {0, decimal_separator},   {5, digit},      {1, grouping_separator},
      {17, grouping_separator}, {6, minus_sign}, {10, monetary_separator},
      {2, pattern_separator},   {3, percent},    {12, per_mill}};
  for (size_t i = 0; i < sizeof(chars) / sizeof(chars[0]); ++i) {
    UErrorCode status = U_ZERO_ERROR;
    UChar value = chars[i].value;
    ICU(unum_setSymbol)(format, chars[i].symbol, &value, 1, &status);
    if (!icu_ok(env, "unum_setSymbol", status))
      return 0;
  }
  for (int i = 0; i < 10; ++i) {
    UErrorCode status = U_ZERO_ERROR;
    UChar value = (UChar)(zero_digit + i);
    ICU(unum_setSymbol)(format, i == 0 ? 4 : 17 + i, &value, 1, &status);
    if (!icu_ok(env, "unum_setSymbol digit", status))
      return 0;
  }
  return 1;
}

static jlong NativeDecimalFormat_open(
    JNIEnv *env, jclass cls, jstring pattern_value, jstring currency_symbol,
    jchar decimal_separator, jchar digit, jstring exponent_separator,
    jchar grouping_separator, jstring infinity, jstring intl_currency,
    jchar minus_sign, jchar monetary_separator, jstring nan,
    jchar pattern_separator, jchar percent, jchar per_mill, jchar zero_digit) {
  (void)cls;
  jsize pattern_length = 0;
  UChar *pattern = string_chars(env, pattern_value, &pattern_length);
  if (pattern == NULL)
    return 0;
  UErrorCode status = U_ZERO_ERROR;
  UParseError error;
  UNumberFormat *format = ICU(unum_open)(UNUM_PATTERN_DECIMAL, pattern,
                                         pattern_length, "", &error, &status);
  free(pattern);
  if (format == NULL || !icu_ok(env, "unum_open", status))
    return 0;
  if (!apply_symbols(env, format, currency_symbol, decimal_separator, digit,
                     exponent_separator, grouping_separator, infinity,
                     intl_currency, minus_sign, monetary_separator, nan,
                     pattern_separator, percent, per_mill, zero_digit)) {
    ICU(unum_close)(format);
    return 0;
  }
  DecimalEntry *entry = (DecimalEntry *)malloc(sizeof(*entry));
  if (entry == NULL) {
    ICU(unum_close)(format);
    throw_new(env, "java/lang/OutOfMemoryError", "decimal formatter");
    return 0;
  }
  entry->token = next_decimal_token++;
  entry->format = format;
  entry->next = decimal_entries;
  decimal_entries = entry;
  return entry->token;
}

static void NativeDecimalFormat_close(JNIEnv *env, jclass cls, jlong token) {
  (void)cls;
  DecimalEntry **cursor = &decimal_entries;
  while (*cursor != NULL && (*cursor)->token != token)
    cursor = &(*cursor)->next;
  if (*cursor == NULL) {
    throw_new(env, "java/lang/IllegalStateException",
              "invalid decimal formatter token");
    return;
  }
  DecimalEntry *entry = *cursor;
  *cursor = entry->next;
  ICU(unum_close)(entry->format);
  free(entry);
}

static jlong NativeDecimalFormat_cloneImpl(JNIEnv *env, jclass cls,
                                           jlong token) {
  (void)cls;
  DecimalEntry *source = decimal_entry(env, token);
  if (source == NULL)
    return 0;
  UErrorCode status = U_ZERO_ERROR;
  UNumberFormat *clone = ICU(unum_clone)(source->format, &status);
  if (clone == NULL || !icu_ok(env, "unum_clone", status))
    return 0;
  DecimalEntry *entry = (DecimalEntry *)malloc(sizeof(*entry));
  if (entry == NULL) {
    ICU(unum_close)(clone);
    throw_new(env, "java/lang/OutOfMemoryError", "decimal clone");
    return 0;
  }
  entry->token = next_decimal_token++;
  entry->format = clone;
  entry->next = decimal_entries;
  decimal_entries = entry;
  return entry->token;
}

static void NativeDecimalFormat_applyPatternImpl(JNIEnv *env, jclass cls,
                                                 jlong token,
                                                 jboolean localized,
                                                 jstring pattern_value) {
  (void)cls;
  DecimalEntry *entry = decimal_entry(env, token);
  if (entry == NULL)
    return;
  jsize length = 0;
  UChar *pattern = string_chars(env, pattern_value, &length);
  if (pattern == NULL)
    return;
  UErrorCode status = U_ZERO_ERROR;
  UParseError error;
  ICU(unum_applyPattern)
  (entry->format, localized, pattern, length, &error, &status);
  free(pattern);
  icu_ok(env, "unum_applyPattern", status);
}

struct ToPatternRead {
  UNumberFormat *format;
  UBool localized;
};
static int32_t read_pattern(void *raw, UChar *out, int32_t capacity,
                            UErrorCode *status) {
  struct ToPatternRead *p = (struct ToPatternRead *)raw;
  return ICU(unum_toPattern)(p->format, p->localized, out, capacity, status);
}
static jstring NativeDecimalFormat_toPatternImpl(JNIEnv *env, jclass cls,
                                                 jlong token,
                                                 jboolean localized) {
  (void)cls;
  DecimalEntry *entry = decimal_entry(env, token);
  if (entry == NULL)
    return NULL;
  struct ToPatternRead read = {entry->format, localized};
  return buffered_string(env, read_pattern, &read, "unum_toPattern");
}

static jcharArray NativeDecimalFormat_formatLong(JNIEnv *env, jclass cls,
                                                 jlong token, jlong value,
                                                 jobject iterator) {
  (void)cls;
  DecimalEntry *entry = decimal_entry(env, token);
  if (entry == NULL)
    return NULL;
  UErrorCode status = U_ZERO_ERROR;
  int32_t length =
      ICU(unum_formatInt64)(entry->format, value, NULL, 0, NULL, &status);
  if (status != U_BUFFER_OVERFLOW_ERROR &&
      !icu_ok(env, "unum_formatInt64", status))
    return NULL;
  status = U_ZERO_ERROR;
  UChar *buffer = (UChar *)malloc(((size_t)length + 1u) * sizeof(UChar));
  if (buffer == NULL) {
    throw_new(env, "java/lang/OutOfMemoryError", "formatted number");
    return NULL;
  }
  length = ICU(unum_formatInt64)(entry->format, value, buffer, length + 1, NULL,
                                 &status);
  if (!icu_ok(env, "unum_formatInt64", status)) {
    free(buffer);
    return NULL;
  }
  if (iterator != NULL) {
    jclass iterator_class = (*env)->GetObjectClass(env, iterator);
    jmethodID set_data =
        (*env)->GetMethodID(env, iterator_class, "setData", "([I)V");
    const size_t capacity = ((size_t)length + UNUM_FIELD_COUNT) * 3u;
    jint *fields = (jint *)malloc(capacity * sizeof(jint));
    if (set_data == NULL || fields == NULL) {
      free(fields);
      free(buffer);
      if (fields == NULL)
        throw_new(env, "java/lang/OutOfMemoryError", "number field positions");
      return NULL;
    }
    size_t count = 0;
    int32_t integer_begin = 0;
    int32_t integer_end = length;
    for (int field = 0; field < UNUM_FIELD_COUNT; ++field) {
      if (field == UNUM_GROUPING_SEPARATOR_FIELD)
        continue;
      UFieldPosition position = {field, 0, 0};
      status = U_ZERO_ERROR;
      ICU(unum_formatInt64)
      (entry->format, value, buffer, length + 1, &position, &status);
      if (!icu_ok(env, "unum_formatInt64 field", status)) {
        free(fields);
        free(buffer);
        return NULL;
      }
      if (position.endIndex > position.beginIndex) {
        if (field == UNUM_INTEGER_FIELD) {
          integer_begin = position.beginIndex;
          integer_end = position.endIndex;
        }
        fields[count++] = field;
        fields[count++] = position.beginIndex;
        fields[count++] = position.endIndex;
      }
    }
    UChar grouping[16];
    status = U_ZERO_ERROR;
    const int32_t grouping_length = ICU(unum_getSymbol)(
        entry->format, UNUM_GROUPING_SEPARATOR_SYMBOL, grouping, 16, &status);
    if (!icu_ok(env, "unum_getSymbol grouping", status)) {
      free(fields);
      free(buffer);
      return NULL;
    }
    if (grouping_length > 0 && grouping_length <= 16) {
      for (int32_t at = integer_begin; at + grouping_length <= integer_end;
           ++at) {
        if (memcmp(buffer + at, grouping,
                   (size_t)grouping_length * sizeof(UChar)) == 0) {
          fields[count++] = UNUM_GROUPING_SEPARATOR_FIELD;
          fields[count++] = at;
          fields[count++] = at + grouping_length;
          at += grouping_length - 1;
        }
      }
    }
    jintArray data = count == 0 ? NULL : (*env)->NewIntArray(env, (jsize)count);
    if (count != 0 && data == NULL) {
      free(fields);
      free(buffer);
      return NULL;
    }
    if (data != NULL)
      (*env)->SetIntArrayRegion(env, data, 0, (jsize)count, fields);
    (*env)->CallVoidMethod(env, iterator, set_data, data);
    if (data != NULL)
      (*env)->DeleteLocalRef(env, data);
    free(fields);
  }
  jcharArray result = (*env)->NewCharArray(env, length);
  if (result != NULL)
    (*env)->SetCharArrayRegion(env, result, 0, length, (const jchar *)buffer);
  free(buffer);
  return result;
}

static jint NativeDecimalFormat_getAttribute(JNIEnv *env, jclass cls,
                                             jlong token, jint attribute) {
  (void)cls;
  DecimalEntry *entry = decimal_entry(env, token);
  return entry == NULL ? 0 : ICU(unum_getAttribute)(entry->format, attribute);
}
static void NativeDecimalFormat_setAttribute(JNIEnv *env, jclass cls,
                                             jlong token, jint attribute,
                                             jint value) {
  (void)cls;
  DecimalEntry *entry = decimal_entry(env, token);
  if (entry != NULL)
    ICU(unum_setAttribute)(entry->format, attribute, value);
}
static void NativeDecimalFormat_setRoundingMode(JNIEnv *env, jclass cls,
                                                jlong token, jint mode,
                                                jdouble increment) {
  (void)cls;
  DecimalEntry *entry = decimal_entry(env, token);
  if (entry == NULL)
    return;
  ICU(unum_setAttribute)(entry->format, UNUM_ROUNDING_MODE, mode);
  ICU(unum_setDoubleAttribute)
  (entry->format, UNUM_ROUNDING_INCREMENT, increment);
}
static void NativeDecimalFormat_setSymbol(JNIEnv *env, jclass cls, jlong token,
                                          jint symbol, jstring value) {
  (void)cls;
  DecimalEntry *entry = decimal_entry(env, token);
  if (entry == NULL)
    return;
  jsize length = 0;
  UChar *chars = string_chars(env, value, &length);
  if (chars == NULL)
    return;
  UErrorCode status = U_ZERO_ERROR;
  ICU(unum_setSymbol)(entry->format, symbol, chars, length, &status);
  free(chars);
  icu_ok(env, "unum_setSymbol", status);
}
static void NativeDecimalFormat_setTextAttribute(JNIEnv *env, jclass cls,
                                                 jlong token, jint attribute,
                                                 jstring value) {
  (void)cls;
  DecimalEntry *entry = decimal_entry(env, token);
  if (entry == NULL)
    return;
  jsize length = 0;
  UChar *chars = string_chars(env, value, &length);
  if (chars == NULL)
    return;
  UErrorCode status = U_ZERO_ERROR;
  ICU(unum_setTextAttribute)(entry->format, attribute, chars, length, &status);
  free(chars);
  icu_ok(env, "unum_setTextAttribute", status);
}
struct TextAttributeRead {
  UNumberFormat *format;
  int attribute;
};
static int32_t read_text_attribute(void *raw, UChar *out, int32_t capacity,
                                   UErrorCode *status) {
  struct TextAttributeRead *p = (struct TextAttributeRead *)raw;
  return ICU(unum_getTextAttribute)(p->format, p->attribute, out, capacity,
                                    status);
}
static jstring NativeDecimalFormat_getTextAttribute(JNIEnv *env, jclass cls,
                                                    jlong token,
                                                    jint attribute) {
  (void)cls;
  DecimalEntry *entry = decimal_entry(env, token);
  if (entry == NULL)
    return NULL;
  struct TextAttributeRead read = {entry->format, attribute};
  return buffered_string(env, read_text_attribute, &read,
                         "unum_getTextAttribute");
}
static void NativeDecimalFormat_setDecimalFormatSymbols(
    JNIEnv *env, jclass cls, jlong token, jstring currency_symbol,
    jchar decimal_separator, jchar digit, jstring exponent_separator,
    jchar grouping_separator, jstring infinity, jstring intl_currency,
    jchar minus_sign, jchar monetary_separator, jstring nan,
    jchar pattern_separator, jchar percent, jchar per_mill, jchar zero_digit) {
  (void)cls;
  DecimalEntry *entry = decimal_entry(env, token);
  if (entry == NULL)
    return;
  apply_symbols(env, entry->format, currency_symbol, decimal_separator, digit,
                exponent_separator, grouping_separator, infinity, intl_currency,
                minus_sign, monetary_separator, nan, pattern_separator, percent,
                per_mill, zero_digit);
}

static jobject NativeDecimalFormat_parse(JNIEnv *env, jclass cls, jlong token,
                                         jstring text_value, jobject position,
                                         jboolean parse_big_decimal) {
  (void)cls;
  if (parse_big_decimal) {
    throw_new(env, "java/lang/UnsupportedOperationException",
              "BigDecimal parsing is outside the audited ICU closure");
    return NULL;
  }
  DecimalEntry *entry = decimal_entry(env, token);
  if (entry == NULL)
    return NULL;
  if (position == NULL) {
    throw_new(env, "java/lang/NullPointerException", "position == null");
    return NULL;
  }
  jclass position_class = (*env)->GetObjectClass(env, position);
  jmethodID get_index =
      (*env)->GetMethodID(env, position_class, "getIndex", "()I");
  jmethodID set_index =
      (*env)->GetMethodID(env, position_class, "setIndex", "(I)V");
  jmethodID set_error =
      (*env)->GetMethodID(env, position_class, "setErrorIndex", "(I)V");
  if (get_index == NULL || set_index == NULL || set_error == NULL)
    return NULL;
  jint index = (*env)->CallIntMethod(env, position, get_index);
  jsize length = 0;
  UChar *text = string_chars(env, text_value, &length);
  if (text == NULL)
    return NULL;
  if (index < 0 || index > length) {
    free(text);
    return NULL;
  }
  UErrorCode double_status = U_ZERO_ERROR;
  int32_t double_end = index;
  const double double_result = ICU(unum_parseDouble)(
      entry->format, text, length, &double_end, &double_status);
  UErrorCode integer_status = U_ZERO_ERROR;
  int32_t integer_end = index;
  const int64_t integer_result = ICU(unum_parseInt64)(
      entry->format, text, length, &integer_end, &integer_status);
  free(text);
  if (double_status > U_ZERO_ERROR || double_end == index) {
    const jint error = double_end > index ? double_end : index;
    (*env)->CallVoidMethod(env, position, set_error, error);
    return NULL;
  }
  (*env)->CallVoidMethod(env, position, set_index, double_end);
  if (integer_status <= U_ZERO_ERROR && integer_end == double_end &&
      (double)integer_result == double_result) {
    jclass long_class = (*env)->FindClass(env, "java/lang/Long");
    jmethodID value_of = (*env)->GetStaticMethodID(env, long_class, "valueOf",
                                                   "(J)Ljava/lang/Long;");
    return value_of == NULL
               ? NULL
               : (*env)->CallStaticObjectMethod(env, long_class, value_of,
                                                (jlong)integer_result);
  }
  jclass double_class = (*env)->FindClass(env, "java/lang/Double");
  jmethodID value_of = (*env)->GetStaticMethodID(env, double_class, "valueOf",
                                                 "(D)Ljava/lang/Double;");
  return value_of == NULL
             ? NULL
             : (*env)->CallStaticObjectMethod(env, double_class, value_of,
                                              (jdouble)double_result);
}

static void TimeZoneNames_fillZoneStrings(JNIEnv *env, jclass cls,
                                          jstring locale_value,
                                          jobjectArray rows) {
  (void)cls;
  char *locale = string_utf8(env, locale_value);
  if (locale == NULL)
    return;
  const jsize count = (*env)->GetArrayLength(env, rows);
  for (jsize i = 0; i < count; ++i) {
    jobjectArray row =
        (jobjectArray)(*env)->GetObjectArrayElement(env, rows, i);
    if (row == NULL || (*env)->GetArrayLength(env, row) < 5) {
      free(locale);
      throw_new(env, "java/lang/IllegalArgumentException",
                "invalid zone strings row");
      return;
    }
    jstring id_value = (jstring)(*env)->GetObjectArrayElement(env, row, 0);
    jsize id_length = 0;
    UChar *id = string_chars(env, id_value, &id_length);
    if (id == NULL) {
      free(locale);
      return;
    }
    if (!((id_length == 3 && id[0] == 'G' && id[1] == 'M' && id[2] == 'T') ||
          (id_length == 3 && id[0] == 'U' && id[1] == 'T' && id[2] == 'C'))) {
      free(id);
      free(locale);
      throw_new(env, "java/lang/UnsupportedOperationException",
                "named time-zone database is outside the audited closure");
      return;
    }
    UErrorCode status = U_ZERO_ERROR;
    UCalendar *calendar =
        ICU(ucal_open)(id, id_length, locale, UCAL_DEFAULT, &status);
    free(id);
    if (calendar == NULL || !icu_ok(env, "ucal_open", status)) {
      free(locale);
      return;
    }
    for (int column = 1; column < 5; ++column) {
      status = U_ZERO_ERROR;
      UChar buffer[128];
      int style = column == 1   ? UCAL_STANDARD
                  : column == 2 ? UCAL_SHORT_STANDARD
                  : column == 3 ? UCAL_DST
                                : UCAL_SHORT_DST;
      int32_t length = ICU(ucal_getTimeZoneDisplayName)(calendar, style, locale,
                                                        buffer, 128, &status);
      if (!icu_ok(env, "ucal_getTimeZoneDisplayName", status)) {
        ICU(ucal_close)(calendar);
        free(locale);
        return;
      }
      jstring value = new_ustring(env, buffer, length);
      (*env)->SetObjectArrayElement(env, row, column, value);
      (*env)->DeleteLocalRef(env, value);
    }
    ICU(ucal_close)(calendar);
    (*env)->DeleteLocalRef(env, row);
  }
  free(locale);
}

static const JNINativeMethod icu_methods[] = {
    {"getBestDateTimePatternNative",
     "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
     (void *)ICU_getBestDateTimePatternNative},
    {"getCurrencyCode", "(Ljava/lang/String;)Ljava/lang/String;",
     (void *)ICU_getCurrencyCode},
    {"getCurrencyDisplayName",
     "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
     (void *)ICU_getCurrencyDisplayName},
    {"getCurrencyFractionDigits", "(Ljava/lang/String;)I",
     (void *)ICU_getCurrencyFractionDigits},
    {"getCurrencySymbol",
     "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
     (void *)ICU_getCurrencySymbol},
    {"getISOCountriesNative", "()[Ljava/lang/String;",
     (void *)ICU_getISOCountriesNative},
    {"getISOLanguagesNative", "()[Ljava/lang/String;",
     (void *)ICU_getISOLanguagesNative},
    {"initLocaleDataNative", "(Ljava/lang/String;Llibcore/icu/LocaleData;)Z",
     (void *)ICU_initLocaleDataNative},
    {"toLowerCase", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
     (void *)ICU_toLowerCase},
    {"toUpperCase", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
     (void *)ICU_toUpperCase}};
static const JNINativeMethod decimal_methods[] = {
    {"applyPatternImpl", "(JZLjava/lang/String;)V",
     (void *)NativeDecimalFormat_applyPatternImpl},
    {"cloneImpl", "(J)J", (void *)NativeDecimalFormat_cloneImpl},
    {"close", "(J)V", (void *)NativeDecimalFormat_close},
    {"formatLong",
     "(JJLlibcore/icu/NativeDecimalFormat$FieldPositionIterator;)[C",
     (void *)NativeDecimalFormat_formatLong},
    {"getAttribute", "(JI)I", (void *)NativeDecimalFormat_getAttribute},
    {"getTextAttribute", "(JI)Ljava/lang/String;",
     (void *)NativeDecimalFormat_getTextAttribute},
    {"open",
     "(Ljava/lang/String;Ljava/lang/String;CCLjava/lang/String;CLjava/lang/"
     "String;Ljava/lang/String;CCLjava/lang/String;CCCC)J",
     (void *)NativeDecimalFormat_open},
    {"parse",
     "(JLjava/lang/String;Ljava/text/ParsePosition;Z)Ljava/lang/Number;",
     (void *)NativeDecimalFormat_parse},
    {"setAttribute", "(JII)V", (void *)NativeDecimalFormat_setAttribute},
    {"setDecimalFormatSymbols",
     "(JLjava/lang/String;CCLjava/lang/String;CLjava/lang/String;Ljava/lang/"
     "String;CCLjava/lang/String;CCCC)V",
     (void *)NativeDecimalFormat_setDecimalFormatSymbols},
    {"setRoundingMode", "(JID)V", (void *)NativeDecimalFormat_setRoundingMode},
    {"setSymbol", "(JILjava/lang/String;)V",
     (void *)NativeDecimalFormat_setSymbol},
    {"setTextAttribute", "(JILjava/lang/String;)V",
     (void *)NativeDecimalFormat_setTextAttribute},
    {"toPatternImpl", "(JZ)Ljava/lang/String;",
     (void *)NativeDecimalFormat_toPatternImpl}};
static const JNINativeMethod timezone_methods[] = {
    {"fillZoneStrings", "(Ljava/lang/String;[[Ljava/lang/String;)V",
     (void *)TimeZoneNames_fillZoneStrings}};

static int register_methods(JNIEnv *env, const char *name,
                            const JNINativeMethod *methods, jint count) {
  jclass cls = (*env)->FindClass(env, name);
  return cls != NULL && (*env)->RegisterNatives(env, cls, methods, count) == 0;
}

int ogplay_icu_on_load(JNIEnv *env) {
  if (!load_icu_data(env))
    return 0;
  return register_methods(
             env, "libcore/icu/ICU", icu_methods,
             (jint)(sizeof(icu_methods) / sizeof(icu_methods[0]))) &&
         register_methods(
             env, "libcore/icu/NativeDecimalFormat", decimal_methods,
             (jint)(sizeof(decimal_methods) / sizeof(decimal_methods[0]))) &&
         register_methods(
             env, "libcore/icu/TimeZoneNames", timezone_methods,
             (jint)(sizeof(timezone_methods) / sizeof(timezone_methods[0])));
}

void ogplay_icu_release(void) {
  while (decimal_entries != NULL) {
    DecimalEntry *entry = decimal_entries;
    decimal_entries = entry->next;
    ICU(unum_close)(entry->format);
    free(entry);
  }
  if (icu_data != NULL)
    ICU(u_cleanup)();
  free(icu_data);
  icu_data = NULL;
}
