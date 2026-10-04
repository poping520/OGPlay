/* Value-only API19 BIGNUM arithmetic. No OpenSSL structure layout or pointer tokens. */
#include <jni.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct bignum_st BIGNUM;
typedef struct bignum_ctx BN_CTX;
extern BIGNUM *BN_new(void);
extern void BN_free(BIGNUM *);
extern BIGNUM *BN_bin2bn(const unsigned char *, int, BIGNUM *);
extern int BN_bn2bin(const BIGNUM *, unsigned char *);
extern int BN_num_bits(const BIGNUM *);
extern int BN_cmp(const BIGNUM *, const BIGNUM *);
extern void BN_set_negative(BIGNUM *, int);
extern BN_CTX *BN_CTX_new(void);
extern void BN_CTX_free(BN_CTX *);
extern int BN_add(BIGNUM *, const BIGNUM *, const BIGNUM *);
extern int BN_mul(BIGNUM *, const BIGNUM *, const BIGNUM *, BN_CTX *);
extern int BN_mul_word(BIGNUM *, unsigned long);
extern int BN_div(BIGNUM *, BIGNUM *, const BIGNUM *, const BIGNUM *, BN_CTX *);
extern int BN_exp(BIGNUM *, const BIGNUM *, const BIGNUM *, BN_CTX *);
extern int BN_lshift(BIGNUM *, const BIGNUM *, int);
extern int BN_rshift(BIGNUM *, const BIGNUM *, int);

#define MAX_BYTES 1048576U
#define MAX_BITS (MAX_BYTES * 8U)
#define MAX_WORK UINT64_C(16777216)
enum operation { ADD, MUL, EXP, DIV, SHIFT, WORD };

