/* Guest libcrypto X509 path validation. JNI 1.6 table slots; no OpenSSL struct layout. */
typedef unsigned int size_t;
typedef long long jlong;
typedef void *jobject;
typedef const void **JNIEnv;
typedef struct x509_st X509;
typedef struct x509_store_st X509_STORE;
typedef struct x509_store_ctx_st X509_STORE_CTX;
typedef struct X509_VERIFY_PARAM_st X509_VERIFY_PARAM;
typedef struct X509_name_st X509_NAME;
typedef struct stack_st _STACK;
#define JNI(slot, type) ((type)((*env)[slot]))
extern void *malloc(size_t);
extern void free(void *);
extern void *memcpy(void *, const void *, size_t);
extern X509 *d2i_X509(X509 **, const unsigned char **, long);
extern void X509_free(X509 *);
extern X509_NAME *X509_get_subject_name(X509 *);
extern X509_NAME *X509_get_issuer_name(X509 *);
extern char *X509_NAME_oneline(X509_NAME *, char *, int);
extern int X509_cmp(X509 *, X509 *);
extern int X509_check_issued(X509 *, X509 *);
extern int X509_check_purpose(X509 *, int, int);
extern unsigned long X509_NAME_hash_old(X509_NAME *);
extern X509_STORE *X509_STORE_new(void);
extern void X509_STORE_free(X509_STORE *);
extern int X509_STORE_add_cert(X509_STORE *, X509 *);
extern int X509_STORE_set1_param(X509_STORE *, X509_VERIFY_PARAM *);
extern X509_STORE_CTX *X509_STORE_CTX_new(void);
extern void X509_STORE_CTX_free(X509_STORE_CTX *);
extern int X509_STORE_CTX_init(X509_STORE_CTX *, X509_STORE *, X509 *, _STACK *);
extern void X509_STORE_CTX_trusted_stack(X509_STORE_CTX *, _STACK *);
extern void X509_STORE_CTX_set_time(X509_STORE_CTX *, unsigned long, long);
extern int X509_STORE_CTX_set_purpose(X509_STORE_CTX *, int);
extern int X509_STORE_CTX_set_depth(X509_STORE_CTX *, int);
extern int X509_verify_cert(X509_STORE_CTX *);
extern int X509_STORE_CTX_get_error(X509_STORE_CTX *);
extern int X509_STORE_CTX_get_error_depth(X509_STORE_CTX *);
extern const char *X509_verify_cert_error_string(long);
extern X509_VERIFY_PARAM *X509_VERIFY_PARAM_new(void);
extern void X509_VERIFY_PARAM_free(X509_VERIFY_PARAM *);
extern void X509_VERIFY_PARAM_set_time(X509_VERIFY_PARAM *, long);
extern void X509_VERIFY_PARAM_set_depth(X509_VERIFY_PARAM *, int);
extern int X509_VERIFY_PARAM_set_purpose(X509_VERIFY_PARAM *, int);
extern int X509_VERIFY_PARAM_set_flags(X509_VERIFY_PARAM *, unsigned long);
extern _STACK *sk_new_null(void);
extern int sk_push(_STACK *, void *);
extern void sk_free(_STACK *);
extern int i2d_X509(X509 *, unsigned char **);
extern void ERR_clear_error(void);
extern int strcmp(const char *, const char *);
#define MAX_CHAIN 16
#define MAX_DER 65536
#define MAX_CHAIN_BYTES (256 * 1024)
#define MAX_DEPTH 8
#define X509_V_ERR_CERT_NOT_YET_VALID 9
#define X509_V_ERR_CERT_HAS_EXPIRED 10
#define X509_PURPOSE_SSL_CLIENT 1
#define X509_PURPOSE_SSL_SERVER 2
#define X509_V_FLAG_X509_STRICT 0x20UL
#define X509_V_FLAG_CHECK_SS_SIGNATURE 0x4000UL
static void fail(JNIEnv *env, const char *type, const char *message) {
    jobject cls = JNI(6, jobject(*)(JNIEnv *, const char *))(env, type);
    if (cls) JNI(14, int (*)(JNIEnv *, jobject, const char *))(env, cls, message);
}
static int pending(JNIEnv *env) { return JNI(228, unsigned char (*)(JNIEnv *))(env); }
static long verification_time(JNIEnv *env) {
    jobject sys = JNI(6, jobject(*)(JNIEnv *, const char *))(env, "java/lang/System");
    jobject method = JNI(113, jobject(*)(JNIEnv *, jobject, const char *, const char *))(
        env, sys, "currentTimeMillis", "()J");
    if (!sys || !method) {
        fail(env, "java/security/cert/CertificateException", "unified Clock is unavailable");
        return 0;
    }
    jlong millis = JNI(132, jlong (*)(JNIEnv *, jobject, jobject))(env, sys, method);
    if (pending(env)) return 0;
    if (millis < 0) millis = 0;
    return (long)(millis / 1000);
}
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
static int load_i32(const unsigned char *p) {
    return (int)((unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) |
                 ((unsigned)p[3] << 24));
}
static X509 *parse_der(JNIEnv *env, const unsigned char *bytes, int n, const char *label) {
    if (n <= 0 || n > MAX_DER) {
        fail(env, "java/security/cert/CertificateException", "certificate encoding exceeds limit");
        return 0;
    }
    const unsigned char *p = bytes;
    X509 *cert = d2i_X509(0, &p, n);
    int consumed = cert ? (int)(p - bytes) : 0;
    if (!cert || consumed != n) {
        if (cert) X509_free(cert);
        fail(env, "java/security/cert/CertificateException", label);
        return 0;
    }
    return cert;
}
static X509 *parse_cert(JNIEnv *env, jobject der, const char *label) {
    int n = length(env, der);
    if (n < 0) return 0;
    unsigned char *bytes = malloc((size_t)n);
    if (!bytes) {
        fail(env, "java/security/cert/CertificateException", "out of memory");
        return 0;
    }
    read_bytes(env, der, 0, n, bytes);
    X509 *cert = parse_der(env, bytes, n, label);
    free(bytes);
    return cert;
}
static void free_certs(X509 **certs, int count) {
    int i;
    if (!certs) return;
    for (i = 0; i < count; ++i)
        if (certs[i]) X509_free(certs[i]);
}
static void append(char *message, int *i, int max, const char *text) {
    int t = 0;
    while (text && text[t] && *i < max) message[(*i)++] = text[t++];
}
static void append_int(char *message, int *i, int max, int value) {
    char digits[12];
    int n = 0, v = value < 0 ? -value : value;
    if (value < 0 && *i < max) message[(*i)++] = '-';
    do {
        digits[n++] = (char)('0' + (v % 10));
        v /= 10;
    } while (v && n < 11);
    while (n && *i < max) message[(*i)++] = digits[--n];
}
static void throw_verify(JNIEnv *env, int error, int depth, int total, int chain_count,
                         int issued, const char *leaf_issuer, const char *anchor_subject) {
    const char *reason = X509_verify_cert_error_string(error);
    char message[256];
    int i = 0;
    append(message, &i, 200, "path validation failed: depth=");
    append_int(message, &i, 200, depth);
    message[i++] = ' ';
    append(message, &i, 220, reason);
    append(message, &i, 250, " certs=");
    append_int(message, &i, 250, total);
    append(message, &i, 250, " chain=");
    append_int(message, &i, 250, chain_count);
    append(message, &i, 250, " issued=");
    append_int(message, &i, 250, issued);
    if (leaf_issuer && leaf_issuer[0]) {
        append(message, &i, 254, " iss=");
        append(message, &i, 254, leaf_issuer);
    }
    if (anchor_subject && anchor_subject[0]) {
        append(message, &i, 254, " sub=");
        append(message, &i, 254, anchor_subject);
    }
    message[i] = 0;
    if (error == X509_V_ERR_CERT_HAS_EXPIRED)
        fail(env, "java/security/cert/CertificateExpiredException", message);
    else if (error == X509_V_ERR_CERT_NOT_YET_VALID)
        fail(env, "java/security/cert/CertificateNotYetValidException", message);
    else
        fail(env, "java/security/cert/CertificateException", message);
}
int Java_org_ogplay_security_NativeTrust_subjectHashOld(JNIEnv *env, jobject cls, jobject der) {
    (void)cls;
    ERR_clear_error();
    X509 *cert = parse_cert(env, der, "certificate DER is invalid");
    if (!cert) return 0;
    unsigned long hash = X509_NAME_hash_old(X509_get_subject_name(cert));
    X509_free(cert);
    return (int)hash;
}
static X509 **parse_packed_bytes(JNIEnv *env, const unsigned char *bytes, int blob_n, int *count_out,
                                 const char *label) {
    if (blob_n < 4) {
        fail(env, "java/security/cert/CertificateException", "packed certificate lengths do not match");
        return 0;
    }
    int count = load_i32(bytes);
    int offset = 4;
    *count_out = count;
    if (count < 0 || count > MAX_CHAIN) {
        fail(env, "java/security/cert/CertificateException", "certificate chain exceeds limit");
        return 0;
    }
    if (count == 0) return 0;
    X509 **certs = malloc((size_t)count * sizeof(X509 *));
    if (!certs) {
        fail(env, "java/security/cert/CertificateException", "out of memory");
        return 0;
    }
    int i;
    for (i = 0; i < count; ++i) certs[i] = 0;
    for (i = 0; i < count; ++i) {
        if (offset > blob_n - 4) {
            fail(env, "java/security/cert/CertificateException", "packed certificate lengths do not match");
            free_certs(certs, i);
            free(certs);
            return 0;
        }
        int n = load_i32(bytes + offset);
        offset += 4;
        if (n < 1 || n > MAX_DER || n > blob_n - offset) {
            fail(env, "java/security/cert/CertificateException", "certificate encoding exceeds limit");
            free_certs(certs, i);
            free(certs);
            return 0;
        }
        certs[i] = parse_der(env, bytes + offset, n, label);
        if (!certs[i]) {
            free_certs(certs, i);
            free(certs);
            return 0;
        }
        offset += n;
    }
    if (offset != blob_n) {
        fail(env, "java/security/cert/CertificateException", "packed certificate lengths do not match");
        free_certs(certs, count);
        free(certs);
        return 0;
    }
    return certs;
}
static X509 **parse_packed_array(JNIEnv *env, jobject packed, int *count_out, const char *label) {
    int blob_n = length(env, packed);
    if (blob_n < 0) return 0;
    unsigned char *bytes = malloc((size_t)blob_n);
    if (!bytes) {
        fail(env, "java/security/cert/CertificateException", "out of memory");
        return 0;
    }
    if (blob_n) read_bytes(env, packed, 0, blob_n, bytes);
    X509 **certs = parse_packed_bytes(env, bytes, blob_n, count_out, label);
    free(bytes);
    return certs;
}
unsigned char Java_org_ogplay_security_NativeTrust_verifyPath(
        JNIEnv *env, jobject cls, jobject leaf, jobject extras, jobject anchors, jobject purpose) {
    (void)cls;
    ERR_clear_error();
    const char *name = purpose
        ? JNI(169, const char *(*)(JNIEnv *, jobject, void *))(env, purpose, 0)
        : 0;
    int client_auth = name && !strcmp(name, "CLIENT");
    if (purpose && name)
        JNI(170, void (*)(JNIEnv *, jobject, const char *))(env, purpose, name);
    X509 *leaf_cert = parse_cert(env, leaf, "certificate DER is invalid");
    if (!leaf_cert) return 0;
    int extra_count = 0;
    int anchor_count = 0;
    X509 **extra_certs = parse_packed_array(env, extras, &extra_count, "certificate DER is invalid");
    if (pending(env)) {
        X509_free(leaf_cert);
        return 0;
    }
    X509 **anchor_certs = parse_packed_array(env, anchors, &anchor_count, "certificate DER is invalid");
    if (pending(env)) {
        X509_free(leaf_cert);
        if (extra_certs) {
            free_certs(extra_certs, extra_count);
            free(extra_certs);
        }
        return 0;
    }
    int chain_count = extra_count + 1;
    int total = chain_count + anchor_count;
    int i;
    X509_check_purpose(leaf_cert, -1, 0);
    for (i = 0; i < extra_count; ++i) X509_check_purpose(extra_certs[i], -1, 0);
    for (i = 0; i < anchor_count; ++i) X509_check_purpose(anchor_certs[i], -1, 0);
    X509_STORE *store = X509_STORE_new();
    X509_STORE_CTX *ctx = X509_STORE_CTX_new();
    _STACK *untrusted = sk_new_null();
    _STACK *trusted = sk_new_null();
    unsigned char ok = 0;
    if (!store || !ctx || !trusted || !untrusted) {
        fail(env, "java/security/cert/CertificateException", "path validator allocation failed");
        goto done;
    }
    for (i = 0; i < extra_count; ++i) {
        if (sk_push(untrusted, extra_certs[i]) <= 0) {
            fail(env, "java/security/cert/CertificateException", "unable to add untrusted certificate");
            goto done;
        }
    }
    for (i = 0; i < anchor_count; ++i) {
        if (X509_STORE_add_cert(store, anchor_certs[i]) != 1 ||
            sk_push(trusted, anchor_certs[i]) <= 0) {
            fail(env, "java/security/cert/CertificateException", "unable to add trust anchor");
            goto done;
        }
    }
    if (X509_STORE_CTX_init(ctx, store, leaf_cert, untrusted) != 1) {
        fail(env, "java/security/cert/CertificateException", "path validator initialization failed");
        goto done;
    }
    X509_STORE_CTX_trusted_stack(ctx, trusted);
    long now = verification_time(env);
    if (pending(env)) goto done;
    X509_STORE_CTX_set_time(ctx, 0, now);
    X509_STORE_CTX_set_depth(ctx, MAX_DEPTH);
    if (X509_STORE_CTX_set_purpose(ctx, client_auth ? X509_PURPOSE_SSL_CLIENT
                                                    : X509_PURPOSE_SSL_SERVER) != 1) {
        fail(env, "java/security/cert/CertificateException", "path validator purpose is invalid");
        goto done;
    }
    for (i = 0; i < anchor_count; ++i) {
        if (X509_cmp(anchor_certs[i], leaf_cert) == 0) {
            if (X509_check_purpose(leaf_cert,
                    client_auth ? X509_PURPOSE_SSL_CLIENT : X509_PURPOSE_SSL_SERVER, 0) != 1) {
                fail(env, "java/security/cert/CertificateException",
                     "trusted leaf certificate purpose is invalid");
                goto done;
            }
            ok = 1;
            goto done;
        }
    }
    if (X509_verify_cert(ctx) != 1) {
        int issued = -1;
        char iss[80];
        char sub[80];
        iss[0] = 0;
        sub[0] = 0;
        X509_NAME_oneline(X509_get_issuer_name(leaf_cert), iss, (int)sizeof(iss));
        if (anchor_count > 0) {
            issued = X509_check_issued(anchor_certs[0], leaf_cert);
            char sub_name[48];
            sub_name[0] = 0;
            X509_NAME_oneline(X509_get_subject_name(anchor_certs[0]), sub_name, (int)sizeof(sub_name));
            int n0 = i2d_X509(leaf_cert, 0);
            int n1 = i2d_X509(anchor_certs[0], 0);
            int si = 0;
            append(sub, &si, (int)sizeof(sub) - 1, sub_name);
            append(sub, &si, (int)sizeof(sub) - 1, " n0=");
            append_int(sub, &si, (int)sizeof(sub) - 1, n0);
            append(sub, &si, (int)sizeof(sub) - 1, " n1=");
            append_int(sub, &si, (int)sizeof(sub) - 1, n1);
            append(sub, &si, (int)sizeof(sub) - 1, " in0=");
            append_int(sub, &si, (int)sizeof(sub) - 1, length(env, leaf));
            append(sub, &si, (int)sizeof(sub) - 1, " in1=");
            append_int(sub, &si, (int)sizeof(sub) - 1, length(env, anchors));
            sub[si] = 0;
        }
        throw_verify(env, X509_STORE_CTX_get_error(ctx), X509_STORE_CTX_get_error_depth(ctx),
                     total, chain_count, issued, iss, sub);
        goto done;
    }
    ok = 1;
done:
    if (untrusted) sk_free(untrusted);
    if (trusted) sk_free(trusted);
    if (ctx) X509_STORE_CTX_free(ctx);
    if (store) X509_STORE_free(store);
    X509_free(leaf_cert);
    if (extra_certs) {
        free_certs(extra_certs, extra_count);
        free(extra_certs);
    }
    if (anchor_certs) {
        free_certs(anchor_certs, anchor_count);
        free(anchor_certs);
    }
    return ok;
}
