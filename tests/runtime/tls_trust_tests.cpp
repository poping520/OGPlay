#include "../dexvm/boot_dex.h"
#include "../dexvm/tls_fixtures.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <doctest/doctest.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"
#include "ogplay/gles/angle_backend.h"
#include "ogplay/loader/apk.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/network_runtime.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/vfs/vfs.h"
#include "ogplay/session/android_app_process.h"

namespace {

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;
using ogplay::session::AndroidAppProcess;
using ogplay::session::AndroidAppProcessRequest;
using ogplay::test::InstallCaPack;
using ogplay::test::ReadTlsFixture;

#ifdef _WIN32
using HostSocket = SOCKET;
constexpr HostSocket kInvalidSocket = INVALID_SOCKET;
#else
using HostSocket = int;
constexpr HostSocket kInvalidSocket = -1;
#endif

[[nodiscard]] std::vector<std::uint8_t> ReadDexFixture(const std::string& name) {
    const std::string path = std::string(OGPLAY_DEXVM_FIXTURE_DIR) + "/" + name;
    std::ifstream stream(path, std::ios::binary);
    REQUIRE_MESSAGE(stream.good(), path);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

[[nodiscard]] std::vector<std::byte> ReadPayloadBytes(const std::string& relative) {
    std::ifstream stream(std::string(OGPLAY_SOURCE_DIR) + "/data/android/19/" + relative,
                         std::ios::binary);
    REQUIRE_MESSAGE(stream.good(), relative);
    const std::vector<char> raw{std::istreambuf_iterator<char>(stream), {}};
    std::vector<std::byte> result(raw.size());
    std::memcpy(result.data(), raw.data(), raw.size());
    return result;
}

[[nodiscard]] ogplay::loader::AndroidManifestFacts AppManifest() {
    ogplay::loader::AndroidManifestFacts manifest;
    manifest.package = "fixture";
    manifest.version_code = 1;
    manifest.target_sdk = 19;
    manifest.application_class = "android.app.Application";
    manifest.activity_components.push_back({
        ogplay::loader::AndroidManifestComponentKind::activity,
        "fixture.MainActivity", std::nullopt, true,
        {{{"android.intent.action.MAIN"}, {"android.intent.category.LAUNCHER"}}}});
    return manifest;
}

class LoopbackTransport final : public NetworkTransport {
public:
    explicit LoopbackTransport(const std::uint16_t port) : port_(port) {
#ifdef _WIN32
        WSADATA data{};
        REQUIRE(WSAStartup(MAKEWORD(2, 2), &data) == 0);
        wsa_ = true;
#endif
    }
    ~LoopbackTransport() override {
        for (auto& [_, socket] : sockets_) CloseSocket(socket);
#ifdef _WIN32
        if (wsa_) WSACleanup();
#endif
    }
    std::vector<std::string> Resolve(std::string_view) override { return {"127.0.0.1"}; }
    std::uint64_t Connect(std::string_view, std::uint16_t, bool tls) override {
        CHECK_FALSE(tls);
        HostSocket socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        REQUIRE(socket != kInvalidSocket);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port_);
        REQUIRE(inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) == 1);
        REQUIRE(::connect(socket, reinterpret_cast<sockaddr*>(&address),
                          sizeof(address)) == 0);
        const auto channel = next_++;
        sockets_[channel] = socket;
        return channel;
    }
    void Send(std::uint64_t channel, std::span<const std::byte> bytes) override {
        auto socket = sockets_.at(channel);
        const auto sent = ::send(socket, reinterpret_cast<const char*>(bytes.data()),
                                 static_cast<int>(bytes.size()), 0);
        REQUIRE(sent == static_cast<int>(bytes.size()));
    }
    std::vector<std::byte> Receive(std::uint64_t channel, std::size_t maximum) override {
        return Receive(channel, maximum, 0);
    }
    std::vector<std::byte> Receive(std::uint64_t channel, std::size_t maximum,
                                   std::int32_t timeout_ms) override {
        auto socket = sockets_.at(channel);
        if (timeout_ms > 0) {
            fd_set read_set;
            FD_ZERO(&read_set);
            FD_SET(socket, &read_set);
            timeval timeout{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
#ifdef _WIN32
            const int ready = ::select(0, &read_set, nullptr, nullptr, &timeout);
#else
            const int ready = ::select(socket + 1, &read_set, nullptr, nullptr, &timeout);
#endif
            if (ready == 0) throw NetworkRuntimeError("socket receive timed out");
            REQUIRE(ready > 0);
        }
        std::vector<std::byte> buffer(maximum);
        const auto received = ::recv(socket, reinterpret_cast<char*>(buffer.data()),
                                     static_cast<int>(buffer.size()), 0);
        if (received <= 0) return {};
        buffer.resize(static_cast<std::size_t>(received));
        return buffer;
    }
    void Close(std::uint64_t channel) noexcept override {
        const auto found = sockets_.find(channel);
        if (found == sockets_.end()) return;
        CloseSocket(found->second);
        sockets_.erase(found);
    }
    void SendDatagram(const NetworkDatagram&) override {
        throw NetworkRuntimeError("datagram is not used by TLS tests");
    }
    NetworkDatagram ReceiveDatagram(std::size_t) override {
        throw NetworkRuntimeError("datagram is not used by TLS tests");
    }

private:
    static void CloseSocket(HostSocket socket) {
#ifdef _WIN32
        closesocket(socket);
#else
        close(socket);
#endif
    }
    std::uint16_t port_{};
    std::uint64_t next_{1};
    std::unordered_map<std::uint64_t, HostSocket> sockets_{};
#ifdef _WIN32
    bool wsa_{};
#endif
};

struct OracleServer final {
    OracleServer(const std::string& cert, const std::string& key,
                 const std::string& client_ca = {}) {
        const auto root = std::filesystem::path(OGPLAY_SOURCE_DIR);
        const auto script = root / "tests/fixtures/tls/loopback_https.py";
        std::string command = "python \"" + script.string() + "\" --cert \"" +
            (root / "tests/fixtures/tls" / cert).string() + "\" --key \"" +
            (root / "tests/fixtures/tls" / key).string() + "\"";
        if (!client_ca.empty()) {
            command += " --client-ca \"" +
                (root / "tests/fixtures/tls" / client_ca).string() + "\"";
        }
#ifdef _WIN32
        SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
        HANDLE write{};
        REQUIRE(CreatePipe(&stdout_read_, &write, &attributes, 0));
        REQUIRE(SetHandleInformation(stdout_read_, HANDLE_FLAG_INHERIT, 0));
        STARTUPINFOA startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = write;
        startup.hStdError = write;
        PROCESS_INFORMATION info{};
        std::vector<char> mutable_command(command.begin(), command.end());
        mutable_command.push_back('\0');
        REQUIRE(CreateProcessA(nullptr, mutable_command.data(), nullptr, nullptr, TRUE,
                               CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info));
        CloseHandle(write);
        CloseHandle(info.hThread);
        process_ = info.hProcess;
        char buffer[64]{};
        DWORD read{};
        std::string line;
        while (line.find("PORT=") == std::string::npos) {
            REQUIRE(ReadFile(stdout_read_, buffer, sizeof(buffer) - 1, &read, nullptr));
            buffer[read] = 0;
            line.append(buffer, read);
            REQUIRE(line.size() < 256);
        }
        const auto marker = line.find("PORT=");
        port_ = static_cast<std::uint16_t>(std::stoi(line.substr(marker + 5)));
#else
        pipe_ = popen(command.c_str(), "r");
        REQUIRE(pipe_ != nullptr);
        char line[64]{};
        REQUIRE(fgets(line, sizeof(line), pipe_) != nullptr);
        REQUIRE(std::strncmp(line, "PORT=", 5) == 0);
        port_ = static_cast<std::uint16_t>(std::atoi(line + 5));
#endif
        REQUIRE(port_ != 0);
    }
    ~OracleServer() {
#ifdef _WIN32
        if (process_ != nullptr) {
            TerminateProcess(process_, 0);
            WaitForSingleObject(process_, 5000);
            CloseHandle(process_);
        }
        if (stdout_read_ != nullptr) CloseHandle(stdout_read_);
#else
        if (pipe_ != nullptr) pclose(pipe_);
#endif
    }
    [[nodiscard]] std::uint16_t Port() const { return port_; }

private:
    std::uint16_t port_{};
#ifdef _WIN32
    HANDLE process_{};
    HANDLE stdout_read_{};
#else
    FILE* pipe_{};
#endif
};

struct TlsApp final {
    VirtualFileSystem filesystem;
    std::shared_ptr<DexVmAndroidContext> context{std::make_shared<DexVmAndroidContext>()};
    ogplay::core::CapabilityLedger ledger;
    ogplay::core::Logger logger;
    std::vector<std::vector<std::byte>> contents;
    std::vector<BionicModuleSource> libraries;
    std::unique_ptr<AndroidAppProcess> app;
    LoopbackTransport* transport{};

