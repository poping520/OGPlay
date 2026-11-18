/* API 19 Conscrypt RSA JNI backed by the pinned ARM OpenSSL 1.0.1. */
#include "crypto_jni.h"

typedef struct bignum_st BIGNUM;
typedef struct env_md_st EVP_MD;
typedef struct pkcs8_priv_key_info_st PKCS8_PRIV_KEY_INFO;

/* Public OpenSSL 1.0.1 prefix. The load-time DER check exercises every BIGNUM
 * field, and RSA_size in the pinned ELF loads n at offset 16. */
typedef struct rsa_st {
    int pad;
    long version;
    const void* meth;
    void* engine;
    BIGNUM *n, *e, *d, *p, *q, *dmp1, *dmq1, *iqmp;
} RSA;

_Static_assert(__builtin_offsetof(RSA, n) == 16 && __builtin_offsetof(RSA, iqmp) == 44, "OpenSSL 1.0.1 ARM RSA prefix");

extern void* malloc(size_t);
extern void free(void*);
extern int memcmp(const void*, const void*, size_t);
extern unsigned long ERR_get_error(void);
extern void ERR_clear_error(void);
extern void ERR_error_string_n(unsigned long, char*, size_t);
extern BIGNUM* BN_bin2bn(const unsigned char*, int, BIGNUM*);
extern int BN_bn2bin(const BIGNUM*, unsigned char*);
extern int BN_num_bits(const BIGNUM*);
extern int BN_is_bit_set(const BIGNUM*, int);
extern int BN_cmp(const BIGNUM*, const BIGNUM*);
extern const BIGNUM* BN_value_one(void);
extern void BN_clear_free(BIGNUM*);
extern RSA* RSA_new(void);
extern void RSA_free(RSA*);
extern int RSA_blinding_on(RSA*, void*);
extern void RSA_blinding_off(RSA*);
extern int RSA_generate_key_ex(RSA*, int, BIGNUM*, void*);
extern int RSA_size(const RSA*);
typedef int RsaOperation(int, const unsigned char*, unsigned char*, RSA*, int);
extern RsaOperation RSA_public_encrypt, RSA_private_decrypt, RSA_private_encrypt, RSA_public_decrypt;
extern int i2d_RSAPrivateKey(const RSA*, unsigned char**);
extern EVP_PKEY* EVP_PKEY_new(void);
extern void EVP_PKEY_free(EVP_PKEY*);
extern int EVP_PKEY_assign(EVP_PKEY*, int, void*);
extern int EVP_PKEY_base_id(const EVP_PKEY*);
extern int EVP_PKEY_size(const EVP_PKEY*);
extern int EVP_PKEY_cmp(const EVP_PKEY*, const EVP_PKEY*);
extern RSA* EVP_PKEY_get1_RSA(EVP_PKEY*);
extern EVP_PKEY* d2i_PUBKEY(EVP_PKEY**, const unsigned char**, long);
extern int i2d_PUBKEY(const EVP_PKEY*, unsigned char**);
extern PKCS8_PRIV_KEY_INFO* d2i_PKCS8_PRIV_KEY_INFO(PKCS8_PRIV_KEY_INFO**, const unsigned char**, long);
extern int i2d_PKCS8_PRIV_KEY_INFO(const PKCS8_PRIV_KEY_INFO*, unsigned char**);
extern int PKCS8_pkey_get0(void*, const unsigned char**, int*, void*, PKCS8_PRIV_KEY_INFO*);
extern void PKCS8_PRIV_KEY_INFO_free(PKCS8_PRIV_KEY_INFO*);
extern EVP_PKEY* EVP_PKCS82PKEY(PKCS8_PRIV_KEY_INFO*);
extern PKCS8_PRIV_KEY_INFO* EVP_PKEY2PKCS8(EVP_PKEY*);
extern const EVP_MD* EVP_get_digestbyname(const char*);
extern EVP_MD_CTX* EVP_MD_CTX_create(void);
extern void EVP_MD_CTX_destroy(EVP_MD_CTX*);
extern int EVP_DigestInit_ex(EVP_MD_CTX*, const EVP_MD*, void*);
extern int EVP_SignFinal(EVP_MD_CTX*, unsigned char*, unsigned int*, EVP_PKEY*);
extern int EVP_VerifyFinal(EVP_MD_CTX*, const unsigned char*, unsigned int, EVP_PKEY*);

