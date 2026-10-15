#include "boot_dex.h"
#include <doctest/doctest.h>

#include <cstdint>
#include <algorithm>
#include <memory>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include "ogplay/runtime/dexvm/big_int_runtime.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/loader/apk.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/io_runtime.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"
#include "ogplay/runtime/vfs/vfs.h"
#include "ogplay/runtime/integration/dexvm_io_vfs.h"
#include "tls_fixtures.h"

#include "bootdex_library_fixture.h"

using namespace ogplay::test::bootdex_library;
using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

namespace {

constexpr auto kEngine = "Lorg/apache/harmony/security/fortress/Engine;";
constexpr auto kDoorType =
    "Lorg/apache/harmony/security/fortress/SecurityAccess;";
constexpr auto kSecurityDoor = "Ljava/security/Security$SecurityDoor;";

VmObjectRef EngineDoor(Dvm87Vm& f) {
    const auto engine = f.linker.FindClass(kEngine);
    REQUIRE(engine.has_value());
    const auto field = f.linker.FindFieldRecursive(*engine, "door", kDoorType);
    REQUIRE(field.has_value());
    return VmObjectRef(
        f.linker.Class(*engine).static_storage[f.linker.Field(*field).slot]);
}

void ExpectDoor(Dvm87Vm& f) {
    const auto door = EngineDoor(f);
    REQUIRE(door.IsValid());
    CHECK(f.linker.Class(f.model.ObjectClass(door)).descriptor == kSecurityDoor);
}

void CallGetProperty(Dvm87Vm& f) {
    const auto key = f.vm.NewStringUtf8("ssl.SocketFactory.provider");
    const auto roots = f.vm.ProtectReferences(std::array{key});
    f.RequireOk(f.Static("Ljava/security/Security;", "getProperty",
                         "(Ljava/lang/String;)Ljava/lang/String;",
                         {VmValue::Ref(key)}));
}

bool EngineDoorDeclared(Dvm87Vm& f) {
    const auto engine = f.linker.FindClass(kEngine);
    REQUIRE(engine.has_value());
    return f.linker.FindFieldRecursive(*engine, "door", kDoorType).has_value();
}
struct CryptoPrngVm final {
    Dvm87Vm base;
    Interpreter& vm{base.vm};
    JavaObjectModel& model{base.model};
    DexClassLinker& linker{base.linker};
    std::size_t entropy_reads{};
    bool fail_entropy{};
    std::unique_ptr<VfsCharacterDeviceRegistration> entropy_device;
    std::vector<Interpreter::RootScope> roots;

    explicit CryptoPrngVm(InterpreterBackend backend,
                          const std::vector<IntrinsicClassDecl>& extras = {})
        : base(backend, "zh", "zho", "CHN", "GMT", extras) {
        entropy_device = base.vfs.RegisterReadOnlyCharacterDevice(
            "/dev/urandom", 0x109U, [this](std::span<std::byte> bytes) {
                ++entropy_reads;
                if (fail_entropy) throw VfsError(5, "test entropy failure");
                std::fill(bytes.begin(), bytes.end(), std::byte{0x5a});
                return bytes.size();
            });
    }
    static void RequireOk(const VmCallOutcome& outcome) { Dvm87Vm::RequireOk(outcome); }
    VmCallOutcome Static(std::string_view owner, std::string_view name,
                         std::string_view signature, std::vector<VmValue> args = {}) {
        return base.Static(owner, name, signature, std::move(args));
    }
    VmCallOutcome Virtual(VmObjectRef object, std::string_view name,
                          std::string_view signature, std::vector<VmValue> args = {}) {
        return base.Virtual(object, name, signature, std::move(args));
    }
    VmObjectRef Keep(VmObjectRef object) {
        roots.push_back(vm.ProtectReferences(std::array{object}));
        return object;
    }
    static std::vector<std::byte> Decode(std::string_view hex) {
        REQUIRE(hex.size() % 2 == 0);
        std::vector<std::byte> bytes;
        for (std::size_t i = 0; i < hex.size(); i += 2)
            bytes.push_back(static_cast<std::byte>(
                std::stoul(std::string(hex.substr(i, 2)), nullptr, 16)));
        return bytes;
    }
    static std::vector<std::byte> Expected(std::string_view name) {
        std::ifstream file(std::filesystem::path(OGPLAY_SOURCE_DIR) /
                           "tests/fixtures/sha1prng-api19.txt");
        REQUIRE(file.good());
        std::string key, hex;
        while (file >> key >> hex) if (key == name) return Decode(hex);
        FAIL("missing SHA1PRNG oracle vector");
        return {};
    }
    VmValue Invoke(VmObjectRef object, std::string_view name,
                   std::string_view signature, std::vector<VmValue> args = {}) {
        const auto outcome = Virtual(object, name, signature, std::move(args));
        RequireOk(outcome);
        return outcome.value;
    }
    VmObjectRef NewRandom(VmObjectRef provider = VmObjectRef{}) {
        auto args = std::vector{VmValue::Ref(vm.NewStringUtf8("SHA1PRNG")),
                               VmValue::Ref(provider.IsValid() ? provider :
                                   vm.NewStringUtf8("Crypto"))};
        const auto result = Static("Ljava/security/SecureRandom;", "getInstance",
            provider.IsValid() ?
                "(Ljava/lang/String;Ljava/security/Provider;)Ljava/security/SecureRandom;" :
                "(Ljava/lang/String;Ljava/lang/String;)Ljava/security/SecureRandom;", args);
        RequireOk(result);
        return Keep(result.value.ref);
    }
    void Seed(VmObjectRef random, std::string_view hex) {
        const auto bytes = Decode(hex);
        const auto array = vm.Model().NewPrimitiveArray(
            linker.ResolveDescriptor("[B"), JniPrimitiveKind::byte,
            static_cast<JniSize>(bytes.size()));
        vm.Model().WriteByteRegion(array, 0, bytes);
        static_cast<void>(Invoke(random, "setSeed", "([B)V", {VmValue::Ref(array)}));
    }
    std::vector<std::byte> Next(VmObjectRef random, JniSize count) {
        const auto array = vm.Model().NewPrimitiveArray(
            linker.ResolveDescriptor("[B"), JniPrimitiveKind::byte, count);
        const auto root = vm.ProtectReferences(std::array{array});
        static_cast<void>(Invoke(random, "nextBytes", "([B)V", {VmValue::Ref(array)}));
        return vm.Model().ReadByteRegion(array, 0, count);
    }
};


} // namespace

