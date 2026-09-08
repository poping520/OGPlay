#pragma once

/* Narrow ICU 51 C ABI used by the API 19 guest JNI bridge.  Keep this file
 * C-only: the bundled Android ICU libraries use the platform STLport C++ ABI.
 */
#include <stdint.h>

typedef uint16_t UChar;
typedef int8_t UBool;
typedef double UDate;
typedef int32_t UErrorCode;
typedef struct UNumberFormat UNumberFormat;
typedef struct UDateFormat UDateFormat;
typedef struct UDateTimePatternGenerator UDateTimePatternGenerator;
typedef struct UCalendar UCalendar;
typedef struct UResourceBundle UResourceBundle;
typedef struct UParseError {
  int32_t line;
  int32_t offset;
  UChar pre_context[16];
  UChar post_context[16];
} UParseError;
typedef struct UFieldPosition {
  int32_t field;
  int32_t beginIndex;
  int32_t endIndex;
} UFieldPosition;

enum {
  U_USING_DEFAULT_WARNING = -127,
  U_ZERO_ERROR = 0,
  U_ILLEGAL_ARGUMENT_ERROR = 1,
  U_MEMORY_ALLOCATION_ERROR = 7,
  U_INDEX_OUTOFBOUNDS_ERROR = 8,
  U_BUFFER_OVERFLOW_ERROR = 15,
  U_UNSUPPORTED_ERROR = 16
};
enum { UDATA_NO_FILES = 3 };
enum {
  UNUM_PATTERN_DECIMAL = 0,
  UNUM_DECIMAL = 1,
  UNUM_CURRENCY = 2,
  UNUM_PERCENT = 3
};
enum { UCURR_SYMBOL_NAME = 0, UCURR_LONG_NAME = 1 };
enum {
  UDAT_FULL = 0,
  UDAT_LONG = 1,
  UDAT_MEDIUM = 2,
  UDAT_SHORT = 3,
  UDAT_NONE = -1
};
enum {
  UDAT_ERAS = 0,
  UDAT_MONTHS = 1,
  UDAT_SHORT_MONTHS = 2,
  UDAT_WEEKDAYS = 3,
  UDAT_SHORT_WEEKDAYS = 4,
  UDAT_AM_PMS = 5,
  UDAT_NARROW_MONTHS = 8,
  UDAT_NARROW_WEEKDAYS = 9,
  UDAT_STANDALONE_MONTHS = 10,
  UDAT_STANDALONE_SHORT_MONTHS = 11,
  UDAT_STANDALONE_NARROW_MONTHS = 12,
  UDAT_STANDALONE_WEEKDAYS = 13,
  UDAT_STANDALONE_SHORT_WEEKDAYS = 14,
  UDAT_STANDALONE_NARROW_WEEKDAYS = 15
};
enum { UCAL_TRADITIONAL = 0, UCAL_DEFAULT = UCAL_TRADITIONAL };
enum { UCAL_FIRST_DAY_OF_WEEK = 1, UCAL_MINIMAL_DAYS_IN_FIRST_WEEK = 2 };
enum {
  UCAL_STANDARD = 0,
  UCAL_SHORT_STANDARD = 1,
  UCAL_DST = 2,
  UCAL_SHORT_DST = 3
};
enum {
  UNUM_DECIMAL_SEPARATOR_SYMBOL = 0,
  UNUM_GROUPING_SEPARATOR_SYMBOL = 1,
  UNUM_PATTERN_SEPARATOR_SYMBOL = 2,
  UNUM_PERCENT_SYMBOL = 3,
  UNUM_ZERO_DIGIT_SYMBOL = 4,
  UNUM_DIGIT_SYMBOL = 5,
  UNUM_MINUS_SIGN_SYMBOL = 6,
  UNUM_CURRENCY_SYMBOL = 8,
  UNUM_INTL_CURRENCY_SYMBOL = 9,
  UNUM_MONETARY_SEPARATOR_SYMBOL = 10,
  UNUM_EXPONENTIAL_SYMBOL = 11,
  UNUM_PERMILL_SYMBOL = 12,
  UNUM_INFINITY_SYMBOL = 14,
  UNUM_NAN_SYMBOL = 15,
  UNUM_MONETARY_GROUPING_SEPARATOR_SYMBOL = 17,
  UNUM_ONE_DIGIT_SYMBOL = 18
};
enum { UNUM_ROUNDING_INCREMENT = 12, UNUM_ROUNDING_MODE = 11 };
enum {
  UNUM_INTEGER_FIELD = 0,
  UNUM_FRACTION_FIELD = 1,
  UNUM_DECIMAL_SEPARATOR_FIELD = 2,
  UNUM_EXPONENT_SYMBOL_FIELD = 3,
  UNUM_EXPONENT_SIGN_FIELD = 4,
  UNUM_EXPONENT_FIELD = 5,
  UNUM_GROUPING_SEPARATOR_FIELD = 6,
  UNUM_CURRENCY_FIELD = 7,
  UNUM_PERCENT_FIELD = 8,
  UNUM_PERMILL_FIELD = 9,
  UNUM_SIGN_FIELD = 10,
  UNUM_FIELD_COUNT = 11
};

#define ICU(name) name##_51

