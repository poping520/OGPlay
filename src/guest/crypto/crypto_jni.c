/* API 19 ARM guest JNI adapter. AES, digest and signature algorithms execute in guest libcrypto.
 * JNI 1.6 table slots follow the Android JNI ABI; no host headers are used. */
typedef unsigned int size_t;
typedef long long jlong;
typedef void *jobject;
typedef const void **JNIEnv;
typedef const void **JavaVM;
typedef struct evp_cipher_ctx_st EVP_CIPHER_CTX;
typedef struct evp_cipher_st EVP_CIPHER;
extern void *malloc(size_t);
extern void free(void *);
extern int ogplay_icu_on_load(JNIEnv *env);
extern void ogplay_icu_release(void);
extern int ogplay_tls_on_load(void);
extern void ogplay_tls_on_unload(void);
/* bionic API 19 pthread_mutex_t is one 32-bit word; static initializer is 0. */
extern int pthread_mutex_lock(int *);
extern int pthread_mutex_unlock(int *);
static int registry_mutex;
static int crypto_initialized;

extern int strcmp(const char *, const char *);
extern EVP_CIPHER_CTX *EVP_CIPHER_CTX_new(void);
extern void EVP_CIPHER_CTX_free(EVP_CIPHER_CTX *);
extern int EVP_CIPHER_CTX_block_size(const EVP_CIPHER_CTX *);
extern int EVP_CIPHER_CTX_set_padding(EVP_CIPHER_CTX *, int);
extern int EVP_CIPHER_CTX_set_key_length(EVP_CIPHER_CTX *, int);
extern int EVP_CIPHER_iv_length(const EVP_CIPHER *);
extern int EVP_CipherInit_ex(EVP_CIPHER_CTX *, const EVP_CIPHER *, void *, const unsigned char *,
                             const unsigned char *, int);
