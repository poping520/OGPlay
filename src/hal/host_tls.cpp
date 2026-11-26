#include "ogplay/hal/host_tls.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <stdexcept>

#if OGPLAY_HAS_HOST_TLS
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#if defined(__APPLE__)
#include <Security/Security.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#endif
#endif

namespace ogplay::hal {

#if OGPLAY_HAS_HOST_TLS
namespace {

std::string LastOpenSslError() {
    const auto code = ERR_get_error();
    if (code == 0) return "TLS failure";
    char buffer[256]{};
    ERR_error_string_n(code, buffer, sizeof(buffer));
    ERR_clear_error();
    return buffer;
}

int ProtocolVersion(const std::string& name) {
    if (name == "TLSv1.3") return TLS1_3_VERSION;
    if (name == "TLSv1.2") return TLS1_2_VERSION;
    if (name == "TLSv1.1") return TLS1_1_VERSION;
    if (name == "TLSv1") return TLS1_VERSION;
    throw std::invalid_argument("unsupported protocol: " + name);
}

// One shared client context: no verification (the guest TrustManager decides),
// no compression, unexpected EOF reported as a clean close like API 19.
SSL_CTX* SharedContext() {
    static SSL_CTX* context = [] {
        auto* created = SSL_CTX_new(TLS_client_method());
        if (created == nullptr) return created;
        SSL_CTX_set_verify(created, SSL_VERIFY_NONE, nullptr);
        SSL_CTX_set_options(created, SSL_OP_NO_COMPRESSION | SSL_OP_IGNORE_UNEXPECTED_EOF);
        SSL_CTX_set_session_cache_mode(created, SSL_SESS_CACHE_CLIENT | SSL_SESS_CACHE_NO_INTERNAL_STORE);
        SSL_CTX_set_security_level(created, 1);
        SSL_CTX_set_min_proto_version(created, TLS1_VERSION);
        SSL_CTX_set_max_proto_version(created, TLS1_3_VERSION);
        return created;
    }();
    return context;
}

struct CipherNames final {
    std::vector<std::string> tls13;
    // Java (RFC) name -> OpenSSL name, TLS 1.2 and earlier.
    std::vector<std::pair<std::string, std::string>> legacy;
};

const CipherNames& Ciphers() {
    static const CipherNames names = [] {
        CipherNames result;
        result.tls13 = {"TLS_AES_128_GCM_SHA256", "TLS_AES_256_GCM_SHA384", "TLS_CHACHA20_POLY1305_SHA256"};
        auto* context = SharedContext();
        if (context == nullptr) return result;
        auto* probe = SSL_new(context);
        if (probe == nullptr) return result;
        if (SSL_set_cipher_list(probe, "ALL:!aNULL:!eNULL:@SECLEVEL=1") == 1) {
            const auto* list = SSL_get_ciphers(probe);
            for (int index = 0; index < sk_SSL_CIPHER_num(list); ++index) {
                const auto* cipher = sk_SSL_CIPHER_value(list, index);
                if (std::string_view(SSL_CIPHER_get_version(cipher)) == "TLSv1.3") continue;
                const char* standard = SSL_CIPHER_standard_name(cipher);
                if (standard == nullptr) continue;
                result.legacy.emplace_back(standard, SSL_CIPHER_get_name(cipher));
            }
        }
        SSL_free(probe);
        return result;
    }();
    return names;
}

class OpenSslSession final : public HostTlsSession {
public:
    explicit OpenSslSession(SSL* ssl) : ssl_(ssl) {}

    ~OpenSslSession() override { SSL_free(ssl_); }

    OpenSslSession(const OpenSslSession&) = delete;
    OpenSslSession& operator=(const OpenSslSession&) = delete;