TEST_CASE("DVM NativeCrypto identity and members come from API 19 BootDex") {
    Dvm87Vm fixture;
    const auto type = fixture.linker.ResolveDescriptor(
        "Lcom/android/org/conscrypt/NativeCrypto;");
    const auto& linked = fixture.linker.Class(type);
    CHECK(linked.own_direct_methods.size() == 232U);
    CHECK(linked.own_virtual_methods.size() == 0U);
    CHECK(linked.own_static_fields.size() == 41U);
    CHECK(linked.own_instance_fields.empty());

    const auto clinit = fixture.linker.FindDirectMethod(type, "<clinit>", "()V");
    REQUIRE(clinit.has_value());
    CHECK(fixture.linker.Method(*clinit).kind == MethodKind::interpreted);
    CHECK(fixture.linker.Method(*clinit).code.has_value());
    CHECK_FALSE(static_cast<bool>(fixture.linker.Method(*clinit).implementation));

    const auto digest_final = fixture.linker.FindDirectMethod(
        type, "EVP_DigestFinal", "(J[BI)I");
    REQUIRE(digest_final.has_value());
    CHECK(fixture.linker.Method(*digest_final).kind == MethodKind::native);
    CHECK_FALSE(static_cast<bool>(fixture.linker.Method(*digest_final).implementation));
    CHECK(fixture.linker.FindDirectMethod(
              type, "verify_signature", "([B[B[BLjava/lang/String;)Z") == std::nullopt);

    const auto callbacks = fixture.linker.ResolveDescriptor(
        "Lcom/android/org/conscrypt/NativeCrypto$SSLHandshakeCallbacks;");
    CHECK(fixture.linker.Class(callbacks).is_interface);
}

