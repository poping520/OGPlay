// org.ogplay.security.HostTls: OGPlayJSSE's host engine. Sessions are host
// OpenSSL objects behind opaque long tokens; ciphertext moves through the
// guest socket the Java SSLSocket owns, so network policy stays in one place.

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "catalog.h"
#include "ogplay/hal/host_tls.h"
#include "ogplay/runtime/dexvm/network_runtime.h"
#include "shared.h"

namespace ogplay::runtime::dexvm::intrinsics {
namespace {
using namespace detail;

constexpr auto kHostTls = "Lorg/ogplay/security/HostTls;";
constexpr std::size_t kMaxWire = 64U * 1024U;
constexpr std::size_t kMaxChainCertificates = 16;
constexpr std::size_t kMaxCertificateBytes = 64U * 1024U;

struct Context final {
    std::vector<std::string> protocols;
    std::vector<std::string> cipher_suites;
};

// Process-wide token table; tokens are never reused.
class Registry final {
public:
    std::int64_t AddContext(Context context) {
        std::scoped_lock lock(mutex_);
        const auto token = ++next_;
        contexts_.emplace(token, std::move(context));
        return token;
    }

    std::optional<Context> FindContext(const std::int64_t token) {
        std::scoped_lock lock(mutex_);
        const auto found = contexts_.find(token);
        if (found == contexts_.end()) return std::nullopt;
        return found->second;
    }

    void RemoveContext(const std::int64_t token) {
        std::scoped_lock lock(mutex_);
        contexts_.erase(token);
    }

    std::int64_t AddSession(std::shared_ptr<hal::HostTlsSession> session) {
        std::scoped_lock lock(mutex_);
        const auto token = ++next_;
        sessions_.emplace(token, std::move(session));
        return token;
    }

    std::shared_ptr<hal::HostTlsSession> FindSession(const std::int64_t token) {
        std::scoped_lock lock(mutex_);
        const auto found = sessions_.find(token);
        return found == sessions_.end() ? nullptr : found->second;
    }

    void RemoveSession(const std::int64_t token) {
        std::scoped_lock lock(mutex_);
        sessions_.erase(token);
    }

private:
    std::mutex mutex_;
    std::int64_t next_{0x4f47000000000000LL};
    std::unordered_map<std::int64_t, Context> contexts_;
    std::unordered_map<std::int64_t, std::shared_ptr<hal::HostTlsSession>> sessions_;
};

Registry& Tokens() {
    static Registry registry;
    return registry;
}

[[noreturn]] void ThrowSsl(const std::string& message) {
    throw VmJavaThrow{"Ljavax/net/ssl/SSLException;", message};
}

std::shared_ptr<hal::HostTlsSession> Session(IntrinsicContext& call) {
    auto session = Tokens().FindSession(call.arguments[0].AsLong());
    if (!session) ThrowSsl("TLS session is closed");
    return session;
}

std::vector<std::string> Strings(Interpreter& vm, const VmObjectRef array) {
    std::vector<std::string> values;
    if (!array.IsValid()) return values;
    const auto length = vm.Model().ArrayLength(array);
    for (std::int32_t index = 0; index < length; ++index) {
        const auto item = vm.Model().GetObjectElement(array, static_cast<JniSize>(index));
        if (!item.IsValid()) throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "null entry"};
        values.push_back(vm.StringUtf8(item));
    }
    return values;
}

VmObjectRef StringArray(Interpreter& vm, const std::vector<std::string>& values) {
    return detail::NewStringArray(vm, values);
}

VmObjectRef Bytes(Interpreter& vm, const std::span<const std::byte> bytes) {
    const auto array = vm.Model().NewPrimitiveArray(vm.Linker().ResolveDescriptor("[B"), JniPrimitiveKind::byte,
                                                    static_cast<JniSize>(bytes.size()));
    if (!bytes.empty()) vm.Model().WriteByteRegion(array, 0, bytes);
    return array;
}

std::vector<std::byte> ArrayBytes(Interpreter& vm, const VmObjectRef array) {
    if (!array.IsValid()) return {};
    return vm.Model().ReadByteRegion(array, 0, vm.Model().ArrayLength(array));
}

void CheckRange(Interpreter& vm, const VmObjectRef array, const std::int32_t offset, const std::int32_t count) {
    if (!array.IsValid()) throw VmJavaThrow{"Ljava/lang/NullPointerException;", "buffer == null"};
    const auto length = vm.Model().ArrayLength(array);
    if (offset < 0 || count < 0 || offset > length || count > length - offset)
        throw VmJavaThrow{"Ljava/lang/ArrayIndexOutOfBoundsException;", "invalid TLS buffer range"};
}

// NativeTrust packing: little-endian count, then length-prefixed DER entries.
std::vector<std::byte> PackChain(const std::vector<std::vector<std::byte>>& chain) {
    std::vector<std::byte> packed;
    const auto put = [&packed](const std::uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8)
            packed.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
    };
    put(static_cast<std::uint32_t>(chain.size()));
    for (const auto& certificate : chain) {
        put(static_cast<std::uint32_t>(certificate.size()));
        packed.insert(packed.end(), certificate.begin(), certificate.end());
    }
    return packed;
}