    void Configure(const std::vector<std::string>& protocols, const std::vector<std::string>& cipher_suites) override {
        std::scoped_lock lock(mutex_);
        if (!protocols.empty()) {
            int lowest = TLS1_3_VERSION, highest = TLS1_VERSION;
            for (const auto& name : protocols) {
                const auto version = ProtocolVersion(name);
                lowest = std::min(lowest, version);
                highest = std::max(highest, version);
            }
            // API 19 callers know no TLS 1.3; a modern peer still gets it.
            if (highest == TLS1_2_VERSION) highest = TLS1_3_VERSION;
            SSL_set_min_proto_version(ssl_, lowest);
            SSL_set_max_proto_version(ssl_, highest);
        }
        if (cipher_suites.empty()) return;
        std::string tls13, legacy;
        const auto& known = Ciphers();
        for (const auto& name : cipher_suites) {
            if (std::find(known.tls13.begin(), known.tls13.end(), name) != known.tls13.end()) {
                if (!tls13.empty()) tls13 += ':';
                tls13 += name;
                continue;
            }
            const auto found = std::find_if(known.legacy.begin(), known.legacy.end(),
                                            [&name](const auto& pair) { return pair.first == name; });
            // Java aliases some suites with an SSL_ prefix.
            const auto alias = name.starts_with("SSL_") ? "TLS_" + name.substr(4) : std::string();
            const auto found_alias = alias.empty()
                                         ? known.legacy.end()
                                         : std::find_if(known.legacy.begin(), known.legacy.end(),
                                                        [&alias](const auto& pair) { return pair.first == alias; });
            const auto match = found != known.legacy.end() ? found : found_alias;
            if (match == known.legacy.end()) throw std::invalid_argument("unsupported cipher suite: " + name);
            if (!legacy.empty()) legacy += ':';
            legacy += match->second;
        }
        // A list without TLS 1.3 suites keeps the engine defaults for TLS 1.3.
        if (!tls13.empty() && SSL_set_ciphersuites(ssl_, tls13.c_str()) != 1)
            throw std::invalid_argument("TLS 1.3 cipher suites are not supported");
        if (!legacy.empty() && SSL_set_cipher_list(ssl_, (legacy + ":@SECLEVEL=1").c_str()) != 1)
            throw std::invalid_argument("cipher suites are not supported");
    }

    void SetClientKey(const std::span<const std::byte> pkcs8,
                      const std::vector<std::vector<std::byte>>& chain) override {
        std::scoped_lock lock(mutex_);
        if (chain.empty()) throw std::invalid_argument("client certificate chain is empty");
        const auto* key_bytes = reinterpret_cast<const unsigned char*>(pkcs8.data());
        auto* key = d2i_AutoPrivateKey(nullptr, &key_bytes, static_cast<long>(pkcs8.size()));
        if (key == nullptr) throw std::invalid_argument("client private key is not PKCS#8");
        for (std::size_t index = 0; index < chain.size(); ++index) {
            const auto* bytes = reinterpret_cast<const unsigned char*>(chain[index].data());
            auto* certificate = d2i_X509(nullptr, &bytes, static_cast<long>(chain[index].size()));
            if (certificate == nullptr) {
                EVP_PKEY_free(key);
                throw std::invalid_argument("client certificate is not DER X.509");
            }
            const auto ok =
                index == 0 ? SSL_use_certificate(ssl_, certificate) : SSL_add1_chain_cert(ssl_, certificate);
            X509_free(certificate);
            if (ok != 1) {
                EVP_PKEY_free(key);
                throw std::invalid_argument("client certificate was rejected");
            }
        }
        const auto ok = SSL_use_PrivateKey(ssl_, key);
        EVP_PKEY_free(key);
        if (ok != 1) throw std::invalid_argument("client private key does not match its certificate");
    }

    int Handshake() override {
        std::scoped_lock lock(mutex_);
        ERR_clear_error();
        const auto result = SSL_do_handshake(ssl_);
        if (result == 1) return kHostTlsOk;
        return Status(result, false);
    }

    int Read(const std::span<std::byte> destination) override {
        std::scoped_lock lock(mutex_);
        if (destination.empty()) return 0;
        ERR_clear_error();
        std::size_t count{};
        const auto result =
            SSL_read_ex(ssl_, destination.data(), std::min<std::size_t>(destination.size(), 1 << 20), &count);
        if (result == 1) return static_cast<int>(count);
        return Status(result, true);
    }

    int Write(const std::span<const std::byte> source) override {
        std::scoped_lock lock(mutex_);
        if (source.empty()) return 0;
        ERR_clear_error();
        std::size_t count{};
        const auto result = SSL_write_ex(ssl_, source.data(), std::min<std::size_t>(source.size(), 1 << 20), &count);
        if (result == 1) return static_cast<int>(count);
        return Status(result, true);
    }