extern int EVP_CipherUpdate(EVP_CIPHER_CTX *, unsigned char *, int *, const unsigned char *, int);
extern int EVP_CipherFinal_ex(EVP_CIPHER_CTX *, unsigned char *, int *);
extern unsigned long ERR_get_error(void);
extern void ERR_clear_error(void);
extern void RAND_seed(const void *, int);
extern int RAND_bytes(unsigned char *, int);
#define AES(bits, mode) extern const EVP_CIPHER *EVP_aes_##bits##_##mode(void);
AES(128, ecb)
AES(192, ecb)
AES(256, ecb)
AES(128, cbc)
AES(192, cbc)
AES(256, cbc) AES(128, ctr) AES(192, ctr) AES(256, ctr)
#define JNI(slot, type) ((type)((*env)[slot]))
    static void wipe32(unsigned char (*buffer)[32]) {
    volatile unsigned char *p = *buffer;
    for (int i = 0; i < 32; ++i) p[i] = 0;
}
static void fail(JNIEnv *env, const char *type, const char *message) {
    jobject cls = JNI(6, jobject(*)(JNIEnv *, const char *))(env, type);
    if (cls) JNI(14, int (*)(JNIEnv *, jobject, const char *))(env, cls, message);
}
static int length(JNIEnv *env, jobject array) {
    if (!array) {
        fail(env, "java/lang/NullPointerException", "array == null");
        return -1;
    }
    return JNI(171, int (*)(JNIEnv *, jobject))(env, array);
}
static int range(JNIEnv *env, jobject array, int offset, int count) {
    int n = length(env, array);
    if (n < 0) return 0;
    if (offset < 0 || count < 0 || offset > n || count > n - offset) {
        fail(env, "java/lang/ArrayIndexOutOfBoundsException", "invalid cipher buffer range");
        return 0;
    }
    return 1;
}
static void read_bytes(JNIEnv *env, jobject a, int off, int n, unsigned char *p) {
    JNI(200, void (*)(JNIEnv *, jobject, int, int, unsigned char *))(env, a, off, n, p);
}
static void write_bytes(JNIEnv *env, jobject a, int off, int n, const unsigned char *p) {
    JNI(208, void (*)(JNIEnv *, jobject, int, int, const unsigned char *))(env, a, off, n, p);
}
typedef struct Context {
    struct Context *next;
    int mutex, references, removed;
    jlong token;
    EVP_CIPHER_CTX *evp;
    int buffered, padding, encrypting, algorithm;
    unsigned char iv[16];
} Context;
static Context *contexts;
static jlong next_token = 1;
static Context *context(JNIEnv *env, jlong token) {
    pthread_mutex_lock(&registry_mutex);
    Context *p = contexts;
    while (p && p->token != token) p = p->next;
    if (p) ++p->references;
    pthread_mutex_unlock(&registry_mutex);
    if (!p)
        fail(env, "java/lang/IllegalStateException", "invalid cipher context token");
    else
        pthread_mutex_lock(&p->mutex);
    return p;
}
static void release_context(Context **address) {
    Context *p = *address;
    if (!p) return;
    pthread_mutex_unlock(&p->mutex);
    pthread_mutex_lock(&registry_mutex);
    int destroy = (--p->references == 0 && p->removed);
    pthread_mutex_unlock(&registry_mutex);
    if (destroy) {
        EVP_CIPHER_CTX_free(p->evp);
        free(p);
    }
}
#define CONTEXT Context *p __attribute__((cleanup(release_context))) = context(env, token)
static int ready(JNIEnv *env, Context *p) {
    if (p && p->algorithm) return 1;
    if (p) fail(env, "java/lang/IllegalStateException", "cipher context is not initialized");
    return 0;
}
static const EVP_CIPHER *cipher(jlong token) {
    switch (token) {
#define CHOICE(id, bits, mode) \
    case id:                   \
        return EVP_aes_##bits##_##mode();
        CHOICE(1, 128, ecb)
        CHOICE(2, 192, ecb)
        CHOICE(3, 256, ecb)
        CHOICE(4, 128, cbc)
        CHOICE(5, 192, cbc)
        CHOICE(6, 256, cbc) CHOICE(7, 128, ctr) CHOICE(8, 192, ctr) CHOICE(9, 256, ctr)
    }
    return (void *)0;
}
#define N(name) Java_com_android_org_conscrypt_NativeCrypto_##name
void N(RAND_1seed)(JNIEnv *env, jobject cls, jobject seed) {
    (void)cls;
    int count = length(env, seed);
    if (count < 0) return;
    unsigned char buffer[4096];
    for (int offset = 0; offset < count;) {
        int chunk = count - offset < (int)sizeof(buffer) ? count - offset : (int)sizeof(buffer);
        read_bytes(env, seed, offset, chunk, buffer);
        RAND_seed(buffer, chunk);
        offset += chunk;
    }
}
void N(RAND_1bytes)(JNIEnv *env, jobject cls, jobject output) {
    (void)cls;
    int count = length(env, output);
    if (count < 0) return;
    unsigned char buffer[4096];
    for (int offset = 0; offset < count;) {
        int chunk = count - offset < (int)sizeof(buffer) ? count - offset : (int)sizeof(buffer);
        if (RAND_bytes(buffer, chunk) != 1) {
            fail(env, "java/security/ProviderException", "RAND_bytes failed");
            return;
        }
        write_bytes(env, output, offset, chunk, buffer);
        offset += chunk;
    }
}
jlong N(EVP_1get_1cipherbyname)(JNIEnv *env, jobject cls, jobject name) {
    (void)cls;
    if (!name) {
        fail(env, "java/lang/NullPointerException", "cipher name == null");
        return 0;
    }
    const char *s = JNI(169, const char *(*)(JNIEnv *, jobject, void *))(env, name, 0);
    if (!s) return 0;
    const char *names[] = {"aes-128-ecb", "aes-192-ecb", "aes-256-ecb",
                           "aes-128-cbc", "aes-192-cbc", "aes-256-cbc",
                           "aes-128-ctr", "aes-192-ctr", "aes-256-ctr"};
    jlong result = 0;
    for (int i = 0; i < 9; i++)
        if (!strcmp(s, names[i])) result = i + 1;
    JNI(170, void (*)(JNIEnv *, jobject, const char *))(env, name, s);
    return result;
}
jlong N(EVP_1CIPHER_1CTX_1new)(JNIEnv *env, jobject cls) {
    (void)cls;
    Context *p = malloc(sizeof(Context));
    if (!p) {
        fail(env, "java/lang/OutOfMemoryError", "cipher context");
        return 0;
    }
    p->evp = EVP_CIPHER_CTX_new();
    if (!p->evp) {
        free(p);
        fail(env, "java/lang/OutOfMemoryError", "EVP cipher context");
        return 0;
    }
    p->mutex = 0;
    p->references = 0;
    p->removed = 0;
    pthread_mutex_lock(&registry_mutex);
    if (next_token == 0x7fffffffffffffffLL) {
        pthread_mutex_unlock(&registry_mutex);
        EVP_CIPHER_CTX_free(p->evp);
        free(p);
        fail(env, "java/lang/OutOfMemoryError", "cipher token space exhausted");
        return 0;
    }
    p->token = next_token++;
    p->next = contexts;
    p->buffered = 0;
    p->padding = 1;
    p->encrypting = 0;
    p->algorithm = 0;
    contexts = p;
    jlong token = p->token;
    pthread_mutex_unlock(&registry_mutex);
    return token;
}
void N(EVP_1CIPHER_1CTX_1cleanup)(JNIEnv *env, jobject cls, jlong token) {
    (void)cls;
    pthread_mutex_lock(&registry_mutex);
    Context **p = &contexts;
    while (*p && (*p)->token != token) p = &(*p)->next;
    if (!*p) {
        pthread_mutex_unlock(&registry_mutex);
        fail(env, "java/lang/IllegalStateException", "invalid cipher context token");
        return;
    }
    Context *old = *p;
    *p = old->next;
    old->removed = 1;
    int destroy = old->references == 0;
    pthread_mutex_unlock(&registry_mutex);
    if (destroy) {
        EVP_CIPHER_CTX_free(old->evp);
        free(old);
    }
}
void N(EVP_1CipherInit_1ex)(JNIEnv *env, jobject cls, jlong token, jlong algorithm, jobject key,
                            jobject iv, unsigned char encrypt) {
    (void)cls;
    CONTEXT;
    if (!p) return;
    const EVP_CIPHER *c = cipher(algorithm);
    if (algorithm && !c) {
        fail(env, "java/security/InvalidAlgorithmParameterException", "unsupported cipher token");
        return;
    }
    int selected = algorithm ? (int)algorithm : p->algorithm;
    if (!selected) {
        fail(env, "java/lang/IllegalStateException", "cipher context is not initialized");
        return;
    }
    if (algorithm && (!key || (selected > 3 && !iv))) {
        fail(env, "java/security/InvalidAlgorithmParameterException",
             "initial AES key and IV are required");
        return;
    }
    unsigned char k[32] __attribute__((cleanup(wipe32))) = {0};
    unsigned char v[16] = {0};
    if (key) {
        int n = length(env, key);
        if (n != 16 + ((selected - 1) % 3) * 8) {
            fail(env, "java/security/InvalidKeyException", "AES key length");
            return;
        }
        read_bytes(env, key, 0, n, k);
    }
    if (iv) {
        if (length(env, iv) != 16) {
            fail(env, "java/security/InvalidAlgorithmParameterException", "AES IV length");
            return;
        }
        read_bytes(env, iv, 0, 16, v);
    }
    if (iv)
        for (int i = 0; i < 16; i++) p->iv[i] = v[i];
    ERR_clear_error();
    int ok =
        EVP_CipherInit_ex(p->evp, c, 0, key ? k : 0, selected > 3 ? p->iv : 0, encrypt ? 1 : 0);
    volatile unsigned char *wipe = k;
    for (int i = 0; i < 32; i++) wipe[i] = 0;
    if (!ok) {
        fail(env, "java/security/InvalidKeyException", "EVP cipher initialization failed");
        return;
    }
    p->buffered = 0;
    p->encrypting = encrypt ? 1 : 0;
    p->algorithm = selected;
}
int N(EVP_1CIPHER_1iv_1length)(JNIEnv *env, jobject cls, jlong token) {
    (void)cls;
    const EVP_CIPHER *c = cipher(token);
    if (!c) {
        fail(env, "java/lang/IllegalStateException", "invalid cipher algorithm token");
        return 0;
    }
    return EVP_CIPHER_iv_length(c);
}
int N(EVP_1CIPHER_1CTX_1block_1size)(JNIEnv *env, jobject cls, jlong token) {
    (void)cls;
    CONTEXT;
    return ready(env, p) ? EVP_CIPHER_CTX_block_size(p->evp) : 0;
}
int N(get_1EVP_1CIPHER_1CTX_1buf_1len)(JNIEnv *env, jobject cls, jlong token) {
    (void)cls;
    CONTEXT;
    return ready(env, p) ? p->buffered : 0;
}
void N(EVP_1CIPHER_1CTX_1set_1padding)(JNIEnv *env, jobject cls, jlong token,
                                       unsigned char padding) {
    (void)cls;
    CONTEXT;
    if (!p) return;
    EVP_CIPHER_CTX_set_padding(p->evp, padding ? 1 : 0);
    p->padding = padding ? 1 : 0;
}
void N(EVP_1CIPHER_1CTX_1set_1key_1length)(JNIEnv *env, jobject cls, jlong token, int size) {
    (void)cls;
    CONTEXT;
    if (!p) return;
    if (!ready(env, p)) return;
    if (size != 16 + ((p->algorithm - 1) % 3) * 8 || !EVP_CIPHER_CTX_set_key_length(p->evp, size))
        fail(env, "java/security/InvalidKeyException", "AES key length");
}
int N(EVP_1CipherUpdate)(JNIEnv *env, jobject cls, jlong token, jobject out, int outoff, jobject in,
                         int inoff, int count) {
    (void)cls;
    CONTEXT;
    if (!ready(env, p)) return 0;
    if (!range(env, in, inoff, count) || !range(env, out, outoff, 0)) return 0;
    if (count > 0x3fffffff) {
        fail(env, "java/lang/OutOfMemoryError", "cipher input too large");
        return 0;
    }
    unsigned char *input = malloc((size_t)count + 1), *output = malloc((size_t)count + 32);
    if (!input || !output) {
        free(input);
        free(output);
        fail(env, "java/lang/OutOfMemoryError", "cipher buffers");
        return 0;
    }
    read_bytes(env, in, inoff, count, input);
    int written = 0;
    ERR_clear_error();
    int ok = EVP_CipherUpdate(p->evp, output, &written, input, count);
    if (!ok)
        fail(env, "java/lang/IllegalStateException", "EVP cipher update failed");
    else if (written > length(env, out) - outoff)
        fail(env, "javax/crypto/ShortBufferException", "cipher output too short");
    else {
        write_bytes(env, out, outoff, written, output);
        p->buffered = (p->buffered + (count % 16)) % 16;
    }
    volatile unsigned char *wipe = input;
    for (int i = 0; i < count; i++) wipe[i] = 0;
    wipe = output;
    for (int i = 0; i < count + 32; i++) wipe[i] = 0;
    free(input);
    free(output);
    return written;
}
int N(EVP_1CipherFinal_1ex)(JNIEnv *env, jobject cls, jlong token, jobject out, int outoff) {
    (void)cls;
    CONTEXT;
    if (!ready(env, p) || !range(env, out, outoff, 0)) return 0;
    unsigned char output[32] __attribute__((cleanup(wipe32)));
    int written = 0;
    ERR_clear_error();
    if (!EVP_CipherFinal_ex(p->evp, output, &written)) {
        unsigned long reason = ERR_get_error() & 0xfff;
        fail(env,
             (reason == 100) ? "javax/crypto/BadPaddingException"
                             : "javax/crypto/IllegalBlockSizeException",
             "EVP cipher final failed");
        return 0;
    }
    if (written > length(env, out) - outoff) {
        fail(env, "javax/crypto/ShortBufferException", "cipher output too short");
        return 0;
    }
    write_bytes(env, out, outoff, written, output);
    p->buffered = 0;
    volatile unsigned char *wipe = output;
    for (int i = 0; i < 32; i++) wipe[i] = 0;
    return written;
}
static void release_crypto_locks(void);
static void release_digests(void);
static void release_hmacs(void);
static void release_mac_keys(void);
static void release_private_keys(void);
__attribute__((destructor)) static void release_contexts(void) {
    while (contexts) {
        Context *p = contexts;
        contexts = p->next;
        EVP_CIPHER_CTX_free(p->evp);
        free(p);
    }
    release_digests();
    release_hmacs();
    release_mac_keys();
    release_private_keys();
    release_crypto_locks();
    ogplay_tls_on_unload();
    ogplay_icu_release();
}