enum {
    RSA_MAX_BITS = 16384, /* OPENSSL_RSA_MAX_MODULUS_BITS in 1.0.1 */
    RSA_PKCS1_PADDING = 1,
    RSA_NO_PADDING = 3,
    ERR_LIB_RSA = 4,
    RSA_R_ALGORITHM_MISMATCH = 100,
    RSA_R_BAD_SIGNATURE = 104,
    RSA_R_MODULUS_TOO_LARGE = 105,
    RSA_R_BLOCK_TYPE_IS_NOT_01 = 106,
    RSA_R_BLOCK_TYPE_IS_NOT_02 = 107,
    RSA_R_DATA_GREATER_THAN_MOD_LEN = 108,
    RSA_R_UNKNOWN_ALGORITHM_TYPE = 117,
    RSA_R_WRONG_SIGNATURE_LENGTH = 119,
    RSA_R_INVALID_MESSAGE_LENGTH = 131,
    RSA_R_DATA_TOO_LARGE_FOR_MODULUS = 132,
    RSA_R_NO_PUBLIC_EXPONENT = 140
};

static void openssl_error(JNIEnv* env, const char* type, const char* fallback) {
    unsigned long error = ERR_get_error();
    ERR_clear_error();
    if (!error) {
        fail(env, type, fallback);
        return;
    }
    if (((error >> 24) & 255) == ERR_LIB_RSA) {
        switch (error & 4095) {
        case RSA_R_BLOCK_TYPE_IS_NOT_01:
        case RSA_R_BLOCK_TYPE_IS_NOT_02: type = "javax/crypto/BadPaddingException"; break;
        case RSA_R_ALGORITHM_MISMATCH:
        case RSA_R_BAD_SIGNATURE:
        case RSA_R_DATA_GREATER_THAN_MOD_LEN:
        case RSA_R_DATA_TOO_LARGE_FOR_MODULUS:
        case RSA_R_INVALID_MESSAGE_LENGTH:
        case RSA_R_WRONG_SIGNATURE_LENGTH: type = "java/security/SignatureException"; break;
        case RSA_R_UNKNOWN_ALGORITHM_TYPE: type = "java/security/NoSuchAlgorithmException"; break;
        case RSA_R_MODULUS_TOO_LARGE:
        case RSA_R_NO_PUBLIC_EXPONENT: type = "java/security/InvalidKeyException"; break;
        }
    }
    char message[256];
    ERR_error_string_n(error, message, sizeof(message));
    fail(env, type, message);
}

static int pending(JNIEnv* env) {
    return JNI(228, unsigned char (*)(JNIEnv*))(env);
}

static jobject new_bytes(JNIEnv* env, int count) {
    return JNI(176, jobject (*)(JNIEnv*, int))(env, count);
}

static void release_rsa(RSA** rsa) {
    RSA_free(*rsa);
}

static void release_bn(BIGNUM** bn) {
    BN_clear_free(*bn);
}

static void release_pkcs8(PKCS8_PRIV_KEY_INFO** address) {
    PKCS8_PRIV_KEY_INFO* info = *address;
    if (!info) return;
    const unsigned char* bytes = 0;
    int size = 0;
    if (PKCS8_pkey_get0(0, &bytes, &size, 0, info) && bytes && size > 0) crypto_wipe((void*)bytes, (size_t)size);
    PKCS8_PRIV_KEY_INFO_free(info);
}