static void fail(JNIEnv *e, const char *type, const char *message) {
    if ((*e)->ExceptionCheck(e)) return;
    jclass c = (*e)->FindClass(e, type);
    if (c) { (*e)->ThrowNew(e, c, message); (*e)->DeleteLocalRef(e, c); }
}
static int budget(JNIEnv *e, uint64_t bits, uint64_t work) {
    if (bits <= MAX_BITS && work <= MAX_WORK) return 1;
    fail(e, "java/lang/UnsupportedOperationException", "integer arithmetic exceeds value/work budget");
    return 0;
}
static BIGNUM *read_value(JNIEnv *e, jbyteArray array) {
    if (!array) { fail(e, "java/lang/NullPointerException", "integer value == null"); return NULL; }
    jsize n = (*e)->GetArrayLength(e, array);
    if ((*e)->ExceptionCheck(e)) return NULL;
    if (n < 1) { fail(e, "java/lang/IllegalArgumentException", "integer sign byte missing"); return NULL; }
    if (!budget(e, (uint64_t)(n-1)*8, 0)) return NULL;
    unsigned char *bytes = malloc((size_t)n);
    if (!bytes) { fail(e, "java/lang/OutOfMemoryError", "integer input allocation"); return NULL; }
    (*e)->GetByteArrayRegion(e, array, 0, n, (jbyte *)bytes);
    BIGNUM *v = NULL;
    if (!(*e)->ExceptionCheck(e)) {
        if (bytes[0] > 1) fail(e, "java/lang/IllegalArgumentException", "invalid integer sign");
        else {
            v = BN_bin2bn(bytes+1, n-1, NULL);
            if (v) BN_set_negative(v, bytes[0]);
            else fail(e, "java/lang/OutOfMemoryError", "integer BIGNUM allocation");
        }
    }
    free(bytes);
    return v;
}
static jbyteArray write_value(JNIEnv *e, const BIGNUM *v, const BIGNUM *zero) {
    int bits = BN_num_bits(v);
    if (!budget(e, (uint64_t)bits, 0)) return NULL;
    int n = (bits+7)/8;
    unsigned char *bytes = malloc((size_t)n+1);
    if (!bytes) { fail(e, "java/lang/OutOfMemoryError", "integer output allocation"); return NULL; }
    bytes[0] = BN_cmp(v, zero) < 0 ? 1 : 0;
    jbyteArray result = NULL;
    if (BN_bn2bin(v, bytes+1) != n)
        fail(e, "java/lang/ArithmeticException", "integer result encoding failed");
    else {
        result = (*e)->NewByteArray(e, n+1);
        if (result) (*e)->SetByteArrayRegion(e, result, 0, n+1, (const jbyte *)bytes);
    }
    free(bytes);
    return (*e)->ExceptionCheck(e) ? NULL : result;
}
static jobject calculate(JNIEnv *e, enum operation op, jbyteArray av, jbyteArray bv, jint param) {
    BIGNUM *a = read_value(e, av), *b = NULL, *r = NULL, *rem = NULL, *zero = NULL;
    BN_CTX *ctx = NULL;
    jobject result = NULL;
    if (!a) goto done;
    if (op != SHIFT && op != WORD) { b = read_value(e, bv); if (!b) goto done; }
    uint64_t ab = (unsigned)BN_num_bits(a), bb = b ? (unsigned)BN_num_bits(b) : 0;
    uint64_t al = (ab+31)/32, bl = (bb+31)/32;
    uint64_t output_bits = ab, work = 0;
    uint32_t exponent = 0;
    if (op == ADD) output_bits = (ab > bb ? ab : bb)+1;
    if (op == MUL) { output_bits = ab && bb ? ab+bb : 0; work = al*bl; }
    if (op == WORD) {
        uint32_t w = (uint32_t)param;
        unsigned wb = 0; for (uint32_t v = w; v; v >>= 1) ++wb;
        output_bits = ab && wb ? ab+wb : 0;
    }
    if (op == SHIFT && param >= 0 && ab) output_bits = ab+(uint32_t)param;
    if (op == DIV) {
        if (!bb) { fail(e, "java/lang/ArithmeticException", "division by zero"); goto done; }
        work = al*bl;
    }
    if (op == EXP) {
        /* BN_exp ignores exponent sign. Bound result and all repeated-square work first. */
        BN_set_negative(b, 0);
        if (ab > 1 && bb > 31) { budget(e, MAX_BITS+1, 0); goto done; }
        if (bb <= 31) {
            unsigned char encoded[4] = {0}; int n = (int)((bb+7)/8);
            BN_bn2bin(b, encoded+4-n);
            for (int i = 0; i < 4; ++i) exponent = (exponent << 8) | encoded[i];
        }
        output_bits = ab > 1 ? ab*exponent : 1;
        uint64_t limbs = (output_bits+31)/32;
        /* Check bits before multiplying to avoid overflow in the work estimate. */
        if (!budget(e, output_bits, 0)) goto done;
        work = ab > 1 ? 2*(bb+1)*limbs*limbs : 0;
    }
    if (!budget(e, output_bits, work)) goto done;
    r = BN_new(); zero = BN_new();
    if (op == DIV) rem = BN_new();
    if (op == MUL || op == DIV || op == EXP) ctx = BN_CTX_new();
    if (!r || !zero || (op == DIV && !rem) ||
        ((op == MUL || op == DIV || op == EXP) && !ctx)) {
        fail(e, "java/lang/OutOfMemoryError", "integer arithmetic allocation"); goto done;
    }
    int ok = 0;
    switch (op) {
        case ADD: ok = BN_add(r, a, b); break;
        case MUL: ok = BN_mul(r, a, b, ctx); break;
        case EXP:
            /* Huge powers of 0/1/-1 need no repeated squaring. */
            if (ab <= 1) {
                unsigned char one = (!ab && bb) ? 0 : 1;
                ok = BN_bin2bn(&one, 1, r) != NULL;
                unsigned char parity[1];
                if (one && BN_cmp(a, zero) < 0 && bb) {
                    /* Last magnitude byte contains parity, even for arbitrarily large exponent. */
                    jsize n = (*e)->GetArrayLength(e, bv);
                    (*e)->GetByteArrayRegion(e, bv, n-1, 1, (jbyte *)parity);
                    BN_set_negative(r, parity[0]&1);
                }
            } else ok = BN_exp(r, a, b, ctx);
            break;
        case DIV: ok = BN_div(r, rem, a, b, ctx); break;
        case SHIFT:
            if (!ab) ok = 1; /* Zero needs no allocation, even for INT_MAX left shift. */
            else if (param < 0 && (uint64_t)(-(int64_t)param) >= ab) ok = 1; /* initialized zero */
            else ok = param >= 0 ? BN_lshift(r, a, param) : BN_rshift(r, a, -param);
            break;
        case WORD:
            ok = BN_lshift(r, a, 0) && BN_mul_word(r, (unsigned long)(uint32_t)param);
            break;
    }
    if ((*e)->ExceptionCheck(e)) goto done;
    if (!ok) { fail(e, "java/lang/ArithmeticException", "guest BIGNUM arithmetic failed"); goto done; }
    jbyteArray first = write_value(e, r, zero);
    if (!first) goto done;
    if (op != DIV) result = first;
    else {
        jbyteArray second = write_value(e, rem, zero);
        if (second) {
            jsize qn = (*e)->GetArrayLength(e, first), rn = (*e)->GetArrayLength(e, second);
            unsigned char *packed = malloc((size_t)qn+(size_t)rn+4);
            if (!packed) fail(e, "java/lang/OutOfMemoryError", "integer division output allocation");
            else {
                for (unsigned i = 0; i < 4; ++i) packed[i] = (unsigned)((uint32_t)qn >> (24-8*i));
                (*e)->GetByteArrayRegion(e, first, 0, qn, (jbyte *)packed+4);
                (*e)->GetByteArrayRegion(e, second, 0, rn, (jbyte *)packed+4+qn);
                if (!(*e)->ExceptionCheck(e)) {
                    jbyteArray both = (*e)->NewByteArray(e, qn+rn+4);
                    if (both) (*e)->SetByteArrayRegion(e, both, 0, qn+rn+4, (jbyte *)packed);
                    result = both;
                }
                free(packed);
            }
            (*e)->DeleteLocalRef(e, second);
        }
        (*e)->DeleteLocalRef(e, first);
    }
done:
    BN_free(a); BN_free(b); BN_free(r); BN_free(rem); BN_free(zero); BN_CTX_free(ctx);
    return (*e)->ExceptionCheck(e) ? NULL : result;
}
#define BINARY(name, op) \
JNIEXPORT jbyteArray JNICALL Java_org_ogplay_math_NativeBigInteger_##name( \
        JNIEnv *e, jclass c, jbyteArray a, jbyteArray b) { \
    (void)c; return (jbyteArray)calculate(e, op, a, b, 0); \
}
BINARY(add, ADD)
BINARY(multiply, MUL)
BINARY(power, EXP)
JNIEXPORT jbyteArray JNICALL Java_org_ogplay_math_NativeBigInteger_divide(
        JNIEnv *e, jclass c, jbyteArray a, jbyteArray b) {
    (void)c; return (jbyteArray)calculate(e, DIV, a, b, 0);
}
JNIEXPORT jbyteArray JNICALL Java_org_ogplay_math_NativeBigInteger_shift(
        JNIEnv *e, jclass c, jbyteArray a, jint count) {
    (void)c; return (jbyteArray)calculate(e, SHIFT, a, NULL, count);
}
JNIEXPORT jbyteArray JNICALL Java_org_ogplay_math_NativeBigInteger_multiplyWord(
        JNIEnv *e, jclass c, jbyteArray a, jint word) {
    (void)c; return (jbyteArray)calculate(e, WORD, a, NULL, word);
}