extern void ICU(u_setDataDirectory)(const char *directory);
extern void ICU(udata_setCommonData)(const void *data, UErrorCode *status);
extern void ICU(udata_setFileAccess)(int32_t access, UErrorCode *status);
extern void ICU(u_init)(UErrorCode *status);
extern void ICU(u_cleanup)(void);
extern const char *ICU(u_errorName)(UErrorCode status);
extern const char *const *ICU(uloc_getISOLanguages)(void);
extern const char *const *ICU(uloc_getISOCountries)(void);
extern int32_t ICU(uloc_getCountry)(const char *, char *, int32_t,
                                    UErrorCode *);
extern int32_t ICU(uloc_getParent)(const char *, char *, int32_t, UErrorCode *);
extern UDateTimePatternGenerator *ICU(udatpg_open)(const char *, UErrorCode *);
extern void ICU(udatpg_close)(UDateTimePatternGenerator *);
extern int32_t ICU(udatpg_getBestPattern)(UDateTimePatternGenerator *,
                                          const UChar *, int32_t, UChar *,
                                          int32_t, UErrorCode *);
extern int32_t ICU(ucurr_forLocale)(const char *, UChar *, int32_t,
                                    UErrorCode *);
extern const UChar *ICU(ucurr_getName)(const UChar *, const char *, int32_t,
                                       UBool *, int32_t *, UErrorCode *);
extern int32_t ICU(ucurr_getDefaultFractionDigits)(const UChar *, UErrorCode *);
extern UBool ICU(ucurr_isAvailable)(const UChar *, UDate, UDate, UErrorCode *);
extern UNumberFormat *ICU(unum_open)(int32_t, const UChar *, int32_t,
                                     const char *, UParseError *, UErrorCode *);
extern void ICU(unum_close)(UNumberFormat *);
extern UNumberFormat *ICU(unum_clone)(const UNumberFormat *, UErrorCode *);
extern int32_t ICU(unum_formatInt64)(const UNumberFormat *, int64_t, UChar *,
                                     int32_t, UFieldPosition *, UErrorCode *);
extern int64_t ICU(unum_parseInt64)(const UNumberFormat *, const UChar *,
                                    int32_t, int32_t *, UErrorCode *);
extern double ICU(unum_parseDouble)(const UNumberFormat *, const UChar *,
                                    int32_t, int32_t *, UErrorCode *);
extern int32_t ICU(unum_getAttribute)(const UNumberFormat *, int32_t);
extern void ICU(unum_setAttribute)(UNumberFormat *, int32_t, int32_t);
extern void ICU(unum_setDoubleAttribute)(UNumberFormat *, int32_t, double);
extern int32_t ICU(unum_getTextAttribute)(const UNumberFormat *, int32_t,
                                          UChar *, int32_t, UErrorCode *);
extern void ICU(unum_setTextAttribute)(UNumberFormat *, int32_t, const UChar *,
                                       int32_t, UErrorCode *);
extern int32_t ICU(unum_toPattern)(const UNumberFormat *, UBool, UChar *,
                                   int32_t, UErrorCode *);
extern void ICU(unum_applyPattern)(UNumberFormat *, UBool, const UChar *,
                                   int32_t, UParseError *, UErrorCode *);
extern int32_t ICU(unum_getSymbol)(const UNumberFormat *, int32_t, UChar *,
                                   int32_t, UErrorCode *);
extern void ICU(unum_setSymbol)(UNumberFormat *, int32_t, const UChar *,
                                int32_t, UErrorCode *);
extern UDateFormat *ICU(udat_open)(int32_t, int32_t, const char *,
                                   const UChar *, int32_t, const UChar *,
                                   int32_t, UErrorCode *);
extern void ICU(udat_close)(UDateFormat *);
extern int32_t ICU(udat_toPattern)(const UDateFormat *, UBool, UChar *, int32_t,
                                   UErrorCode *);
extern int32_t ICU(udat_countSymbols)(const UDateFormat *, int32_t);
extern int32_t ICU(udat_getSymbols)(const UDateFormat *, int32_t, int32_t,
                                    UChar *, int32_t, UErrorCode *);
extern UCalendar *ICU(ucal_open)(const UChar *, int32_t, const char *, int32_t,
                                 UErrorCode *);
extern void ICU(ucal_close)(UCalendar *);
extern int32_t ICU(ucal_getAttribute)(const UCalendar *, int32_t);
extern int32_t ICU(ucal_getTimeZoneDisplayName)(const UCalendar *, int32_t,
                                                const char *, UChar *, int32_t,
                                                UErrorCode *);
extern UResourceBundle *ICU(ures_open)(const char *, const char *,
                                       UErrorCode *);
extern UResourceBundle *ICU(ures_openDirect)(const char *, const char *,
                                             UErrorCode *);
extern void ICU(ures_close)(UResourceBundle *);
extern UResourceBundle *ICU(ures_getByKey)(const UResourceBundle *,
                                           const char *, UResourceBundle *,
                                           UErrorCode *);
extern UResourceBundle *ICU(ures_getByIndex)(const UResourceBundle *, int32_t,
                                             UResourceBundle *, UErrorCode *);
extern const UChar *ICU(ures_getString)(const UResourceBundle *, int32_t *,
                                        UErrorCode *);
extern const UChar *ICU(ures_getStringByIndex)(const UResourceBundle *, int32_t,
                                               int32_t *, UErrorCode *);
extern const UChar *ICU(ures_getStringByKey)(const UResourceBundle *,
                                             const char *, int32_t *,
                                             UErrorCode *);
extern int32_t ICU(u_strToLower)(UChar *, int32_t, const UChar *, int32_t,
                                 const char *, UErrorCode *);
extern int32_t ICU(u_strToUpper)(UChar *, int32_t, const UChar *, int32_t,
                                 const char *, UErrorCode *);