TEST_CASE("DVM-149 Apache HTTP BootDex supports the Restlet startup object path") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        Dvm87Vm f(backend);

        const auto params =
            f.vm.NewIntrinsicInstance("Lorg/apache/http/params/BasicHttpParams;");
        f.Construct(params, "Lorg/apache/http/params/BasicHttpParams;", "()V");
        const auto key = f.vm.NewStringUtf8("ogplay.dvm149");
        const auto value = f.vm.NewStringUtf8("restlet");
        auto result = f.Virtual(
            params, "setParameter",
            "(Ljava/lang/String;Ljava/lang/Object;)Lorg/apache/http/params/HttpParams;",
            {VmValue::Ref(key), VmValue::Ref(value)});
        f.RequireOk(result);
        CHECK(result.value.ref == params);
        result = f.Virtual(params, "getParameter",
                           "(Ljava/lang/String;)Ljava/lang/Object;",
                           {VmValue::Ref(key)});
        f.RequireOk(result);
        CHECK(result.value.ref == value);

        f.RequireOk(f.Static(
            "Lorg/apache/http/params/HttpConnectionParams;", "setTcpNoDelay",
            "(Lorg/apache/http/params/HttpParams;Z)V",
            {VmValue::Ref(params), VmValue::Int(1)}));
        f.RequireOk(f.Static(
            "Lorg/apache/http/params/HttpConnectionParams;", "setConnectionTimeout",
            "(Lorg/apache/http/params/HttpParams;I)V",
            {VmValue::Ref(params), VmValue::Int(15000)}));
        f.RequireOk(f.Static(
            "Lorg/apache/http/params/HttpConnectionParams;", "setSoTimeout",
            "(Lorg/apache/http/params/HttpParams;I)V",
            {VmValue::Ref(params), VmValue::Int(15000)}));

        const auto registry =
            f.vm.NewIntrinsicInstance("Lorg/apache/http/conn/scheme/SchemeRegistry;");
        f.Construct(registry, "Lorg/apache/http/conn/scheme/SchemeRegistry;", "()V");
        result = f.Static(
            "Lorg/apache/http/conn/scheme/PlainSocketFactory;", "getSocketFactory",
            "()Lorg/apache/http/conn/scheme/PlainSocketFactory;");
        f.RequireOk(result);
        const auto socket_factory = result.value.ref;
        const auto scheme = f.vm.NewIntrinsicInstance("Lorg/apache/http/conn/scheme/Scheme;");
        f.Construct(scheme, "Lorg/apache/http/conn/scheme/Scheme;",
                    "(Ljava/lang/String;Lorg/apache/http/conn/scheme/SocketFactory;I)V",
                    {VmValue::Ref(f.vm.NewStringUtf8("http")),
                     VmValue::Ref(socket_factory), VmValue::Int(80)});
        result = f.Virtual(
            registry, "register",
            "(Lorg/apache/http/conn/scheme/Scheme;)Lorg/apache/http/conn/scheme/Scheme;",
            {VmValue::Ref(scheme)});
        f.RequireOk(result);
        CHECK_FALSE(result.value.ref.IsValid());

        const auto manager = f.vm.NewIntrinsicInstance(
            "Lorg/apache/http/impl/conn/tsccm/ThreadSafeClientConnManager;");
        f.Construct(
            manager,
            "Lorg/apache/http/impl/conn/tsccm/ThreadSafeClientConnManager;",
            "(Lorg/apache/http/params/HttpParams;Lorg/apache/http/conn/scheme/SchemeRegistry;)V",
            {VmValue::Ref(params), VmValue::Ref(registry)});
        const auto client =
            f.vm.NewIntrinsicInstance("Lorg/apache/http/impl/client/DefaultHttpClient;");
        f.Construct(
            client, "Lorg/apache/http/impl/client/DefaultHttpClient;",
            "(Lorg/apache/http/conn/ClientConnectionManager;Lorg/apache/http/params/HttpParams;)V",
            {VmValue::Ref(manager), VmValue::Ref(params)});
        result = f.Virtual(client, "getParams", "()Lorg/apache/http/params/HttpParams;");
        f.RequireOk(result);
        CHECK(result.value.ref == params);

        const auto bytes =
            f.vm.NewIntrinsicInstance("Ljava/io/ByteArrayOutputStream;");
        f.Construct(bytes, "Ljava/io/ByteArrayOutputStream;", "()V");
        const auto writer =
            f.vm.NewIntrinsicInstance("Ljava/io/OutputStreamWriter;");
        f.Construct(writer, "Ljava/io/OutputStreamWriter;",
                    "(Ljava/io/OutputStream;)V", {VmValue::Ref(bytes)});
        const auto message = f.vm.NewStringUtf8("Starting the Apache HTTP client");
        f.RequireOk(f.Virtual(writer, "write", "(Ljava/lang/String;)V",
                              {VmValue::Ref(message)}));
        f.RequireOk(f.Virtual(writer, "flush", "()V"));
        result = f.Virtual(bytes, "toByteArray", "()[B");
        f.RequireOk(result);
        const auto encoded = f.vm.Model().ReadByteRegion(
            result.value.ref, 0, f.vm.Model().ArrayLength(result.value.ref));
        CHECK(std::string(reinterpret_cast<const char*>(encoded.data()),
                          encoded.size()) == "Starting the Apache HTTP client");

        const auto https_class = f.linker.ResolveDescriptor(
            "Ljavax/net/ssl/HttpsURLConnection;");
        const auto https_getter = f.linker.FindDirectMethod(
            https_class, "getDefaultSSLSocketFactory",
            "()Ljavax/net/ssl/SSLSocketFactory;");
        REQUIRE(https_getter.has_value());
        CHECK(f.linker.Method(*https_getter).kind == MethodKind::interpreted);
        const auto url_connection = f.linker.ResolveDescriptor(
            "Ljava/net/URLConnection;");
        const auto timeout = f.linker.FindVtableIndex(
            url_connection, "setConnectTimeout", "(I)V");
        REQUIRE(timeout.has_value());
        CHECK(f.linker.Method(
                  f.linker.Class(url_connection).vtable[*timeout]).kind ==
              MethodKind::interpreted);
        result = f.Static(
            "Ljavax/net/ssl/HttpsURLConnection;",
            "setDefaultSSLSocketFactory",
            "(Ljavax/net/ssl/SSLSocketFactory;)V",
            {VmValue::Ref(VmObjectRef{})});
        REQUIRE(result.exception.IsValid());
        CHECK(f.linker.Class(result.exception_class).descriptor ==
              "Ljava/lang/IllegalArgumentException;");
        result = f.Static(
            "Ljavax/net/ssl/HttpsURLConnection;",
            "getDefaultHostnameVerifier",
            "()Ljavax/net/ssl/HostnameVerifier;");
        f.RequireOk(result);
        CHECK(result.value.ref.IsValid());
        result = f.Static(
            "Ljavax/net/ssl/HttpsURLConnection;",
            "getDefaultSSLSocketFactory",
            "()Ljavax/net/ssl/SSLSocketFactory;");
        REQUIRE(result.exception.IsValid());
        CHECK(f.linker.Class(result.exception_class).descriptor ==
              "Ljava/lang/UnsatisfiedLinkError;");

        result = f.Static("Ljavax/net/ssl/SSLContext;", "getInstance",
                          "(Ljava/lang/String;)Ljavax/net/ssl/SSLContext;",
                          {VmValue::Ref(f.vm.NewStringUtf8("TLS"))});
        f.RequireOk(result);
        const auto ssl_init = f.Virtual(
            result.value.ref, "init",
            "([Ljavax/net/ssl/KeyManager;[Ljavax/net/ssl/TrustManager;"
            "Ljava/security/SecureRandom;)V",
            {VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{}),
             VmValue::Ref(VmObjectRef{})});
        REQUIRE(ssl_init.exception.IsValid());
        CHECK(f.linker.Class(ssl_init.exception_class).descriptor ==
              "Ljava/lang/UnsatisfiedLinkError;");
    }
}

