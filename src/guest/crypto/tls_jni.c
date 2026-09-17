/* Guest libssl BIO client. JNI 1.6 table slots; no OpenSSL struct layout. */
typedef unsigned int size_t;
typedef long long jlong;
typedef void *jobject;
typedef const void **JNIEnv;
typedef struct ssl_st SSL;
typedef struct ssl_ctx_st SSL_CTX;
typedef struct ssl_session_st SSL_SESSION;
typedef struct ssl_method_st SSL_METHOD;
typedef struct ssl_cipher_st SSL_CIPHER;
typedef struct bio_st BIO;
typedef struct bio_method_st BIO_METHOD;
typedef struct x509_st X509;
typedef struct x509_store_ctx_st X509_STORE_CTX;
typedef struct evp_pkey_st EVP_PKEY;
typedef struct ec_key_st EC_KEY;
typedef struct stack_st _STACK;
#define JNI(slot, type) ((type)((*env)[slot]))
extern void *malloc(size_t);
extern void free(void *);
extern void *memcpy(void *, const void *, size_t);
extern int strcmp(const char *, const char *);
extern int pthread_mutex_lock(int *);
extern int pthread_mutex_unlock(int *);
extern int SSL_library_init(void);
extern void SSL_load_error_strings(void);
extern const SSL_METHOD *SSLv23_client_method(void);
extern SSL_CTX *SSL_CTX_new(const SSL_METHOD *);
extern void SSL_CTX_free(SSL_CTX *);
extern long SSL_CTX_ctrl(SSL_CTX *, int, long, void *);
extern int SSL_CTX_set_cipher_list(SSL_CTX *, const char *);
extern void SSL_CTX_set_verify(SSL_CTX *, int, int (*)(int, X509_STORE_CTX *));
extern SSL *SSL_new(SSL_CTX *);
extern void SSL_free(SSL *);
extern void SSL_set_connect_state(SSL *);
extern void SSL_set_bio(SSL *, BIO *, BIO *);
extern int SSL_do_handshake(SSL *);
extern int SSL_read(SSL *, void *, int);
extern int SSL_write(SSL *, const void *, int);
extern int SSL_shutdown(SSL *);
extern int SSL_get_error(const SSL *, int);
extern long SSL_ctrl(SSL *, int, long, void *);
extern int SSL_set_ex_data(SSL *, int, void *);
extern void *SSL_get_ex_data(const SSL *, int);
extern int SSL_get_ex_new_index(long, void *, void *, void *, void *);
extern int SSL_get_ex_data_X509_STORE_CTX_idx(void);
extern X509 *SSL_get_peer_certificate(const SSL *);
extern _STACK *SSL_get_peer_cert_chain(const SSL *);
extern const char *SSL_get_version(const SSL *);
extern const SSL_CIPHER *SSL_get_current_cipher(const SSL *);
extern const char *SSL_CIPHER_get_name(const SSL_CIPHER *);
extern const char *SSL_get_cipher_list(const SSL *, int);
extern int SSL_use_certificate(SSL *, X509 *);
extern int SSL_use_PrivateKey(SSL *, EVP_PKEY *);
extern BIO *BIO_new(BIO_METHOD *);
extern int BIO_free(BIO *);
extern BIO_METHOD *BIO_s_mem(void);
extern int BIO_read(BIO *, void *, int);
extern int BIO_write(BIO *, const void *, int);
extern long BIO_ctrl(BIO *, int, long, void *);
extern int SSL_set_cipher_list(SSL *, const char *);
extern SSL_SESSION *SSL_get1_session(SSL *);
extern void SSL_SESSION_free(SSL_SESSION *);
extern const unsigned char *SSL_SESSION_get_id(const SSL_SESSION *, unsigned int *);
extern BIO *SSL_get_wbio(const SSL *);
extern BIO *SSL_get_rbio(const SSL *);
extern X509 *d2i_X509(X509 **, const unsigned char **, long);
extern void X509_free(X509 *);
extern int i2d_X509(X509 *, unsigned char **);
extern EVP_PKEY *d2i_AutoPrivateKey(EVP_PKEY **, const unsigned char **, long);
extern void EVP_PKEY_free(EVP_PKEY *);
extern EVP_PKEY *X509_get_pubkey(X509 *);
extern int EVP_PKEY_base_id(const EVP_PKEY *);
extern int sk_num(const _STACK *);
extern void *sk_value(const _STACK *, int);
extern SSL_CTX *SSL_get_SSL_CTX(const SSL *);
extern void *X509_STORE_CTX_get_ex_data(X509_STORE_CTX *, int);
extern X509 *X509_STORE_CTX_get_current_cert(X509_STORE_CTX *);
extern int X509_STORE_CTX_get_error_depth(X509_STORE_CTX *);
extern _STACK *X509_STORE_CTX_get_chain(X509_STORE_CTX *);
extern int X509_cmp(X509 *, X509 *);
extern void ERR_clear_error(void);
extern unsigned long ERR_get_error(void);
extern void RAND_seed(const void *, int);
extern int RAND_status(void);
extern EC_KEY *EC_KEY_new_by_curve_name(int);
extern void EC_KEY_free(EC_KEY *);
#define SSL_CTRL_OPTIONS 32
#define SSL_CTRL_CLEAR_OPTIONS 77
#define SSL_CTRL_MODE 33
#define SSL_CTRL_EXTRA_CHAIN_CERT 14
#define SSL_CTRL_SET_TMP_ECDH 4
#define SSL_CTRL_SET_TLSEXT_HOSTNAME 55
#define SSL_OP_NO_SSLv2 0x01000000L
#define SSL_OP_NO_SSLv3 0x02000000L
#define SSL_OP_NO_TLSv1 0x04000000L
#define SSL_OP_NO_TLSv1_1 0x10000000L
#define SSL_OP_NO_TLSv1_2 0x08000000L
#define SSL_OP_SINGLE_ECDH_USE 0x00080000L
#define SSL_MODE_ENABLE_PARTIAL_WRITE 1L
#define SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER 2L
#define SSL_VERIFY_NONE 0x00
#define SSL_VERIFY_PEER 0x01
#define NID_X9_62_PRIME256V1 415
#define SSL_ERROR_WANT_READ 2
#define SSL_ERROR_WANT_WRITE 3
#define SSL_ERROR_SYSCALL 5
#define SSL_ERROR_ZERO_RETURN 6
#define BIO_CTRL_PENDING 10
#define BIO_C_SET_BUF_MEM_EOF_RETURN 130
#define BIO_C_SET_NBIO 102
#define EVP_PKEY_RSA 6
#define EVP_PKEY_DSA 116
#define EVP_PKEY_EC 408
#define MAX_DER 65536
#define MAX_CHAIN 16
#define MAX_WIRE 65536
#define STATUS_OK 1
#define STATUS_WANT_READ 2
#define STATUS_WANT_WRITE 3
#define STATUS_CLOSED 0
#define STATUS_FAILED -1
typedef struct CipherName {
    const char *openssl;
    const char *java;
} CipherName;
static const CipherName kCipherNames[] = {
    {"ECDHE-ECDSA-AES128-GCM-SHA256", "TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256"},
    {"ECDHE-RSA-AES128-GCM-SHA256", "TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256"},
    {"ECDHE-ECDSA-AES256-GCM-SHA384", "TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384"},
    {"ECDHE-RSA-AES256-GCM-SHA384", "TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384"},
    {"ECDHE-ECDSA-AES128-SHA256", "TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA256"},
    {"ECDHE-RSA-AES128-SHA256", "TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256"},
    {"ECDHE-ECDSA-AES128-SHA", "TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA"},
    {"ECDHE-RSA-AES128-SHA", "TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA"},
    {"ECDHE-ECDSA-AES256-SHA", "TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA"},
    {"ECDHE-RSA-AES256-SHA", "TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA"},
    {"AES128-GCM-SHA256", "TLS_RSA_WITH_AES_128_GCM_SHA256"},
    {"AES256-GCM-SHA384", "TLS_RSA_WITH_AES_256_GCM_SHA384"},
    {"AES128-SHA256", "TLS_RSA_WITH_AES_128_CBC_SHA256"},
    {"AES256-SHA256", "TLS_RSA_WITH_AES_256_CBC_SHA256"},
    {"AES128-SHA", "TLS_RSA_WITH_AES_128_CBC_SHA"},
    {"AES256-SHA", "TLS_RSA_WITH_AES_256_CBC_SHA"},
};
static const char *kDefaultCiphers =
    "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:"
    "ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:"
    "ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-AES128-SHA256:"
    "ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-AES128-SHA:"
    "ECDHE-ECDSA-AES256-SHA:ECDHE-RSA-AES256-SHA:"
    "AES128-GCM-SHA256:AES256-GCM-SHA384:AES128-SHA256:AES256-SHA256:AES128-SHA:AES256-SHA";