    int Shutdown() override {
        std::scoped_lock lock(mutex_);
        ERR_clear_error();
        const auto result = SSL_shutdown(ssl_);
        if (result >= 0) return kHostTlsOk;
        return Status(result, false);
    }

    std::vector<std::byte> PullWire(const std::size_t maximum) override {
        std::scoped_lock lock(mutex_);
        auto* output = SSL_get_wbio(ssl_);
        const auto pending = std::min<std::size_t>(BIO_ctrl_pending(output), maximum);
        std::vector<std::byte> bytes(pending);
        if (pending == 0) return bytes;
        const auto read = BIO_read(output, bytes.data(), static_cast<int>(pending));
        bytes.resize(read > 0 ? static_cast<std::size_t>(read) : 0U);
        return bytes;
    }

    void PushWire(const std::span<const std::byte> bytes) override {
        std::scoped_lock lock(mutex_);
        if (bytes.empty()) return;
        auto* input = SSL_get_rbio(ssl_);
        if (BIO_write(input, bytes.data(), static_cast<int>(bytes.size())) != static_cast<int>(bytes.size()))
            throw std::runtime_error("unable to queue TLS ciphertext");
    }

    std::vector<std::vector<std::byte>> PeerChain() const override {
        std::scoped_lock lock(mutex_);
        std::vector<std::vector<std::byte>> result;
        // Client side: the chain includes the peer's own certificate first.
        const auto* chain = SSL_get_peer_cert_chain(ssl_);
        for (int index = 0; chain != nullptr && index < sk_X509_num(chain); ++index)
            result.push_back(Encode(sk_X509_value(chain, index)));
        if (result.empty()) {
            if (auto* leaf = SSL_get1_peer_certificate(ssl_)) {
                result.push_back(Encode(leaf));
                X509_free(leaf);
            }
        }
        return result;
    }

    std::string Protocol() const override {
        std::scoped_lock lock(mutex_);
        return SSL_get_version(ssl_);
    }

    std::string CipherSuite() const override {
        std::scoped_lock lock(mutex_);
        const auto* cipher = SSL_get_current_cipher(ssl_);
        const char* name = cipher != nullptr ? SSL_CIPHER_standard_name(cipher) : nullptr;
        return name != nullptr ? name : "SSL_NULL_WITH_NULL_NULL";
    }

    std::string AuthType() const override {
        std::scoped_lock lock(mutex_);
        const auto* cipher = SSL_get_current_cipher(ssl_);
        if (cipher == nullptr) return "UNKNOWN";
        if (SSL_version(ssl_) >= TLS1_3_VERSION) {
            // TLS 1.3 separates key exchange from the certificate; API 19
            // TrustManagers know only the TLS 1.2 names, so use the leaf key.
            auto* leaf = SSL_get1_peer_certificate(ssl_);
            std::string type = "ECDHE_RSA";
            if (leaf != nullptr) {
                if (const auto* key = X509_get0_pubkey(leaf); key != nullptr && EVP_PKEY_base_id(key) == EVP_PKEY_EC)
                    type = "ECDHE_ECDSA";
                X509_free(leaf);
            }
            return type;
        }
        const auto kx = SSL_CIPHER_get_kx_nid(cipher);
        const auto auth = SSL_CIPHER_get_auth_nid(cipher);
        const std::string signer = auth == NID_auth_ecdsa ? "ECDSA" : auth == NID_auth_dss ? "DSS" : "RSA";
        if (kx == NID_kx_ecdhe) return "ECDHE_" + signer;
        if (kx == NID_kx_dhe) return "DHE_" + signer;
        return "RSA";
    }

    std::vector<std::byte> SessionId() const override {
        std::scoped_lock lock(mutex_);
        const auto* session = SSL_get_session(ssl_);
        if (session == nullptr) return {};
        unsigned int length{};
        const auto* id = SSL_SESSION_get_id(session, &length);
        const auto* begin = reinterpret_cast<const std::byte*>(id);
        return std::vector<std::byte>(begin, begin + length);
    }