TEST_CASE("TLS-01 TrustManagerFactory and AndroidCAStore use the injected pack") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        CAPTURE(backend == InterpreterBackend::threaded ? "threaded" : "switch");
        Dvm87Vm f(backend);
        auto algorithm = f.Static(
            "Ljavax/net/ssl/TrustManagerFactory;", "getDefaultAlgorithm",
            "()Ljava/lang/String;");
        f.RequireOk(algorithm);
        CHECK(f.vm.StringUtf8(algorithm.value.ref) == "PKIX");
        auto factory = f.Static(
            "Ljavax/net/ssl/TrustManagerFactory;", "getInstance",
            "(Ljava/lang/String;)Ljavax/net/ssl/TrustManagerFactory;",
            {VmValue::Ref(f.vm.NewStringUtf8("PKIX"))});
        f.RequireOk(factory);
        auto alias = f.Static(
            "Ljavax/net/ssl/TrustManagerFactory;", "getInstance",
            "(Ljava/lang/String;)Ljavax/net/ssl/TrustManagerFactory;",
            {VmValue::Ref(f.vm.NewStringUtf8("X509"))});
        f.RequireOk(alias);
        auto params = f.Virtual(
            factory.value.ref, "init",
            "(Ljavax/net/ssl/ManagerFactoryParameters;)V",
            {VmValue::Ref(VmObjectRef{})});
        REQUIRE(params.exception.IsValid());
        CHECK(f.linker.Class(params.exception_class).descriptor ==
              "Ljava/security/InvalidAlgorithmParameterException;");

        auto store = f.Static(
            "Ljava/security/KeyStore;", "getInstance",
            "(Ljava/lang/String;)Ljava/security/KeyStore;",
            {VmValue::Ref(f.vm.NewStringUtf8("AndroidCAStore"))});
        f.RequireOk(store);
        auto write = f.Virtual(
            store.value.ref, "setCertificateEntry",
            "(Ljava/lang/String;Ljava/security/cert/Certificate;)V",
            {VmValue::Ref(f.vm.NewStringUtf8("user:test")),
             VmValue::Ref(VmObjectRef{})});
        REQUIRE(write.exception.IsValid());
        CHECK(f.linker.Class(write.exception_class).descriptor ==
              "Ljava/security/KeyStoreException;");
        auto remove = f.Virtual(store.value.ref, "deleteEntry",
                                "(Ljava/lang/String;)V",
                                {VmValue::Ref(f.vm.NewStringUtf8("system:x"))});
        REQUIRE(remove.exception.IsValid());
        CHECK(f.linker.Class(remove.exception_class).descriptor ==
              "Ljava/security/KeyStoreException;");

        auto empty = f.Static(
            "Ljava/security/KeyStore;", "getInstance",
            "(Ljava/lang/String;)Ljava/security/KeyStore;",
            {VmValue::Ref(f.vm.NewStringUtf8("BKS"))});
        f.RequireOk(empty);
        f.RequireOk(f.Virtual(empty.value.ref, "load",
                              "(Ljava/io/InputStream;[C)V",
                              {VmValue::Ref(VmObjectRef{}),
                               VmValue::Ref(VmObjectRef{})}));
        auto empty_factory = f.Static(
            "Ljavax/net/ssl/TrustManagerFactory;", "getInstance",
            "(Ljava/lang/String;)Ljavax/net/ssl/TrustManagerFactory;",
            {VmValue::Ref(f.vm.NewStringUtf8("PKIX"))});
        f.RequireOk(empty_factory);
        f.RequireOk(f.Virtual(empty_factory.value.ref, "init",
                              "(Ljava/security/KeyStore;)V",
                              {VmValue::Ref(empty.value.ref)}));
        auto empty_managers = f.Virtual(
            empty_factory.value.ref, "getTrustManagers",
            "()[Ljavax/net/ssl/TrustManager;");
        f.RequireOk(empty_managers);
        auto empty_issuers = f.Virtual(
            f.model.GetObjectElement(empty_managers.value.ref, 0),
            "getAcceptedIssuers", "()[Ljava/security/cert/X509Certificate;");
        f.RequireOk(empty_issuers);
        CHECK(f.model.ArrayLength(empty_issuers.value.ref) == 0);

        auto unknown = f.Static(
            "Ljavax/net/ssl/TrustManagerFactory;", "getInstance",
            "(Ljava/lang/String;Ljava/lang/String;)Ljavax/net/ssl/TrustManagerFactory;",
            {VmValue::Ref(f.vm.NewStringUtf8("PKIX")),
             VmValue::Ref(f.vm.NewStringUtf8("NoSuchProvider"))});
        REQUIRE(unknown.exception.IsValid());
        CHECK(f.linker.Class(unknown.exception_class).descriptor ==
              "Ljava/security/NoSuchProviderException;");
        auto engine = f.Static(
            "Ljavax/net/ssl/SSLContext;", "getInstance",
            "(Ljava/lang/String;)Ljavax/net/ssl/SSLContext;",
            {VmValue::Ref(f.vm.NewStringUtf8("TLS"))});
        f.RequireOk(engine);
        auto ssl_engine = f.Virtual(engine.value.ref, "createSSLEngine",
                                    "()Ljavax/net/ssl/SSLEngine;");
        REQUIRE(ssl_engine.exception.IsValid());
        CHECK(f.linker.Class(ssl_engine.exception_class).descriptor ==
              "Ljava/lang/IllegalStateException;");
    }
}

TEST_CASE("DVM-173/174 KeyStore Java closure keeps state in the guest object graph") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        Dvm87Vm f(backend);
        const auto default_type = f.Static(
            "Ljava/security/KeyStore;", "getDefaultType", "()Ljava/lang/String;");
        f.RequireOk(default_type);
        CHECK(f.vm.StringUtf8(default_type.value.ref) == "BKS");
        const auto public_store = f.Static(
            "Ljava/security/KeyStore;", "getInstance",
            "(Ljava/lang/String;)Ljava/security/KeyStore;",
            {VmValue::Ref(f.vm.NewStringUtf8("BKS"))});
        f.RequireOk(public_store);
        const auto provider = f.Virtual(
            public_store.value.ref, "getProvider", "()Ljava/security/Provider;");
        f.RequireOk(provider);
        const auto provider_name = f.Virtual(
            provider.value.ref, "getName", "()Ljava/lang/String;");
        f.RequireOk(provider_name);
        CHECK(f.vm.StringUtf8(provider_name.value.ref) == "OGPlayKeyStore");
        f.RequireOk(f.Virtual(
            public_store.value.ref, "load", "(Ljava/io/InputStream;[C)V",
            {VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{})}));
        const auto store = f.vm.NewIntrinsicInstance(
            "Lorg/ogplay/security/BksKeyStoreSpi;");
        f.Construct(store, "Lorg/ogplay/security/BksKeyStoreSpi;", "()V");
        f.RequireOk(f.Virtual(
            store, "engineLoad", "(Ljava/io/InputStream;[C)V",
            {VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{})}));
        auto size = f.Virtual(store, "engineSize", "()I");
        f.RequireOk(size);
        CHECK(size.value.AsInt() == 0);

        const auto empty = f.model.NewPrimitiveArray(
            f.linker.ResolveDescriptor("[B"), JniPrimitiveKind::byte, 0);
        const auto input = f.vm.NewIntrinsicInstance("Ljava/io/ByteArrayInputStream;");
        f.Construct(input, "Ljava/io/ByteArrayInputStream;", "([B)V",
                    {VmValue::Ref(empty)});
        const auto failed_load = f.Virtual(
            store, "engineLoad", "(Ljava/io/InputStream;[C)V",
            {VmValue::Ref(input), VmValue::Ref(VmObjectRef{})});
        REQUIRE(failed_load.exception.IsValid());
        CHECK(f.linker.Class(failed_load.exception_class).descriptor ==
              "Ljava/io/IOException;");
        size = f.Virtual(store, "engineSize", "()I");
        f.RequireOk(size);
        CHECK(size.value.AsInt() == 0);

    }
}