    explicit TlsApp(NetworkTransport* network = nullptr,
                    const InterpreterBackend backend = InterpreterBackend::switch_dispatch) {
        InstallCaPack(filesystem);
        for (const auto name : {"libc.so", "libm.so", "libdl.so", "libstdc++.so", "libz.so",
                                "libcrypto.so", "libssl.so", "libgabi++.so", "libicui18n.so",
                                "libicuuc.so", "libstlport.so", "libogplay_jni.so"}) {
            contents.push_back(ReadPayloadBytes(std::string("lib/") + name));
            libraries.push_back({name, contents.back()});
        }
        context->apk_bytes = {std::byte{0x50}, std::byte{0x4b}, std::byte{3}, std::byte{4}};
        if (network != nullptr) {
            context->network_policy = NetworkPolicy{true, true, false, {"tls.test", "127.0.0.1"}};
            context->network_transport = network;
        }
        AndroidAppProcessRequest request;
        request.manifest = AppManifest();
        request.system_libraries = libraries;
        request.dex_bytes = ReadDexFixture("cipher.dex");
        request.icu_data = ReadPayloadBytes("icu/icudt51l.dat");
        request.boot_dex_bytes = ogplay::test::ReadBootDex();
        request.context = context;
        request.dexvm.interpreter.backend = backend;
        request.surface_width = 64;
        request.surface_height = 36;
        request.maximum_ticks_per_call = UINT64_C(100000000);
#if defined(_WIN32)
        request.backend = {ogplay::gles::AngleRenderer::d3d11,
                           ogplay::gles::AngleDevice::hardware};
#elif defined(__APPLE__)
        request.backend = {ogplay::gles::AngleRenderer::metal,
                           ogplay::gles::AngleDevice::hardware};
#else
        request.backend = {ogplay::gles::AngleRenderer::vulkan,
                           ogplay::gles::AngleDevice::hardware};
#endif
        request.filesystem = &filesystem;
        request.ledger = &ledger;
        request.logger = &logger;
        app = AndroidAppProcess::Create(std::move(request));
    }

    Interpreter& Vm() { return app->DexVm().Vm(); }
    DexClassLinker& Linker() { return Vm().Linker(); }

    VmValue Direct(const char* owner, const char* name, const char* desc,
                   std::vector<VmValue> args = {}) {
        auto type = Linker().FindClass(owner);
        REQUIRE(type.has_value());
        auto method = Linker().FindDirectMethod(*type, name, desc);
        REQUIRE_MESSAGE(method.has_value(), name);
        auto result = Vm().Call(*method, args);
        REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
        return result.value;
    }
    VmCallOutcome DirectResult(const char* owner, const char* name, const char* desc,
                               std::vector<VmValue> args = {}) {
        auto type = Linker().FindClass(owner);
        REQUIRE(type.has_value());
        auto method = Linker().FindDirectMethod(*type, name, desc);
        REQUIRE_MESSAGE(method.has_value(), name);
        return Vm().Call(*method, args);
    }
    VmValue Invoke(VmObjectRef obj, const char* name, const char* desc,
                   std::vector<VmValue> args = {}) {
        auto result = InvokeResult(obj, name, desc, std::move(args));
        REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
        return result.value;
    }
    VmCallOutcome InvokeResult(VmObjectRef obj, const char* name, const char* desc,
                               std::vector<VmValue> args = {}) {
        auto type = Vm().Model().ObjectClass(obj);
        auto slot = Linker().FindVtableIndex(type, name, desc);
        REQUIRE_MESSAGE(slot.has_value(), name);
        args.insert(args.begin(), VmValue::Ref(obj));
        return Vm().Call(Linker().Class(type).vtable[*slot], args);
    }
    VmObjectRef Bytes(const std::vector<std::byte>& data) {
        auto array = Vm().Model().NewPrimitiveArray(
            Linker().ResolveDescriptor("[B"), JniPrimitiveKind::byte,
            static_cast<JniSize>(data.size()));
        Vm().Model().WriteByteRegion(array, 0, data);
        return array;
    }
    VmObjectRef Certificate(const std::string_view fixture) {
        const auto der = ReadTlsFixture(fixture);
        auto encoded = Bytes(der);
        auto stream = Vm().NewIntrinsicInstance("Ljava/io/ByteArrayInputStream;");
        auto roots = Vm().ProtectReferences(std::array{encoded, stream});
        Direct("Ljava/io/ByteArrayInputStream;", "<init>", "([B)V",
               {VmValue::Ref(stream), VmValue::Ref(encoded)});
        auto factory = Direct("Ljava/security/cert/CertificateFactory;", "getInstance",
                              "(Ljava/lang/String;)Ljava/security/cert/CertificateFactory;",
                              {VmValue::Ref(Vm().NewStringUtf8("X.509"))}).ref;
        const auto factory_roots = Vm().ProtectReferences(std::array{factory});
        return Invoke(factory, "generateCertificate",
                      "(Ljava/io/InputStream;)Ljava/security/cert/Certificate;",
                      {VmValue::Ref(stream)}).ref;
    }
    VmObjectRef CertArray(std::initializer_list<std::string_view> fixtures) {
        auto array = Vm().Model().NewObjectArray(
            Linker().ResolveDescriptor("[Ljava/security/cert/X509Certificate;"),
            Linker().ResolveDescriptor("Ljava/security/cert/X509Certificate;"),
            static_cast<JniSize>(fixtures.size()));
        const auto roots = Vm().ProtectReferences(std::array{array});
        JniSize index{};
        for (const auto fixture : fixtures) {
            Vm().Model().SetObjectElement(array, index++, Certificate(fixture));
        }
        return array;
    }
    VmObjectRef TrustManager(std::initializer_list<std::string_view> anchors) {
        auto store = Direct("Ljava/security/KeyStore;", "getInstance",
                            "(Ljava/lang/String;)Ljava/security/KeyStore;",
                            {VmValue::Ref(Vm().NewStringUtf8("BKS"))}).ref;
        const auto roots = Vm().ProtectReferences(std::array{store});
        Invoke(store, "load", "(Ljava/io/InputStream;[C)V",
               {VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{})});
        int index{};
        for (const auto fixture : anchors) {
            Invoke(store, "setCertificateEntry",
                   "(Ljava/lang/String;Ljava/security/cert/Certificate;)V",
                   {VmValue::Ref(Vm().NewStringUtf8("ca" + std::to_string(index++))),
                    VmValue::Ref(Certificate(fixture))});
        }
        auto factory = Direct("Ljavax/net/ssl/TrustManagerFactory;", "getInstance",
                              "(Ljava/lang/String;)Ljavax/net/ssl/TrustManagerFactory;",
                              {VmValue::Ref(Vm().NewStringUtf8("PKIX"))}).ref;
        Invoke(factory, "init", "(Ljava/security/KeyStore;)V", {VmValue::Ref(store)});
        auto managers = Invoke(factory, "getTrustManagers",
                               "()[Ljavax/net/ssl/TrustManager;").ref;
        return Vm().Model().GetObjectElement(managers, 0);
    }
};