/* Stateless signature verification: Java owns DER/PEM parsing and fields.
 * OpenSSL owns key decoding and the actual signature algorithm. */
typedef struct env_md_ctx_st EVP_MD_CTX;
typedef struct evp_pkey_st EVP_PKEY;
typedef struct pkcs8_priv_key_info_st PKCS8_PRIV_KEY_INFO;
typedef struct env_md_st EVP_MD;
extern EVP_PKEY *d2i_PUBKEY(EVP_PKEY **, const unsigned char **, long);
extern void EVP_PKEY_free(EVP_PKEY *);
extern int EVP_PKEY_base_id(const EVP_PKEY *);
extern PKCS8_PRIV_KEY_INFO *d2i_PKCS8_PRIV_KEY_INFO(
    PKCS8_PRIV_KEY_INFO **, const unsigned char **, long);
extern int i2d_PKCS8_PRIV_KEY_INFO(const PKCS8_PRIV_KEY_INFO *, unsigned char **);
extern void PKCS8_PRIV_KEY_INFO_free(PKCS8_PRIV_KEY_INFO *);
extern EVP_PKEY *EVP_PKCS82PKEY(PKCS8_PRIV_KEY_INFO *);
extern PKCS8_PRIV_KEY_INFO *EVP_PKEY2PKCS8(EVP_PKEY *);
extern const EVP_CIPHER *EVP_des_ede3_cbc(void);
extern int EVP_add_digest(const EVP_MD *);
extern const EVP_MD *EVP_sha1(void);
extern const EVP_MD *EVP_sha224(void);
extern const EVP_MD *EVP_sha256(void);
extern const EVP_MD *EVP_sha384(void);
extern const EVP_MD *EVP_sha512(void);
extern int CRYPTO_num_locks(void);
extern void CRYPTO_set_locking_callback(void (*)(int, int, const char *, int));
extern void CRYPTO_set_id_callback(unsigned long (*)(void));
extern unsigned long pthread_self(void);
static int *crypto_locks;
static int crypto_lock_count;
static void crypto_lock(int mode, int index, const char *file, int line) {
    (void)file;
    (void)line;
    if (index < 0 || index >= crypto_lock_count) return;
    if (mode & 1)
        pthread_mutex_lock(&crypto_locks[index]);
    else
        pthread_mutex_unlock(&crypto_locks[index]);
}
int JNI_OnLoad(JavaVM *vm, void *reserved) {
    (void)reserved;
    JNIEnv *env = 0;
    if (((int (*)(JavaVM *, void **, int))((*vm)[6]))(
            vm, (void **)&env, 0x00010006) != 0 ||
        !env || !ogplay_icu_on_load(env) || !ogplay_tls_on_load())
        return -1;
    crypto_lock_count = CRYPTO_num_locks();
    crypto_locks = malloc((size_t)crypto_lock_count * sizeof(int));
    if (!crypto_locks) return -1;
    for (int i = 0; i < crypto_lock_count; ++i) crypto_locks[i] = 0;
    CRYPTO_set_locking_callback(crypto_lock);
    CRYPTO_set_id_callback(pthread_self);
    if (!EVP_add_digest(EVP_sha1()) || !EVP_add_digest(EVP_sha224()) ||
        !EVP_add_digest(EVP_sha256()) || !EVP_add_digest(EVP_sha384()) ||
        !EVP_add_digest(EVP_sha512()))
        return -1;
    crypto_initialized = 1;
    return 0x00010006;
}
void N(clinit)(JNIEnv *env, jobject cls) {
    (void)cls;
    if (!crypto_initialized)
        fail(env, "java/security/ProviderException",
             "OGPlay crypto JNI initialization is incomplete");
}
static unsigned char *encoded(JNIEnv *env, jobject array, int *count, const char *exception) {
    *count = length(env, array);
    if (*count < 0) return 0;
    if (*count > 1048576) {
        fail(env, exception, "encoded value exceeds 1 MiB limit");
        return 0;
    }
    unsigned char *buffer = malloc((size_t)*count + 1);
    if (!buffer)
        fail(env, "java/lang/OutOfMemoryError", "encoded signature/key");
    else if (*count)
        read_bytes(env, array, 0, *count, buffer);
    return buffer;
}
extern int strncmp(const char *, const char *, size_t);
extern int EVP_DigestInit_ex(EVP_MD_CTX *, const EVP_MD *, void *);
extern int EVP_DigestUpdate(EVP_MD_CTX *, const void *, size_t);
extern int EVP_VerifyFinal(EVP_MD_CTX *, const unsigned char *, unsigned int, EVP_PKEY *);
extern EVP_MD_CTX *EVP_MD_CTX_create(void);
extern void EVP_MD_CTX_destroy(EVP_MD_CTX *);
unsigned char Java_org_ogplay_security_NativeVerification_verify(
        JNIEnv *env, jobject cls, jobject spki, jobject message,
        jobject signature, jobject algorithm) {
    (void)cls;
    if (!algorithm) {
        fail(env, "java/lang/NullPointerException", "algorithm == null");
        return 0;
    }
    const char *name = JNI(169, const char *(*)(JNIEnv *, jobject, void *))(env, algorithm, 0);
    if (!name) return 0;
    const char *rsa[] = {"SHA1withRSA", "SHA224withRSA", "SHA256withRSA", "SHA384withRSA",
                         "SHA512withRSA"};
    const char *ec[] = {"SHA1withECDSA", "SHA224withECDSA", "SHA256withECDSA", "SHA384withECDSA",
                        "SHA512withECDSA"};
    const EVP_MD *(*digests[])(void) = {EVP_sha1, EVP_sha224, EVP_sha256, EVP_sha384, EVP_sha512};
    const EVP_MD *digest = 0;
    int expected_type = 0;
    for (int i = 0; i < 5; ++i) {
        if (!strcmp(name, rsa[i])) {
            expected_type = 6;
            digest = digests[i]();
        }
        if (!strcmp(name, ec[i])) {
            expected_type = 408;
            digest = digests[i]();
        }
    }
    JNI(170, void (*)(JNIEnv *, jobject, const char *))(env, algorithm, name);
    if (!digest) {
        fail(env, "java/security/NoSuchAlgorithmException", "unsupported verification algorithm");
        return 0;
    }
    int key_size, message_size = 0, signature_size = 0;
    unsigned char *key_bytes = encoded(env, spki, &key_size, "java/security/InvalidKeyException");
    if (!key_bytes) return 0;
    unsigned char *message_bytes = 0, *signature_bytes = 0;
    EVP_MD_CTX *ctx = 0;
    ERR_clear_error();
    const unsigned char *cursor = key_bytes;
    EVP_PKEY *key = d2i_PUBKEY(0, &cursor, key_size);
    unsigned char valid = 0;
    if (!key || cursor != key_bytes + key_size || EVP_PKEY_base_id(key) != expected_type) {
        fail(env, "java/security/InvalidKeyException",
             "invalid or incompatible public key SubjectPublicKeyInfo");
        goto done;
    }
    /* Both null means engineInitVerify: validate the key without verifying data. */
    if (!message && !signature) {
        valid = 1;
        goto done;
    }
    message_bytes = encoded(env, message, &message_size, "java/security/SignatureException");
    if (!message_bytes) goto done;
    signature_bytes = encoded(env, signature, &signature_size, "java/security/SignatureException");
    if (!signature_bytes) goto done;
    ctx = EVP_MD_CTX_create();
    if (!ctx) {
        fail(env, "java/lang/OutOfMemoryError", "signature digest context");
        goto done;
    }
    if (!EVP_DigestInit_ex(ctx, digest, 0) ||
        !EVP_DigestUpdate(ctx, message_bytes, (size_t)message_size)) {
        fail(env, "java/security/SignatureException", "signature digest failed");
        goto done;
    }
    int result = EVP_VerifyFinal(ctx, signature_bytes, (unsigned int)signature_size, key);
    if (result < 0)
        fail(env, "java/security/SignatureException", "signature verification failed");
    else
        valid = result == 1;
done:
    if (ctx) EVP_MD_CTX_destroy(ctx);
    EVP_PKEY_free(key);
    free(key_bytes);
    free(message_bytes);
    free(signature_bytes);
    ERR_clear_error();
    return valid;
}