TEST_CASE("DVM-104 NativeBN tokens are isolated checked and swept with BigInt owners") {
    Dvm87Vm f;
    const auto baseline=f.vm.BigInts().Size();
    const auto owner=f.vm.NewIntrinsicInstance("Ljava/math/BigInt;");f.Construct(owner,"Ljava/math/BigInt;","()V");
    auto call=[&](const char* name,const char* sig,std::vector<VmValue> args){return f.Virtual(owner,name,sig,args);};
    f.RequireOk(call("putLongInt","(J)V",{VmValue::Long(INT64_MIN)}));
    const auto field=f.linker.FindFieldRecursive(f.model.ObjectClass(owner),"bignum","J");REQUIRE(field);
    const auto slot=f.linker.Field(*field).slot;const auto slots=f.model.InstanceSlots(owner);
    const auto token=static_cast<std::uint64_t>(slots[slot].bits)|(static_cast<std::uint64_t>(slots[slot+1].bits)<<32U);
    CHECK(f.vm.BigInts().Require(token).LowLong()==(UINT64_C(1)<<63U));
    CHECK(f.vm.BigInts().Size()==baseline+1);
    const auto unsupported=f.Static("Ljava/math/NativeBN;", "BN_gcd", "(JJJ)V", {VmValue::Long(static_cast<std::int64_t>(token)),VmValue::Long(static_cast<std::int64_t>(token)),VmValue::Long(static_cast<std::int64_t>(token))}); REQUIRE(unsupported.exception.IsValid());

    Dvm87Vm other;CHECK_THROWS(other.vm.BigInts().Require(token));
    static_cast<void>(f.vm.CollectGarbage());CHECK(f.vm.BigInts().Size()==baseline);CHECK_THROWS(f.vm.BigInts().Require(token));
}

TEST_CASE("DVM-106 BigInteger certificate values preserve long signed encodings") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        Dvm87Vm f(backend);
        struct Case {
            const char* bytes;
            const char* decimal;
            const char* hex;
            int bits;
        };
        for (const auto& value : std::array{
                 Case{"00", "0", "0", 0},
                 Case{"01", "1", "1", 1},
                 Case{"ff", "-1", "-1", 0},
                 Case{"7f", "127", "7f", 7},
                 Case{"0080", "128", "80", 8},
                 Case{"80", "-128", "-80", 7},
                 Case{"ff7f", "-129", "-81", 8},
                 Case{"0100000000", "4294967296", "100000000", 33},
                 Case{"ff00000000", "-4294967296", "-100000000", 32},
                 Case{"00ffffffffffffffff", "18446744073709551615", "ffffffffffffffff", 64},
                 Case{"010000000000000000", "18446744073709551616", "10000000000000000", 65},
                 Case{"ff0000000000000000", "-18446744073709551616", "-10000000000000000", 64},
                 Case{"00ffffffffffffffffffffffffffffffffffffffff",
                      "1461501637330902918203684832716283019655932542975",
                      "ffffffffffffffffffffffffffffffffffffffff", 160},
                 Case{"8000000000000000000000000000000000000000",
                      "-730750818665451459101842416358141509827966271488",
                      "-8000000000000000000000000000000000000000", 159},
                 Case{"1234567890abcdef1234567890abcdef12345678",
                      "103929005307927756724354605802047639613112342136",
                      "1234567890abcdef1234567890abcdef12345678", 157},
             }) {
            CAPTURE(value.decimal);
            std::vector<std::byte> data;
            for (std::size_t i = 0; value.bytes[i]; i += 2)
                data.push_back(static_cast<std::byte>(
                    std::stoul(std::string(value.bytes + i, 2), nullptr, 16)));
            const auto array =
                f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[B"), JniPrimitiveKind::byte,
                                          static_cast<JniSize>(data.size()));
            f.model.WriteByteRegion(array, 0, data);
            const auto array_root = f.vm.ProtectReferences(std::array{array});
            const auto integer = f.vm.NewIntrinsicInstance("Ljava/math/BigInteger;");
            const auto root = f.vm.ProtectReferences(std::array{integer});
            f.Construct(integer, "Ljava/math/BigInteger;", "([B)V", {VmValue::Ref(array)});
            const auto decimal = f.Virtual(integer, "toString", "()Ljava/lang/String;");
            f.RequireOk(decimal);
            CHECK(f.vm.StringUtf8(decimal.value.ref) == value.decimal);
            CHECK(f.Virtual(integer, "bitLength", "()I").value.AsInt() == value.bits);
            const auto output = f.Virtual(integer, "toByteArray", "()[B");
            f.RequireOk(output);
            CHECK(f.model.ReadByteRegion(output.value.ref, 0,
                                         f.model.ArrayLength(output.value.ref)) == data);
            const auto hex =
                f.Virtual(integer, "toString", "(I)Ljava/lang/String;", {VmValue::Int(16)});
            f.RequireOk(hex);
            CHECK(f.vm.StringUtf8(hex.value.ref) == value.hex);
            const auto from_decimal = f.vm.NewIntrinsicInstance("Ljava/math/BigInteger;");
            const auto decimal_root = f.vm.ProtectReferences(std::array{from_decimal});
            f.Construct(from_decimal, "Ljava/math/BigInteger;", "(Ljava/lang/String;)V",
                        {VmValue::Ref(f.vm.NewStringUtf8(value.decimal))});
            const auto parsed = f.Virtual(from_decimal, "toByteArray", "()[B");
            f.RequireOk(parsed);
            CHECK(f.model.ReadByteRegion(parsed.value.ref, 0,
                                         f.model.ArrayLength(parsed.value.ref)) == data);
            static_cast<void>(f.vm.CollectGarbage());
            CHECK(
                f.vm.StringUtf8(f.Virtual(integer, "toString", "()Ljava/lang/String;").value.ref) ==
                value.decimal);
        }
    }
}

