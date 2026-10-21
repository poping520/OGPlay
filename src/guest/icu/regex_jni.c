/* API19 Pattern/Matcher native protocol over the ICU51 C ABI. */
#include <jni.h>
#include <stdint.h>
#include <pthread.h>
#include <stdlib.h>
#include "icu51_capi.h"

typedef struct RegexEntry {
  jlong token;
  int matcher;
  URegularExpression *regex;
  UChar *input;
  struct RegexEntry *next;
} RegexEntry;
static RegexEntry *entries;
static pthread_mutex_t registry_mutex = PTHREAD_MUTEX_INITIALIZER;
static jlong next_token = 1;

static void fail(JNIEnv *env, const char *type, const char *message) {
  jclass cls = (*env)->FindClass(env, type);
  if (cls != NULL) (*env)->ThrowNew(env, cls, message);
}
static int checked(JNIEnv *env, UErrorCode status) {
  if (status <= 0) return 1;
  const char *type = status == U_MEMORY_ALLOCATION_ERROR ? "java/lang/OutOfMemoryError" :
      status == U_INDEX_OUTOFBOUNDS_ERROR ? "java/lang/IndexOutOfBoundsException" : "java/lang/IllegalArgumentException";
  fail(env, type, ICU(u_errorName)(status));
  return 0;
}
static RegexEntry *lookup(JNIEnv *env, jlong token, int matcher) {
  pthread_mutex_lock(&registry_mutex);
  RegexEntry *result = entries;
  while (result != NULL && (result->token != token || result->matcher != matcher)) result = result->next;
  pthread_mutex_unlock(&registry_mutex);
  /* Java Matcher synchronization and the live Java owner protect each peer.
   * The registry lock only protects different owners' publication/removal. */
  if (result == NULL) fail(env, "java/lang/IllegalStateException", "invalid regex token");
  return result;
}
static jlong publish(JNIEnv *env, URegularExpression *regex, int matcher) {
  RegexEntry *e = (RegexEntry *)calloc(1, sizeof(*e));
  if (e == NULL) {
    ICU(uregex_close)(regex);
    fail(env, "java/lang/OutOfMemoryError", "regex registry");
    return 0;
  }
  pthread_mutex_lock(&registry_mutex);
  e->token = next_token++;
  e->matcher = matcher;
  e->regex = regex;
  e->next = entries;
  entries = e;
  pthread_mutex_unlock(&registry_mutex);
  return e->token;
}
static UChar *chars(JNIEnv *env, jstring string, jsize *length) {
  if (string == NULL) { fail(env, "java/lang/NullPointerException", "input == null"); return NULL; }
  *length = (*env)->GetStringLength(env, string);
  UChar *result = (UChar *)calloc((size_t)*length + 1, sizeof(UChar));
  if (result == NULL) { fail(env, "java/lang/OutOfMemoryError", "regex text"); return NULL; }
  (*env)->GetStringRegion(env, string, 0, *length, (jchar *)result);
  if ((*env)->ExceptionCheck(env)) { free(result); return NULL; }
  return result;
}
static jlong Pattern_compileImpl(JNIEnv *env, jclass cls, jstring expression, jint flags) {
  (void)cls;
  jsize length;
  UChar *text = chars(env, expression, &length);
  if (text == NULL) return 0;
  UParseError error = {.offset = -1};
  UErrorCode status = 0;
  URegularExpression *regex = ICU(uregex_open)(text, length == 0 ? -1 : length, (uint32_t)flags | 512u, &error, &status);
  free(text);
  if (status > 0) {
    if (regex != NULL) ICU(uregex_close)(regex);
    jclass type = (*env)->FindClass(env, "java/util/regex/PatternSyntaxException");
    if (type == NULL) return 0;
    jmethodID ctor = (*env)->GetMethodID(env, type, "<init>", "(Ljava/lang/String;Ljava/lang/String;I)V");
    if (ctor == NULL) return 0;
    jstring message = (*env)->NewStringUTF(env, ICU(u_errorName)(status));
    if (message == NULL) return 0;
    jobject exception = (*env)->NewObject(env, type, ctor, message, expression, (jint)error.offset);
    if (exception != NULL) (*env)->Throw(env, exception);
    return 0;
  }
  return publish(env, regex, 0);
}
static void close_regex(JNIEnv *env, jlong token, int matcher) {
  if (token == 0) return;
  pthread_mutex_lock(&registry_mutex);
  RegexEntry **at = &entries;
  while (*at != NULL && ((*at)->token != token || (*at)->matcher != matcher)) at = &(*at)->next;
  if (*at == NULL) {
    pthread_mutex_unlock(&registry_mutex);
    fail(env, "java/lang/IllegalStateException", "invalid regex token"); return;
  }
  RegexEntry *e = *at;
  *at = e->next;
  pthread_mutex_unlock(&registry_mutex);
  ICU(uregex_close)(e->regex);
  free(e->input);
  free(e);
}
static void Pattern_closeImpl(JNIEnv *env, jclass cls, jlong token) { (void)cls; close_regex(env, token, 0); }
static void Matcher_closeImpl(JNIEnv *env, jclass cls, jlong token) { (void)cls; close_regex(env, token, 1); }
static jlong Matcher_openImpl(JNIEnv *env, jclass cls, jlong token) {
  (void)cls;
  RegexEntry *pattern = lookup(env, token, 0);
  if (pattern == NULL) return 0;
  UErrorCode status = 0;
  URegularExpression *regex = ICU(uregex_clone)(pattern->regex, &status);
  if (!checked(env, status)) { if (regex != NULL) ICU(uregex_close)(regex); return 0; }
  return publish(env, regex, 1);
}
static void Matcher_setInputImpl(JNIEnv *env, jclass cls, jlong token, jstring input, jint start, jint end) {
  (void)cls;
  RegexEntry *e = lookup(env, token, 1);
  if (e == NULL) return;
  jsize length;
  UChar *text = chars(env, input, &length);
  if (text == NULL) return;
  if (start < 0 || end < start || end > length) {
    free(text); fail(env, "java/lang/IndexOutOfBoundsException", "invalid regex region"); return;
  }
  UErrorCode status = 0;
  ICU(uregex_setText)(e->regex, text, length, &status);
  if (!checked(env, status)) { free(text); return; }
  free(e->input);
  e->input = text;
  ICU(uregex_setRegion)(e->regex, start, end, &status);
  checked(env, status);
}
static jboolean match(JNIEnv *env, jlong token, jint start, jintArray offsets, int operation) {
  RegexEntry *e = lookup(env, token, 1);
  if (e == NULL) return JNI_FALSE;
  UErrorCode status = 0;
  UBool found = operation == 0 ? ICU(uregex_find)(e->regex, start, &status) :
      operation == 1 ? ICU(uregex_findNext)(e->regex, &status) :
      operation == 2 ? ICU(uregex_matches)(e->regex, -1, &status) : ICU(uregex_lookingAt)(e->regex, -1, &status);
  if (!checked(env, status) || !found) return JNI_FALSE;
  const int32_t count = ICU(uregex_groupCount)(e->regex, &status) + 1;
  if (!checked(env, status)) return JNI_FALSE;
  if (offsets == NULL || (*env)->GetArrayLength(env, offsets) / 2 < count) {
    fail(env, "java/lang/ArrayIndexOutOfBoundsException", "regex offsets too short"); return JNI_FALSE;
  }
  for (int32_t i = 0; i < count; ++i) {
    jint pair[2] = {ICU(uregex_start)(e->regex, i, &status), ICU(uregex_end)(e->regex, i, &status)};
    if (!checked(env, status)) return JNI_FALSE;
    (*env)->SetIntArrayRegion(env, offsets, 2*i, 2, pair);
    if ((*env)->ExceptionCheck(env)) return JNI_FALSE;
  }
  return JNI_TRUE;
}
static jboolean Matcher_findImpl(JNIEnv *e, jclass c, jlong t, jstring s, jint start, jintArray a) {
  (void)c; (void)s; return match(e,t,start,a,0);
}
#define MATCH_FN(name, op) \
static jboolean Matcher_##name(JNIEnv *e, jclass c, jlong t, jstring s, jintArray a) { \
  (void)c; (void)s; return match(e,t,-1,a,op); }