typedef struct PrivateKeyToken {
    struct PrivateKeyToken *next;
    int mutex, references, removed;
    jlong token;
    EVP_PKEY *key;
} PrivateKeyToken;
static PrivateKeyToken *private_keys;
static PrivateKeyToken *private_key(JNIEnv *env, jlong token) {
    pthread_mutex_lock(&registry_mutex);
    PrivateKeyToken *p = private_keys;
    while (p && p->token != token) p = p->next;
    if (p) ++p->references;
    pthread_mutex_unlock(&registry_mutex);
    if (!p) fail(env, "java/lang/IllegalStateException", "invalid private key token");
    else pthread_mutex_lock(&p->mutex);
    return p;
}
static void release_private_key(PrivateKeyToken **address) {
    PrivateKeyToken *p = *address;
    if (!p) return;
    pthread_mutex_unlock(&p->mutex);
    pthread_mutex_lock(&registry_mutex);
    int destroy = --p->references == 0 && p->removed;
    pthread_mutex_unlock(&registry_mutex);
    if (destroy) { EVP_PKEY_free(p->key); free(p); }
}
static jlong publish_private_key(JNIEnv *env, EVP_PKEY *key) {
    PrivateKeyToken *p = malloc(sizeof(PrivateKeyToken));
    if (!p) {
        EVP_PKEY_free(key);
        fail(env, "java/lang/OutOfMemoryError", "private key token");
        return 0;
    }
    p->mutex = p->references = p->removed = 0;
    p->key = key;
    pthread_mutex_lock(&registry_mutex);
    if (next_token == 0x7fffffffffffffffLL) {
        pthread_mutex_unlock(&registry_mutex);
        EVP_PKEY_free(key); free(p);
        fail(env, "java/lang/OutOfMemoryError", "native token space exhausted");
        return 0;
    }
    p->token = next_token++;
    p->next = private_keys;
    private_keys = p;
    pthread_mutex_unlock(&registry_mutex);
    return p->token;
}
static jobject new_bytes(JNIEnv *env, int count) {
    return JNI(176, jobject(*)(JNIEnv *, int))(env, count);
}
jobject Java_org_ogplay_security_NativeKeyStoreCrypto_desEdeCbc(
        JNIEnv *env, jobject cls, unsigned char encrypt, jobject key,
        jobject iv, jobject input) {
    (void)cls;
    if (length(env, key) != 24 || length(env, iv) != 8) {
        fail(env, "java/security/InvalidKeyException", "3DES requires 24-byte key and 8-byte IV");
        return 0;
    }
    int input_size = length(env, input);
    if (input_size < 0 || input_size > 1048576) {
        if (input_size > 1048576) fail(env, "java/lang/IllegalArgumentException", "3DES input exceeds 1 MiB");
        return 0;
    }
    unsigned char key_bytes[24], iv_bytes[8];
    unsigned char *source = malloc((size_t)input_size + 1);
    unsigned char *output = malloc((size_t)input_size + 9);
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!source || !output || !ctx) {
        fail(env, "java/lang/OutOfMemoryError", "3DES buffer");
        goto failed;
    }
    read_bytes(env, key, 0, 24, key_bytes);
    read_bytes(env, iv, 0, 8, iv_bytes);
    if (input_size) read_bytes(env, input, 0, input_size, source);
    int first = 0, last = 0;
    ERR_clear_error();
    if (!EVP_CipherInit_ex(ctx, EVP_des_ede3_cbc(), 0, key_bytes, iv_bytes, encrypt != 0) ||
        !EVP_CipherUpdate(ctx, output, &first, source, input_size) ||
        !EVP_CipherFinal_ex(ctx, output + first, &last)) {
        fail(env, encrypt ? "java/security/ProviderException" : "javax/crypto/BadPaddingException",
             encrypt ? "3DES encryption failed" : "incorrect key password");
        goto failed;
    }
    jobject result = new_bytes(env, first + last);
    if (result) write_bytes(env, result, 0, first + last, output);
    if (ctx) EVP_CIPHER_CTX_free(ctx);
    if (source) { volatile unsigned char *p = source; for (int i = 0; i < input_size; ++i) p[i] = 0; }
    if (output) { volatile unsigned char *p = output; for (int i = 0; i < input_size + 8; ++i) p[i] = 0; }
    {
        volatile unsigned char *wipe = key_bytes;
        for (int i = 0; i < 24; ++i) wipe[i] = 0;
        wipe = iv_bytes;
        for (int i = 0; i < 8; ++i) wipe[i] = 0;
    }
    free(source); free(output);
    return result;