int ogplay_rsa_layout_check(void) {
    /* n=61*47, e=17, d=2273, and the five distinct CRT values. */
    static const unsigned char der[] = {0x30, 0x1d, 0x02, 0x01, 0x00, 0x02, 0x02, 0x0b, 0x33, 0x02, 0x01,
                                        0x11, 0x02, 0x02, 0x08, 0xe1, 0x02, 0x01, 0x3d, 0x02, 0x01, 0x2f,
                                        0x02, 0x01, 0x35, 0x02, 0x01, 0x13, 0x02, 0x01, 0x0d};
    static const unsigned char values[][2] = {{0x0b, 0x33}, {0, 17}, {0x08, 0xe1}, {0, 61},
                                              {0, 47},      {0, 53}, {0, 19},      {0, 13}};
    RSA* rsa __attribute__((cleanup(release_rsa))) = RSA_new();
    if (!rsa) return 0;
    BIGNUM** fields[] = {&rsa->n, &rsa->e, &rsa->d, &rsa->p, &rsa->q, &rsa->dmp1, &rsa->dmq1, &rsa->iqmp};
    for (size_t i = 0; i < sizeof(fields) / sizeof(*fields); ++i) {
        *fields[i] = BN_bin2bn(values[i], sizeof(values[i]), 0);
        if (!*fields[i]) return 0;
    }
    if (RSA_size(rsa) != 2 || i2d_RSAPrivateKey(rsa, 0) != (int)sizeof(der)) return 0;
    unsigned char output[sizeof(der)];
    unsigned char* cursor = output;
    return i2d_RSAPrivateKey(rsa, &cursor) == (int)sizeof(der) && memcmp(output, der, sizeof(der)) == 0;
}

static unsigned char* read_encoding(JNIEnv* env, jobject input, int* size, const char* exception) {
    *size = length(env, input);
    if (*size < 0) return 0;
    if (*size == 0 || *size > CRYPTO_MAX_ENCODING) {
        fail(env, exception, "invalid encoded key length (limit 1 MiB)");
        return 0;
    }
    unsigned char* bytes = malloc((size_t)*size);
    if (!bytes) {
        fail(env, "java/lang/OutOfMemoryError", "encoded RSA key buffer");
        return 0;
    }
    read_bytes(env, input, 0, *size, bytes);
    if (pending(env)) {
        crypto_wipe(bytes, (size_t)*size);
        free(bytes);
        return 0;
    }
    return bytes;
}

static BIGNUM* read_bignum(JNIEnv* env, jobject input) {
    int size;
    unsigned char* bytes = read_encoding(env, input, &size, "java/lang/IllegalArgumentException");
    if (!bytes) return 0;
    BIGNUM* bn = 0;
    if (bytes[0] & 0x80)
        fail(env, "java/lang/IllegalArgumentException", "negative RSA BigInteger");
    else if (!(bn = BN_bin2bn(bytes, size, 0)))
        fail(env, "java/lang/RuntimeException", "Conversion to BIGNUM failed");
    crypto_wipe(bytes, (size_t)size);
    free(bytes);
    return bn;
}

static jobject write_bignum(JNIEnv* env, const BIGNUM* bn) {
    int size = (BN_num_bits(bn) + 7) / 8;
    unsigned char* bytes = malloc((size_t)size + 1);
    if (!bytes) {
        fail(env, "java/lang/OutOfMemoryError", "RSA parameter buffer");
        return 0;
    }
    bytes[0] = 0; /* Conscrypt always writes a sign byte. */
    if (size) BN_bn2bin(bn, bytes + 1);
    jobject output = new_bytes(env, size + 1);
    if (output) write_bytes(env, output, 0, size + 1, bytes);
    crypto_wipe(bytes, (size_t)size + 1);
    free(bytes);
    return output;
}