MATCH_FN(findNextImpl, 1)
MATCH_FN(matchesImpl, 2)
MATCH_FN(lookingAtImpl, 3)
static jint Matcher_groupCountImpl(JNIEnv *env, jclass cls, jlong token) {
  (void)cls; RegexEntry *e = lookup(env, token, 1); if (e == NULL) return 0;
  UErrorCode status = 0; jint value = ICU(uregex_groupCount)(e->regex, &status); checked(env,status); return value;
}
#define QUERY_FN(name, api) \
static jboolean Matcher_##name(JNIEnv *env, jclass cls, jlong token) { \
  (void)cls; RegexEntry *e = lookup(env, token, 1); if (e == NULL) return JNI_FALSE; \
  UErrorCode status = 0; UBool value = ICU(api)(e->regex, &status); checked(env,status); return value; }
QUERY_FN(hitEndImpl, uregex_hitEnd)
QUERY_FN(requireEndImpl, uregex_requireEnd)
#define BOUNDS_FN(name, api) \
static void Matcher_##name(JNIEnv *env, jclass cls, jlong token, jboolean value) { \
  (void)cls; RegexEntry *e = lookup(env, token, 1); if (e == NULL) return; \
  UErrorCode status = 0; ICU(api)(e->regex,value,&status); checked(env,status); }