failed:
    if (ctx) EVP_CIPHER_CTX_free(ctx);
    {
        volatile unsigned char *wipe = key_bytes;
        for (int i = 0; i < 24; ++i) wipe[i] = 0;
        wipe = iv_bytes;
        for (int i = 0; i < 8; ++i) wipe[i] = 0;
    }
    if (source) { volatile unsigned char *p = source; for (int i = 0; i < input_size; ++i) p[i] = 0; }
    if (output) { volatile unsigned char *p = output; for (int i = 0; i < input_size + 8; ++i) p[i] = 0; }
    free(source); free(output);
    return 0;
}
jlong Java_org_ogplay_security_NativeKeyStoreCrypto_decodePrivateKey(
        JNIEnv *env, jobject cls, jobject encoded_key) {
    (void)cls;
    int count = 0;
    unsigned char *bytes = encoded(env, encoded_key, &count, "java/security/InvalidKeyException");
    if (!bytes) return 0;
    const unsigned char *cursor = bytes;
    PKCS8_PRIV_KEY_INFO *info = d2i_PKCS8_PRIV_KEY_INFO(0, &cursor, count);
    EVP_PKEY *key = info && cursor == bytes + count ? EVP_PKCS82PKEY(info) : 0;
    if (info) PKCS8_PRIV_KEY_INFO_free(info);
    free(bytes);
    if (!key || (EVP_PKEY_base_id(key) != 6 && EVP_PKEY_base_id(key) != 408)) {
        if (key) EVP_PKEY_free(key);
        fail(env, "java/security/InvalidKeyException", "invalid RSA/EC PKCS#8 private key");
        return 0;
    }
    return publish_private_key(env, key);
}
jobject Java_org_ogplay_security_NativeKeyStoreCrypto_encodePrivateKey(
        JNIEnv *env, jobject cls, jlong token) {
    (void)cls;
    PrivateKeyToken *p __attribute__((cleanup(release_private_key))) = private_key(env, token);
    if (!p) return 0;
    PKCS8_PRIV_KEY_INFO *info = EVP_PKEY2PKCS8(p->key);
    int count = info ? i2d_PKCS8_PRIV_KEY_INFO(info, 0) : -1;
    unsigned char *bytes = count > 0 ? malloc((size_t)count) : 0;
    unsigned char *cursor = bytes;
    int written = bytes ? i2d_PKCS8_PRIV_KEY_INFO(info, &cursor) : -1;
    if (info) PKCS8_PRIV_KEY_INFO_free(info);
    if (written != count) {
        free(bytes);
        fail(env, "java/security/ProviderException", "private key encoding failed");
        return 0;
    }
    jobject result = new_bytes(env, count);
    if (result) write_bytes(env, result, 0, count, bytes);
    if (bytes) { volatile unsigned char *wipe = bytes; for (int i = 0; i < count; ++i) wipe[i] = 0; }
    free(bytes);
    return result;
}
int Java_org_ogplay_security_NativeKeyStoreCrypto_privateKeyType(
        JNIEnv *env, jobject cls, jlong token) {
    (void)cls;
    PrivateKeyToken *p __attribute__((cleanup(release_private_key))) = private_key(env, token);
    return p ? EVP_PKEY_base_id(p->key) : 0;
}
void Java_org_ogplay_security_NativeKeyStoreCrypto_freePrivateKey(
        JNIEnv *env, jobject cls, jlong token) {
    (void)cls;
    pthread_mutex_lock(&registry_mutex);
    PrivateKeyToken **cursor = &private_keys;
    while (*cursor && (*cursor)->token != token) cursor = &(*cursor)->next;
    if (!*cursor) {
        pthread_mutex_unlock(&registry_mutex);
        fail(env, "java/lang/IllegalStateException", "invalid private key token");
        return;
    }
    PrivateKeyToken *p = *cursor;
    *cursor = p->next;
    p->removed = 1;
    int destroy = p->references == 0;
    pthread_mutex_unlock(&registry_mutex);
    if (destroy) { EVP_PKEY_free(p->key); free(p); }
}
static void release_private_keys(void) {
    while (private_keys) {
        PrivateKeyToken *p = private_keys;
        private_keys = p->next;
        EVP_PKEY_free(p->key);
        free(p);
    }
}