static int valid_rsa(JNIEnv* env, RSA* rsa) {
    if (!rsa->n || !BN_num_bits(rsa->n) || BN_num_bits(rsa->n) > RSA_MAX_BITS) {
        fail(env, "java/security/InvalidKeyException", "invalid RSA modulus size");
        return 0;
    }
    return 1;
}

static int configure_blinding(JNIEnv* env, RSA* rsa) {
    if (!rsa->d) return 1;
    if (!rsa->e || !BN_num_bits(rsa->e)) {
        RSA_blinding_off(rsa);
        return 1;
    }
    if (!crypto_seed_rng(env, "java/security/ProviderException")) return 0;
    ERR_clear_error();
    if (RSA_blinding_on(rsa, 0) == 1) return 1;
    openssl_error(env, "java/security/InvalidKeyException", "RSA blinding setup failed");
    return 0;
}

static jlong publish_rsa(JNIEnv* env, RSA** rsa) {
    EVP_PKEY* key = EVP_PKEY_new();
    if (!key || EVP_PKEY_assign(key, CRYPTO_PKEY_RSA, *rsa) != 1) {
        EVP_PKEY_free(key);
        fail(env, "java/lang/RuntimeException", "EVP_PKEY_new failed");
        return 0;
    }
    *rsa = 0;
    return crypto_publish_pkey(env, key);
}

jlong N(EVP_1PKEY_1new_1RSA)(JNIEnv* env, jobject cls, jobject n, jobject e, jobject d, jobject p, jobject q,
                             jobject dmp1, jobject dmq1, jobject iqmp) {
    (void)cls;
    if (!e && !d) {
        fail(env, "java/lang/IllegalArgumentException", "e == NULL && d == NULL");
        return 0;
    }
    RSA* rsa __attribute__((cleanup(release_rsa))) = RSA_new();
    if (!rsa) {
        fail(env, "java/lang/RuntimeException", "RSA_new failed");
        return 0;
    }
    jobject values[] = {n, e, d, p, q, dmp1, dmq1, iqmp};
    BIGNUM** fields[] = {&rsa->n, &rsa->e, &rsa->d, &rsa->p, &rsa->q, &rsa->dmp1, &rsa->dmq1, &rsa->iqmp};
    for (size_t i = 0; i < sizeof(values) / sizeof(*values); ++i) {
        if ((i == 0 || values[i]) && !(*fields[i] = read_bignum(env, values[i]))) return 0;
    }
    if (!valid_rsa(env, rsa) || !configure_blinding(env, rsa)) return 0;
    return publish_rsa(env, &rsa);
}

#define ACQUIRE_KEY(var, token)                                                                                        \
    CryptoKey* var __attribute__((cleanup(crypto_release_key))) = crypto_acquire_key(env, token)
#define LOCK_KEY(key)                                                                                                  \
    crypto_lock_key(key);                                                                                              \
    CryptoKey* locked_key __attribute__((cleanup(crypto_unlock_key), unused)) = key

static RSA* get_rsa(JNIEnv* env, const CryptoKey* key) {
    if (crypto_key_type(key) == CRYPTO_PKEY_RSA) {
        RSA* rsa = EVP_PKEY_get1_RSA(crypto_key_evp(key));
        if (rsa) return rsa;
    }
    openssl_error(env, "java/security/InvalidKeyException", "Need RSA private or public key");
    return 0;
}

int N(EVP_1PKEY_1type)(JNIEnv* env, jobject cls, jlong token) {
    (void)cls;
    ACQUIRE_KEY(key, token);
    return key ? crypto_key_type(key) : -1;
}

int N(EVP_1PKEY_1size)(JNIEnv* env, jobject cls, jlong token) {
    (void)cls;
    ACQUIRE_KEY(key, token);
    EVP_PKEY* evp = key ? crypto_key_evp(key) : 0;
    return evp ? EVP_PKEY_size(evp) : 0;
}