TEST_CASE("NativeBN decimal input preserves prefix count sign and bounded failure") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        Dvm87Vm f(backend);
        const auto allocated = f.Static("Ljava/math/NativeBN;", "BN_new", "()J", {});
        f.RequireOk(allocated);
        const auto token = allocated.value;
        const auto parse = [&](const std::string& text) {
            return f.Static("Ljava/math/NativeBN;", "BN_dec2bn", "(JLjava/lang/String;)I",
                            {token, VmValue::Ref(f.vm.NewStringUtf8(text))});
        };
        for (const auto& value : std::array{
                 std::pair{"-0000", "0"}, std::pair{"0000123x", "123"},
                 std::pair{"-18446744073709551617!", "-18446744073709551617"}}) {
            const auto parsed = parse(value.first);
            f.RequireOk(parsed);
            const auto expected_count = std::string(value.first).find_first_of("x!");
            CHECK(parsed.value.AsInt() == static_cast<int>(expected_count == std::string::npos
                ? std::string(value.first).size() : expected_count));
            CHECK(f.vm.BigInts().Require(static_cast<std::uint64_t>(token.AsLong())).String(10)
                  == value.second);
        }
        for (const auto* invalid : {"", "-", "+1", "x1"}) {
            const auto result = parse(invalid);
            f.RequireOk(result);
            CHECK(result.value.AsInt() == 0);
        }
        const auto null = f.Static("Ljava/math/NativeBN;", "BN_dec2bn", "(JLjava/lang/String;)I",
                                   {token, VmValue::Ref(VmObjectRef{})});
        REQUIRE(null.exception.IsValid());
        CHECK(f.linker.Class(null.exception_class).descriptor == "Ljava/lang/NullPointerException;");
        for (const auto size : {200000U, 1048577U}) {
            const auto rejected = parse(std::string(size, '9'));
            REQUIRE(rejected.exception.IsValid());
            CHECK(f.linker.Class(rejected.exception_class).descriptor
                  == "Ljava/lang/UnsupportedOperationException;");
        }
        CHECK(f.vm.BigInts().Require(static_cast<std::uint64_t>(token.AsLong())).String(10)
              == "-18446744073709551617");
        f.vm.BigInts().Free(static_cast<std::uint64_t>(token.AsLong()));
    }
}

TEST_CASE("DVM-106 NativeBN unsigned and limb codecs preserve magnitude and validate length") {
    Dvm87Vm f;
    const auto native = [&](const char* name,const char* sig,std::vector<VmValue> args) {
        auto result=f.Static("Ljava/math/NativeBN;",name,sig,args); f.RequireOk(result); return result.value;
    };
    const auto token=native("BN_new","()J",{});
    const auto bytes=f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[B"),JniPrimitiveKind::byte,9);
    const auto bytes_root=f.vm.ProtectReferences(std::array{bytes});
    const std::vector<std::byte> value{std::byte{0x80},std::byte{},std::byte{},std::byte{},std::byte{},std::byte{},std::byte{},std::byte{},std::byte{1}};
    f.model.WriteByteRegion(bytes,0,value);
    native("BN_bin2bn","([BIZJ)V",{VmValue::Ref(bytes),VmValue::Int(9),VmValue::Int(1),token});
    CHECK(f.vm.StringUtf8(native("BN_bn2dec","(J)Ljava/lang/String;",{token}).ref)=="-2361183241434822606849");
    CHECK(f.vm.StringUtf8(native("BN_bn2hex","(J)Ljava/lang/String;",{token}).ref)=="-800000000000000001");
    const auto output=native("BN_bn2bin","(J)[B",{token}).ref;
    CHECK(f.model.ReadByteRegion(output,0,f.model.ArrayLength(output))==value);
    const auto invalid=f.Static("Ljava/math/NativeBN;","BN_bin2bn","([BIZJ)V",{VmValue::Ref(bytes),VmValue::Int(10),VmValue::Int(0),token});
    REQUIRE(invalid.exception.IsValid());
    CHECK(f.linker.Class(invalid.exception_class).descriptor=="Ljava/lang/ArrayIndexOutOfBoundsException;");
    CHECK(f.vm.StringUtf8(native("BN_bn2dec","(J)Ljava/lang/String;",{token}).ref)=="-2361183241434822606849");
    const auto words=f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[I"),JniPrimitiveKind::integer,4);
    f.model.SetPrimitiveElement(words,0,UINT32_MAX);
    f.model.SetPrimitiveElement(words,3,1);
    native("litEndInts2bn","([IIZJ)V",{VmValue::Ref(words),VmValue::Int(4),VmValue::Int(0),token});
    CHECK(f.vm.StringUtf8(native("BN_bn2hex","(J)Ljava/lang/String;",{token}).ref)=="10000000000000000FFFFFFFF");
    CHECK(native("longInt","(J)J",{token}).AsLong()==UINT32_MAX);
    native("putLongInt","(JJ)V",{token,VmValue::Long(0)});
    CHECK(f.model.ArrayLength(native("BN_bn2bin","(J)[B",{token}).ref)==0);
    native("BN_free","(J)V",{token});
}

TEST_CASE("SetIntrinsicStaticRef links BootDex statics without running clinit") {
    Dvm87Vm f;
    CHECK_FALSE(EngineDoorDeclared(f));
    const auto engine = f.linker.FindClass(kEngine);
    f.linker.EnsureClassLinked(*engine);
    CHECK(f.linker.Class(*engine).clinit_state == ClinitState::uninitialized);
    const auto missing = std::string(kEngine) + ".missing " + kDoorType;
    try {
        f.vm.SetIntrinsicStaticRef(kEngine, "missing", kDoorType, VmObjectRef{});
        FAIL("expected missing-field DexVmError");
    } catch (const DexVmError& error) {
        CHECK(error.Reason() == DexVmErrorReason::invalid_member);
        CHECK(std::string(error.what()) ==
              "intrinsic static field is not declared: " + missing);
    }
    const auto instance = std::string(kEngine) + ".serviceName Ljava/lang/String;";
    try {
        f.vm.SetIntrinsicStaticRef(kEngine, "serviceName", "Ljava/lang/String;",
                                   VmObjectRef{});
        FAIL("expected non-static DexVmError");
    } catch (const DexVmError& error) {
        CHECK(error.Reason() == DexVmErrorReason::invalid_member);
        CHECK(std::string(error.what()) ==
              "intrinsic static target is not static: " + instance);
    }
    CHECK(f.linker.Class(*engine).clinit_state == ClinitState::uninitialized);
}

TEST_CASE("Security.getProperty writes Engine.door on a cold VM") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        CAPTURE(backend == InterpreterBackend::threaded ? "threaded" : "switch");
        Dvm87Vm f(backend);
        CHECK_FALSE(EngineDoorDeclared(f));
        CallGetProperty(f);
        ExpectDoor(f);
        const auto door_id = EngineDoor(f).Value();
        static_cast<void>(f.vm.CollectGarbage("security-engine-door"));
        CHECK(EngineDoor(f).Value() == door_id);
        ExpectDoor(f);
    }
}