static void release_crypto_locks(void) {
    CRYPTO_set_locking_callback(0);
    CRYPTO_set_id_callback(0);
    free(crypto_locks);
}

/* API 19 MessageDigest: Java owns the digest API and ctx field.
 * Tokens are monotonically allocated, never native pointers. */
extern const EVP_MD *EVP_md5(void);
extern int EVP_DigestFinal_ex(EVP_MD_CTX *, unsigned char *, unsigned int *);
extern int EVP_MD_CTX_copy_ex(EVP_MD_CTX *, const EVP_MD_CTX *);
typedef struct Digest {
    struct Digest *next;
    int mutex, references, removed;
    jlong token;
    EVP_MD_CTX *evp;
    int size;
} Digest;
static int update_hmac_token(JNIEnv *, jlong, jobject, int, int);
static int destroy_hmac_token(JNIEnv *, jlong);
static Digest *digests;
static Digest *digest_context(JNIEnv *env, jlong token) {
    pthread_mutex_lock(&registry_mutex);
    Digest *p = digests;
    while (p && p->token != token) p = p->next;
    if (p) ++p->references;
    pthread_mutex_unlock(&registry_mutex);
    if (!p) fail(env, "java/lang/IllegalStateException", "invalid digest context token");
    else pthread_mutex_lock(&p->mutex);
    return p;
}
static void release_digest(Digest **address) {
    Digest *p = *address;
    if (!p) return;
    pthread_mutex_unlock(&p->mutex);
    pthread_mutex_lock(&registry_mutex);
    int destroy = --p->references == 0 && p->removed;
    pthread_mutex_unlock(&registry_mutex);
    if (destroy) {
        EVP_MD_CTX_destroy(p->evp);
        free(p);
    }
}
#define DIGEST Digest *p __attribute__((cleanup(release_digest))) = digest_context(env, token)
typedef struct DigestAlgorithm {
    const char *name, *upper;
    const EVP_MD *(*evp)(void);
    int size;
} DigestAlgorithm;
static const DigestAlgorithm digest_algorithms[] = {
    {"md5", "MD5", EVP_md5, 16}, {"sha1", "SHA1", EVP_sha1, 20},
    {"sha256", "SHA256", EVP_sha256, 32}, {"sha384", "SHA384", EVP_sha384, 48},
    {"sha512", "SHA512", EVP_sha512, 64}
};
static const DigestAlgorithm *digest_algorithm(JNIEnv *env, jlong token) {
    if (token >= 1 && token <= 5) return &digest_algorithms[token - 1];
    fail(env, "java/lang/IllegalArgumentException", "unsupported digest algorithm token");
    return 0;
}
static jlong publish_digest(JNIEnv *env, EVP_MD_CTX *evp, int size) {
    Digest *p = malloc(sizeof(Digest));
    if (!p) {
        EVP_MD_CTX_destroy(evp);
        fail(env, "java/lang/OutOfMemoryError", "digest context");
        return 0;
    }
    p->mutex = p->references = p->removed = 0;
    p->evp = evp;
    p->size = size;
    pthread_mutex_lock(&registry_mutex);
    if (next_token == 0x7fffffffffffffffLL) {
        pthread_mutex_unlock(&registry_mutex);
        EVP_MD_CTX_destroy(evp);
        free(p);
        fail(env, "java/lang/OutOfMemoryError", "native token space exhausted");
        return 0;
    }
    p->token = next_token++;
    p->next = digests;
    digests = p;
    jlong result = p->token;
    pthread_mutex_unlock(&registry_mutex);
    return result;
}
jlong N(EVP_1get_1digestbyname)(JNIEnv *env, jobject cls, jobject name) {
    (void)cls;
    if (!name) {
        fail(env, "java/lang/NullPointerException", "digest name == null");
        return 0;
    }
    const char *text = JNI(169, const char *(*)(JNIEnv *, jobject, void *))(env, name, 0);
    if (!text) return 0;
    jlong result = 0;
    for (int i = 0; i < 5; ++i)
        if (!strcmp(text, digest_algorithms[i].name) || !strcmp(text, digest_algorithms[i].upper)) {
            result = i + 1;
            break;
        }
    JNI(170, void (*)(JNIEnv *, jobject, const char *))(env, name, text);
    return result;
}
int N(EVP_1MD_1size)(JNIEnv *env, jobject cls, jlong algorithm) {
    (void)cls;
    const DigestAlgorithm *a = digest_algorithm(env, algorithm);
    return a ? a->size : 0;
}
jlong N(EVP_1DigestInit)(JNIEnv *env, jobject cls, jlong algorithm) {
    (void)cls;
    const DigestAlgorithm *a = digest_algorithm(env, algorithm);
    if (!a) return 0;
    EVP_MD_CTX *evp = EVP_MD_CTX_create();
    if (!evp) {
        fail(env, "java/lang/OutOfMemoryError", "EVP digest context");
        return 0;
    }
    if (!EVP_DigestInit_ex(evp, a->evp(), 0)) {
        EVP_MD_CTX_destroy(evp);
        fail(env, "java/security/ProviderException", "EVP digest initialization failed");
        return 0;
    }
    return publish_digest(env, evp, a->size);
}
void N(EVP_1DigestUpdate)(JNIEnv *env, jobject cls, jlong token, jobject input, int offset, int count) {
    (void)cls;
    Digest *p __attribute__((cleanup(release_digest))) = 0;
    pthread_mutex_lock(&registry_mutex);
    p = digests;
    while (p && p->token != token) p = p->next;
    if (p) ++p->references;
    pthread_mutex_unlock(&registry_mutex);
    if (!p) {
        (void)update_hmac_token(env, token, input, offset, count);
        return;
    }
    pthread_mutex_lock(&p->mutex);
    if (!range(env, input, offset, count)) return;
    // Bound temporary memory, not the Java array or cumulative message length.
    const int capacity = count < 65536 ? count : 65536;
    unsigned char *data = malloc((size_t)capacity + 1);
    if (!data) {
        fail(env, "java/lang/OutOfMemoryError", "digest input buffer");
        return;
    }
    int ok = 1;
    while (count && ok) {
        int chunk = count < capacity ? count : capacity;
        read_bytes(env, input, offset, chunk, data);
        ok = EVP_DigestUpdate(p->evp, data, (size_t)chunk);
        offset += chunk;
        count -= chunk;
    }
    free(data);
    if (!ok) fail(env, "java/security/ProviderException", "EVP digest update failed");
}
void N(EVP_1MD_1CTX_1destroy)(JNIEnv *env, jobject cls, jlong token) {
    (void)cls;
    pthread_mutex_lock(&registry_mutex);
    Digest **cursor = &digests;
    while (*cursor && (*cursor)->token != token) cursor = &(*cursor)->next;
    if (!*cursor) {
        pthread_mutex_unlock(&registry_mutex);
        if (!destroy_hmac_token(env, token))
            fail(env, "java/lang/IllegalStateException", "invalid digest context token");
        return;
    }
    Digest *p = *cursor;
    *cursor = p->next;
    p->removed = 1;
    int destroy = p->references == 0;
    pthread_mutex_unlock(&registry_mutex);
    if (destroy) {
        EVP_MD_CTX_destroy(p->evp);
        free(p);
    }
}
int N(EVP_1DigestFinal)(JNIEnv *env, jobject cls, jlong token, jobject output, int offset) {
    DIGEST;
    if (!p || !range(env, output, offset, p->size)) return 0;
    unsigned char data[64];
    unsigned int count = 0;
    if (!EVP_DigestFinal_ex(p->evp, data, &count) || count != (unsigned int)p->size) {
        fail(env, "java/security/ProviderException", "EVP digest final failed");
        return 0;
    }
    write_bytes(env, output, offset, p->size, data);
    N(EVP_1MD_1CTX_1destroy)(env, cls, token);
    return p->size;
}
jlong N(EVP_1MD_1CTX_1copy)(JNIEnv *env, jobject cls, jlong token) {
    (void)cls;
    DIGEST;
    if (!p) return 0;
    EVP_MD_CTX *copy = EVP_MD_CTX_create();
    if (!copy) {
        fail(env, "java/lang/OutOfMemoryError", "digest copy");
        return 0;
    }
    if (!EVP_MD_CTX_copy_ex(copy, p->evp)) {
        EVP_MD_CTX_destroy(copy);
        fail(env, "java/security/ProviderException", "EVP digest copy failed");
        return 0;
    }
    return publish_digest(env, copy, p->size);
}
static void release_digests(void) {
    while (digests) {
        Digest *p = digests;
        digests = p->next;
        EVP_MD_CTX_destroy(p->evp);
        free(p);
    }
}