int N(EVP_1PKEY_1cmp)(JNIEnv* env, jobject cls, jlong left, jlong right) {
    (void)cls;
    ACQUIRE_KEY(a, left);
    if (!a) return -1;
    ACQUIRE_KEY(b, right);
    if (!b) return -1;
    if (crypto_key_type(a) != crypto_key_type(b)) return -1;
    EVP_PKEY* a_evp = crypto_key_evp(a);
    EVP_PKEY* b_evp = crypto_key_evp(b);
    /* EVP_PKEY_cmp only reads the immutable public parameters. */
    return a_evp && b_evp ? EVP_PKEY_cmp(a_evp, b_evp) : -2;
}

jlong N(RSA_1generate_1key_1ex)(JNIEnv* env, jobject cls, int bits, jobject exponent) {
    (void)cls;
    BIGNUM* e __attribute__((cleanup(release_bn))) = read_bignum(env, exponent);
    if (!e) return 0;
    if (bits < 16 || bits > RSA_MAX_BITS || BN_cmp(e, BN_value_one()) <= 0) {
        fail(env, "java/lang/IllegalArgumentException", "invalid RSA key generation parameters");
        return 0;
    }
    /* OpenSSL 1.0.1's prime search cannot succeed for an even exponent. */
    if (!BN_is_bit_set(e, 0)) {
        fail(env, "java/lang/IllegalArgumentException", "RSA public exponent must be odd");
        return 0;
    }
    if (!crypto_seed_rng(env, "java/security/ProviderException")) return 0;
    RSA* rsa __attribute__((cleanup(release_rsa))) = RSA_new();
    if (!rsa) {
        fail(env, "java/lang/RuntimeException", "RSA_new failed");
        return 0;
    }
    ERR_clear_error();
    if (RSA_generate_key_ex(rsa, bits, e, 0) != 1) {
        openssl_error(env, "java/lang/RuntimeException", "RSA_generate_key_ex failed");
        return 0;
    }
    return publish_rsa(env, &rsa);
}

static jlong decode_key(JNIEnv* env, jobject input, int private_key) {
    int size;
    unsigned char* bytes = read_encoding(env, input, &size, "java/security/InvalidKeyException");
    if (!bytes) return 0;
    const unsigned char* cursor = bytes;
    EVP_PKEY* evp = 0;
    ERR_clear_error();
    if (private_key) {
        PKCS8_PRIV_KEY_INFO* info __attribute__((cleanup(release_pkcs8))) = d2i_PKCS8_PRIV_KEY_INFO(0, &cursor, size);
        if (info && cursor == bytes + size) evp = EVP_PKCS82PKEY(info);
    } else {
        evp = d2i_PUBKEY(0, &cursor, size);
    }
    int complete = cursor == bytes + size;
    crypto_wipe(bytes, (size_t)size);
    free(bytes);
    if (!evp || !complete) {
        EVP_PKEY_free(evp);
        openssl_error(env, "java/security/InvalidKeyException",
                      private_key ? "invalid PKCS#8 key" : "invalid public key");
        return 0;
    }
    if (EVP_PKEY_base_id(evp) == CRYPTO_PKEY_RSA) {
        RSA* rsa __attribute__((cleanup(release_rsa))) = EVP_PKEY_get1_RSA(evp);
        if (!rsa || !valid_rsa(env, rsa)) {
            EVP_PKEY_free(evp);
            if (!rsa) openssl_error(env, "java/security/InvalidKeyException", "invalid RSA key");
            return 0;
        }
        if (rsa->d && (!rsa->e || !BN_num_bits(rsa->e))) RSA_blinding_off(rsa);
    }
    return crypto_publish_pkey(env, evp);
}

jlong N(d2i_1PUBKEY)(JNIEnv* env, jobject cls, jobject input) {
    (void)cls;
    return decode_key(env, input, 0);
}

jlong N(d2i_1PKCS8_1PRIV_1KEY_1INFO)(JNIEnv* env, jobject cls, jobject input) {
    (void)cls;
    return decode_key(env, input, 1);
}