[[nodiscard]] std::vector<std::byte> PackTlsCerts(
    std::initializer_list<std::string_view> fixtures) {
    std::vector<std::byte> out;
    const auto put_i32 = [&](int value) {
        out.push_back(static_cast<std::byte>(value));
        out.push_back(static_cast<std::byte>(value >> 8));
        out.push_back(static_cast<std::byte>(value >> 16));
        out.push_back(static_cast<std::byte>(value >> 24));
    };
    put_i32(static_cast<int>(fixtures.size()));
    for (const auto fixture : fixtures) {
        const auto der = ReadTlsFixture(fixture);
        put_i32(static_cast<int>(der.size()));
        out.insert(out.end(), der.begin(), der.end());
    }
    return out;
}

void CheckBytes(TlsApp& app, VmObjectRef array, const std::vector<std::byte>& expected) {
    REQUIRE(app.Vm().Model().ArrayLength(array) == static_cast<JniSize>(expected.size()));
    CHECK(app.Vm().Model().ReadByteRegion(array, 0, static_cast<JniSize>(expected.size())) ==
          expected);
}

[[nodiscard]] VmObjectRef RefField(TlsApp& app, VmObjectRef object, const char* name,
                                   const char* descriptor) {
    const auto field = app.Linker().FindFieldRecursive(
        app.Vm().Model().ObjectClass(object), name, descriptor);
    REQUIRE(field.has_value());
    const auto& linked = app.Linker().Field(*field);
    REQUIRE(linked.is_ref);
    const auto slots = app.Vm().Model().InstanceSlots(object);
    REQUIRE(linked.slot < slots.size());
    return VmObjectRef(slots[linked.slot].bits);
}

void ExpectCheckFailure(TlsApp& app, VmObjectRef manager, VmObjectRef chain,
                        const char* auth, const char* exception) {
    const auto roots = app.Vm().ProtectReferences(std::array{manager, chain});
    std::string fixture = "unknown";
    if (app.Vm().Model().ArrayLength(chain) > 0) {
        const auto cert = app.Vm().Model().GetObjectElement(chain, 0);
        const auto cert_roots = app.Vm().ProtectReferences(std::array{cert});
        const auto encoded = app.Invoke(cert, "getEncoded", "()[B").ref;
        const auto bytes = app.Vm().Model().ReadByteRegion(
            encoded, 0, app.Vm().Model().ArrayLength(encoded));
        for (const auto name : {"untrusted.der", "expired.der", "notyet.der", "non-ca-leaf.der",
                                "pathlen-leaf.der", "wrong-eku.der", "nc-leaf.der",
                                "unknown-critical.der", "server.der"}) {
            if (bytes == ReadTlsFixture(name)) fixture = name;
        }
    }
    CAPTURE(fixture);
    const auto auth_string = app.Vm().NewStringUtf8(auth);
    const auto auth_roots = app.Vm().ProtectReferences(std::array{auth_string});
    auto result = app.InvokeResult(
        manager, "checkServerTrusted",
        "([Ljava/security/cert/X509Certificate;Ljava/lang/String;)V",
        {VmValue::Ref(chain), VmValue::Ref(auth_string)});
    REQUIRE(result.exception.IsValid());
    CHECK(app.Linker().Class(result.exception_class).descriptor == exception);
}

}  // namespace