/* Bounded API 19 HmacSHA1 state. Public Mac/MacSpi behavior stays in BootDex;
 * this registry owns only incremental guest OpenSSL contexts and key pads. */
typedef struct HmacSha1 {
    struct HmacSha1 *next;
    int mutex, references, removed;
    jlong token;
    EVP_MD_CTX *inner;
    int initialized;
    unsigned char ipad[64], opad[64];
} HmacSha1;
static HmacSha1 *hmacs;
static int hmac_reset(HmacSha1 *p) {
    return EVP_DigestInit_ex(p->inner, EVP_sha1(), 0) &&
           EVP_DigestUpdate(p->inner, p->ipad, sizeof(p->ipad));
}
static HmacSha1 *hmac_context(JNIEnv *env, jlong token) {
    pthread_mutex_lock(&registry_mutex);
    HmacSha1 *p = hmacs;
    while (p && p->token != token) p = p->next;
    if (p) ++p->references;
    pthread_mutex_unlock(&registry_mutex);
    if (!p) fail(env, "java/lang/IllegalStateException", "invalid HmacSHA1 token");
    else pthread_mutex_lock(&p->mutex);
    return p;
}
static void release_hmac(HmacSha1 **address) {
    HmacSha1 *p = *address;
    if (!p) return;
    pthread_mutex_unlock(&p->mutex);
    pthread_mutex_lock(&registry_mutex);
    int destroy = --p->references == 0 && p->removed;
    pthread_mutex_unlock(&registry_mutex);
    if (destroy) {
        EVP_MD_CTX_destroy(p->inner);
        volatile unsigned char *wipe = p->ipad;
        for (int i = 0; i < 128; ++i) wipe[i] = 0;
        free(p);
    }
}
#define HMAC_SHA1 HmacSha1 *p __attribute__((cleanup(release_hmac))) = hmac_context(env, token)
static void final_hmac_token(JNIEnv *env, jlong token, jobject output) {
    HMAC_SHA1;
    if (!p || !p->initialized || !range(env, output, 0, 20)) return;
    unsigned char inner[20], result[20];
    unsigned int inner_size = 0, result_size = 0;
    EVP_MD_CTX *outer = EVP_MD_CTX_create();
    int ok = outer && EVP_DigestFinal_ex(p->inner, inner, &inner_size) &&
             inner_size == 20 && EVP_DigestInit_ex(outer, EVP_sha1(), 0) &&
             EVP_DigestUpdate(outer, p->opad, sizeof(p->opad)) &&
             EVP_DigestUpdate(outer, inner, sizeof(inner)) &&
             EVP_DigestFinal_ex(outer, result, &result_size) && result_size == 20 &&
             hmac_reset(p);
    if (outer) EVP_MD_CTX_destroy(outer);
    if (ok) write_bytes(env, output, 0, 20, result);
    else fail(env, "java/security/ProviderException", "HmacSHA1 final failed");
    volatile unsigned char *wipe_inner = inner;
    volatile unsigned char *wipe_result = result;
    for (int i = 0; i < 20; ++i) {
        wipe_inner[i] = 0;
        wipe_result[i] = 0;
    }
}
static void release_hmacs(void) {
    while (hmacs) {
        HmacSha1 *p = hmacs;
        hmacs = p->next;
        EVP_MD_CTX_destroy(p->inner);
        volatile unsigned char *wipe = p->ipad;
        for (int i = 0; i < 128; ++i) wipe[i] = 0;
        free(p);
    }
}