static int encode_der(EVP_PKEY* key, PKCS8_PRIV_KEY_INFO* info, unsigned char** output) {
    return info ? i2d_PKCS8_PRIV_KEY_INFO(info, output) : i2d_PUBKEY(key, output);
}

static jobject encode_key(JNIEnv* env, jlong token, int private_key) {
    ACQUIRE_KEY(key, token);
    if (!key) return 0;
    EVP_PKEY* evp = crypto_key_evp(key);
    if (!evp) {
        fail(env, "java/security/InvalidKeyException", "key has no ASN.1 encoding");
        return 0;
    }
    ERR_clear_error();
    PKCS8_PRIV_KEY_INFO* info __attribute__((cleanup(release_pkcs8))) = private_key ? EVP_PKEY2PKCS8(evp) : 0;
    int size = private_key && !info ? -1 : encode_der(evp, info, 0);
    if (size <= 0 || size > CRYPTO_MAX_ENCODING) {
        openssl_error(env, "java/lang/RuntimeException", "could not encode key");
        return 0;
    }
    unsigned char* bytes = malloc((size_t)size);
    if (!bytes) {
        fail(env, "java/lang/OutOfMemoryError", "encoded key");
        return 0;
    }
    unsigned char* cursor = bytes;
    jobject output = 0;
    if (encode_der(evp, info, &cursor) == size) {
        output = new_bytes(env, size);
        if (output) write_bytes(env, output, 0, size, bytes);
    } else {
        openssl_error(env, "java/lang/RuntimeException", "could not encode key");
    }
    crypto_wipe(bytes, (size_t)size);
    free(bytes);
    return output;
}

jobject N(i2d_1PUBKEY)(JNIEnv* env, jobject cls, jlong token) {
    (void)cls;
    return encode_key(env, token, 0);
}

jobject N(i2d_1PKCS8_1PRIV_1KEY_1INFO)(JNIEnv* env, jobject cls, jlong token) {
    (void)cls;
    return encode_key(env, token, 1);
}

static jobject rsa_params(JNIEnv* env, jlong token, int private_key) {
    ACQUIRE_KEY(key, token);
    if (!key) return 0;
    RSA* rsa __attribute__((cleanup(release_rsa))) = get_rsa(env, key);
    if (!rsa) return 0;
    if (!private_key && !rsa->e) {
        fail(env, "java/lang/NullPointerException", "e");
        return 0;
    }
    const BIGNUM* params[] = {rsa->n, rsa->e, rsa->d, rsa->p, rsa->q, rsa->dmp1, rsa->dmq1, rsa->iqmp};
    int count = private_key ? 8 : 2;
    /* The guest JNI class registry does not resolve array classes, so Java allocates byte[][]. */
    jobject type = JNI(6, jobject (*)(JNIEnv*, const char*))(env, "org/ogplay/security/NativeArrays");
    if (!type) return 0;
    typedef void* jmethodID;
    jmethodID allocate =
        JNI(113, jmethodID (*)(JNIEnv*, jobject, const char*, const char*))(env, type, "newByteArrays", "(I)[[B");
    if (!allocate) return 0;

    union {
        jlong wide;
        int integer;
    } args[] = {{.integer = count}}; /* jvalue is an 8-byte union on ARM. */

    jobject output = JNI(116, jobject (*)(JNIEnv*, jobject, jmethodID, const void*))(env, type, allocate, args);
    if (!output) return 0;
    for (int i = 0; i < count; ++i) {
        if (!params[i]) continue;
        jobject param = write_bignum(env, params[i]);
        if (!param || pending(env)) return 0;
        JNI(174, void (*)(JNIEnv*, jobject, int, jobject))(env, output, i, param);
        if (pending(env)) return 0;
    }
    return output;
}