    std::vector<std::byte> SessionState() const override {
        std::scoped_lock lock(mutex_);
        auto* session = SSL_get1_session(ssl_);
        if (session == nullptr) return {};
        std::vector<std::byte> bytes;
        if (SSL_SESSION_is_resumable(session) == 1) {
            const auto length = i2d_SSL_SESSION(session, nullptr);
            if (length > 0) {
                bytes.resize(static_cast<std::size_t>(length));
                auto* cursor = reinterpret_cast<unsigned char*>(bytes.data());
                i2d_SSL_SESSION(session, &cursor);
            }
        }
        SSL_SESSION_free(session);
        return bytes;
    }

    bool SetSession(const std::span<const std::byte> encoded) override {
        std::scoped_lock lock(mutex_);
        if (encoded.empty()) return false;
        const auto* bytes = reinterpret_cast<const unsigned char*>(encoded.data());
        auto* session = d2i_SSL_SESSION(nullptr, &bytes, static_cast<long>(encoded.size()));
        if (session == nullptr) return false;
        const auto ok = SSL_set_session(ssl_, session) == 1;
        SSL_SESSION_free(session);
        return ok;
    }

    bool SessionReused() const override {
        std::scoped_lock lock(mutex_);
        return SSL_session_reused(ssl_) == 1;
    }

private:
    static std::vector<std::byte> Encode(X509* certificate) {
        const auto length = i2d_X509(certificate, nullptr);
        if (length <= 0) return {};
        std::vector<std::byte> bytes(static_cast<std::size_t>(length));
        auto* cursor = reinterpret_cast<unsigned char*>(bytes.data());
        i2d_X509(certificate, &cursor);
        return bytes;
    }

    int Status(const int result, const bool io) const {
        const auto error = SSL_get_error(ssl_, result);
        if (error == SSL_ERROR_WANT_READ) return io ? kHostTlsIoWantRead : kHostTlsWantRead;
        if (error == SSL_ERROR_WANT_WRITE) return io ? kHostTlsIoWantWrite : kHostTlsWantWrite;
        if (error == SSL_ERROR_ZERO_RETURN) return kHostTlsClosed;
        // Pending ciphertext (an alert) must still reach the peer.
        if (BIO_ctrl_pending(SSL_get_wbio(ssl_)) > 0 && !io) return kHostTlsWantWrite;
        ERR_clear_error();
        return kHostTlsFailed;
    }

    mutable std::mutex mutex_;
    SSL* ssl_;
};

#if !defined(__APPLE__) && !defined(_WIN32)
std::vector<X509*> ParseChain(const std::vector<std::vector<std::byte>>& chain) {
    std::vector<X509*> certificates;
    for (const auto& der : chain) {
        const auto* bytes = reinterpret_cast<const unsigned char*>(der.data());
        auto* certificate = d2i_X509(nullptr, &bytes, static_cast<long>(der.size()));
        if (certificate == nullptr) {
            for (auto* parsed : certificates)
                X509_free(parsed);
            return {};
        }
        certificates.push_back(certificate);
    }
    return certificates;
}

// Android keeps one PEM per root, named by the old subject hash, which the
// OpenSSL 3 hashed-directory lookup cannot find; load every file instead.
void LoadCertificateDirectory(X509_STORE* store, const std::filesystem::path& directory) {
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        if (!entry.is_regular_file(error)) continue;
        auto* file = BIO_new_file(entry.path().string().c_str(), "r");
        if (file == nullptr) continue;
        while (auto* certificate = PEM_read_bio_X509(file, nullptr, nullptr, nullptr)) {
            X509_STORE_add_cert(store, certificate);
            X509_free(certificate);
        }
        ERR_clear_error();
        BIO_free(file);
    }
}

X509_STORE* SystemStore() {
    static X509_STORE* store = [] {
        auto* created = X509_STORE_new();
        for (const auto* directory : {"/apex/com.android.conscrypt/cacerts", "/system/etc/security/cacerts",
                                      "/etc/ssl/certs", "/etc/pki/tls/certs"})
            LoadCertificateDirectory(created, directory);
        for (const auto* bundle :
             {"/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt", "/etc/ssl/cert.pem"})
            X509_STORE_load_file(created, bundle);
        ERR_clear_error();
        return created;
    }();
    return store;
}
#endif

}  // namespace