static const char *java_cipher(const char *openssl) {
    size_t i;
    if (!openssl) return "SSL_NULL_WITH_NULL_NULL";
    for (i = 0; i < sizeof(kCipherNames) / sizeof(kCipherNames[0]); ++i)
        if (!strcmp(kCipherNames[i].openssl, openssl)) return kCipherNames[i].java;
    return openssl;
}
static const char *openssl_cipher(const char *java) {
    size_t i;
    if (!java) return 0;
    for (i = 0; i < sizeof(kCipherNames) / sizeof(kCipherNames[0]); ++i)
        if (!strcmp(kCipherNames[i].java, java) || !strcmp(kCipherNames[i].openssl, java))
            return kCipherNames[i].openssl;
    return 0;
}
static int registry_mutex;
static int tls_ex_index = -1;
typedef struct Ctx {
    struct Ctx *next;
    int mutex, references, removed;
    jlong token;
    SSL_CTX *ssl;
} Ctx;
typedef struct Session {
    struct Session *next;
    int mutex, references, removed;
    jlong token;
    SSL *ssl;
    JNIEnv *env;
    jobject trust_manager;
    unsigned char client_auth;
    int verified;
} Session;
static Ctx *contexts;
static Session *sessions;
static jlong next_token = 1;
static void fail(JNIEnv *env, const char *type, const char *message) {
    jobject cls = JNI(6, jobject(*)(JNIEnv *, const char *))(env, type);
    if (cls) JNI(14, int (*)(JNIEnv *, jobject, const char *))(env, cls, message);
}
static int pending(JNIEnv *env) { return JNI(228, unsigned char (*)(JNIEnv *))(env); }
static int length(JNIEnv *env, jobject array) {
    if (!array) {
        fail(env, "java/lang/NullPointerException", "array == null");
        return -1;
    }
    return JNI(171, int (*)(JNIEnv *, jobject))(env, array);
}
static void read_bytes(JNIEnv *env, jobject a, int off, int n, unsigned char *p) {
    JNI(200, void (*)(JNIEnv *, jobject, int, int, unsigned char *))(env, a, off, n, p);
}
static void write_bytes(JNIEnv *env, jobject a, int off, int n, const unsigned char *p) {
    JNI(208, void (*)(JNIEnv *, jobject, int, int, const unsigned char *))(env, a, off, n, p);
}
static void store_i32(unsigned char *p, int value) {
    p[0] = (unsigned char)value;
    p[1] = (unsigned char)(value >> 8);
    p[2] = (unsigned char)(value >> 16);
    p[3] = (unsigned char)(value >> 24);
}
static int load_i32(const unsigned char *p) {
    return (int)((unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) |
                 ((unsigned)p[3] << 24));
}
static const char *utf8(JNIEnv *env, jobject s) {
    return s ? JNI(169, const char *(*)(JNIEnv *, jobject, void *))(env, s, 0) : 0;
}
static void release_utf8(JNIEnv *env, jobject s, const char *p) {
    if (s && p) JNI(170, void (*)(JNIEnv *, jobject, const char *))(env, s, p);
}
static jobject new_bytes(JNIEnv *env, int n) {
    return JNI(176, jobject(*)(JNIEnv *, int))(env, n);
}
static jobject new_string(JNIEnv *env, const char *text) {
    return JNI(167, jobject(*)(JNIEnv *, const char *))(env, text);
}
static jobject string_class(JNIEnv *env) {
    return JNI(6, jobject(*)(JNIEnv *, const char *))(env, "java/lang/String");
}
static jobject new_object_array(JNIEnv *env, int n, jobject cls) {
    return JNI(172, jobject(*)(JNIEnv *, int, jobject, jobject))(env, n, cls, 0);
}
static void set_element(JNIEnv *env, jobject array, int index, jobject value) {
    JNI(174, void (*)(JNIEnv *, jobject, int, jobject))(env, array, index, value);
}
static jobject element(JNIEnv *env, jobject array, int index) {
    return JNI(173, jobject(*)(JNIEnv *, jobject, int))(env, array, index);
}
static Ctx *lock_ctx(JNIEnv *env, jlong token) {
    pthread_mutex_lock(&registry_mutex);
    Ctx *p = contexts;
    while (p && p->token != token) p = p->next;
    if (p) ++p->references;
    pthread_mutex_unlock(&registry_mutex);
    if (!p)
        fail(env, "java/lang/IllegalStateException", "invalid TLS context token");
    else
        pthread_mutex_lock(&p->mutex);
    return p;
}
static void release_ctx(Ctx **address) {
    Ctx *p = *address;
    if (!p) return;
    pthread_mutex_unlock(&p->mutex);
    pthread_mutex_lock(&registry_mutex);
    int destroy = (--p->references == 0 && p->removed);
    pthread_mutex_unlock(&registry_mutex);
    if (destroy) {
        SSL_CTX_free(p->ssl);
        free(p);
    }
}
static Session *lock_session(JNIEnv *env, jlong token) {
    pthread_mutex_lock(&registry_mutex);
    Session *p = sessions;
    while (p && p->token != token) p = p->next;
    if (p) ++p->references;
    pthread_mutex_unlock(&registry_mutex);
    if (!p)
        fail(env, "java/lang/IllegalStateException", "invalid TLS session token");
    else
        pthread_mutex_lock(&p->mutex);
    return p;
}
static void release_session(Session **address) {
    Session *p = *address;
    if (!p) return;
    pthread_mutex_unlock(&p->mutex);
    pthread_mutex_lock(&registry_mutex);
    int destroy = (--p->references == 0 && p->removed);
    pthread_mutex_unlock(&registry_mutex);
    if (destroy) {
        if (p->ssl) SSL_free(p->ssl);
        free(p);
    }
}
#define CONTEXT Ctx *ctx __attribute__((cleanup(release_ctx))) = lock_ctx(env, token)
#define SESSION Session *s __attribute__((cleanup(release_session))) = lock_session(env, token)
static const char *auth_type(X509 *cert) {
    EVP_PKEY *key = X509_get_pubkey(cert);
    if (!key) return "RSA";
    int id = EVP_PKEY_base_id(key);
    EVP_PKEY_free(key);
    if (id == EVP_PKEY_EC) return "EC";
    if (id == EVP_PKEY_DSA) return "DSA";
    return "RSA";
}
static int invoke_verify(Session *s, X509 *leaf, _STACK *chain) {
    JNIEnv *env = s->env;
    if (!env || !s->trust_manager || !leaf) return 0;
    int extra = chain ? sk_num(chain) : 0;
    if (extra < 0) extra = 0;
    if (extra > MAX_CHAIN - 1) extra = MAX_CHAIN - 1;
    int count = 1;
    int sizes[MAX_CHAIN];
    int total = 4;
    int i;
    sizes[0] = i2d_X509(leaf, 0);
    if (sizes[0] < 1) return 0;
    total += 4 + sizes[0];
    for (i = 0; i < extra; ++i) {
        X509 *item = (X509 *)sk_value(chain, i);
        if (!item || X509_cmp(item, leaf) == 0) {
            sizes[i + 1] = 0;
            continue;
        }
        sizes[i + 1] = i2d_X509(item, 0);
        if (sizes[i + 1] < 1) return 0;
        total += 4 + sizes[i + 1];
        ++count;
    }
    unsigned char *blob = malloc((size_t)total);
    if (!blob) return 0;
    store_i32(blob, count);
    int offset = 4;
    store_i32(blob + offset, sizes[0]);
    offset += 4;
    {
        unsigned char *p = blob + offset;
        i2d_X509(leaf, &p);
        offset += sizes[0];
    }
    for (i = 0; i < extra; ++i) {
        if (sizes[i + 1] < 1) continue;
        store_i32(blob + offset, sizes[i + 1]);
        offset += 4;
        unsigned char *p = blob + offset;
        i2d_X509((X509 *)sk_value(chain, i), &p);
        offset += sizes[i + 1];
    }
    jobject packed = new_bytes(env, total);
    if (packed) write_bytes(env, packed, 0, total, blob);
    free(blob);
    if (!packed || pending(env)) return 0;
    jobject native = JNI(6, jobject(*)(JNIEnv *, const char *))(env, "org/ogplay/security/NativeTls");
    jobject method = JNI(113, jobject(*)(JNIEnv *, jobject, const char *, const char *))(
        env, native, "dispatchVerify", "(Ljava/lang/Object;[BLjava/lang/String;Z)V");
    if (!method) {
        fail(env, "java/security/cert/CertificateException", "dispatchVerify is unavailable");
        return 0;
    }
    jobject auth = new_string(env, auth_type(leaf));
    JNI(141, void (*)(JNIEnv *, jobject, jobject, jobject, jobject, jobject, unsigned char))(
        env, native, method, s->trust_manager, packed, auth, s->client_auth);
    if (pending(env)) return 0;
    s->verified = 1;
    return 1;
}
static int verify_cb(int preverify_ok, X509_STORE_CTX *ctx) {
    (void)preverify_ok;
    if (X509_STORE_CTX_get_error_depth(ctx) != 0) return 1;
    SSL *ssl = (SSL *)X509_STORE_CTX_get_ex_data(ctx, SSL_get_ex_data_X509_STORE_CTX_idx());
    if (!ssl) return 0;
    Session *s = (Session *)SSL_get_ex_data(ssl, tls_ex_index);
    if (!s) return 0;
    X509 *leaf = X509_STORE_CTX_get_current_cert(ctx);
    if (!leaf) return 0;
    return invoke_verify(s, leaf, X509_STORE_CTX_get_chain(ctx));
}
static int fill_cipher_list(JNIEnv *env, jobject ciphers, char *cipher_list, int capacity) {
    int used = 0;
    cipher_list[0] = 0;
    if (!ciphers) {
        int i = 0;
        while (kDefaultCiphers[i] && i < capacity - 1) {
            cipher_list[i] = kDefaultCiphers[i];
            ++i;
        }
        cipher_list[i] = 0;
        return 1;
    }
    int n = length(env, ciphers);
    int i;
    if (n < 0) return 0;
    for (i = 0; i < n; ++i) {
        jobject item = element(env, ciphers, i);
        const char *name = utf8(env, item);
        const char *ossl = openssl_cipher(name);
        release_utf8(env, item, name);
        if (!ossl) {
            fail(env, "java/lang/IllegalArgumentException", "unsupported cipher suite");
            return 0;
        }
        int len = 0;
        while (ossl[len]) ++len;
        if (used + len + 2 >= capacity) {
            fail(env, "java/lang/IllegalArgumentException", "cipher list exceeds limit");
            return 0;
        }
        if (used) cipher_list[used++] = ':';
        memcpy(cipher_list + used, ossl, (size_t)len);
        used += len;
        cipher_list[used] = 0;
    }
    return 1;
}
void Java_org_ogplay_security_NativeTls_seed(JNIEnv *env, jobject cls, jobject entropy) {
    (void)cls;
    int count = length(env, entropy);
    if (count < 0) return;
    if (count < 32) {
        fail(env, "java/security/ProviderException", "TLS entropy is shorter than 32 bytes");
        return;
    }
    unsigned char buffer[256];
    int offset = 0;
    while (offset < count) {
        int chunk = count - offset;
        if (chunk > (int)sizeof(buffer)) chunk = (int)sizeof(buffer);
        read_bytes(env, entropy, offset, chunk, buffer);
        RAND_seed(buffer, chunk);
        offset += chunk;
    }
}
int ogplay_tls_on_load(void) {
    if (SSL_library_init() != 1) return 0;
    SSL_load_error_strings();
    if (tls_ex_index < 0)
        tls_ex_index = SSL_get_ex_new_index(0, 0, 0, 0, 0);
    return tls_ex_index >= 0;
}
void ogplay_tls_on_unload(void) {
    pthread_mutex_lock(&registry_mutex);
    while (sessions) {
        Session *p = sessions;
        sessions = p->next;
        if (p->ssl) SSL_free(p->ssl);
        free(p);
    }
    while (contexts) {
        Ctx *p = contexts;
        contexts = p->next;
        SSL_CTX_free(p->ssl);
        free(p);
    }
    pthread_mutex_unlock(&registry_mutex);
}
static long disable_unused(const SSL_METHOD *method, jobject protocols, JNIEnv *env) {
    (void)method;
    long options = SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3;
    int allow_v1 = 0, allow_v11 = 0, allow_v12 = 0;
    int n = length(env, protocols);
    int i;
    if (n < 0) return -1;
    for (i = 0; i < n; ++i) {
        jobject item = element(env, protocols, i);
        const char *name = utf8(env, item);
        if (!name) return -1;
        if (!strcmp(name, "TLSv1")) allow_v1 = 1;
        else if (!strcmp(name, "TLSv1.1")) allow_v11 = 1;
        else if (!strcmp(name, "TLSv1.2")) allow_v12 = 1;
        else if (!strcmp(name, "SSLv3") || !strcmp(name, "SSLv2")) {
            release_utf8(env, item, name);
            fail(env, "java/lang/IllegalArgumentException", "SSLv3 is not supported");
            return -1;
        }
        release_utf8(env, item, name);
    }
    if (!allow_v1) options |= SSL_OP_NO_TLSv1;
    if (!allow_v11) options |= SSL_OP_NO_TLSv1_1;
    if (!allow_v12) options |= SSL_OP_NO_TLSv1_2;
    if (!allow_v1 && !allow_v11 && !allow_v12) {
        fail(env, "java/lang/IllegalArgumentException", "no enabled TLS protocol");
        return -1;
    }
    return options;
}
jlong Java_org_ogplay_security_NativeTls_createContext(JNIEnv *env, jobject cls, jobject protocols,
                                                       jobject ciphers) {
    (void)cls;
    ERR_clear_error();
    if (RAND_status() != 1) {
        fail(env, "javax/net/ssl/SSLException", "OpenSSL RNG is not seeded");
        return 0;
    }
    long options = disable_unused(0, protocols, env);
    if (options < 0) return 0;
    SSL_CTX *ssl = SSL_CTX_new(SSLv23_client_method());
    if (!ssl) {
        fail(env, "javax/net/ssl/SSLException", "SSL_CTX_new failed");
        return 0;
    }
    options |= SSL_OP_SINGLE_ECDH_USE;
    SSL_CTX_ctrl(ssl, SSL_CTRL_OPTIONS, options, 0);
    SSL_CTX_ctrl(ssl, SSL_CTRL_MODE,
                 SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER, 0);
    EC_KEY *ecdh = EC_KEY_new_by_curve_name(NID_X9_62_PRIME256V1);
    if (!ecdh || SSL_CTX_ctrl(ssl, SSL_CTRL_SET_TMP_ECDH, 0, ecdh) != 1) {
        if (ecdh) EC_KEY_free(ecdh);
        SSL_CTX_free(ssl);
        fail(env, "javax/net/ssl/SSLException", "ECDHE is not available");
        return 0;
    }
    char cipher_list[1024];
    if (!fill_cipher_list(env, ciphers, cipher_list, (int)sizeof(cipher_list))) {
        SSL_CTX_free(ssl);
        return 0;
    }
    if (SSL_CTX_set_cipher_list(ssl, cipher_list[0] ? cipher_list : kDefaultCiphers) != 1) {
        SSL_CTX_free(ssl);
        fail(env, "javax/net/ssl/SSLException", "cipher list is not supported");
        return 0;
    }
    SSL_CTX_set_verify(ssl, SSL_VERIFY_PEER, verify_cb);
    Ctx *p = malloc(sizeof(Ctx));
    if (!p) {
        SSL_CTX_free(ssl);
        fail(env, "javax/net/ssl/SSLException", "out of memory");
        return 0;
    }
    p->mutex = 0;
    p->references = 1;
    p->removed = 0;
    p->ssl = ssl;
    pthread_mutex_lock(&registry_mutex);
    p->token = next_token++;
    p->next = contexts;
    contexts = p;
    pthread_mutex_unlock(&registry_mutex);
    return p->token;
}
jlong Java_org_ogplay_security_NativeTls_createSsl(JNIEnv *env, jobject cls, jlong token,
                                                   jobject hostname) {
    (void)cls;
    CONTEXT;
    if (!ctx) return 0;
    SSL *ssl = SSL_new(ctx->ssl);
    BIO *in = BIO_new(BIO_s_mem());
    BIO *out = BIO_new(BIO_s_mem());
    if (!ssl || !in || !out) {
        if (ssl) SSL_free(ssl);
        else {
            if (in) BIO_free(in);
            if (out) BIO_free(out);
        }
        fail(env, "javax/net/ssl/SSLException", "SSL_new failed");
        return 0;
    }
    BIO_ctrl(in, BIO_C_SET_BUF_MEM_EOF_RETURN, -1, 0);
    BIO_ctrl(out, BIO_C_SET_BUF_MEM_EOF_RETURN, -1, 0);
    BIO_ctrl(in, BIO_C_SET_NBIO, 1, 0);
    BIO_ctrl(out, BIO_C_SET_NBIO, 1, 0);
    SSL_set_bio(ssl, in, out);
    SSL_set_connect_state(ssl);
    const char *host = utf8(env, hostname);
    if (host && host[0]) SSL_ctrl(ssl, SSL_CTRL_SET_TLSEXT_HOSTNAME, 0, (void *)host);
    release_utf8(env, hostname, host);
    Session *p = malloc(sizeof(Session));
    if (!p) {
        SSL_free(ssl);
        fail(env, "javax/net/ssl/SSLException", "out of memory");
        return 0;
    }
    p->mutex = 0;
    p->references = 1;
    p->removed = 0;
    p->ssl = ssl;
    p->env = 0;
    p->trust_manager = 0;
    p->client_auth = 0;
    p->verified = 0;
    pthread_mutex_lock(&registry_mutex);
    p->token = next_token++;
    p->next = sessions;
    sessions = p;
    pthread_mutex_unlock(&registry_mutex);
    SSL_set_ex_data(ssl, tls_ex_index, p);
    return p->token;
}
void Java_org_ogplay_security_NativeTls_configure(JNIEnv *env, jobject cls, jlong token,
                                                  jobject protocols, jobject ciphers) {
    (void)cls;
    SESSION;
    if (!s) return;
    long options = disable_unused(0, protocols, env);
    if (options < 0) return;
    SSL_ctrl(s->ssl, SSL_CTRL_CLEAR_OPTIONS,
             SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1 | SSL_OP_NO_TLSv1_2, 0);
    SSL_ctrl(s->ssl, SSL_CTRL_OPTIONS, options, 0);
    char cipher_list[1024];
    if (!fill_cipher_list(env, ciphers, cipher_list, (int)sizeof(cipher_list))) return;
    if (SSL_set_cipher_list(s->ssl, cipher_list[0] ? cipher_list : kDefaultCiphers) != 1)
        fail(env, "javax/net/ssl/SSLException", "cipher list is not supported");
}
jobject Java_org_ogplay_security_NativeTls_sessionId(JNIEnv *env, jobject cls, jlong token) {
    (void)cls;
    SESSION;
    if (!s) return 0;
    SSL_SESSION *session = SSL_get1_session(s->ssl);
    if (!session) {
        fail(env, "javax/net/ssl/SSLException", "TLS session is missing");
        return 0;
    }
    unsigned int id_len = 0;
    const unsigned char *id = SSL_SESSION_get_id(session, &id_len);
    jobject bytes = 0;
    if (id && id_len > 0 && id_len <= 256) {
        bytes = new_bytes(env, (int)id_len);
        if (bytes) write_bytes(env, bytes, 0, (int)id_len, id);
    } else {
        bytes = new_bytes(env, 0);
    }
    SSL_SESSION_free(session);
    return bytes;
}
void Java_org_ogplay_security_NativeTls_setClientKey(JNIEnv *env, jobject cls, jlong token,
                                                     jobject pkcs8, jobject chain) {
    (void)cls;
    SESSION;
    if (!s) return;
    int n = length(env, pkcs8);
    if (n < 0) return;
    if (n == 0 || n > MAX_DER) {
        fail(env, "javax/net/ssl/SSLHandshakeException", "client key encoding is invalid");
        return;
    }
    unsigned char *bytes = malloc((size_t)n);
    if (!bytes) {
        fail(env, "javax/net/ssl/SSLHandshakeException", "out of memory");
        return;
    }
    read_bytes(env, pkcs8, 0, n, bytes);
    const unsigned char *p = bytes;
    EVP_PKEY *key = d2i_AutoPrivateKey(0, &p, n);
    free(bytes);
    if (!key) {
        fail(env, "javax/net/ssl/SSLHandshakeException", "client private key is not usable");
        return;
    }
    int blob_n = length(env, chain);
    if (blob_n < 8) {
        EVP_PKEY_free(key);
        fail(env, "javax/net/ssl/SSLHandshakeException", "client certificate chain is missing");
        return;
    }
    unsigned char *blob = malloc((size_t)blob_n);
    if (!blob) {
        EVP_PKEY_free(key);
        fail(env, "javax/net/ssl/SSLHandshakeException", "out of memory");
        return;
    }
    read_bytes(env, chain, 0, blob_n, blob);
    int chain_n = load_i32(blob);
    int offset = 4;
    if (chain_n < 1 || offset > blob_n - 4) {
        free(blob);
        EVP_PKEY_free(key);
        fail(env, "javax/net/ssl/SSLHandshakeException", "client certificate chain is missing");
        return;
    }
    int der_n = load_i32(blob + offset);
    offset += 4;
    if (der_n < 1 || der_n > blob_n - offset) {
        free(blob);
        EVP_PKEY_free(key);
        fail(env, "javax/net/ssl/SSLHandshakeException", "client certificate encoding is invalid");
        return;
    }
    const unsigned char *der_p = blob + offset;
    X509 *cert = d2i_X509(0, &der_p, der_n);
    offset += der_n;
    if (!cert || SSL_use_certificate(s->ssl, cert) != 1 || SSL_use_PrivateKey(s->ssl, key) != 1) {
        if (cert) X509_free(cert);
        EVP_PKEY_free(key);
        free(blob);
        fail(env, "javax/net/ssl/SSLHandshakeException", "unable to install client certificate");
        return;
    }
    X509_free(cert);
    EVP_PKEY_free(key);
    int i;
    for (i = 1; i < chain_n && i < MAX_CHAIN; ++i) {
        if (offset > blob_n - 4) break;
        int extra_n = load_i32(blob + offset);
        offset += 4;
        if (extra_n < 1 || extra_n > blob_n - offset) break;
        const unsigned char *extra_p = blob + offset;
        X509 *extra_cert = d2i_X509(0, &extra_p, extra_n);
        offset += extra_n;
        if (extra_cert) {
            if (SSL_CTX_ctrl(SSL_get_SSL_CTX(s->ssl), SSL_CTRL_EXTRA_CHAIN_CERT, 0,
                             extra_cert) != 1)
                X509_free(extra_cert);
        }
    }
    free(blob);
}
static int map_error(const SSL *ssl, int result) {
    if (result > 0) return STATUS_OK;
    int error = SSL_get_error(ssl, result);
    if (error == SSL_ERROR_WANT_READ) return STATUS_WANT_READ;
    if (error == SSL_ERROR_WANT_WRITE) return STATUS_WANT_WRITE;
    if (error == SSL_ERROR_ZERO_RETURN) return STATUS_CLOSED;
    if (error == SSL_ERROR_SYSCALL || result == 0) {
        BIO *out = SSL_get_wbio(ssl);
        long pending_bytes = out ? BIO_ctrl(out, BIO_CTRL_PENDING, 0, 0) : 0;
        if (pending_bytes > 0) return STATUS_WANT_WRITE;
        return STATUS_FAILED;
    }
    return STATUS_FAILED;
}
int Java_org_ogplay_security_NativeTls_handshake(JNIEnv *env, jobject cls, jlong token,
                                                 jobject trust_manager, unsigned char client_auth) {
    (void)cls;
    SESSION;
    if (!s) return STATUS_FAILED;
    if (!trust_manager) {
        fail(env, "javax/net/ssl/SSLHandshakeException", "TrustManager is required");
        return STATUS_FAILED;
    }
    s->env = env;
    s->trust_manager = trust_manager;
    s->client_auth = client_auth;
    ERR_clear_error();
    int result = SSL_do_handshake(s->ssl);
    int status = map_error(s->ssl, result);
    s->env = 0;
    s->trust_manager = 0;
    if (status == STATUS_OK && !s->verified) {
        fail(env, "javax/net/ssl/SSLHandshakeException", "TrustManager was not invoked");
        return STATUS_FAILED;
    }
    if (status == STATUS_FAILED && !pending(env)) {
        int ssl_err = SSL_get_error(s->ssl, result);
        unsigned long err = ERR_get_error();
        int rand_ok = RAND_status();
        char message[96];
        int i = 0;
        const char *prefix = "TLS handshake failed ssl=";
        while (prefix[i]) {
            message[i] = prefix[i];
            ++i;
        }
        if (ssl_err < 0) {
            message[i++] = '-';
            ssl_err = -ssl_err;
        }
        char digits[12];
        int n = 0;
        int v = ssl_err;
        do {
            digits[n++] = (char)('0' + (v % 10));
            v /= 10;
        } while (v && n < 12);
        while (n && i < 70) message[i++] = digits[--n];
        message[i++] = ' ';
        message[i++] = 'e';
        message[i++] = 'r';
        message[i++] = 'r';
        message[i++] = '=';
        n = 0;
        unsigned long ev = err;
        if (!ev) digits[n++] = '0';
        while (ev && n < 12) {
            digits[n++] = (char)("0123456789abcdef"[ev & 15]);
            ev >>= 4;
        }
        while (n && i < (int)sizeof(message) - 10) message[i++] = digits[--n];
        message[i++] = ' ';
        message[i++] = 'r';
        message[i++] = 'a';
        message[i++] = 'n';
        message[i++] = 'd';
        message[i++] = '=';
        message[i++] = (char)('0' + (rand_ok != 0));
        message[i] = 0;
        fail(env, "javax/net/ssl/SSLHandshakeException", message);
    }
    return status;
}
jobject Java_org_ogplay_security_NativeTls_peerCertificates(JNIEnv *env, jobject cls, jlong token) {
    (void)cls;
    SESSION;
    if (!s) return 0;
    X509 *leaf = SSL_get_peer_certificate(s->ssl);
    if (!leaf) {
        fail(env, "javax/net/ssl/SSLPeerUnverifiedException", "peer not authenticated");
        return 0;
    }
    _STACK *chain = SSL_get_peer_cert_chain(s->ssl);
    int extra = chain ? sk_num(chain) : 0;
    if (extra < 0) extra = 0;
    int count = extra + 1;
    int sizes[MAX_CHAIN];
    int total = 4;
    int i;
    sizes[0] = i2d_X509(leaf, 0);
    if (sizes[0] < 1) {
        X509_free(leaf);
        fail(env, "javax/net/ssl/SSLPeerUnverifiedException", "unable to encode peer certificate");
        return 0;
    }
    total += 4 + sizes[0];
    for (i = 0; i < extra && i < MAX_CHAIN - 1; ++i) {
        X509 *item = (X509 *)sk_value(chain, i);
        sizes[i + 1] = item ? i2d_X509(item, 0) : 0;
        if (sizes[i + 1] < 1) {
            X509_free(leaf);
            fail(env, "javax/net/ssl/SSLPeerUnverifiedException", "unable to encode peer certificate");
            return 0;
        }
        total += 4 + sizes[i + 1];
    }
    unsigned char *blob = malloc((size_t)total);
    if (!blob) {
        X509_free(leaf);
        fail(env, "javax/net/ssl/SSLPeerUnverifiedException", "out of memory");
        return 0;
    }
    store_i32(blob, count);
    int offset = 4;
    store_i32(blob + offset, sizes[0]);
    offset += 4;
    {
        unsigned char *p = blob + offset;
        i2d_X509(leaf, &p);
        offset += sizes[0];
    }
    for (i = 0; i < extra && i < MAX_CHAIN - 1; ++i) {
        store_i32(blob + offset, sizes[i + 1]);
        offset += 4;
        unsigned char *p = blob + offset;
        i2d_X509((X509 *)sk_value(chain, i), &p);
        offset += sizes[i + 1];
    }
    jobject packed = new_bytes(env, total);
    if (packed) write_bytes(env, packed, 0, total, blob);
    free(blob);
    X509_free(leaf);
    return packed;
}
jobject Java_org_ogplay_security_NativeTls_protocol(JNIEnv *env, jobject cls, jlong token) {
    (void)cls;
    SESSION;
    if (!s) return 0;
    return new_string(env, SSL_get_version(s->ssl));
}
jobject Java_org_ogplay_security_NativeTls_cipherSuite(JNIEnv *env, jobject cls, jlong token) {
    (void)cls;
    SESSION;
    if (!s) return 0;
    const SSL_CIPHER *cipher = SSL_get_current_cipher(s->ssl);
    return new_string(env, java_cipher(cipher ? SSL_CIPHER_get_name(cipher) : 0));
}
int Java_org_ogplay_security_NativeTls_read(JNIEnv *env, jobject cls, jlong token, jobject buffer,
                                            int offset, int count) {
    (void)cls;
    SESSION;
    if (!s) return STATUS_FAILED;
    int n = length(env, buffer);
    if (n < 0) return STATUS_FAILED;
    if (offset < 0 || count < 0 || offset > n || count > n - offset) {
        fail(env, "java/lang/ArrayIndexOutOfBoundsException", "invalid TLS buffer range");
        return STATUS_FAILED;
    }
    if (count > MAX_WIRE) count = MAX_WIRE;
    unsigned char scratch[4096];
    int want = count < (int)sizeof(scratch) ? count : (int)sizeof(scratch);
    ERR_clear_error();
    int result = SSL_read(s->ssl, scratch, want);
    if (result > 0) {
        write_bytes(env, buffer, offset, result, scratch);
        return result;
    }
    return map_error(s->ssl, result);
}
int Java_org_ogplay_security_NativeTls_write(JNIEnv *env, jobject cls, jlong token, jobject buffer,
                                             int offset, int count) {
    (void)cls;
    SESSION;
    if (!s) return STATUS_FAILED;
    int n = length(env, buffer);
    if (n < 0) return STATUS_FAILED;
    if (offset < 0 || count < 0 || offset > n || count > n - offset) {
        fail(env, "java/lang/ArrayIndexOutOfBoundsException", "invalid TLS buffer range");
        return STATUS_FAILED;
    }
    if (count > MAX_WIRE) count = MAX_WIRE;
    unsigned char scratch[4096];
    int want = count < (int)sizeof(scratch) ? count : (int)sizeof(scratch);
    read_bytes(env, buffer, offset, want, scratch);
    ERR_clear_error();
    int result = SSL_write(s->ssl, scratch, want);
    if (result > 0) return result;
    return map_error(s->ssl, result);
}
jobject Java_org_ogplay_security_NativeTls_pullWire(JNIEnv *env, jobject cls, jlong token) {
    (void)cls;
    SESSION;
    if (!s) return 0;
    BIO *out = SSL_get_wbio(s->ssl);
    long pending_bytes = BIO_ctrl(out, BIO_CTRL_PENDING, 0, 0);
    if (pending_bytes <= 0) return new_bytes(env, 0);
    if (pending_bytes > MAX_WIRE) pending_bytes = MAX_WIRE;
    unsigned char *scratch = malloc((size_t)pending_bytes);
    if (!scratch) {
        fail(env, "javax/net/ssl/SSLException", "out of memory");
        return 0;
    }
    int n = BIO_read(out, scratch, (int)pending_bytes);
    jobject array = new_bytes(env, n > 0 ? n : 0);
    if (n > 0) write_bytes(env, array, 0, n, scratch);
    free(scratch);
    return array;
}
void Java_org_ogplay_security_NativeTls_pushWire(JNIEnv *env, jobject cls, jlong token, jobject buffer,
                                                 int offset, int count) {
    (void)cls;
    SESSION;
    if (!s) return;
    int n = length(env, buffer);
    if (n < 0) return;
    if (offset < 0 || count < 0 || offset > n || count > n - offset) {
        fail(env, "java/lang/ArrayIndexOutOfBoundsException", "invalid TLS buffer range");
        return;
    }
    if (count == 0) return;
    unsigned char *scratch = malloc((size_t)count);
    if (!scratch) {
        fail(env, "javax/net/ssl/SSLException", "out of memory");
        return;
    }
    read_bytes(env, buffer, offset, count, scratch);
    BIO *in = SSL_get_rbio(s->ssl);
    if (BIO_write(in, scratch, count) != count)
        fail(env, "javax/net/ssl/SSLException", "unable to queue TLS ciphertext");
    free(scratch);
}
int Java_org_ogplay_security_NativeTls_shutdown(JNIEnv *env, jobject cls, jlong token) {
    (void)cls;
    SESSION;
    if (!s) return STATUS_FAILED;
    ERR_clear_error();
    return map_error(s->ssl, SSL_shutdown(s->ssl));
}
void Java_org_ogplay_security_NativeTls_freeSsl(JNIEnv *env, jobject cls, jlong token) {
    (void)cls;
    (void)env;
    pthread_mutex_lock(&registry_mutex);
    Session **link = &sessions;
    while (*link && (*link)->token != token) link = &(*link)->next;
    Session *p = *link;
    if (p) {
        *link = p->next;
        p->removed = 1;
        int destroy = (p->references == 0);
        pthread_mutex_unlock(&registry_mutex);
        if (destroy) {
            if (p->ssl) SSL_free(p->ssl);
            free(p);
        }
    } else {
        pthread_mutex_unlock(&registry_mutex);
    }
}
void Java_org_ogplay_security_NativeTls_freeContext(JNIEnv *env, jobject cls, jlong token) {
    (void)cls;
    (void)env;
    pthread_mutex_lock(&registry_mutex);
    Ctx **link = &contexts;
    while (*link && (*link)->token != token) link = &(*link)->next;
    Ctx *p = *link;
    if (p) {
        *link = p->next;
        p->removed = 1;
        int destroy = (p->references == 0);
        pthread_mutex_unlock(&registry_mutex);
        if (destroy) {
            SSL_CTX_free(p->ssl);
            free(p);
        }
    } else {
        pthread_mutex_unlock(&registry_mutex);
    }
}
static jobject cipher_array(JNIEnv *env) {
    SSL_CTX *ssl_ctx = SSL_CTX_new(SSLv23_client_method());
    if (!ssl_ctx) return new_object_array(env, 0, string_class(env));
    SSL_CTX_set_cipher_list(ssl_ctx, kDefaultCiphers);
    SSL *ssl = SSL_new(ssl_ctx);
    int count = 0;
    while (ssl && SSL_get_cipher_list(ssl, count)) ++count;
    jobject array = new_object_array(env, count, string_class(env));
    int i;
    for (i = 0; i < count; ++i)
        set_element(env, array, i, new_string(env, java_cipher(SSL_get_cipher_list(ssl, i))));
    if (ssl) SSL_free(ssl);
    SSL_CTX_free(ssl_ctx);
    return array;
}
jobject Java_org_ogplay_security_NativeTls_supportedCipherSuites(JNIEnv *env, jobject cls) {
    (void)cls;
    return cipher_array(env);
}
jobject Java_org_ogplay_security_NativeTls_defaultCipherSuites(JNIEnv *env, jobject cls) {
    (void)cls;
    return cipher_array(env);
}