jobject N(get_1RSA_1public_1params)(JNIEnv* env, jobject cls, jlong token) {
    (void)cls;
    return rsa_params(env, token, 0);
}

jobject N(get_1RSA_1private_1params)(JNIEnv* env, jobject cls, jlong token) {
    (void)cls;
    return rsa_params(env, token, 1);
}

int N(RSA_1size)(JNIEnv* env, jobject cls, jlong token) {
    (void)cls;
    ACQUIRE_KEY(key, token);
    if (!key) return 0;
    RSA* rsa __attribute__((cleanup(release_rsa))) = get_rsa(env, key);
    return rsa ? RSA_size(rsa) : 0;
}

static int rsa_crypt(JNIEnv* env, int size, jobject input, jobject output, jlong token, int padding,
                     RsaOperation* operation, int private_key) {
    ACQUIRE_KEY(key, token);
    if (!key) return -1;
    RSA* rsa __attribute__((cleanup(release_rsa))) = get_rsa(env, key);
    if (!rsa) return -1;
    int capacity = RSA_size(rsa);
    if (!range(env, input, 0, size) || !range(env, output, 0, capacity)) return -1;
    if (size > capacity) {
        fail(env, "java/security/SignatureException", "data greater than mod len");
        return -1;
    }
    if (padding != RSA_NO_PADDING && padding != RSA_PKCS1_PADDING) {
        fail(env, "java/lang/IllegalArgumentException", "unsupported RSA padding");
        return -1;
    }
    if ((private_key && !rsa->d) || (!private_key && !rsa->e)) {
        fail(env, "java/security/InvalidKeyException", "missing RSA exponent");
        return -1;
    }
    if (((private_key && rsa->e && BN_num_bits(rsa->e)) ||
         (operation == RSA_public_encrypt && padding == RSA_PKCS1_PADDING)) &&
        !crypto_seed_rng(env, "java/security/ProviderException"))
        return -1;

    unsigned char from[RSA_MAX_BITS / 8], to[RSA_MAX_BITS / 8];
    read_bytes(env, input, 0, size, from);
    int result = -1;
    if (!pending(env)) {
        LOCK_KEY(key);
        ERR_clear_error();
        result = operation(size, from, to, rsa, padding);
        if (result >= 0)
            write_bytes(env, output, 0, result, to);
        else
            openssl_error(env, "java/lang/RuntimeException", "RSA_crypt_operation failed");
    }
    crypto_wipe(from, (size_t)size);
    crypto_wipe(to, (size_t)capacity);
    return result;
}

#define RSA_CRYPT(native_name, function, is_private)                                                                   \
    int N(native_name)(JNIEnv * env, jobject cls, int size, jobject input, jobject output, jlong token, int padding) { \
        (void)cls;                                                                                                     \
        return rsa_crypt(env, size, input, output, token, padding, function, is_private);                              \
    }
RSA_CRYPT(RSA_1public_1encrypt, RSA_public_encrypt, 0)
RSA_CRYPT(RSA_1private_1decrypt, RSA_private_decrypt, 1)
RSA_CRYPT(RSA_1private_1encrypt, RSA_private_encrypt, 1)
RSA_CRYPT(RSA_1public_1decrypt, RSA_public_decrypt, 0)

static jlong signature_init(JNIEnv* env, jobject algorithm) {
    if (!algorithm) {
        fail(env, "java/lang/NullPointerException", "algorithm == null");
        return 0;
    }
    const char* name = JNI(169, const char* (*)(JNIEnv*, jobject, void*))(env, algorithm, 0);
    if (!name) return 0;
    const EVP_MD* digest = EVP_get_digestbyname(name);
    JNI(170, void (*)(JNIEnv*, jobject, const char*))(env, algorithm, name);
    if (!digest) {
        fail(env, "java/security/NoSuchAlgorithmException", "Hash algorithm not found");
        return 0;
    }
    EVP_MD_CTX* ctx = EVP_MD_CTX_create();
    if (!ctx) {
        fail(env, "java/lang/OutOfMemoryError", "Unable to allocate EVP_MD_CTX");
        return 0;
    }
    ERR_clear_error();
    if (!EVP_DigestInit_ex(ctx, digest, 0)) {
        EVP_MD_CTX_destroy(ctx);
        openssl_error(env, "java/lang/RuntimeException", "EVP signature digest initialization failed");
        return 0;
    }
    return crypto_publish_signature(env, ctx);
}