std::optional<std::vector<std::vector<std::byte>>> UnpackChain(const std::span<const std::byte> packed) {
    const auto get = [&packed](const std::size_t offset) -> std::optional<std::uint32_t> {
        if (offset + 4 > packed.size()) return std::nullopt;
        std::uint32_t value{};
        for (std::size_t index = 0; index < 4; ++index)
            value |= std::to_integer<std::uint32_t>(packed[offset + index]) << (index * 8U);
        return value;
    };
    const auto count = get(0);
    if (!count || *count == 0 || *count > kMaxChainCertificates) return std::nullopt;
    std::vector<std::vector<std::byte>> chain;
    std::size_t offset = 4;
    for (std::uint32_t index = 0; index < *count; ++index) {
        const auto size = get(offset);
        if (!size || *size == 0 || *size > kMaxCertificateBytes || offset + 4 + *size > packed.size())
            return std::nullopt;
        offset += 4;
        chain.emplace_back(packed.begin() + static_cast<std::ptrdiff_t>(offset),
                           packed.begin() + static_cast<std::ptrdiff_t>(offset + *size));
        offset += *size;
    }
    return chain;
}

template <class Operation>
auto Unlocked(IntrinsicContext& call, Operation&& operation) {
    const auto depth = call.vm.ExecutionLock().ReleaseForBlocking();
    try {
        auto result = operation();
        call.vm.ExecutionLock().ReacquireAfterBlocking(depth);
        return result;
    } catch (...) {
        call.vm.ExecutionLock().ReacquireAfterBlocking(depth);
        throw;
    }
}

}  // namespace