TEST_CASE("TLS-01 path validation uses guest libcrypto") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        CAPTURE(static_cast<int>(backend));
        TlsApp app(nullptr, backend);
        {
            auto leaf = app.Bytes(ReadTlsFixture("server.der"));
            auto extras = app.Bytes(PackTlsCerts({}));
            auto anchors = app.Bytes(PackTlsCerts({"ca.der"}));
            const auto roots = app.Vm().ProtectReferences(std::array{leaf, extras, anchors});
            const auto verified = app.DirectResult(
                "Lorg/ogplay/security/NativeTrust;", "verifyPath",
                "([B[B[BLjava/lang/String;)Z",
                {VmValue::Ref(leaf), VmValue::Ref(extras), VmValue::Ref(anchors),
                 VmValue::Ref(app.Vm().NewStringUtf8("SERVER"))});
            REQUIRE_MESSAGE(!verified.exception.IsValid(), verified.exception_message);
        }
        {
            auto leaf = app.Certificate("server.der");
            auto leaf_roots = app.Vm().ProtectReferences(std::array{leaf});
            auto ca = app.Certificate("ca.der");
            const auto ca_roots = app.Vm().ProtectReferences(std::array{ca});
            CheckBytes(app, app.Invoke(leaf, "getEncoded", "()[B").ref,
                       ReadTlsFixture("server.der"));
            CheckBytes(app, app.Invoke(ca, "getEncoded", "()[B").ref,
                       ReadTlsFixture("ca.der"));
        }
        auto trusted = app.TrustManager({"ca.der"});
        auto trust_roots = app.Vm().ProtectReferences(std::array{trusted});
        auto packed_anchors = RefField(app, trusted, "packedAnchors", "[B");
        const auto packed_anchor_roots =
            app.Vm().ProtectReferences(std::array{packed_anchors});
        CheckBytes(app, packed_anchors, PackTlsCerts({"ca.der"}));
        auto accepted = app.Invoke(trusted, "getAcceptedIssuers",
                                  "()[Ljava/security/cert/X509Certificate;").ref;
        const auto accepted_roots = app.Vm().ProtectReferences(std::array{accepted});
        REQUIRE(app.Vm().Model().ArrayLength(accepted) == 1);
        auto issuer_cert = app.Vm().Model().GetObjectElement(accepted, 0);
        const auto issuer_roots = app.Vm().ProtectReferences(std::array{issuer_cert});
        CheckBytes(app, app.Invoke(issuer_cert, "getEncoded", "()[B").ref,
                   ReadTlsFixture("ca.der"));
        CheckBytes(app,
                   app.Direct("Lorg/ogplay/security/NativeTrust;", "encodedCopy",
                              "(Ljava/security/cert/X509Certificate;)[B",
                              {VmValue::Ref(issuer_cert)}).ref,
                   ReadTlsFixture("ca.der"));
        CheckBytes(app,
                   app.Direct("Lorg/ogplay/security/X509TrustManagerImpl;", "copyEncoded",
                              "(Ljava/security/cert/X509Certificate;)[B",
                              {VmValue::Ref(issuer_cert)}).ref,
                   ReadTlsFixture("ca.der"));
        CheckBytes(app,
                   app.Direct("Lorg/ogplay/security/X509TrustManagerImpl;", "packCertificates",
                              "([Ljava/security/cert/X509Certificate;)[B",
                              {VmValue::Ref(accepted)}).ref,
                   PackTlsCerts({"ca.der"}));
        auto chain = app.CertArray({"server.der"});
        const auto chain_roots = app.Vm().ProtectReferences(std::array{chain});
        auto chain_leaf = app.Vm().Model().GetObjectElement(chain, 0);
        const auto chain_leaf_roots = app.Vm().ProtectReferences(std::array{chain_leaf});
        CheckBytes(app, app.Invoke(chain_leaf, "getEncoded", "()[B").ref,
                   ReadTlsFixture("server.der"));
        CheckBytes(app,
                   app.Direct("Lorg/ogplay/security/NativeTrust;", "encodedCopy",
                              "(Ljava/security/cert/X509Certificate;)[B",
                              {VmValue::Ref(chain_leaf)}).ref,
                   ReadTlsFixture("server.der"));
        app.Invoke(trusted, "checkServerTrusted",
                   "([Ljava/security/cert/X509Certificate;Ljava/lang/String;)V",
                   {VmValue::Ref(chain),
                    VmValue::Ref(app.Vm().NewStringUtf8("RSA"))});
        CheckBytes(app, RefField(app, trusted, "packedAnchors", "[B"),
                   PackTlsCerts({"ca.der"}));
        auto accepted_after_decode = app.Invoke(
            trusted, "getAcceptedIssuers", "()[Ljava/security/cert/X509Certificate;").ref;
        const auto accepted_after_decode_roots =
            app.Vm().ProtectReferences(std::array{accepted_after_decode});
        auto issuer_after_decode = app.Vm().Model().GetObjectElement(accepted_after_decode, 0);
        const auto issuer_after_decode_roots =
            app.Vm().ProtectReferences(std::array{issuer_after_decode});
        CheckBytes(app, app.Invoke(issuer_after_decode, "getEncoded", "()[B").ref,
                   ReadTlsFixture("ca.der"));
        auto trusted_leaf_chain = app.CertArray({"trusted-leaf.der"});
        const auto trusted_leaf_chain_roots =
            app.Vm().ProtectReferences(std::array{trusted_leaf_chain});
        CheckBytes(app, RefField(app, trusted, "packedAnchors", "[B"),
                   PackTlsCerts({"ca.der"}));
        app.Invoke(trusted, "checkServerTrusted",
                   "([Ljava/security/cert/X509Certificate;Ljava/lang/String;)V",
                   {VmValue::Ref(trusted_leaf_chain),
                    VmValue::Ref(app.Vm().NewStringUtf8("RSA"))});
        auto leaf_trusted = app.TrustManager({"trusted-leaf.der"});
        const auto leaf_trusted_roots = app.Vm().ProtectReferences(std::array{leaf_trusted});
        auto leaf_trusted_chain = app.CertArray({"trusted-leaf.der"});
        const auto leaf_trusted_chain_roots =
            app.Vm().ProtectReferences(std::array{leaf_trusted_chain});
        app.Invoke(leaf_trusted, "checkServerTrusted",
                   "([Ljava/security/cert/X509Certificate;Ljava/lang/String;)V",
                   {VmValue::Ref(leaf_trusted_chain),
                    VmValue::Ref(app.Vm().NewStringUtf8("RSA"))});
        auto empty = app.Vm().Model().NewObjectArray(
            app.Linker().ResolveDescriptor("[Ljava/security/cert/X509Certificate;"),
            app.Linker().ResolveDescriptor("Ljava/security/cert/X509Certificate;"), 0);
        const auto empty_roots = app.Vm().ProtectReferences(std::array{empty});
        auto empty_args = app.InvokeResult(
            trusted, "checkServerTrusted",
            "([Ljava/security/cert/X509Certificate;Ljava/lang/String;)V",
            {VmValue::Ref(empty), VmValue::Ref(app.Vm().NewStringUtf8("RSA"))});
        REQUIRE(empty_args.exception.IsValid());
        CHECK(app.Linker().Class(empty_args.exception_class).descriptor ==
              "Ljava/lang/IllegalArgumentException;");
        ExpectCheckFailure(app, trusted, app.CertArray({"untrusted.der"}), "RSA",
                           "Ljava/security/cert/CertificateException;");
        ExpectCheckFailure(app, trusted, app.CertArray({"expired.der"}), "RSA",
                           "Ljava/security/cert/CertificateExpiredException;");
        ExpectCheckFailure(app, trusted, app.CertArray({"notyet.der"}), "RSA",
                           "Ljava/security/cert/CertificateNotYetValidException;");
        ExpectCheckFailure(app, trusted, app.CertArray({"non-ca-leaf.der", "non-ca.der"}),
                           "RSA", "Ljava/security/cert/CertificateException;");
        auto pathlen_manager = app.TrustManager({"pathlen-ca.der"});
        const auto pathlen_manager_roots =
            app.Vm().ProtectReferences(std::array{pathlen_manager});
        ExpectCheckFailure(app, pathlen_manager,
                           app.CertArray({"pathlen-leaf.der", "pathlen-bridge.der",
                                          "pathlen-mid.der"}), "RSA",
                           "Ljava/security/cert/CertificateException;");
        ExpectCheckFailure(app, trusted, app.CertArray({"wrong-eku.der"}), "RSA",
                           "Ljava/security/cert/CertificateException;");
        auto nc_manager = app.TrustManager({"nc-ca.der"});
        const auto nc_manager_roots = app.Vm().ProtectReferences(std::array{nc_manager});
        ExpectCheckFailure(app, nc_manager,
                           app.CertArray({"nc-leaf.der"}), "RSA",
                           "Ljava/security/cert/CertificateException;");
        ExpectCheckFailure(app, trusted, app.CertArray({"unknown-critical.der"}), "RSA",
                           "Ljava/security/cert/CertificateException;");
        auto same_subject_manager = app.TrustManager({"same-subject-ca.der"});
        const auto same_subject_manager_roots =
            app.Vm().ProtectReferences(std::array{same_subject_manager});
        ExpectCheckFailure(app, same_subject_manager,
                           app.CertArray({"server.der"}), "RSA",
                           "Ljava/security/cert/CertificateException;");
        auto cross_chain = app.CertArray({"cross-leaf.der", "cross-mid.der"});
        const auto cross_chain_roots = app.Vm().ProtectReferences(std::array{cross_chain});
        for (JniSize i = 0; i < 2; ++i) {
            const auto cert = app.Vm().Model().GetObjectElement(cross_chain, i);
            CheckBytes(app,
                       app.Direct("Lorg/ogplay/security/NativeTrust;", "encodedCopy",
                                  "(Ljava/security/cert/X509Certificate;)[B",
                                  {VmValue::Ref(cert)}).ref,
                       ReadTlsFixture(i == 0 ? "cross-leaf.der" : "cross-mid.der"));
        }
        app.Invoke(trusted, "checkServerTrusted",
                   "([Ljava/security/cert/X509Certificate;Ljava/lang/String;)V",
                   {VmValue::Ref(cross_chain),
                    VmValue::Ref(app.Vm().NewStringUtf8("RSA"))});
        auto empty_store = app.TrustManager({});
        const auto empty_store_roots = app.Vm().ProtectReferences(std::array{empty_store});
        ExpectCheckFailure(app, empty_store, app.CertArray({"server.der"}), "RSA",
                           "Ljava/security/cert/CertificateException;");
        auto default_factory = app.Direct(
            "Ljavax/net/ssl/TrustManagerFactory;", "getInstance",
            "(Ljava/lang/String;)Ljavax/net/ssl/TrustManagerFactory;",
            {VmValue::Ref(app.Vm().NewStringUtf8("PKIX"))}).ref;
        const auto default_factory_roots =
            app.Vm().ProtectReferences(std::array{default_factory});
        app.Invoke(default_factory, "init", "(Ljava/security/KeyStore;)V",
                   {VmValue::Ref(VmObjectRef{})});
        auto default_managers = app.Invoke(
            default_factory, "getTrustManagers", "()[Ljavax/net/ssl/TrustManager;").ref;
        const auto default_managers_roots =
            app.Vm().ProtectReferences(std::array{default_managers});
        auto default_manager = app.Vm().Model().GetObjectElement(default_managers, 0);
        const auto default_manager_roots =
            app.Vm().ProtectReferences(std::array{default_manager});
        auto issuers = app.Invoke(default_manager, "getAcceptedIssuers",
                                  "()[Ljava/security/cert/X509Certificate;").ref;
        const auto issuers_roots = app.Vm().ProtectReferences(std::array{issuers});
        REQUIRE(app.Vm().Model().ArrayLength(issuers) == 1);
        app.Vm().Model().SetObjectElement(issuers, 0, VmObjectRef{});
        auto issuers_again = app.Invoke(default_manager, "getAcceptedIssuers",
                                        "()[Ljava/security/cert/X509Certificate;").ref;
        const auto issuers_again_roots =
            app.Vm().ProtectReferences(std::array{issuers_again});
        CHECK(app.Vm().Model().GetObjectElement(issuers_again, 0).IsValid());
        auto default_chain = app.CertArray({"server.der"});
        const auto default_chain_roots = app.Vm().ProtectReferences(std::array{default_chain});
        app.Invoke(default_manager, "checkServerTrusted",
                   "([Ljava/security/cert/X509Certificate;Ljava/lang/String;)V",
                   {VmValue::Ref(default_chain),
                    VmValue::Ref(app.Vm().NewStringUtf8("RSA"))});
        auto android_store = app.Direct(
            "Ljava/security/KeyStore;", "getInstance",
            "(Ljava/lang/String;)Ljava/security/KeyStore;",
            {VmValue::Ref(app.Vm().NewStringUtf8("AndroidCAStore"))}).ref;
        const auto android_store_roots = app.Vm().ProtectReferences(std::array{android_store});
        app.Invoke(android_store, "load", "(Ljava/io/InputStream;[C)V",
                   {VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{})});
        CHECK(app.Invoke(android_store, "size", "()I").AsInt() == 1);
        auto write_cert = app.Certificate("ca.der");
        const auto write_cert_roots = app.Vm().ProtectReferences(std::array{write_cert});
        auto write = app.InvokeResult(
            android_store, "setCertificateEntry",
            "(Ljava/lang/String;Ljava/security/cert/Certificate;)V",
            {VmValue::Ref(app.Vm().NewStringUtf8("user:test")),
             VmValue::Ref(write_cert)});
        REQUIRE(write.exception.IsValid());
        CHECK(app.Linker().Class(write.exception_class).descriptor ==
              "Ljava/lang/UnsupportedOperationException;");
        auto auth_chain = app.CertArray({"server.der"});
        const auto auth_chain_roots = app.Vm().ProtectReferences(std::array{auth_chain});
        auto auth = app.InvokeResult(
            trusted, "checkServerTrusted",
            "([Ljava/security/cert/X509Certificate;Ljava/lang/String;)V",
            {VmValue::Ref(auth_chain),
             VmValue::Ref(app.Vm().NewStringUtf8("ECDSA"))});
        REQUIRE(auth.exception.IsValid());
        CHECK(app.Linker().Class(auth.exception_class).descriptor ==
              "Ljava/security/cert/CertificateException;");
        const auto previous = app.context->uptime_millis.load();
        // AndroidAppProcess wall clock is 1_400_000_000_000 + uptime_millis.
        // 714'380'800'000 yields 2037-01-01, after fixture notAfter 2036-12-31.
        app.context->uptime_millis.store(714'380'800'000LL);
        auto expired_chain = app.CertArray({"server.der"});
        const auto expired_chain_roots =
            app.Vm().ProtectReferences(std::array{expired_chain});
        auto clocked = app.InvokeResult(
            trusted, "checkServerTrusted",
            "([Ljava/security/cert/X509Certificate;Ljava/lang/String;)V",
            {VmValue::Ref(expired_chain),
             VmValue::Ref(app.Vm().NewStringUtf8("RSA"))});
        app.context->uptime_millis.store(previous);
        REQUIRE(clocked.exception.IsValid());
        CHECK(app.Linker().Class(clocked.exception_class).descriptor ==
              "Ljava/security/cert/CertificateExpiredException;");
    }
}