BOUNDS_FN(useAnchoringBoundsImpl, uregex_useAnchoringBounds)
BOUNDS_FN(useTransparentBoundsImpl, uregex_useTransparentBounds)

static const JNINativeMethod patterns[] = {
  {"compileImpl", "(Ljava/lang/String;I)J", (void*)Pattern_compileImpl},
  {"closeImpl", "(J)V", (void*)Pattern_closeImpl}
};
static const JNINativeMethod matchers[] = {
  {"closeImpl", "(J)V", (void*)Matcher_closeImpl},
  {"findImpl", "(JLjava/lang/String;I[I)Z", (void*)Matcher_findImpl},
  {"findNextImpl", "(JLjava/lang/String;[I)Z", (void*)Matcher_findNextImpl},
  {"groupCountImpl", "(J)I", (void*)Matcher_groupCountImpl},
  {"hitEndImpl", "(J)Z", (void*)Matcher_hitEndImpl},
  {"lookingAtImpl", "(JLjava/lang/String;[I)Z", (void*)Matcher_lookingAtImpl},
  {"matchesImpl", "(JLjava/lang/String;[I)Z", (void*)Matcher_matchesImpl},
  {"openImpl", "(J)J", (void*)Matcher_openImpl},
  {"requireEndImpl", "(J)Z", (void*)Matcher_requireEndImpl},
  {"setInputImpl", "(JLjava/lang/String;II)V", (void*)Matcher_setInputImpl},
  {"useAnchoringBoundsImpl", "(JZ)V", (void*)Matcher_useAnchoringBoundsImpl},
  {"useTransparentBoundsImpl", "(JZ)V", (void*)Matcher_useTransparentBoundsImpl}
};
int ogplay_regex_on_load(JNIEnv *env) {
  jclass p = (*env)->FindClass(env, "java/util/regex/Pattern");
  if (p == NULL || (*env)->RegisterNatives(env,p,patterns,sizeof(patterns)/sizeof(patterns[0])) != 0) return 0;
  jclass m = (*env)->FindClass(env, "java/util/regex/Matcher");
  return m != NULL && (*env)->RegisterNatives(env,m,matchers,sizeof(matchers)/sizeof(matchers[0])) == 0;
}
void ogplay_regex_release(void) {
  while (entries != NULL) {
    RegexEntry *e = entries; entries = e->next;
    ICU(uregex_close)(e->regex); free(e->input); free(e);
  }
}