IntrinsicClassDecl Declare_org_ogplay_security_HostTls_Natives() {
    auto b = IntrinsicClassBuilder::Class(kHostTls, "Ljava/lang/Object;", {}, kAccFinal);
    b.MergesBootClass();
    constexpr auto kStatic = kAccStatic | kAccNative;
    b.StaticMethod(
        "selected", "()Z",
        [](IntrinsicContext& call) { return VmValue::Int(call.vm.Network().HostTls() && hal::HostTlsAvailable()); },
        kStatic);
    b.StaticMethod(
        "createContext", "([Ljava/lang/String;[Ljava/lang/String;)J",
        [](IntrinsicContext& call) {
            if (!hal::HostTlsAvailable()) ThrowSsl("host TLS engine is unavailable");
            return VmValue::Long(Tokens().AddContext(
                {Strings(call.vm, call.arguments[0].ref), Strings(call.vm, call.arguments[1].ref)}));
        },
        kStatic);
    b.StaticMethod(
        "freeContext", "(J)V",
        [](IntrinsicContext& call) {
            Tokens().RemoveContext(call.arguments[0].AsLong());
            return VmValue::Void();
        },
        kStatic);
    b.StaticMethod(
        "createSsl", "(JLjava/lang/String;)J",
        [](IntrinsicContext& call) {
            const auto context = Tokens().FindContext(call.arguments[0].AsLong());
            if (!context) ThrowSsl("TLS context is closed");
            const auto host = call.arguments[1].ref.IsValid() ? call.vm.StringUtf8(call.arguments[1].ref) : "";
            std::string error;
            std::shared_ptr<hal::HostTlsSession> session = hal::CreateHostTlsSession(host, &error);
            if (!session) ThrowSsl(error);
            try {
                session->Configure(context->protocols, context->cipher_suites);
            } catch (const std::invalid_argument& failure) {
                throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", failure.what()};
            }
            return VmValue::Long(Tokens().AddSession(std::move(session)));
        },
        kStatic);
    b.StaticMethod(
        "configure", "(J[Ljava/lang/String;[Ljava/lang/String;)V",
        [](IntrinsicContext& call) {
            try {
                Session(call)->Configure(Strings(call.vm, call.arguments[1].ref),
                                         Strings(call.vm, call.arguments[2].ref));
            } catch (const std::invalid_argument& failure) {
                throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", failure.what()};
            }
            return VmValue::Void();
        },
        kStatic);
    b.StaticMethod(
        "setClientKey", "(J[B[B)V",
        [](IntrinsicContext& call) {
            const auto chain = UnpackChain(ArrayBytes(call.vm, call.arguments[2].ref));
            if (!chain)
                throw VmJavaThrow{"Ljavax/net/ssl/SSLHandshakeException;", "client certificate chain is invalid"};
            try {
                Session(call)->SetClientKey(ArrayBytes(call.vm, call.arguments[1].ref), *chain);
            } catch (const std::invalid_argument& failure) {
                throw VmJavaThrow{"Ljavax/net/ssl/SSLHandshakeException;", failure.what()};
            }
            return VmValue::Void();
        },
        kStatic);
    b.StaticMethod(
        "handshake", "(J)I", [](IntrinsicContext& call) { return VmValue::Int(Session(call)->Handshake()); }, kStatic);
    b.StaticMethod(
        "peerCertificates", "(J)[B",
        [](IntrinsicContext& call) {
            const auto chain = Session(call)->PeerChain();
            if (chain.empty()) return VmValue::Ref(VmObjectRef{});
            return VmValue::Ref(Bytes(call.vm, PackChain(chain)));
        },
        kStatic);
    b.StaticMethod(
        "protocol", "(J)Ljava/lang/String;",
        [](IntrinsicContext& call) { return VmValue::Ref(call.vm.NewStringUtf8(Session(call)->Protocol())); }, kStatic);
    b.StaticMethod(
        "cipherSuite", "(J)Ljava/lang/String;",
        [](IntrinsicContext& call) { return VmValue::Ref(call.vm.NewStringUtf8(Session(call)->CipherSuite())); },
        kStatic);
    b.StaticMethod(
        "authType", "(J)Ljava/lang/String;",
        [](IntrinsicContext& call) { return VmValue::Ref(call.vm.NewStringUtf8(Session(call)->AuthType())); }, kStatic);
    b.StaticMethod(
        "sessionId", "(J)[B",
        [](IntrinsicContext& call) { return VmValue::Ref(Bytes(call.vm, Session(call)->SessionId())); }, kStatic);
    b.StaticMethod(
        "sessionState", "(J)[B",
        [](IntrinsicContext& call) { return VmValue::Ref(Bytes(call.vm, Session(call)->SessionState())); }, kStatic);
    b.StaticMethod(
        "setSession", "(J[B)Z",
        [](IntrinsicContext& call) {
            return VmValue::Int(Session(call)->SetSession(ArrayBytes(call.vm, call.arguments[1].ref)) ? 1 : 0);
        },
        kStatic);
    b.StaticMethod(
        "sessionReused", "(J)Z", [](IntrinsicContext& call) { return VmValue::Int(Session(call)->SessionReused()); },
        kStatic);
    b.StaticMethod(
        "read", "(J[BII)I",
        [](IntrinsicContext& call) {
            const auto session = Session(call);
            const auto array = call.arguments[1].ref;
            const auto offset = call.arguments[2].AsInt();
            const auto count = std::min(call.arguments[3].AsInt(), static_cast<std::int32_t>(kMaxWire));
            CheckRange(call.vm, array, offset, call.arguments[3].AsInt());
            std::vector<std::byte> scratch(static_cast<std::size_t>(count));
            const auto result = session->Read(scratch);
            if (result > 0)
                call.vm.Model().WriteByteRegion(array, offset,
                                                std::span(scratch).first(static_cast<std::size_t>(result)));
            return VmValue::Int(result);
        },
        kStatic);
    b.StaticMethod(
        "write", "(J[BII)I",
        [](IntrinsicContext& call) {
            const auto session = Session(call);
            const auto array = call.arguments[1].ref;
            const auto offset = call.arguments[2].AsInt();
            CheckRange(call.vm, array, offset, call.arguments[3].AsInt());
            const auto count = std::min(call.arguments[3].AsInt(), static_cast<std::int32_t>(kMaxWire));
            const auto bytes = call.vm.Model().ReadByteRegion(array, offset, count);
            return VmValue::Int(session->Write(bytes));
        },
        kStatic);
    b.StaticMethod(
        "pullWire", "(J)[B",
        [](IntrinsicContext& call) { return VmValue::Ref(Bytes(call.vm, Session(call)->PullWire(kMaxWire))); },
        kStatic);
    b.StaticMethod(
        "pushWire", "(J[BII)V",
        [](IntrinsicContext& call) {
            const auto session = Session(call);
            const auto array = call.arguments[1].ref;
            CheckRange(call.vm, array, call.arguments[2].AsInt(), call.arguments[3].AsInt());
            try {
                session->PushWire(
                    call.vm.Model().ReadByteRegion(array, call.arguments[2].AsInt(), call.arguments[3].AsInt()));
            } catch (const std::runtime_error& failure) {
                ThrowSsl(failure.what());
            }
            return VmValue::Void();
        },
        kStatic);
    b.StaticMethod(
        "shutdown", "(J)I", [](IntrinsicContext& call) { return VmValue::Int(Session(call)->Shutdown()); }, kStatic);
    b.StaticMethod(
        "freeSsl", "(J)V",
        [](IntrinsicContext& call) {
            Tokens().RemoveSession(call.arguments[0].AsLong());
            return VmValue::Void();
        },
        kStatic);
    b.StaticMethod(
        "supportedCipherSuites", "()[Ljava/lang/String;",
        [](IntrinsicContext& call) { return VmValue::Ref(StringArray(call.vm, hal::HostTlsSupportedCipherSuites())); },
        kStatic);
    b.StaticMethod(
        "defaultCipherSuites", "()[Ljava/lang/String;",
        [](IntrinsicContext& call) { return VmValue::Ref(StringArray(call.vm, hal::HostTlsDefaultCipherSuites())); },
        kStatic);
    b.StaticMethod(
        "verifyServer", "([B)V",
        [](IntrinsicContext& call) {
            const auto chain = UnpackChain(ArrayBytes(call.vm, call.arguments[0].ref));
            if (!chain)
                throw VmJavaThrow{"Ljava/security/cert/CertificateException;", "peer certificate chain is invalid"};
            // Host evaluation may fetch missing intermediates; never under the VM lock.
            const auto result = Unlocked(call, [&] { return hal::VerifyServerChainWithHostRoots(*chain); });
            if (!result.trusted) throw VmJavaThrow{"Ljava/security/cert/CertificateException;", result.reason};
            return VmValue::Void();
        },
        kStatic);
    return std::move(b).Build();
}

}  // namespace ogplay::runtime::dexvm::intrinsics