TEST_CASE("TLS-02 client handshake HTTPS layered autoClose") {
    OracleServer server("server.crt", "server.key");
    LoopbackTransport transport(server.Port());
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        CAPTURE(backend == InterpreterBackend::threaded ? "threaded" : "switch");
        TlsApp app(&transport, backend);
        auto context = app.Direct(
            "Ljavax/net/ssl/SSLContext;", "getInstance",
            "(Ljava/lang/String;)Ljavax/net/ssl/SSLContext;",
            {VmValue::Ref(app.Vm().NewStringUtf8("TLS"))}).ref;
        app.Invoke(context, "init",
                   "([Ljavax/net/ssl/KeyManager;[Ljavax/net/ssl/TrustManager;"
                   "Ljava/security/SecureRandom;)V",
                   {VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{}),
                    VmValue::Ref(VmObjectRef{})});
        auto factory = app.Invoke(context, "getSocketFactory",
                                  "()Ljavax/net/ssl/SSLSocketFactory;").ref;
        auto suites = app.Invoke(factory, "getDefaultCipherSuites",
                                 "()[Ljava/lang/String;").ref;
        CHECK(app.Vm().Model().ArrayLength(suites) > 0);
        auto socket = app.Invoke(
            factory, "createSocket",
            "(Ljava/lang/String;I)Ljava/net/Socket;",
            {VmValue::Ref(app.Vm().NewStringUtf8("tls.test")),
             VmValue::Int(server.Port())}).ref;
        auto handshake = app.InvokeResult(socket, "startHandshake", "()V");
        REQUIRE_MESSAGE(!handshake.exception.IsValid(), handshake.exception_message);
        auto session = app.Invoke(socket, "getSession",
                                  "()Ljavax/net/ssl/SSLSession;").ref;
        CHECK(app.Vm().StringUtf8(app.Invoke(session, "getProtocol",
                                             "()Ljava/lang/String;").ref) ==
              "TLSv1.2");
        CHECK(app.Vm().StringUtf8(app.Invoke(session, "getCipherSuite",
                                             "()Ljava/lang/String;").ref)
                  .find("TLS_") == 0);
        auto session_id = app.Invoke(session, "getId", "()[B").ref;
        CHECK(app.Vm().Model().ArrayLength(session_id) > 4);
        CHECK(app.Vm().StringUtf8(app.Invoke(session, "getPeerHost",
                                             "()Ljava/lang/String;").ref) ==
              "tls.test");
        auto peers = app.Invoke(session, "getPeerCertificates",
                                "()[Ljava/security/cert/Certificate;").ref;
        CHECK(app.Vm().Model().ArrayLength(peers) >= 1);
        app.Invoke(socket, "close", "()V");

        auto url = app.Vm().NewIntrinsicInstance("Ljava/net/URL;");
        const auto spec = "https://tls.test:" + std::to_string(server.Port()) + "/";
        app.Direct("Ljava/net/URL;", "<init>", "(Ljava/lang/String;)V",
                   {VmValue::Ref(url), VmValue::Ref(app.Vm().NewStringUtf8(spec))});
        auto connection = app.Invoke(url, "openConnection",
                                     "()Ljava/net/URLConnection;").ref;
        CHECK(app.Linker().Class(app.Vm().Model().ObjectClass(connection)).descriptor ==
              "Lorg/ogplay/security/OgPlayHttpsURLConnection;");
        auto opened_connect = app.InvokeResult(connection, "connect", "()V");
        REQUIRE_MESSAGE(!opened_connect.exception.IsValid(), opened_connect.exception_message);
        auto body_result = app.InvokeResult(connection, "getInputStream",
                                            "()Ljava/io/InputStream;");
        REQUIRE_MESSAGE(!body_result.exception.IsValid(), body_result.exception_message);
        auto code = app.InvokeResult(connection, "getResponseCode", "()I");
        REQUIRE_MESSAGE(!code.exception.IsValid(), code.exception_message);
        CHECK(code.value.AsInt() == 200);
        auto body = app.Invoke(connection, "getInputStream", "()Ljava/io/InputStream;").ref;
        auto bytes = app.Vm().NewIntrinsicInstance("Ljava/io/ByteArrayOutputStream;");
        app.Direct("Ljava/io/ByteArrayOutputStream;", "<init>", "()V", {VmValue::Ref(bytes)});
        std::array<std::byte, 16> scratch{};
        auto buffer = app.Bytes(std::vector<std::byte>(scratch.begin(), scratch.end()));
        int total{};
        while (true) {
            auto read = app.Invoke(body, "read", "([B)I", {VmValue::Ref(buffer)});
            if (read.AsInt() < 0) break;
            app.Invoke(bytes, "write", "([BII)V",
                       {VmValue::Ref(buffer), VmValue::Int(0), read});
            total += read.AsInt();
            REQUIRE(total < 64);
        }
        auto payload = app.Invoke(bytes, "toByteArray", "()[B").ref;
        const auto text = app.Vm().Model().ReadByteRegion(
            payload, 0, app.Vm().Model().ArrayLength(payload));
        CHECK(std::string(reinterpret_cast<const char*>(text.data()), text.size()) ==
              "tls-ok");
        app.Invoke(connection, "disconnect", "()V");

        auto raw = app.Vm().NewIntrinsicInstance("Ljava/net/Socket;");
        app.Direct("Ljava/net/Socket;", "<init>", "(Ljava/lang/String;I)V",
                   {VmValue::Ref(raw), VmValue::Ref(app.Vm().NewStringUtf8("tls.test")),
                    VmValue::Int(server.Port())});
        auto layered = app.Invoke(
            factory, "createSocket",
            "(Ljava/net/Socket;Ljava/lang/String;IZ)Ljava/net/Socket;",
            {VmValue::Ref(raw), VmValue::Ref(app.Vm().NewStringUtf8("tls.test")),
             VmValue::Int(server.Port()), VmValue::Int(0)}).ref;
        app.Invoke(layered, "close", "()V");
        auto closed = app.Invoke(raw, "isClosed", "()Z");
        CHECK(closed.AsInt() == 0);
        app.Invoke(raw, "close", "()V");
    }
}