jlong N(EVP_1SignInit)(JNIEnv* env, jobject cls, jobject algorithm) {
    (void)cls;
    return signature_init(env, algorithm);
}

jlong N(EVP_1VerifyInit)(JNIEnv* env, jobject cls, jobject algorithm) {
    (void)cls;
    return signature_init(env, algorithm);
}

#define SIGNATURE_CONTEXT                                                                                              \
    CryptoDigest* ctx __attribute__((cleanup(crypto_release_digest))) = crypto_signature_context(env, token)

void N(EVP_1SignUpdate)(JNIEnv* env, jobject cls, jlong token, jobject input, int offset, int size) {
    (void)cls;
    SIGNATURE_CONTEXT;
    if (ctx) crypto_update_digest(env, crypto_digest_evp(ctx), input, offset, size);
}

void N(EVP_1VerifyUpdate)(JNIEnv* env, jobject cls, jlong token, jobject input, int offset, int size) {
    N(EVP_1SignUpdate)(env, cls, token, input, offset, size);
}

int N(EVP_1SignFinal)(JNIEnv* env, jobject cls, jlong token, jobject output, int offset, jlong key_token) {
    (void)cls;
    SIGNATURE_CONTEXT;
    if (!ctx) return -1;
    ACQUIRE_KEY(key, key_token);
    if (!key) return -1;
    RSA* rsa __attribute__((cleanup(release_rsa))) = get_rsa(env, key);
    if (!rsa) return -1;
    if (!rsa->d) {
        fail(env, "java/security/InvalidKeyException", "missing RSA private exponent");
        return -1;
    }
    int capacity = RSA_size(rsa);
    if (!range(env, output, offset, capacity)) return -1;
    if (rsa->e && BN_num_bits(rsa->e) && !crypto_seed_rng(env, "java/security/ProviderException")) return -1;
    LOCK_KEY(key);
    unsigned char bytes[RSA_MAX_BITS / 8];
    unsigned int size = 0;
    ERR_clear_error();
    if (EVP_SignFinal(crypto_digest_evp(ctx), bytes, &size, crypto_key_evp(key)) != 1) {
        openssl_error(env, "java/security/SignatureException", "EVP_SignFinal failed");
        return -1;
    }
    write_bytes(env, output, offset, (int)size, bytes);
    return (int)size;
}

int N(EVP_1VerifyFinal)(JNIEnv* env, jobject cls, jlong token, jobject input, int offset, int size, jlong key_token) {
    (void)cls;
    SIGNATURE_CONTEXT;
    if (!ctx) return -1;
    ACQUIRE_KEY(key, key_token);
    if (!key) return -1;
    RSA* rsa __attribute__((cleanup(release_rsa))) = get_rsa(env, key);
    if (!rsa || !range(env, input, offset, size)) return -1;
    if (!rsa->e || size != RSA_size(rsa)) return 0;
    unsigned char bytes[RSA_MAX_BITS / 8];
    read_bytes(env, input, offset, size, bytes);
    if (pending(env)) return -1;
    LOCK_KEY(key);
    ERR_clear_error();
    int result = EVP_VerifyFinal(crypto_digest_evp(ctx), bytes, (unsigned int)size, crypto_key_evp(key));
    if (result < 0)
        openssl_error(env, "java/security/SignatureException", "EVP_VerifyFinal failed");
    else
        ERR_clear_error();
    return result;
}
