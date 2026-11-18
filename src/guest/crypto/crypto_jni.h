#ifndef OGPLAY_GUEST_CRYPTO_JNI_H
#define OGPLAY_GUEST_CRYPTO_JNI_H

typedef unsigned int size_t;
typedef long long jlong;
typedef void* jobject;
typedef const void** JNIEnv;
typedef struct evp_pkey_st EVP_PKEY;
typedef struct env_md_ctx_st EVP_MD_CTX;

#define JNI(slot, type) ((type)((*env)[slot]))
#define N(name) Java_com_android_org_conscrypt_NativeCrypto_##name

enum { CRYPTO_MAX_ENCODING = 1024 * 1024, CRYPTO_PKEY_RSA = 6, CRYPTO_PKEY_HMAC = 855 };

#pragma GCC visibility push(hidden)

void fail(JNIEnv* env, const char* type, const char* message);
void crypto_wipe(void* buffer, size_t size);
int length(JNIEnv* env, jobject array);
int range(JNIEnv* env, jobject array, int offset, int count);
void read_bytes(JNIEnv* env, jobject array, int offset, int count, unsigned char* buffer);
void write_bytes(JNIEnv* env, jobject array, int offset, int count, const unsigned char* buffer);
int crypto_seed_rng(JNIEnv* env, const char* exception);
void crypto_mix_seed(JNIEnv* env, jobject seed);

typedef struct CryptoKey CryptoKey;
jlong crypto_publish_pkey(JNIEnv* env, EVP_PKEY* evp);
CryptoKey* crypto_acquire_key(JNIEnv* env, jlong token);
void crypto_release_key(CryptoKey** key);
int crypto_key_type(const CryptoKey* key);
EVP_PKEY* crypto_key_evp(const CryptoKey* key);
void crypto_lock_key(CryptoKey* key);
void crypto_unlock_key(CryptoKey** key);

typedef struct Digest CryptoDigest;
jlong crypto_publish_signature(JNIEnv* env, EVP_MD_CTX* evp);
CryptoDigest* crypto_signature_context(JNIEnv* env, jlong token);
void crypto_release_digest(CryptoDigest** context);
EVP_MD_CTX* crypto_digest_evp(const CryptoDigest* context);
void crypto_update_digest(JNIEnv* env, EVP_MD_CTX* evp, jobject input, int offset, int count);

int ogplay_rsa_layout_check(void);

#pragma GCC visibility pop

#endif