bool HostTlsAvailable() {
    return SharedContext() != nullptr;
}

std::vector<std::string> HostTlsSupportedCipherSuites() {
    const auto& known = Ciphers();
    auto result = known.tls13;
    for (const auto& [java, openssl] : known.legacy)
        result.push_back(java);
    return result;
}

std::vector<std::string> HostTlsDefaultCipherSuites() {
    const auto& known = Ciphers();
    auto result = known.tls13;
    // Forward-secret AEAD and CBC suites, then plain RSA, as OpenSSL's DEFAULT orders them.
    for (const auto& [java, openssl] : known.legacy)
        if (java.find("_WITH_NULL_") == std::string::npos && java.find("_anon_") == std::string::npos &&
            java.find("PSK") == std::string::npos && java.find("SRP") == std::string::npos &&
            java.find("CAMELLIA") == std::string::npos && java.find("ARIA") == std::string::npos)
            result.push_back(java);
    return result;
}

std::unique_ptr<HostTlsSession> CreateHostTlsSession(const std::string_view server_name, std::string* error) {
    auto* context = SharedContext();
    if (context == nullptr) {
        if (error != nullptr) *error = "host TLS context is unavailable";
        return nullptr;
    }
    auto* ssl = SSL_new(context);
    if (ssl == nullptr) {
        if (error != nullptr) *error = LastOpenSslError();
        return nullptr;
    }
    auto* input = BIO_new(BIO_s_mem());
    auto* output = BIO_new(BIO_s_mem());
    if (input == nullptr || output == nullptr) {
        BIO_free(input);
        BIO_free(output);
        SSL_free(ssl);
        if (error != nullptr) *error = "TLS buffers are unavailable";
        return nullptr;
    }
    SSL_set_bio(ssl, input, output);
    SSL_set_connect_state(ssl);
    // SNI only for DNS names (RFC 6066 forbids address literals).
    const std::string name(server_name);
    const bool literal = name.empty() || name.find(':') != std::string::npos ||
                         std::all_of(name.begin(), name.end(),
                                     [](const char value) { return (value >= '0' && value <= '9') || value == '.'; });
    if (!literal) SSL_set_tlsext_host_name(ssl, name.c_str());
    return std::make_unique<OpenSslSession>(ssl);
}

HostTrustResult VerifyServerChainWithHostRoots(const std::vector<std::vector<std::byte>>& chain) {
    if (chain.empty()) return {false, "peer certificate chain is empty"};
#if defined(__APPLE__)
    auto* certificates = CFArrayCreateMutable(nullptr, 0, &kCFTypeArrayCallBacks);
    for (const auto& der : chain) {
        auto* data =
            CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(der.data()), static_cast<CFIndex>(der.size()));
        auto* certificate = data != nullptr ? SecCertificateCreateWithData(nullptr, data) : nullptr;
        if (data != nullptr) CFRelease(data);
        if (certificate == nullptr) {
            CFRelease(certificates);
            return {false, "peer certificate is not DER X.509"};
        }
        CFArrayAppendValue(certificates, certificate);
        CFRelease(certificate);
    }
    auto* policy = SecPolicyCreateSSL(true, nullptr);
    SecTrustRef trust{};
    auto status = SecTrustCreateWithCertificates(certificates, policy, &trust);
    CFRelease(certificates);
    CFRelease(policy);
    if (status != errSecSuccess || trust == nullptr) return {false, "host trust evaluation is unavailable"};
    CFErrorRef failure{};
    const bool trusted = SecTrustEvaluateWithError(trust, &failure);
    std::string reason;
    if (!trusted && failure != nullptr) {
        if (auto* description = CFErrorCopyDescription(failure)) {
            char buffer[512]{};
            if (CFStringGetCString(description, buffer, sizeof(buffer), kCFStringEncodingUTF8)) reason = buffer;
            CFRelease(description);
        }
    }
    if (failure != nullptr) CFRelease(failure);
    CFRelease(trust);
    return {trusted, trusted ? std::string() : reason.empty() ? "certificate is not trusted by the host" : reason};