TEST_CASE("TLS-02 SSLContext init does not replace the HTTPS factory") {
    TlsApp app;
    auto defaults = app.Direct(
        "Ljavax/net/ssl/SSLContext;", "getInstance",
        "(Ljava/lang/String;)Ljavax/net/ssl/SSLContext;",
        {VmValue::Ref(app.Vm().NewStringUtf8("Default"))}).ref;
    auto original = app.Direct(
        "Ljavax/net/ssl/HttpsURLConnection;", "getDefaultSSLSocketFactory",
        "()Ljavax/net/ssl/SSLSocketFactory;").ref;
    auto context = app.Direct(
        "Ljavax/net/ssl/SSLContext;", "getInstance",
        "(Ljava/lang/String;)Ljavax/net/ssl/SSLContext;",
        {VmValue::Ref(app.Vm().NewStringUtf8("TLS"))}).ref;
    app.Invoke(context, "init",
               "([Ljavax/net/ssl/KeyManager;[Ljavax/net/ssl/TrustManager;"
               "Ljava/security/SecureRandom;)V",
               {VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{}),
                VmValue::Ref(VmObjectRef{})});
    auto after = app.Direct(
        "Ljavax/net/ssl/HttpsURLConnection;", "getDefaultSSLSocketFactory",
        "()Ljavax/net/ssl/SSLSocketFactory;").ref;
    CHECK(after.Value() == original.Value());
    auto factory = app.Invoke(context, "getSocketFactory",
                              "()Ljavax/net/ssl/SSLSocketFactory;").ref;
    CHECK(factory.Value() != original.Value());
    app.Direct("Ljavax/net/ssl/HttpsURLConnection;", "setDefaultSSLSocketFactory",
               "(Ljavax/net/ssl/SSLSocketFactory;)V", {VmValue::Ref(factory)});
    auto created_default = app.Direct(
        "Ljavax/net/ssl/SSLContext;", "getInstance",
        "(Ljava/lang/String;)Ljavax/net/ssl/SSLContext;",
        {VmValue::Ref(app.Vm().NewStringUtf8("Default"))}).ref;
    auto after_default = app.Direct(
        "Ljavax/net/ssl/HttpsURLConnection;", "getDefaultSSLSocketFactory",
        "()Ljavax/net/ssl/SSLSocketFactory;").ref;
    CHECK(after_default.Value() == factory.Value());
    auto url = app.Vm().NewIntrinsicInstance("Ljava/net/URL;");
    app.Direct("Ljava/net/URL;", "<init>", "(Ljava/lang/String;)V",
               {VmValue::Ref(url),
                VmValue::Ref(app.Vm().NewStringUtf8("https://tls.test/"))});
    auto connection = app.InvokeResult(url, "openConnection",
                                       "()Ljava/net/URLConnection;");
    REQUIRE(connection.exception.IsValid());
    CHECK(app.Linker().Class(connection.exception_class).descriptor ==
          "Ljava/net/UnknownHostException;");
    (void)defaults;
    (void)created_default;
}

TEST_CASE("TLS-02 enabled protocols and SNI apply to the handshake") {
    OracleServer server("server.crt", "server.key");
    LoopbackTransport transport(server.Port());
    TlsApp app(&transport);
    auto context = app.Direct(
        "Ljavax/net/ssl/SSLContext;", "getInstance",
        "(Ljava/lang/String;)Ljavax/net/ssl/SSLContext;",
        {VmValue::Ref(app.Vm().NewStringUtf8("TLS"))}).ref;
    app.Invoke(context, "init",
               "([Ljavax/net/ssl/KeyManager;[Ljavax/net/ssl/TrustManager;"
               "Ljava/security/SecureRandom;)V",
               {VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{}),
                VmValue::Ref(VmObjectRef{})});
    auto factory = app.Invoke(context, "getSocketFactory",
                              "()Ljavax/net/ssl/SSLSocketFactory;").ref;
    auto tls1 = app.Invoke(factory, "createSocket", "()Ljava/net/Socket;").ref;
    auto v1 = app.Vm().Model().NewObjectArray(
        app.Linker().ResolveDescriptor("[Ljava/lang/String;"),
        app.Linker().ResolveDescriptor("Ljava/lang/String;"), 1);
    app.Vm().Model().SetObjectElement(v1, 0, app.Vm().NewStringUtf8("TLSv1"));
    app.Invoke(tls1, "setEnabledProtocols", "([Ljava/lang/String;)V",
               {VmValue::Ref(v1)});
    auto endpoint = app.Vm().NewIntrinsicInstance("Ljava/net/InetSocketAddress;");
    app.Direct("Ljava/net/InetSocketAddress;", "<init>", "(Ljava/lang/String;I)V",
               {VmValue::Ref(endpoint), VmValue::Ref(app.Vm().NewStringUtf8("tls.test")),
                VmValue::Int(server.Port())});
    app.Invoke(tls1, "connect", "(Ljava/net/SocketAddress;)V", {VmValue::Ref(endpoint)});
    auto denied = app.InvokeResult(tls1, "startHandshake", "()V");
    REQUIRE(denied.exception.IsValid());
    app.Invoke(tls1, "close", "()V");

    auto created = app.Invoke(factory, "createSocket", "()Ljava/net/Socket;").ref;
    auto created_endpoint = app.Vm().NewIntrinsicInstance("Ljava/net/InetSocketAddress;");
    app.Direct("Ljava/net/InetSocketAddress;", "<init>", "(Ljava/lang/String;I)V",
               {VmValue::Ref(created_endpoint),
                VmValue::Ref(app.Vm().NewStringUtf8("tls.test")),
                VmValue::Int(server.Port())});
    app.Invoke(created, "connect", "(Ljava/net/SocketAddress;)V",
               {VmValue::Ref(created_endpoint)});
    auto handshake = app.InvokeResult(created, "startHandshake", "()V");
    REQUIRE_MESSAGE(!handshake.exception.IsValid(), handshake.exception_message);
    auto session = app.Invoke(created, "getSession", "()Ljavax/net/ssl/SSLSession;").ref;
    CHECK(app.Vm().StringUtf8(app.Invoke(session, "getPeerHost",
                                         "()Ljava/lang/String;").ref) == "tls.test");
    CHECK(app.Vm().StringUtf8(app.Invoke(session, "getProtocol",
                                         "()Ljava/lang/String;").ref) == "TLSv1.2");
    app.Invoke(created, "close", "()V");
}

