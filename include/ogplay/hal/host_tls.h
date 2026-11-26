#pragma once

// Host TLS client engine (OpenSSL 3, memory BIOs) and host trust evaluation.
// Ciphertext never touches a host socket here: the caller moves it between
// PullWire/PushWire and its own transport. Peer verification is deferred to
// the caller (the guest's TrustManager or VerifyServerChainWithHostRoots).

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ogplay::hal {

// Handshake/shutdown statuses, matching OGPlayJSSE's NativeTls.
inline constexpr int kHostTlsOk = 1;
inline constexpr int kHostTlsWantRead = 2;
inline constexpr int kHostTlsWantWrite = 3;
inline constexpr int kHostTlsClosed = 0;
inline constexpr int kHostTlsFailed = -1;
// Read/Write return a positive byte count or one of these.
inline constexpr int kHostTlsIoWantRead = -2;
inline constexpr int kHostTlsIoWantWrite = -3;

[[nodiscard]] bool HostTlsAvailable();

// Java (RFC) cipher suite names this engine can negotiate, TLS 1.3 first.
[[nodiscard]] std::vector<std::string> HostTlsSupportedCipherSuites();
[[nodiscard]] std::vector<std::string> HostTlsDefaultCipherSuites();

class HostTlsSession {
public:
    virtual ~HostTlsSession() = default;
    // protocols: Java names (TLSv1.3, TLSv1.2, TLSv1.1, TLSv1). When TLSv1.2 is
    // the highest enabled version TLS 1.3 is also offered. Throws on bad names.
    virtual void Configure(const std::vector<std::string>& protocols,
                           const std::vector<std::string>& cipher_suites) = 0;
    virtual void SetClientKey(std::span<const std::byte> pkcs8, const std::vector<std::vector<std::byte>>& chain) = 0;
    [[nodiscard]] virtual int Handshake() = 0;
    [[nodiscard]] virtual int Read(std::span<std::byte> destination) = 0;
    [[nodiscard]] virtual int Write(std::span<const std::byte> source) = 0;
    [[nodiscard]] virtual int Shutdown() = 0;
    [[nodiscard]] virtual std::vector<std::byte> PullWire(std::size_t maximum) = 0;
    virtual void PushWire(std::span<const std::byte> bytes) = 0;
    // Leaf first, DER.
    [[nodiscard]] virtual std::vector<std::vector<std::byte>> PeerChain() const = 0;
    [[nodiscard]] virtual std::string Protocol() const = 0;
    [[nodiscard]] virtual std::string CipherSuite() const = 0;
    // X509TrustManager authType: the key exchange (ECDHE_RSA, RSA, ...), GENERIC for TLS 1.3.
    [[nodiscard]] virtual std::string AuthType() const = 0;
    [[nodiscard]] virtual std::vector<std::byte> SessionId() const = 0;
    [[nodiscard]] virtual std::vector<std::byte> SessionState() const = 0;
    [[nodiscard]] virtual bool SetSession(std::span<const std::byte> encoded) = 0;
    [[nodiscard]] virtual bool SessionReused() const = 0;
};

// Null with *error filled when the engine is unavailable or setup fails.
[[nodiscard]] std::unique_ptr<HostTlsSession> CreateHostTlsSession(std::string_view server_name, std::string* error);

struct HostTrustResult final {
    bool trusted{};
    std::string reason;
};

// Validates a server chain (leaf first, DER) for TLS server authentication
// against the host operating system's trusted roots, at the current host
// time. No host name check: HTTPS keeps that on the guest HostnameVerifier.
[[nodiscard]] HostTrustResult VerifyServerChainWithHostRoots(const std::vector<std::vector<std::byte>>& chain);

}  // namespace ogplay::hal