typedef struct MacKey {
    struct MacKey *next;
    jlong token;
    unsigned char normalized[64];
} MacKey;
static MacKey *mac_keys;
static void release_mac_keys(void) {
    while (mac_keys) {
        MacKey *p = mac_keys;
        mac_keys = p->next;
        volatile unsigned char *wipe = p->normalized;
        for (int i = 0; i < 64; ++i) wipe[i] = 0;
        free(p);
    }
}
static int normalize_hmac_key(JNIEnv *env, jobject key, unsigned char normalized[64]) {
    int key_size = length(env, key);
    if (key_size < 0) return 0;
    for (int i = 0; i < 64; ++i) normalized[i] = 0;
    if (key_size <= 64) {
        if (key_size) read_bytes(env, key, 0, key_size, normalized);
        return 1;
    }
    EVP_MD_CTX *hash = EVP_MD_CTX_create();
    unsigned char buffer[4096];
    unsigned int hashed = 0;
    int ok = hash && EVP_DigestInit_ex(hash, EVP_sha1(), 0);
    for (int offset = 0; ok && offset < key_size;) {
        int chunk = key_size - offset < (int)sizeof(buffer)
            ? key_size - offset : (int)sizeof(buffer);
        read_bytes(env, key, offset, chunk, buffer);
        ok = EVP_DigestUpdate(hash, buffer, (size_t)chunk);
        offset += chunk;
    }
    if (ok) ok = EVP_DigestFinal_ex(hash, normalized, &hashed) && hashed == 20;
    if (hash) EVP_MD_CTX_destroy(hash);
    if (!ok) fail(env, "java/security/InvalidKeyException", "HmacSHA1 key hashing failed");
    return ok;
}
static jlong publish_empty_hmac(JNIEnv *env) {
    HmacSha1 *p = malloc(sizeof(HmacSha1));
    if (!p) {
        fail(env, "java/lang/OutOfMemoryError", "HmacSHA1 context");
        return 0;
    }
    p->inner = EVP_MD_CTX_create();
    if (!p->inner) {
        free(p);
        fail(env, "java/lang/OutOfMemoryError", "HmacSHA1 digest context");
        return 0;
    }
    p->mutex = p->references = p->removed = p->initialized = 0;
    for (int i = 0; i < 64; ++i) p->ipad[i] = p->opad[i] = 0;
    pthread_mutex_lock(&registry_mutex);
    if (next_token == 0x7fffffffffffffffLL) {
        pthread_mutex_unlock(&registry_mutex);
        EVP_MD_CTX_destroy(p->inner);
        free(p);
        fail(env, "java/lang/OutOfMemoryError", "native token space exhausted");
        return 0;
    }
    p->token = next_token++;
    p->next = hmacs;
    hmacs = p;
    jlong result = p->token;
    pthread_mutex_unlock(&registry_mutex);
    return result;
}
static int update_hmac_token(JNIEnv *env, jlong token, jobject input,
                             int offset, int count) {
    HMAC_SHA1;
    if (!p) return 0;
    if (!p->initialized) {
        fail(env, "java/lang/IllegalStateException", "HmacSHA1 is not initialized");
        return 1;
    }
    if (!range(env, input, offset, count)) return 1;
    unsigned char buffer[4096];
    int ok = 1;
    while (count && ok) {
        int chunk = count < (int)sizeof(buffer) ? count : (int)sizeof(buffer);
        read_bytes(env, input, offset, chunk, buffer);
        ok = EVP_DigestUpdate(p->inner, buffer, (size_t)chunk);
        offset += chunk;
        count -= chunk;
    }
    if (!ok) fail(env, "java/security/ProviderException", "HmacSHA1 update failed");
    return 1;
}
static int destroy_hmac_token(JNIEnv *env, jlong token) {
    (void)env;
    pthread_mutex_lock(&registry_mutex);
    HmacSha1 **cursor = &hmacs;
    while (*cursor && (*cursor)->token != token) cursor = &(*cursor)->next;
    if (!*cursor) {
        pthread_mutex_unlock(&registry_mutex);
        return 0;
    }
    HmacSha1 *p = *cursor;
    *cursor = p->next;
    p->removed = 1;
    int destroy = p->references == 0;
    pthread_mutex_unlock(&registry_mutex);
    if (destroy) {
        EVP_MD_CTX_destroy(p->inner);
        volatile unsigned char *wipe = p->ipad;
        for (int i = 0; i < 128; ++i) wipe[i] = 0;
        free(p);
    }
    return 1;
}
jlong N(EVP_1MD_1CTX_1create)(JNIEnv *env, jobject cls) {
    (void)cls;
    return publish_empty_hmac(env);
}
void N(EVP_1MD_1CTX_1init)(JNIEnv *env, jobject cls, jlong token) {
    (void)cls;
    HMAC_SHA1;
    if (p && p->initialized && !hmac_reset(p))
        fail(env, "java/security/ProviderException", "HmacSHA1 reset failed");
}
jlong N(EVP_1PKEY_1new_1mac_1key)(JNIEnv *env, jobject cls, int type, jobject key) {
    (void)cls;
    if (type != 855) {
        fail(env, "java/security/InvalidKeyException", "unsupported MAC key type");
        return 0;
    }
    MacKey *p = malloc(sizeof(MacKey));
    if (!p) {
        fail(env, "java/lang/OutOfMemoryError", "MAC key");
        return 0;
    }
    if (!normalize_hmac_key(env, key, p->normalized)) {
        free(p);
        return 0;
    }
    pthread_mutex_lock(&registry_mutex);
    if (next_token == 0x7fffffffffffffffLL) {
        pthread_mutex_unlock(&registry_mutex);
        free(p);
        fail(env, "java/lang/OutOfMemoryError", "native token space exhausted");
        return 0;
    }
    p->token = next_token++;
    p->next = mac_keys;
    mac_keys = p;
    jlong result = p->token;
    pthread_mutex_unlock(&registry_mutex);
    return result;
}
void N(EVP_1PKEY_1free)(JNIEnv *env, jobject cls, jlong token) {
    (void)cls;
    pthread_mutex_lock(&registry_mutex);
    MacKey **cursor = &mac_keys;
    while (*cursor && (*cursor)->token != token) cursor = &(*cursor)->next;
    if (!*cursor) {
        pthread_mutex_unlock(&registry_mutex);
        fail(env, "java/lang/IllegalStateException", "invalid MAC key token");
        return;
    }
    MacKey *p = *cursor;
    *cursor = p->next;
    pthread_mutex_unlock(&registry_mutex);
    volatile unsigned char *wipe = p->normalized;
    for (int i = 0; i < 64; ++i) wipe[i] = 0;
    free(p);
}
void N(EVP_1DigestSignInit)(JNIEnv *env, jobject cls, jlong token,
                            jlong algorithm, jlong key_token) {
    (void)cls;
    if (algorithm != 2) {
        fail(env, "java/security/InvalidAlgorithmParameterException",
             "only HmacSHA1 is supported");
        return;
    }
    unsigned char normalized[64];
    pthread_mutex_lock(&registry_mutex);
    MacKey *key = mac_keys;
    while (key && key->token != key_token) key = key->next;
    if (key) for (int i = 0; i < 64; ++i) normalized[i] = key->normalized[i];
    pthread_mutex_unlock(&registry_mutex);
    if (!key) {
        fail(env, "java/security/InvalidKeyException", "invalid MAC key token");
        return;
    }
    HMAC_SHA1;
    if (!p) return;
    for (int i = 0; i < 64; ++i) {
        p->ipad[i] = normalized[i] ^ 0x36;
        p->opad[i] = normalized[i] ^ 0x5c;
        normalized[i] = 0;
    }
    p->initialized = 1;
    if (!hmac_reset(p))
        fail(env, "java/security/ProviderException", "HmacSHA1 initialization failed");
}
jobject N(EVP_1DigestSignFinal)(JNIEnv *env, jobject cls, jlong token) {
    (void)cls;
    jobject output = JNI(176, jobject(*)(JNIEnv *, int))(env, 20);
    if (!output) return 0;
    final_hmac_token(env, token, output);
    return output;
}