TEST_CASE("TLS-02 session cache resumes and honors timeout") {
    OracleServer server("server.crt", "server.key");
    LoopbackTransport transport(server.Port());
    TlsApp app(&transport);
    auto context = app.Direct(
        "Ljavax/net/ssl/SSLContext;", "getInstance",
        "(Ljava/lang/String;)Ljavax/net/ssl/SSLContext;",
        {VmValue::Ref(app.Vm().NewStringUtf8("TLS"))}).ref;
    app.Invoke(context, "init",
               "([Ljavax/net/ssl/KeyManager;[Ljavax/net/ssl/TrustManager;"
               "Ljava/security/SecureRandom;)V",
               {VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{}),
                VmValue::Ref(VmObjectRef{})});
    auto factory = app.Invoke(context, "getSocketFactory",
                              "()Ljavax/net/ssl/SSLSocketFactory;").ref;
    auto first = app.Invoke(
        factory, "createSocket", "(Ljava/lang/String;I)Ljava/net/Socket;",
        {VmValue::Ref(app.Vm().NewStringUtf8("tls.test")),
         VmValue::Int(server.Port())}).ref;
    auto first_handshake = app.InvokeResult(first, "startHandshake", "()V");
    REQUIRE_MESSAGE(!first_handshake.exception.IsValid(), first_handshake.exception_message);
    auto first_session = app.Invoke(first, "getSession",
                                    "()Ljavax/net/ssl/SSLSession;").ref;
    auto first_id = app.Invoke(first_session, "getId", "()[B").ref;
    REQUIRE(app.Vm().Model().ArrayLength(first_id) > 4);
    auto session_context = app.Invoke(
        context, "getClientSessionContext",
        "()Ljavax/net/ssl/SSLSessionContext;").ref;
    const auto session_roots = app.Vm().ProtectReferences(
        std::array{context, factory, first, first_session, first_id, session_context});
    auto cached = app.Invoke(session_context, "getSession",
                             "([B)Ljavax/net/ssl/SSLSession;",
                             {VmValue::Ref(first_id)}).ref;
    REQUIRE(cached.IsValid());
    auto cached_id = app.Invoke(cached, "getId", "()[B").ref;
    CHECK(app.Vm().Model().ReadByteRegion(
              first_id, 0, app.Vm().Model().ArrayLength(first_id)) ==
          app.Vm().Model().ReadByteRegion(
              cached_id, 0, app.Vm().Model().ArrayLength(cached_id)));
    auto ids = app.Invoke(session_context, "getIds",
                          "()Ljava/util/Enumeration;").ref;
    CHECK(app.Invoke(ids, "hasMoreElements", "()Z").AsInt() == 1);
    auto enumerated = app.Invoke(ids, "nextElement", "()Ljava/lang/Object;").ref;
    CHECK(app.Vm().Model().ReadByteRegion(
              first_id, 0, app.Vm().Model().ArrayLength(first_id)) ==
          app.Vm().Model().ReadByteRegion(
              enumerated, 0, app.Vm().Model().ArrayLength(enumerated)));
    app.Invoke(first, "close", "()V");

    const auto original_uptime = app.context->uptime_millis.load();
    app.context->uptime_millis.store(original_uptime + 500);
    app.Invoke(session_context, "setSessionCacheSize", "(I)V", {VmValue::Int(1)});
    CHECK(app.Invoke(session_context, "getSession", "([B)Ljavax/net/ssl/SSLSession;",
                     {VmValue::Ref(first_id)}).ref.IsValid());
    app.Invoke(session_context, "setSessionCacheSize", "(I)V", {VmValue::Int(0)});
    auto resumed = app.Invoke(
        factory, "createSocket", "(Ljava/lang/String;I)Ljava/net/Socket;",
        {VmValue::Ref(app.Vm().NewStringUtf8("tls.test")),
         VmValue::Int(server.Port())}).ref;
    app.Invoke(resumed, "setEnableSessionCreation", "(Z)V", {VmValue::Int(0)});
    auto resume_handshake = app.InvokeResult(resumed, "startHandshake", "()V");
    REQUIRE_MESSAGE(!resume_handshake.exception.IsValid(),
                    resume_handshake.exception_message);
    auto resumed_session = app.Invoke(resumed, "getSession",
                                      "()Ljavax/net/ssl/SSLSession;").ref;
    auto resumed_id = app.Invoke(resumed_session, "getId", "()[B").ref;
    CHECK(resumed_session.Value() == first_session.Value());
    CHECK(app.Invoke(session_context, "getSession", "([B)Ljavax/net/ssl/SSLSession;",
                     {VmValue::Ref(first_id)}).ref.IsValid());
    CHECK(app.Vm().Model().ReadByteRegion(
              first_id, 0, app.Vm().Model().ArrayLength(first_id)) ==
          app.Vm().Model().ReadByteRegion(
              resumed_id, 0, app.Vm().Model().ArrayLength(resumed_id)));
    app.Invoke(resumed, "close", "()V");

    auto isolated = app.Direct(
        "Ljavax/net/ssl/SSLContext;", "getInstance",
        "(Ljava/lang/String;)Ljavax/net/ssl/SSLContext;",
        {VmValue::Ref(app.Vm().NewStringUtf8("TLS"))}).ref;
    app.Invoke(isolated, "init",
               "([Ljavax/net/ssl/KeyManager;[Ljavax/net/ssl/TrustManager;"
               "Ljava/security/SecureRandom;)V",
               {VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{}),
                VmValue::Ref(VmObjectRef{})});
    auto isolated_factory = app.Invoke(isolated, "getSocketFactory",
                                       "()Ljavax/net/ssl/SSLSocketFactory;").ref;
    auto disabled = app.Invoke(
        isolated_factory, "createSocket", "(Ljava/lang/String;I)Ljava/net/Socket;",
        {VmValue::Ref(app.Vm().NewStringUtf8("tls.test")),
         VmValue::Int(server.Port())}).ref;
    app.Invoke(disabled, "setEnableSessionCreation", "(Z)V", {VmValue::Int(0)});
    auto blocked = app.InvokeResult(disabled, "startHandshake", "()V");
    REQUIRE(blocked.exception.IsValid());
    app.Invoke(disabled, "close", "()V");

    app.Invoke(session_context, "setSessionTimeout", "(I)V", {VmValue::Int(1)});
    const auto previous = app.context->uptime_millis.load();
    app.context->uptime_millis.store(original_uptime + 900);
    CHECK(app.Invoke(session_context, "getSession", "([B)Ljavax/net/ssl/SSLSession;",
                     {VmValue::Ref(first_id)}).ref.IsValid());
    app.context->uptime_millis.store(original_uptime + 1100);
    auto expired = app.Invoke(session_context, "getSession",
                              "([B)Ljavax/net/ssl/SSLSession;",
                              {VmValue::Ref(first_id)}).ref;
    CHECK_FALSE(expired.IsValid());
    app.context->uptime_millis.store(previous);
    app.Invoke(session_context, "setSessionTimeout", "(I)V", {VmValue::Int(0)});
    for (int i = 0; i < 3; ++i) {
        auto item = app.Vm().NewIntrinsicInstance("Lorg/ogplay/security/OgPlaySslSession;");
        auto id = app.Bytes(std::vector<std::byte>{static_cast<std::byte>(i + 1)});
        const auto roots = app.Vm().ProtectReferences(std::array{item, id});
        app.Direct("Lorg/ogplay/security/OgPlaySslSession;", "<init>",
            "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;I[Ljava/security/cert/Certificate;Lorg/ogplay/security/OgPlaySslSessionContext;[B[B)V",
            {VmValue::Ref(item), VmValue::Ref(app.Vm().NewStringUtf8("TLSv1.2")),
             VmValue::Ref(app.Vm().NewStringUtf8("fixture")),
             VmValue::Ref(app.Vm().NewStringUtf8("cache.test")), VmValue::Int(i),
             VmValue::Ref(VmObjectRef{}), VmValue::Ref(session_context),
             VmValue::Ref(id), VmValue::Ref(VmObjectRef{})});
        app.Invoke(session_context, "put", "(Lorg/ogplay/security/OgPlaySslSession;)V",
                   {VmValue::Ref(item)});
        CHECK(app.Invoke(session_context, "getSession", "([B)Ljavax/net/ssl/SSLSession;",
                         {VmValue::Ref(id)}).ref.IsValid());
    }
    app.Invoke(session_context, "setSessionCacheSize", "(I)V", {VmValue::Int(2)});
    auto remaining = app.Invoke(session_context, "getIds", "()Ljava/util/Enumeration;").ref;
    int count = 0;
    while (app.Invoke(remaining, "hasMoreElements", "()Z").AsInt()) {
        app.Invoke(remaining, "nextElement", "()Ljava/lang/Object;");
        ++count;
        REQUIRE(count <= 3);
    }
    CHECK(count == 2);
}