#elif defined(_WIN32)
    auto* store = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, nullptr);
    if (store == nullptr) return {false, "host trust evaluation is unavailable"};
    const auto* leaf = CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                                    reinterpret_cast<const BYTE*>(chain[0].data()),
                                                    static_cast<DWORD>(chain[0].size()));
    if (leaf == nullptr) {
        CertCloseStore(store, 0);
        return {false, "peer certificate is not DER X.509"};
    }
    for (std::size_t index = 1; index < chain.size(); ++index)
        CertAddEncodedCertificateToStore(store, X509_ASN_ENCODING, reinterpret_cast<const BYTE*>(chain[index].data()),
                                         static_cast<DWORD>(chain[index].size()), CERT_STORE_ADD_ALWAYS, nullptr);
    char server_auth[] = szOID_PKIX_KP_SERVER_AUTH;
    LPSTR usages[] = {server_auth};
    CERT_CHAIN_PARA parameters{};
    parameters.cbSize = sizeof(parameters);
    parameters.RequestedUsage.dwType = USAGE_MATCH_TYPE_AND;
    parameters.RequestedUsage.Usage.cUsageIdentifier = 1;
    parameters.RequestedUsage.Usage.rgpszUsageIdentifier = usages;
    PCCERT_CHAIN_CONTEXT built{};
    const bool have_chain =
        CertGetCertificateChain(nullptr, leaf, nullptr, store, &parameters, 0, nullptr, &built) != FALSE;
    HostTrustResult result{false, "certificate chain could not be built"};
    if (have_chain) {
        SSL_EXTRA_CERT_CHAIN_POLICY_PARA ssl{};
        ssl.cbSize = sizeof(ssl);
        ssl.dwAuthType = AUTHTYPE_SERVER;
        ssl.fdwChecks = SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
        CERT_CHAIN_POLICY_PARA policy{};
        policy.cbSize = sizeof(policy);
        policy.pvExtraPolicyPara = &ssl;
        CERT_CHAIN_POLICY_STATUS status{};
        status.cbSize = sizeof(status);
        if (CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, built, &policy, &status) && status.dwError == 0)
            result = {true, {}};
        else
            result = {false, "certificate is not trusted by the host (0x" + std::to_string(status.dwError) + ")"};
        CertFreeCertificateChain(built);
    }
    CertFreeCertificateContext(leaf);
    CertCloseStore(store, 0);
    return result;
#else
    static std::mutex mutex;
    std::scoped_lock lock(mutex);
    auto certificates = ParseChain(chain);
    if (certificates.empty()) return {false, "peer certificate is not DER X.509"};
    auto* untrusted = sk_X509_new_null();
    for (std::size_t index = 1; index < certificates.size(); ++index)
        sk_X509_push(untrusted, certificates[index]);
    auto* context = X509_STORE_CTX_new();
    HostTrustResult result{false, "host trust evaluation is unavailable"};
    if (context != nullptr && X509_STORE_CTX_init(context, SystemStore(), certificates[0], untrusted) == 1) {
        X509_STORE_CTX_set_purpose(context, X509_PURPOSE_SSL_SERVER);
        if (X509_verify_cert(context) == 1) {
            result = {true, {}};
        } else {
            result = {false, X509_verify_cert_error_string(X509_STORE_CTX_get_error(context))};
        }
    }
    X509_STORE_CTX_free(context);
    sk_X509_free(untrusted);
    for (auto* certificate : certificates)
        X509_free(certificate);
    ERR_clear_error();
    return result;
#endif
}

#else  // !OGPLAY_HAS_HOST_TLS

bool HostTlsAvailable() {
    return false;
}

std::vector<std::string> HostTlsSupportedCipherSuites() {
    return {};
}

std::vector<std::string> HostTlsDefaultCipherSuites() {
    return {};
}

std::unique_ptr<HostTlsSession> CreateHostTlsSession(std::string_view, std::string* error) {
    if (error != nullptr) *error = "this build has no host TLS engine";
    return nullptr;
}

HostTrustResult VerifyServerChainWithHostRoots(const std::vector<std::vector<std::byte>>&) {
    return {false, "this build has no host trust evaluation"};
}

#endif

}  // namespace ogplay::hal