TEST_CASE("Security.getProperty still writes Engine.door after other JCA classes") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        CAPTURE(backend == InterpreterBackend::threaded ? "threaded" : "switch");
        Dvm87Vm f(backend);
        for (const auto* type :
             {"Ljavax/crypto/Cipher;", "Ljavax/crypto/Mac;"}) {
            f.RequireOk(f.vm.EnsureClassInitialized(f.linker.ResolveDescriptor(type)));
        }
        CHECK(EngineDoorDeclared(f));
        CHECK_FALSE(EngineDoor(f).IsValid());
        CallGetProperty(f);
        ExpectDoor(f);
    }
}

TEST_CASE("guest worker first Security.getProperty writes Engine.door") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        CAPTURE(backend == InterpreterBackend::threaded ? "threaded" : "switch");
        auto worker = IntrinsicClassBuilder::Class("Ltest/SecurityDoorWorker;",
                                                   "Ljava/lang/Thread;");
        worker.OverrideMethod("run", "()V", [](IntrinsicContext& context) {
            const auto type = context.vm.Linker().ResolveDescriptor(
                "Ljava/security/Security;");
            const auto method = context.vm.Linker().FindDirectMethod(
                type, "getProperty", "(Ljava/lang/String;)Ljava/lang/String;");
            if (!method.has_value()) {
                throw DexVmError(DexVmErrorReason::unresolved_reference,
                                 "Security.getProperty is missing");
            }
            const auto key =
                context.vm.NewStringUtf8("ssl.SocketFactory.provider");
            const auto roots = context.vm.ProtectReferences(std::array{key});
            const auto outcome =
                context.vm.Call(*method, std::array{VmValue::Ref(key)});
            if (outcome.exception.IsValid()) {
                throw VmJavaThrow{
                    context.vm.Linker().Class(outcome.exception_class)
                        .descriptor,
                    outcome.exception_message, outcome.exception};
            }
            return VmValue::Void();
        });
        Dvm87Vm f(backend, "en", "eng", "USA", "GMT",
                  {std::move(worker).Build()});
        CHECK_FALSE(EngineDoorDeclared(f));
        const auto thread = f.vm.NewIntrinsicInstance("Ltest/SecurityDoorWorker;");
        const auto roots = f.vm.ProtectReferences(std::array{thread});
        f.Construct(thread, "Ljava/lang/Thread;", "()V");
        f.RequireOk(f.Virtual(thread, "start", "()V"));
        f.RequireOk(f.Virtual(thread, "join", "()V"));
        CHECK_FALSE(f.threads.TakeFailure().has_value());
        ExpectDoor(f);
        static_cast<void>(f.vm.CollectGarbage("security-engine-door-worker"));
        ExpectDoor(f);
    }
}

TEST_CASE("DVM-208 Crypto SHA1PRNG preserves API19 seeded streams and Provider identity") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        CryptoPrngVm f(backend);
        for (auto name : {"SHA1PRNG_SecureRandomImpl", "SHA1Impl", "SHA1Constants"}) {
            const auto type = f.linker.ResolveDescriptor(
                std::string("Lorg/apache/harmony/security/provider/crypto/") + name + ";");
            CHECK(f.linker.Class(type).is_boot_dex);
            for (auto method : f.linker.Class(type).own_direct_methods)
                CHECK(f.linker.Method(method).kind != MethodKind::intrinsic);
            for (auto method : f.linker.Class(type).own_virtual_methods)
                CHECK(f.linker.Method(method).kind != MethodKind::intrinsic);
        }
        const auto first = f.NewRandom();
        const auto provider = f.Keep(f.Invoke(first, "getProvider", "()Ljava/security/Provider;").ref);
        CHECK(f.vm.StringUtf8(f.Invoke(provider, "getName", "()Ljava/lang/String;").ref) == "Crypto");
        const auto services = f.Keep(f.Invoke(provider, "getServices", "()Ljava/util/Set;").ref);
        CHECK(f.Invoke(services, "size", "()I").AsInt() == 1);
        const auto discovered = f.Static("Ljava/security/Security;", "getProviders",
            "(Ljava/lang/String;)[Ljava/security/Provider;",
            {VmValue::Ref(f.vm.NewStringUtf8("SecureRandom.SHA1PRNG"))});
        f.RequireOk(discovered);
        bool found{};
        for (JniSize i = 0; i < f.model.ArrayLength(discovered.value.ref); ++i)
            if (f.model.GetObjectElement(discovered.value.ref, i) == provider) found = true;
        CHECK(found);
        const auto default_random = f.Static("Ljava/security/SecureRandom;", "getInstance",
            "(Ljava/lang/String;)Ljava/security/SecureRandom;",
            {VmValue::Ref(f.vm.NewStringUtf8("SHA1PRNG"))});
        f.RequireOk(default_random);
        CHECK(f.vm.StringUtf8(f.Invoke(f.Invoke(default_random.value.ref, "getProvider",
            "()Ljava/security/Provider;").ref, "getName", "()Ljava/lang/String;").ref) == "AndroidOpenSSL");

        f.Seed(first, "0001020304050607");
        const auto expected = f.Expected("seed8_128");
        CHECK(f.Next(first, 128) == expected);
        const auto second = f.NewRandom(provider);
        f.Seed(second, "0001020304050607");
        auto split = f.Next(second, 7);
        static_cast<void>(f.vm.CollectGarbage("crypto-prng-continuation"));
        const auto tail = f.Next(second, 121);
        split.insert(split.end(), tail.begin(), tail.end());
        CHECK(split == expected);
        const auto long_seed = f.NewRandom();
        f.Seed(long_seed, "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f");
        CHECK(f.Next(long_seed, 64) == f.Expected("seed64_64"));
        const auto append = f.NewRandom();
        f.Seed(append, "0001020304050607"); f.Seed(append, "ff00");
        CHECK(f.Next(append, 64) == f.Expected("append_before_64"));
        const auto reseed = f.NewRandom();
        f.Seed(reseed, "0001020304050607"); static_cast<void>(f.Next(reseed, 21)); f.Seed(reseed, "ff00");
        CHECK(f.Next(reseed, 43) == f.Expected("reseed_after_43"));
        const auto empty = f.NewRandom(); f.Seed(empty, "");
        CHECK(f.Next(empty, 40) == f.Expected("empty_seed_40"));
        CHECK(f.entropy_reads == 0);

        const auto generator_result = f.Static("Ljavax/crypto/KeyGenerator;", "getInstance",
            "(Ljava/lang/String;)Ljavax/crypto/KeyGenerator;", {VmValue::Ref(f.vm.NewStringUtf8("AES"))});
        f.RequireOk(generator_result);
        const auto generator = f.Keep(generator_result.value.ref);
        for (int i = 0; i < 2; ++i) {
            const auto random = f.NewRandom(); f.Seed(random, "0001020304050607");
            static_cast<void>(f.Invoke(generator, "init", "(ILjava/security/SecureRandom;)V",
                {VmValue::Int(128), VmValue::Ref(random)}));
            const auto key = f.Keep(f.Invoke(generator, "generateKey", "()Ljavax/crypto/SecretKey;").ref);
            const auto encoded = f.Invoke(key, "getEncoded", "()[B").ref;
            CHECK(f.model.ReadByteRegion(encoded, 0, 16) ==
                  std::vector<std::byte>(expected.begin(), expected.begin() + 16));
        }
    }
}