void HandshakeMutualTls(TlsApp& app, std::uint16_t port, const char* algorithm,
                        const char* pkcs8, const char* der) {
    auto store = app.Direct("Ljava/security/KeyStore;", "getInstance",
                            "(Ljava/lang/String;)Ljava/security/KeyStore;",
                            {VmValue::Ref(app.Vm().NewStringUtf8("BKS"))}).ref;
    app.Invoke(store, "load", "(Ljava/io/InputStream;[C)V",
               {VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{})});
    auto key = app.Vm().NewIntrinsicInstance("Lorg/ogplay/security/BksPrivateKey;");
    app.Direct("Lorg/ogplay/security/BksPrivateKey;", "<init>",
               "(Ljava/lang/String;[B)V",
               {VmValue::Ref(key), VmValue::Ref(app.Vm().NewStringUtf8(algorithm)),
                VmValue::Ref(app.Bytes(ReadTlsFixture(pkcs8)))});
    auto chain = app.Vm().Model().NewObjectArray(
        app.Linker().ResolveDescriptor("[Ljava/security/cert/Certificate;"),
        app.Linker().ResolveDescriptor("Ljava/security/cert/Certificate;"), 1);
    app.Vm().Model().SetObjectElement(chain, 0, app.Certificate(der));
    auto password = app.Vm().Model().NewPrimitiveArray(
        app.Linker().ResolveDescriptor("[C"), JniPrimitiveKind::character, 0);
    app.Invoke(store, "setKeyEntry",
               "(Ljava/lang/String;Ljava/security/Key;[C[Ljava/security/cert/Certificate;)V",
               {VmValue::Ref(app.Vm().NewStringUtf8("client")), VmValue::Ref(key),
                VmValue::Ref(password), VmValue::Ref(chain)});
    auto kmf = app.Direct("Ljavax/net/ssl/KeyManagerFactory;", "getInstance",
                          "(Ljava/lang/String;)Ljavax/net/ssl/KeyManagerFactory;",
                          {VmValue::Ref(app.Vm().NewStringUtf8("PKIX"))}).ref;
    app.Invoke(kmf, "init", "(Ljava/security/KeyStore;[C)V",
               {VmValue::Ref(store), VmValue::Ref(password)});
    auto kms = app.Invoke(kmf, "getKeyManagers", "()[Ljavax/net/ssl/KeyManager;").ref;
    auto context = app.Direct(
        "Ljavax/net/ssl/SSLContext;", "getInstance",
        "(Ljava/lang/String;)Ljavax/net/ssl/SSLContext;",
        {VmValue::Ref(app.Vm().NewStringUtf8("TLS"))}).ref;
    app.Invoke(context, "init",
               "([Ljavax/net/ssl/KeyManager;[Ljavax/net/ssl/TrustManager;"
               "Ljava/security/SecureRandom;)V",
               {VmValue::Ref(kms), VmValue::Ref(VmObjectRef{}),
                VmValue::Ref(VmObjectRef{})});
    auto factory = app.Invoke(context, "getSocketFactory",
                              "()Ljavax/net/ssl/SSLSocketFactory;").ref;
    auto socket = app.Invoke(
        factory, "createSocket", "(Ljava/lang/String;I)Ljava/net/Socket;",
        {VmValue::Ref(app.Vm().NewStringUtf8("tls.test")),
         VmValue::Int(port)}).ref;
    auto protocol = app.Invoke(app.Invoke(socket, "getSession",
                                          "()Ljavax/net/ssl/SSLSession;").ref,
                               "getProtocol", "()Ljava/lang/String;");
    CHECK(app.Vm().StringUtf8(protocol.ref) == "TLSv1.2");
    app.Invoke(socket, "close", "()V");
}

TEST_CASE("TLS-02 mutual TLS installs a BKS client certificate") {
    OracleServer server("server.crt", "server.key", "ca.crt");
    LoopbackTransport transport(server.Port());
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        CAPTURE(backend == InterpreterBackend::threaded ? "threaded" : "switch");
        TlsApp app(&transport, backend);
        HandshakeMutualTls(app, server.Port(), "RSA", "client.pkcs8", "client.der");
        HandshakeMutualTls(app, server.Port(), "EC", "ec-client.pkcs8", "ec-client.der");
    }
}

TEST_CASE("TLS-02 network stays offline by default") {
    TlsApp app;
    CHECK_FALSE(app.Vm().Network().Policy().enabled);
    auto url = app.Vm().NewIntrinsicInstance("Ljava/net/URL;");
    app.Direct("Ljava/net/URL;", "<init>", "(Ljava/lang/String;)V",
               {VmValue::Ref(url),
                VmValue::Ref(app.Vm().NewStringUtf8("https://tls.test/"))});
    auto opened = app.InvokeResult(url, "openConnection",
                                   "()Ljava/net/URLConnection;");
    REQUIRE(opened.exception.IsValid());
    CHECK(app.Linker().Class(opened.exception_class).descriptor ==
          "Ljava/net/UnknownHostException;");
}