TEST_CASE("DVM-208 Crypto SHA1PRNG obtains entropy through VFS and propagates failure") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        CryptoPrngVm f(backend);
        const auto random = f.NewRandom();
        CHECK(f.entropy_reads == 0);
        CHECK(f.Next(random, 20).size() == 20);
        CHECK(f.entropy_reads == 1);
        static_cast<void>(f.Next(random, 21)); CHECK(f.entropy_reads == 1);
        const auto seed = f.Invoke(random, "generateSeed", "(I)[B", {VmValue::Int(20)}).ref;
        CHECK(f.model.ArrayLength(seed) == 20); CHECK(f.entropy_reads == 2);
        const auto zero = f.Invoke(random, "generateSeed", "(I)[B", {VmValue::Int(0)}).ref;
        CHECK(f.model.ArrayLength(zero) == 0); CHECK(f.entropy_reads == 2);
        const auto negative = f.Virtual(random, "generateSeed", "(I)[B", {VmValue::Int(-1)});
        REQUIRE(negative.exception.IsValid());
        CHECK(f.linker.Class(negative.exception_class).descriptor == "Ljava/lang/NegativeArraySizeException;");
        const auto null_seed = f.Virtual(random, "setSeed", "([B)V", {VmValue::Ref(VmObjectRef{})});
        REQUIRE(null_seed.exception.IsValid());
        CHECK(f.linker.Class(null_seed.exception_class).descriptor == "Ljava/lang/NullPointerException;");
        f.fail_entropy = true;
        const auto failed = f.Virtual(f.NewRandom(), "nextBytes", "([B)V", {
            VmValue::Ref(f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[B"), JniPrimitiveKind::byte, 20))});
        REQUIRE(failed.exception.IsValid());
        CHECK(f.linker.Class(failed.exception_class).descriptor == "Ljava/security/ProviderException;");
        const auto cause = f.Invoke(failed.exception, "getCause", "()Ljava/lang/Throwable;").ref;
        REQUIRE(cause.IsValid());
        CHECK(f.linker.IsAssignable(f.linker.ResolveDescriptor("Ljava/io/IOException;"), f.model.ObjectClass(cause)));
    }
}

TEST_CASE("DVM-208 Crypto SHA1PRNG instances are isolated across guest threads") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        auto action = std::make_shared<std::function<void()>>();
        auto worker_class = IntrinsicClassBuilder::Class(
            "Ltest/CryptoPrngWorker;", "Ljava/lang/Thread;");
        worker_class.OverrideMethod("run", "()V", [action](IntrinsicContext&) {
            (*action)();
            return VmValue::Void();
        });
        CryptoPrngVm f(backend, {std::move(worker_class).Build()});
        const auto main_random = f.NewRandom(); f.Seed(main_random, "0001020304050607");
        auto main_output = f.Next(main_random, 7);
        std::vector<std::byte> worker_output;
        *action = [&] {
            const auto result = f.Static("Ljava/security/SecureRandom;", "getInstance",
                "(Ljava/lang/String;Ljava/lang/String;)Ljava/security/SecureRandom;",
                {VmValue::Ref(f.vm.NewStringUtf8("SHA1PRNG")),
                 VmValue::Ref(f.vm.NewStringUtf8("Crypto"))});
            f.RequireOk(result);
            const auto random = result.value.ref;
            // RootScope must be released on the execution context that owns it.
            const auto root = f.vm.ProtectReferences(std::array{random});
            f.Seed(random, "0001020304050607");
            worker_output = f.Next(random, 128);
        };
        const auto worker = f.Keep(f.vm.NewIntrinsicInstance("Ltest/CryptoPrngWorker;"));
        f.base.Construct(worker, "Ljava/lang/Thread;", "()V");
        static_cast<void>(f.Invoke(worker, "start", "()V"));
        static_cast<void>(f.Invoke(worker, "join", "()V"));
        REQUIRE_FALSE(f.base.threads.TakeFailure().has_value());
        CHECK(worker_output == f.Expected("seed8_128"));
        const auto tail = f.Next(main_random, 121);
        main_output.insert(main_output.end(), tail.begin(), tail.end());
        CHECK(main_output == worker_output);
        CHECK(f.entropy_reads == 0);
    }
}
