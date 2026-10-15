#include "ogplay/runtime/integration/bitmap_pixels.h"
#include "runtime/boundary/core/boundary_symbols.h"
#include "ogplay/runtime/dexvm/big_int_runtime.h"
#include <set>
#include "ogplay/runtime/integration/native_activity_runtime.h"
#include "ogplay/runtime/dexvm/nio_runtime.h"
#include "runtime/integration/dexvm_android/shared.h"
#include "../fixtures/bitmap_decode_data.h"
#include <chrono>
#include <atomic>
#include "ogplay/runtime/vfs/sandbox_store.h"
#include "../dexvm/boot_dex.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <future>
#include "ogplay/runtime/dexvm/vm_monitors.h"
#include <thread>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include <doctest/doctest.h>
#include "ogplay/runtime/dexvm/reflection.h"

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/encoding.h"
#include "ogplay/core/logger.h"
#include "ogplay/loader/elf.h"
#include "ogplay/runtime/dexvm/access_flags.h"
#include "ogplay/runtime/dexvm/class_loader_facade.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/integration/dexvm_bridge.h"
#include "ogplay/runtime/integration/native_library_loader.h"
#include "ogplay/runtime/integration/apk_assets.h"
#include "ogplay/runtime/jni/jni.h"
#include "ogplay/runtime/jni/jni_java_vm.h"
#include "ogplay/runtime/jni/jni_field_store.h"
#include "ogplay/runtime/jni/jni_native_registry.h"
#include "ogplay/runtime/jni/jni_object_array.h"
#include "ogplay/runtime/jni_guest/jni_guest_abi.h"
#include "ogplay/runtime/jni_guest/jni_guest_static_calls.h"
#include "ogplay/runtime/jni_guest/jni_guest_bindings.h"
#include "ogplay/cpu/interpreter.h"
#include "ogplay/memory/bus.h"
#include "ogplay/session/android_app_process.h"
#include "ogplay/session/dex_activity_lifecycle.h"
#include "ogplay/video/fake_video_player.h"

#include "native_library_fixture.h"

using namespace ogplay::test::native_library;

TEST_CASE("DVM-171 failed unified crypto JNI initialization aborts process creation") {
    using namespace ogplay;
    runtime::VirtualFileSystem filesystem;
    core::CapabilityLedger ledger;
    core::Logger logger;
    std::vector<std::vector<std::byte>> contents;
    std::vector<runtime::BionicModuleSource> libraries;
    for (const auto name : {"libc.so", "libm.so", "libdl.so", "libstdc++.so", "libz.so",
                            "libcrypto.so", "libssl.so", "libgabi++.so", "libicui18n.so", "libicuuc.so",
                            "libstlport.so"}) {
        std::ifstream stream(std::string(OGPLAY_SOURCE_DIR) + "/data/android/19/lib/" + name,
                             std::ios::binary);
        REQUIRE_MESSAGE(stream.good(), name);
        std::vector<char> data{std::istreambuf_iterator<char>(stream), {}};
        contents.emplace_back(data.size());
        std::transform(data.begin(), data.end(), contents.back().begin(),
                       [](char c) { return static_cast<std::byte>(c); });
        libraries.push_back({name, contents.back()});
    }
    contents.push_back(AppElf(
        {"libogplay_jni.so", "", 0x00090009U, false}));
    libraries.push_back({"libogplay_jni.so", contents.back()});

    auto context = std::make_shared<runtime::DexVmAndroidContext>();
    context->apk_bytes = {std::byte{0x50}, std::byte{0x4b}, std::byte{3}, std::byte{4}};
    session::AndroidAppProcessRequest request;
    request.manifest = AppManifest("fixture.MainActivity");
    request.system_libraries = libraries;
    request.dex_bytes = ReadDexFixture("cipher.dex");
    request.icu_data = ReadPayloadBytes("icu/icudt51l.dat");
    request.boot_dex_bytes = test::ReadBootDex();
    request.context = context;
    request.surface_width = 64;
    request.surface_height = 36;
    request.maximum_ticks_per_call = UINT64_C(100000000);
    request.filesystem = &filesystem;
    request.ledger = &ledger;
    request.logger = &logger;

    try {
        static_cast<void>(session::AndroidAppProcess::Create(std::move(request)));
        FAIL("invalid unified JNI initialization unexpectedly succeeded");
    } catch (const session::AndroidAppProcessError& error) {
        CHECK(std::string(error.what()).find(
                  "API 19 guest JNI initialization failed") != std::string::npos);
        CHECK(std::string(error.what()).find(
                  "unsupported JNI version") != std::string::npos);
    }
}

TEST_CASE("DVM-106 Certificate parses DER PEM and verifies RSA EC through guest OpenSSL") {
    using namespace ogplay;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        CAPTURE(backend == InterpreterBackend::threaded ? "threaded" : "switch");
        runtime::VirtualFileSystem filesystem;
        core::CapabilityLedger ledger;
        core::Logger logger;
        std::vector<std::vector<std::byte>> contents;
        std::vector<runtime::BionicModuleSource> libraries;
        for (const auto name : {"libc.so", "libm.so", "libdl.so", "libstdc++.so", "libz.so",
                                "libcrypto.so", "libssl.so", "libgabi++.so", "libicui18n.so", "libicuuc.so",
                                "libstlport.so", "libogplay_jni.so"}) {
            std::ifstream stream(std::string(OGPLAY_SOURCE_DIR) + "/data/android/19/lib/" + name,
                                 std::ios::binary);
            REQUIRE_MESSAGE(stream.good(), name);
            std::vector<char> data{std::istreambuf_iterator<char>(stream), {}};
            contents.emplace_back(data.size());
            std::transform(data.begin(), data.end(), contents.back().begin(),
                           [](char c) { return static_cast<std::byte>(c); });
            libraries.push_back({name, contents.back()});
        }
        auto context = std::make_shared<runtime::DexVmAndroidContext>();
        context->apk_bytes = {std::byte{0x50}, std::byte{0x4b}, std::byte{3}, std::byte{4}};
        session::AndroidAppProcessRequest request;
        request.manifest = AppManifest("fixture.MainActivity");
        request.system_libraries = libraries;
        request.dex_bytes = ReadDexFixture("cipher.dex");
        request.icu_data = ReadPayloadBytes("icu/icudt51l.dat");
        request.boot_dex_bytes = test::ReadBootDex();
        request.context = context;
        request.dexvm.interpreter.backend = backend;
        request.surface_width = 64;
        request.surface_height = 36;
        request.maximum_ticks_per_call = UINT64_C(100000000);
#if defined(_WIN32)
        request.backend = {gles::AngleRenderer::d3d11, gles::AngleDevice::hardware};
#elif defined(__APPLE__)
        request.backend = {gles::AngleRenderer::metal, gles::AngleDevice::hardware};
#else
        request.backend = {gles::AngleRenderer::vulkan, gles::AngleDevice::hardware};
#endif
        request.filesystem = &filesystem;
        request.ledger = &ledger;
        request.logger = &logger;
        auto app = session::AndroidAppProcess::Create(std::move(request));
        auto& vm = app->DexVm().Vm();
        auto& linker = vm.Linker();
        const auto direct = [&](const char* owner, const char* name, const char* desc,
                                std::vector<VmValue> args) {
            auto type = linker.FindClass(owner);
            REQUIRE(type.has_value());
            auto method = linker.FindDirectMethod(*type, name, desc);
            REQUIRE_MESSAGE(method.has_value(), name);
            auto result = vm.Call(*method, args);
            REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
            return result.value;
        };
        const auto invoke = [&](VmObjectRef obj, const char* name, const char* desc,
                                std::vector<VmValue> args) {
            auto type = vm.Model().ObjectClass(obj);
            auto slot = linker.FindVtableIndex(type, name, desc);
            REQUIRE_MESSAGE(slot.has_value(), name);
            args.insert(args.begin(), VmValue::Ref(obj));
            auto result = vm.Call(linker.Class(type).vtable[*slot], args);
            REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
            return result.value;
        };
        const auto array = [&](const std::vector<std::byte>& data) {
            auto result = vm.Model().NewPrimitiveArray(linker.ResolveDescriptor("[B"),
                                                       runtime::JniPrimitiveKind::byte,
                                                       static_cast<runtime::JniSize>(data.size()));
            vm.Model().WriteByteRegion(result, 0, data);
            return result;
        };
        const auto file = [&](const char* name) {
            std::ifstream input(
                std::string(OGPLAY_SOURCE_DIR) + "/tests/fixtures/certificates/" + name,
                std::ios::binary);
            REQUIRE(input.good());
            std::vector<char> chars{std::istreambuf_iterator<char>(input), {}};
            std::vector<std::byte> result(chars.size());
            std::transform(chars.begin(), chars.end(), result.begin(),
                           [](char b) { return static_cast<std::byte>(b); });
            return result;
        };
        const auto raw = [&](VmObjectRef object, const char* name, const char* desc,
                             std::vector<VmValue> args) {
            const auto type = vm.Model().ObjectClass(object);
            const auto slot = linker.FindVtableIndex(type, name, desc);
            REQUIRE_MESSAGE(slot.has_value(), name);
            args.insert(args.begin(), VmValue::Ref(object));
            return vm.Call(linker.Class(type).vtable[*slot], args);
        };
        const auto factory = direct("Ljava/security/cert/CertificateFactory;", "getInstance",
                                    "(Ljava/lang/String;)Ljava/security/cert/CertificateFactory;",
                                    {VmValue::Ref(vm.NewStringUtf8("X.509"))})
                                 .ref;
        const auto factory_root = vm.ProtectReferences(std::array{factory});
        const auto stream = [&](const std::vector<std::byte>& data) {
            const auto bytes = array(data);
            const auto bytes_root = vm.ProtectReferences(std::array{bytes});
            const auto input = vm.NewIntrinsicInstance("Ljava/io/ByteArrayInputStream;");
            direct("Ljava/io/ByteArrayInputStream;", "<init>", "([B)V",
                   {VmValue::Ref(input), VmValue::Ref(bytes)});
            return input;
        };
        const auto parse = [&](const std::vector<std::byte>& data) {
            return invoke(factory, "generateCertificate",
                          "(Ljava/io/InputStream;)Ljava/security/cert/Certificate;",
                          {VmValue::Ref(stream(data))})
                .ref;
        };
        const auto read = [&](VmObjectRef bytes) {
            return vm.Model().ReadByteRegion(bytes, 0, vm.Model().ArrayLength(bytes));
        };
        for (const auto stem : {"rsa", "ec"}) {
            CAPTURE(stem);
            const auto der = file((std::string(stem) + ".der").c_str());
            const auto cert = parse(der);
            const auto cert_root = vm.ProtectReferences(std::array{cert});
            const auto pem = parse(file((std::string(stem) + ".pem").c_str()));
            const auto pem_root = vm.ProtectReferences(std::array{pem});
            CHECK(invoke(cert, "equals", "(Ljava/lang/Object;)Z", {VmValue::Ref(pem)}).AsInt() ==
                  1);
            CHECK(read(invoke(cert, "getEncoded", "()[B", {}).ref) == der);
            CHECK(invoke(cert, "getVersion", "()I", {}).AsInt() ==
                  (std::string(stem) == "rsa" ? 3 : 1));
            CHECK(vm.StringUtf8(invoke(cert, "getType", "()Ljava/lang/String;", {}).ref) ==
                  "X.509");
            const auto serial = invoke(cert, "getSerialNumber", "()Ljava/math/BigInteger;", {}).ref;
            const auto serial_root = vm.ProtectReferences(std::array{serial});
            CHECK(
                vm.StringUtf8(
                    invoke(serial, "toString", "(I)Ljava/lang/String;", {VmValue::Int(16)}).ref) ==
                (std::string(stem) == "rsa" ? "1234567890abcdef1234567890abcdef12345678" : "2a"));
            CHECK(
                !vm.StringUtf8(invoke(serial, "toString", "()Ljava/lang/String;", {}).ref).empty());
            const auto issuer = invoke(cert, "getIssuerX500Principal",
                                       "()Ljavax/security/auth/x500/X500Principal;", {})
                                    .ref;
            const auto issuer_root = vm.ProtectReferences(std::array{issuer});
            const auto subject = invoke(cert, "getSubjectX500Principal",
                                        "()Ljavax/security/auth/x500/X500Principal;", {})
                                     .ref;
            CHECK(invoke(issuer, "equals", "(Ljava/lang/Object;)Z", {VmValue::Ref(subject)})
                      .AsInt() == 1);
            CHECK(vm.StringUtf8(invoke(cert, "getSigAlgName", "()Ljava/lang/String;", {}).ref) ==
                  (std::string(stem) == "rsa" ? "SHA256withRSA" : "SHA256withECDSA"));
            const auto pub = invoke(cert, "getPublicKey", "()Ljava/security/PublicKey;", {}).ref;
            const auto pub_root = vm.ProtectReferences(std::array{pub});
            CHECK(vm.StringUtf8(invoke(pub, "getFormat", "()Ljava/lang/String;", {}).ref) ==
                  "X.509");
            invoke(cert, "verify", "(Ljava/security/PublicKey;)V", {VmValue::Ref(pub)});
            invoke(cert, "verify", "(Ljava/security/PublicKey;Ljava/lang/String;)V",
                   {VmValue::Ref(pub), VmValue::Ref(vm.NewStringUtf8("AndroidOpenSSL"))});
            const auto bad_provider =
                raw(cert, "verify", "(Ljava/security/PublicKey;Ljava/lang/String;)V",
                    {VmValue::Ref(pub), VmValue::Ref(vm.NewStringUtf8("missing"))});
            REQUIRE(bad_provider.exception.IsValid());
            CHECK(linker.Class(bad_provider.exception_class).descriptor ==
                  "Ljava/security/NoSuchProviderException;");
            auto tampered = der;
            tampered.back() ^= std::byte{1};
            const auto invalid = parse(tampered);
            const auto bad =
                raw(invalid, "verify", "(Ljava/security/PublicKey;)V", {VmValue::Ref(pub)});
            REQUIRE(bad.exception.IsValid());
            CHECK(linker.Class(bad.exception_class).descriptor ==
                  "Ljava/security/SignatureException;");
            for (const auto milliseconds :
                 {INT64_C(0), INT64_C(1900000000000), INT64_C(4102444800000)}) {
                const auto date = vm.NewIntrinsicInstance("Ljava/util/Date;");
                direct("Ljava/util/Date;", "<init>", "(J)V",
                       {VmValue::Ref(date), VmValue::Long(milliseconds)});
                const auto validity =
                    raw(cert, "checkValidity", "(Ljava/util/Date;)V", {VmValue::Ref(date)});
                if (milliseconds == INT64_C(1900000000000))
                    CHECK(!validity.exception.IsValid());
                else {
                    REQUIRE(validity.exception.IsValid());
                    CHECK(linker.Class(validity.exception_class).descriptor ==
                          (milliseconds == 0
                               ? "Ljava/security/cert/CertificateNotYetValidException;"
                               : "Ljava/security/cert/CertificateExpiredException;"));
                }
            }
            if (std::string(stem) == "rsa") {
                CHECK(invoke(cert, "getBasicConstraints", "()I", {}).AsInt() == 1);
                const auto usage = invoke(cert, "getKeyUsage", "()[Z", {}).ref;
                CHECK(vm.Model().GetPrimitiveElement(usage, 0) == 1);
                CHECK(vm.Model().GetPrimitiveElement(usage, 5) == 1);
                const auto names =
                    invoke(cert, "getSubjectAlternativeNames", "()Ljava/util/Collection;", {}).ref;
                CHECK(invoke(names, "size", "()I", {}).AsInt() == 2);
            }
        }
        auto bundle = file("rsa.pem");
        const auto ec = file("ec.pem");
        bundle.insert(bundle.end(), ec.begin(), ec.end());
        const auto certs =
            invoke(factory, "generateCertificates", "(Ljava/io/InputStream;)Ljava/util/Collection;",
                   {VmValue::Ref(stream(bundle))})
                .ref;
        CHECK(invoke(certs, "size", "()I", {}).AsInt() == 2);
        const auto issuer_cert = parse(file("rsa.der"));
        const auto issuer_root = vm.ProtectReferences(std::array{issuer_cert});
        const auto issuer_key =
            invoke(issuer_cert, "getPublicKey", "()Ljava/security/PublicKey;", {}).ref;
        const auto issuer_key_root = vm.ProtectReferences(std::array{issuer_key});
        const auto leaf = parse(file("leaf.der"));
        const auto leaf_root = vm.ProtectReferences(std::array{leaf});
        invoke(leaf, "verify", "(Ljava/security/PublicKey;)V", {VmValue::Ref(issuer_key)});
        const auto leaf_key = invoke(leaf, "getPublicKey", "()Ljava/security/PublicKey;", {}).ref;
        const auto wrong_key =
            raw(leaf, "verify", "(Ljava/security/PublicKey;)V", {VmValue::Ref(leaf_key)});
        REQUIRE(wrong_key.exception.IsValid());
        CHECK(linker.Class(wrong_key.exception_class).descriptor ==
              "Ljava/security/SignatureException;");
        const auto legacy = direct("Ljavax/security/cert/X509Certificate;", "getInstance",
                                   "([B)Ljavax/security/cert/X509Certificate;",
                                   {VmValue::Ref(array(file("rsa.der")))})
                                .ref;
        const auto legacy_root = vm.ProtectReferences(std::array{legacy});
        CHECK(read(invoke(legacy, "getEncoded", "()[B", {}).ref) == file("rsa.der"));
        invoke(legacy, "verify", "(Ljava/security/PublicKey;)V", {VmValue::Ref(issuer_key)});
        const auto chain = vm.NewIntrinsicInstance("Ljava/util/ArrayList;");
        const auto chain_root = vm.ProtectReferences(std::array{chain});
        direct("Ljava/util/ArrayList;", "<init>", "()V", {VmValue::Ref(chain)});
        invoke(chain, "add", "(Ljava/lang/Object;)Z", {VmValue::Ref(leaf)});
        invoke(chain, "add", "(Ljava/lang/Object;)Z", {VmValue::Ref(issuer_cert)});
        const auto path =
            invoke(factory, "generateCertPath", "(Ljava/util/List;)Ljava/security/cert/CertPath;",
                   {VmValue::Ref(chain)})
                .ref;
        const auto path_root = vm.ProtectReferences(std::array{path});
        for (const auto encoding : {"PkiPath", "PKCS7"}) {
            const auto der = invoke(path, "getEncoded", "(Ljava/lang/String;)[B",
                                    {VmValue::Ref(vm.NewStringUtf8(encoding))})
                                 .ref;
            const auto der_root = vm.ProtectReferences(std::array{der});
            const auto decoded =
                invoke(factory, "generateCertPath",
                       "(Ljava/io/InputStream;Ljava/lang/String;)Ljava/security/cert/CertPath;",
                       {VmValue::Ref(stream(read(der))), VmValue::Ref(vm.NewStringUtf8(encoding))})
                    .ref;
            if (std::string_view(encoding) == "PkiPath")
                CHECK(invoke(decoded, "equals", "(Ljava/lang/Object;)Z", {VmValue::Ref(path)})
                          .AsInt() == 1);
            else {  // PKCS7 contains a SET OF certificates; it does not preserve path order.
                const auto list = invoke(decoded, "getCertificates", "()Ljava/util/List;", {}).ref;
                const auto list_root = vm.ProtectReferences(std::array{list});
                CHECK(invoke(list, "size", "()I", {}).AsInt() == 2);
                CHECK(invoke(list, "contains", "(Ljava/lang/Object;)Z", {VmValue::Ref(leaf)})
                          .AsInt() == 1);
                CHECK(invoke(list, "contains", "(Ljava/lang/Object;)Z", {VmValue::Ref(issuer_cert)})
                          .AsInt() == 1);
            }
        }
        const auto bundle_der = file("chain.p7b");
        const auto pkcs7 =
            invoke(factory, "generateCertificates", "(Ljava/io/InputStream;)Ljava/util/Collection;",
                   {VmValue::Ref(stream(bundle_der))})
                .ref;
        CHECK(invoke(pkcs7, "size", "()I", {}).AsInt() == 2);
        std::ifstream vectors(std::string(OGPLAY_SOURCE_DIR) +
                              "/tests/fixtures/certificates/signatures.txt");
        REQUIRE(vectors.good());
        const std::string message = "OGPlay certificate signature fixture\n";
        std::vector<std::byte> message_bytes;
        for (char ch : message) message_bytes.push_back(static_cast<std::byte>(ch));
        std::string algorithm, signature_hex;
        int vector_count = 0;
        while (vectors >> algorithm >> signature_hex) {
            ++vector_count;
            CAPTURE(algorithm);
            const bool rsa = algorithm.ends_with("RSA");
            const auto cert = parse(file(rsa ? "rsa.der" : "ec.der"));
            const auto cert_root = vm.ProtectReferences(std::array{cert});
            const auto key = invoke(cert, "getPublicKey", "()Ljava/security/PublicKey;", {}).ref;
            const auto key_root = vm.ProtectReferences(std::array{key});
            const auto verifier = direct("Ljava/security/Signature;", "getInstance",
                                         "(Ljava/lang/String;)Ljava/security/Signature;",
                                         {VmValue::Ref(vm.NewStringUtf8(algorithm))})
                                      .ref;
            const auto verifier_root = vm.ProtectReferences(std::array{verifier});
            invoke(verifier, "initVerify", "(Ljava/security/PublicKey;)V", {VmValue::Ref(key)});
            std::vector<std::byte> sig;
            for (std::size_t i = 0; i < signature_hex.size(); i += 2)
                sig.push_back(
                    static_cast<std::byte>(std::stoul(signature_hex.substr(i, 2), nullptr, 16)));
            invoke(verifier, "update", "(B)V",
                   {VmValue::Int(std::to_integer<int>(message_bytes[0]))});
            invoke(verifier, "update", "([BII)V",
                   {VmValue::Ref(array(message_bytes)), VmValue::Int(1),
                    VmValue::Int(static_cast<int>(message_bytes.size()) - 1)});
            static_cast<void>(vm.CollectGarbage());
            CHECK(invoke(verifier, "verify", "([B)Z", {VmValue::Ref(array(sig))}).AsInt() == 1);
            invoke(verifier, "update", "([B)V", {VmValue::Ref(array(message_bytes))});
            CHECK(invoke(verifier, "verify", "([B)Z", {VmValue::Ref(array(sig))}).AsInt() ==
                  1);  // Reset after success.
            auto bad_message = message_bytes;
            bad_message[0] ^= std::byte{1};
            invoke(verifier, "update", "([B)V", {VmValue::Ref(array(bad_message))});
            CHECK(invoke(verifier, "verify", "([B)Z", {VmValue::Ref(array(sig))}).AsInt() == 0);
            invoke(verifier, "update", "([B)V", {VmValue::Ref(array(message_bytes))});
            CHECK(invoke(verifier, "verify", "([B)Z", {VmValue::Ref(array(sig))}).AsInt() ==
                  1);  // Reset after mismatch.
        }
        CHECK(vector_count == 10);
        const auto verifier = direct("Ljava/security/Signature;", "getInstance",
                                     "(Ljava/lang/String;)Ljava/security/Signature;",
                                     {VmValue::Ref(vm.NewStringUtf8("SHA256withRSA"))})
                                  .ref;
        const auto verifier_root = vm.ProtectReferences(std::array{verifier});
        const auto invalid_key =
            vm.NewIntrinsicInstance("Lorg/apache/harmony/security/x509/X509PublicKey;");
        direct("Lorg/apache/harmony/security/x509/X509PublicKey;", "<init>",
               "(Ljava/lang/String;[B[B)V",
               {VmValue::Ref(invalid_key), VmValue::Ref(vm.NewStringUtf8("RSA")),
                VmValue::Ref(array({std::byte{1}})), VmValue::Ref(VmObjectRef{})});
        const auto key_failure = raw(verifier, "initVerify", "(Ljava/security/PublicKey;)V",
                                     {VmValue::Ref(invalid_key)});
        REQUIRE(key_failure.exception.IsValid());
        CHECK(linker.Class(key_failure.exception_class).descriptor ==
              "Ljava/security/InvalidKeyException;");
        invoke(verifier, "initVerify", "(Ljava/security/PublicKey;)V", {VmValue::Ref(issuer_key)});
        invoke(verifier, "update", "([B)V", {VmValue::Ref(array(std::vector<std::byte>(1048576)))});
        const auto limit_failure = raw(verifier, "update", "(B)V", {VmValue::Int(1)});
        REQUIRE(limit_failure.exception.IsValid());
        CHECK(linker.Class(limit_failure.exception_class).descriptor ==
              "Ljava/security/SignatureException;");
        const auto signature_type = linker.ResolveDescriptor("Ljava/security/Signature;");
        const auto get_signature = linker.FindDirectMethod(
            signature_type, "getInstance", "(Ljava/lang/String;)Ljava/security/Signature;");
        REQUIRE(get_signature);
        for (const auto unsupported : {"SHA256withRSA/PSS", "SHA256withDSA", "Ed25519"}) {
            const auto failure =
                vm.Call(*get_signature, std::array{VmValue::Ref(vm.NewStringUtf8(unsupported))});
            REQUIRE(failure.exception.IsValid());
            CHECK(linker.Class(failure.exception_class).descriptor ==
                  "Ljava/security/NoSuchAlgorithmException;");
        }
        // JNI storage must apply VM covariance to arrays nested inside Object[].
        const auto objects =
            vm.Model().NewObjectArray(linker.ResolveDescriptor("[Ljava/lang/Object;"),
                                      linker.ResolveDescriptor("Ljava/lang/Object;"), 1);
        const auto objects_root = vm.ProtectReferences(std::array{objects});
        const auto nested = vm.Model().NewObjectArray(linker.ResolveDescriptor("[[B"),
                                                      linker.ResolveDescriptor("[B"), 1);
        vm.Model().SetObjectElement(objects, 0, nested);
        vm.Model().SetObjectElement(nested, 0, array(message_bytes));
        const auto integers = vm.Model().NewPrimitiveArray(linker.ResolveDescriptor("[I"),
                                                           runtime::JniPrimitiveKind::integer, 1);
        CHECK_THROWS_AS(vm.Model().SetObjectElement(nested, 0, integers),
                        runtime::JniObjectArrayError);
        for (const auto& malformed :
             {std::vector<std::byte>{}, std::vector<std::byte>{std::byte{0x30}, std::byte{0x80}},
              std::vector<std::byte>{std::byte{1}, std::byte{2}}}) {
            const auto result = raw(factory, "generateCertificate",
                                    "(Ljava/io/InputStream;)Ljava/security/cert/Certificate;",
                                    {VmValue::Ref(stream(malformed))});
            REQUIRE(result.exception.IsValid());
            CHECK(linker.Class(result.exception_class).descriptor ==
                   "Ljava/security/cert/CertificateException;");
        }

    }
}

TEST_CASE("DVM-108/109 UUID MessageDigest and serialization use BootDex with real guest crypto") {
    using namespace ogplay;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        CAPTURE(backend == InterpreterBackend::threaded ? "threaded" : "switch");
        runtime::VirtualFileSystem filesystem;
        core::CapabilityLedger ledger;
        core::Logger logger;
        std::vector<std::vector<std::byte>> contents;
        std::vector<runtime::BionicModuleSource> libraries;
        for (const auto name : {"libc.so", "libm.so", "libdl.so", "libstdc++.so", "libz.so",
                                "libcrypto.so", "libssl.so", "libgabi++.so", "libicui18n.so", "libicuuc.so",
                                "libstlport.so", "libogplay_jni.so"}) {
            std::ifstream stream(std::string(OGPLAY_SOURCE_DIR) + "/data/android/19/lib/" + name,
                                 std::ios::binary);
            REQUIRE_MESSAGE(stream.good(), name);
            std::vector<char> data{std::istreambuf_iterator<char>(stream), {}};
            contents.emplace_back(data.size());
            std::transform(data.begin(), data.end(), contents.back().begin(),
                           [](char c) { return static_cast<std::byte>(c); });
            libraries.push_back({name, contents.back()});
        }
        auto context = std::make_shared<runtime::DexVmAndroidContext>();
        context->apk_bytes = {std::byte{0x50}, std::byte{0x4b}, std::byte{3}, std::byte{4}};
        session::AndroidAppProcessRequest request;
        request.manifest = AppManifest("fixture.MainActivity");
        request.system_libraries = libraries;
        request.dex_bytes = ReadDexFixture("cipher.dex");
        request.icu_data = ReadPayloadBytes("icu/icudt51l.dat");
        request.boot_dex_bytes = test::ReadBootDex();
        request.context = context;
        request.dexvm.interpreter.backend = backend;
        request.surface_width = 64;
        request.surface_height = 36;
        request.maximum_ticks_per_call = UINT64_C(100000000);
#if defined(_WIN32)
        request.backend = {gles::AngleRenderer::d3d11, gles::AngleDevice::hardware};
#elif defined(__APPLE__)
        request.backend = {gles::AngleRenderer::metal, gles::AngleDevice::hardware};
#else
        request.backend = {gles::AngleRenderer::vulkan, gles::AngleDevice::hardware};
#endif
        request.filesystem = &filesystem;
        request.ledger = &ledger;
        request.logger = &logger;
        auto app = session::AndroidAppProcess::Create(std::move(request));
        auto& vm = app->DexVm().Vm();
        auto& linker = vm.Linker();
        const auto direct = [&](const char* owner, const char* name, const char* desc,
                                std::vector<VmValue> args) {
            auto type = linker.FindClass(owner);
            REQUIRE(type.has_value());
            auto method = linker.FindDirectMethod(*type, name, desc);
            REQUIRE_MESSAGE(method.has_value(), name);
            auto result = vm.Call(*method, args);
            REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
            return result.value;
        };
        const auto invoke = [&](VmObjectRef obj, const char* name, const char* desc,
                                std::vector<VmValue> args) {
            auto type = vm.Model().ObjectClass(obj);
            auto slot = linker.FindVtableIndex(type, name, desc);
            REQUIRE_MESSAGE(slot.has_value(), name);
            args.insert(args.begin(), VmValue::Ref(obj));
            auto result = vm.Call(linker.Class(type).vtable[*slot], args);
            REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
            return result.value;
        };
        const auto bytes = [&](const char* hex) {
            std::vector<std::byte> data;
            for (std::size_t i = 0; hex[i]; i += 2)
                data.push_back(
                    static_cast<std::byte>(std::stoul(std::string(hex + i, 2), nullptr, 16)));
            auto array = vm.Model().NewPrimitiveArray(linker.ResolveDescriptor("[B"),
                                                      runtime::JniPrimitiveKind::byte,
                                                      static_cast<runtime::JniSize>(data.size()));
            vm.Model().WriteByteRegion(array, 0, data);
            return array;
        };

        const auto raw = [&](VmObjectRef obj, const char* name, const char* desc, std::vector<VmValue> args) {
            const auto type = vm.Model().ObjectClass(obj);
            const auto slot = linker.FindVtableIndex(type, name, desc);
            REQUIRE(slot.has_value());
            args.insert(args.begin(), VmValue::Ref(obj));
            return vm.Call(linker.Class(type).vtable[*slot], args);
        };
        const auto expect = [&](const VmCallOutcome& result, const char* type) {
            REQUIRE(result.exception.IsValid());
            CHECK(linker.Class(result.exception_class).descriptor == type);
        };
        const auto digest = direct("Ljava/security/MessageDigest;", "getInstance", "(Ljava/lang/String;)Ljava/security/MessageDigest;", {VmValue::Ref(vm.NewStringUtf8("MD5"))}).ref;
        const auto digest_roots = vm.ProtectReferences(std::array{digest});
        const auto read = [&](VmObjectRef array) { return vm.Model().ReadByteRegion(array, 0, vm.Model().ArrayLength(array)); };
        // DVM-109 exercises the same original Java object streams used by apps.
        const auto roundtrip = [&](VmObjectRef value, const char* expected_wire = nullptr) {
            const auto value_root = vm.ProtectReferences(std::array{value});
            const auto sink = vm.NewIntrinsicInstance("Ljava/io/ByteArrayOutputStream;");
            direct("Ljava/io/ByteArrayOutputStream;", "<init>", "()V", {VmValue::Ref(sink)});
            const auto output = vm.NewIntrinsicInstance("Ljava/io/ObjectOutputStream;");
            const auto output_roots = vm.ProtectReferences(std::array{sink, output});
            direct("Ljava/io/ObjectOutputStream;", "<init>", "(Ljava/io/OutputStream;)V", {VmValue::Ref(output), VmValue::Ref(sink)});
            invoke(output, "writeObject", "(Ljava/lang/Object;)V", {VmValue::Ref(value)});
            invoke(output, "flush", "()V", {});
            const auto wire = invoke(sink, "toByteArray", "()[B", {}).ref;
            if (expected_wire) CHECK(read(wire) == read(bytes(expected_wire)));
            const auto source = vm.NewIntrinsicInstance("Ljava/io/ByteArrayInputStream;");
            direct("Ljava/io/ByteArrayInputStream;", "<init>", "([B)V", {VmValue::Ref(source), VmValue::Ref(wire)});
            const auto input = vm.NewIntrinsicInstance("Ljava/io/ObjectInputStream;");
            const auto input_roots = vm.ProtectReferences(std::array{source, input});
            direct("Ljava/io/ObjectInputStream;", "<init>", "(Ljava/io/InputStream;)V", {VmValue::Ref(input), VmValue::Ref(source)});
            static_cast<void>(vm.CollectGarbage("serialization-live-streams"));
            return invoke(input, "readObject", "()Ljava/lang/Object;", {}).ref;
        };
        // DVM-151: builder hooks require the same real SHA/serialization backend.
        for (const auto* owner : {"Ljava/lang/StringBuilder;", "Ljava/lang/StringBuffer;"}) {
            CAPTURE(owner);
            const auto builder = vm.NewIntrinsicInstance(owner);
            const auto root = vm.ProtectReferences(std::array{builder});
            const auto text = vm.Model().NewString(std::u16string_view(u"A😀 B", 5));
            direct(owner, "<init>", "(Ljava/lang/String;)V", {VmValue::Ref(builder), VmValue::Ref(text)});
            invoke(builder, "ensureCapacity", "(I)V", {VmValue::Int(80)});
            const auto capacity = invoke(builder, "capacity", "()I", {}).AsInt();
            const auto snapshot = vm.Model().StringValue(invoke(builder, "toString", "()Ljava/lang/String;", {}).ref);
            const auto copy = roundtrip(builder);
            const auto copy_root = vm.ProtectReferences(std::array{copy});
            CHECK(copy != builder);
            CHECK(vm.Model().ObjectClass(copy) == vm.Model().ObjectClass(builder));
            CHECK(vm.Model().StringValue(invoke(copy, "toString", "()Ljava/lang/String;", {}).ref) == snapshot);
            CHECK(invoke(copy, "capacity", "()I", {}).AsInt() == capacity);
            static_cast<void>(vm.CollectGarbage("builder-roundtrip"));
            const auto signature = std::string("(C)") + owner;
            invoke(copy, "append", signature.c_str(), {VmValue::Int('!')});
            CHECK(vm.Model().StringValue(invoke(builder, "toString", "()Ljava/lang/String;", {}).ref) == snapshot);
            CHECK(vm.Model().StringValue(invoke(copy, "toString", "()Ljava/lang/String;", {}).ref) == snapshot + u"!");
        }
        // DVM-115: real Java serialization includes Throwable's private callbacks and stack arrays.
        {
            const auto exception = vm.MakeThrowable("Ljava/lang/Exception;", "outer");
            const auto exception_root = vm.ProtectReferences(std::array{exception});
            const auto cause = vm.MakeThrowable("Ljava/lang/IllegalStateException;", "inner");
            vm.InitThrowableCause(exception, cause);
            invoke(exception, "addSuppressed", "(Ljava/lang/Throwable;)V", {VmValue::Ref(cause)});
            const auto frame = vm.NewIntrinsicInstance("Ljava/lang/StackTraceElement;");
            const auto frame_root = vm.ProtectReferences(std::array{frame});
            direct("Ljava/lang/StackTraceElement;", "<init>", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;I)V",
                {VmValue::Ref(frame), VmValue::Ref(vm.NewStringUtf8("Probe")), VmValue::Ref(vm.NewStringUtf8("work")),
                 VmValue::Ref(vm.NewStringUtf8("Probe.java")), VmValue::Int(42)});
            const auto trace = vm.Model().NewObjectArray(linker.ResolveDescriptor("[Ljava/lang/StackTraceElement;"),
                linker.ResolveDescriptor("Ljava/lang/StackTraceElement;"), 1);
            vm.Model().SetObjectElement(trace, 0, frame);
            invoke(exception, "setStackTrace", "([Ljava/lang/StackTraceElement;)V", {VmValue::Ref(trace)});
            const auto copy = roundtrip(exception);
            const auto copy_root = vm.ProtectReferences(std::array{copy});
            CHECK(copy != exception);
            CHECK(vm.StringUtf8(invoke(copy, "getMessage", "()Ljava/lang/String;", {}).ref) == "outer");
            const auto copy_cause = invoke(copy, "getCause", "()Ljava/lang/Throwable;", {}).ref;
            CHECK(copy_cause != cause);
            CHECK(vm.StringUtf8(invoke(copy_cause, "getMessage", "()Ljava/lang/String;", {}).ref) == "inner");
            const auto suppressed = invoke(copy, "getSuppressed", "()[Ljava/lang/Throwable;", {}).ref;
            REQUIRE(vm.Model().ArrayLength(suppressed) == 1);
            CHECK(vm.Model().GetObjectElement(suppressed, 0) == copy_cause);
            const auto copy_trace = invoke(copy, "getStackTrace", "()[Ljava/lang/StackTraceElement;", {}).ref;
            REQUIRE(vm.Model().ArrayLength(copy_trace) == 1);
            CHECK(invoke(vm.Model().GetObjectElement(copy_trace, 0), "equals", "(Ljava/lang/Object;)Z", {VmValue::Ref(frame)}).AsInt() == 1);
            const auto sink = vm.NewIntrinsicInstance("Ljava/io/StringWriter;");
            const auto sink_root = vm.ProtectReferences(std::array{sink});
            direct("Ljava/io/StringWriter;", "<init>", "()V", {VmValue::Ref(sink)});
            const auto writer = vm.NewIntrinsicInstance("Ljava/io/PrintWriter;");
            const auto writer_root = vm.ProtectReferences(std::array{writer});
            direct("Ljava/io/PrintWriter;", "<init>", "(Ljava/io/Writer;)V", {VmValue::Ref(writer), VmValue::Ref(sink)});
            invoke(copy, "printStackTrace", "(Ljava/io/PrintWriter;)V", {VmValue::Ref(writer)});
            const auto printed = vm.StringUtf8(invoke(sink, "toString", "()Ljava/lang/String;", {}).ref);
            CHECK(printed.find("java.lang.Exception: outer\n\tat Probe.work(Probe.java:42)\n") == 0);
            CHECK(printed.find("Caused by: java.lang.IllegalStateException: inner") != std::string::npos);
        }
        const auto field_value = [&](VmObjectRef object, const char* owner, const char* name) {
            const auto field = linker.FindFieldRecursive(linker.ResolveDescriptor(owner), name, "I");
            REQUIRE(field);
            return static_cast<std::int32_t>(vm.Model().InstanceSlots(object)[linker.Field(*field).slot].bits);
        };
        const auto value = vm.NewIntrinsicInstance("Lfixture/SerializationValue;");
        direct("Lfixture/SerializationValue;", "<init>", "()V", {VmValue::Ref(value)});
        const auto next_field = linker.FindFieldRecursive(linker.ResolveDescriptor("Lfixture/SerializationValue;"), "next", "Ljava/lang/Object;");
        REQUIRE(next_field);
        vm.Model().InstanceSlots(value)[linker.Field(*next_field).slot] = {value.Value(), SlotTag::ref};
        auto restored_value = roundtrip(value);
        CHECK(restored_value != value);
        CHECK(field_value(restored_value, "Lfixture/SerializationBase;", "inherited") == 17);
        CHECK(field_value(restored_value, "Lfixture/SerializationValue;", "restored") == 73);
        CHECK(vm.Model().InstanceSlots(restored_value)[linker.Field(*next_field).slot].bits == restored_value.Value());
        const auto fields = vm.NewIntrinsicInstance("Lfixture/SerializationFields;");
        direct("Lfixture/SerializationFields;", "<init>", "()V", {VmValue::Ref(fields)});
        CHECK(field_value(roundtrip(fields), "Lfixture/SerializationFields;", "number") == 42);
        const auto uid = direct("Ljava/io/ObjectStreamClass;", "lookup", "(Ljava/lang/Class;)Ljava/io/ObjectStreamClass;",
            {VmValue::Ref(vm.Model().ClassObject(linker.ResolveDescriptor("Lfixture/DefaultUid;")))}).ref;
        CHECK(invoke(uid, "getSerialVersionUID", "()J", {}).AsLong() == INT64_C(5156351520895513822));
        const auto default_value = vm.NewIntrinsicInstance("Lfixture/DefaultUid;");
        direct("Lfixture/DefaultUid;", "<init>", "()V", {VmValue::Ref(default_value)});
        CHECK(vm.Model().ObjectClass(roundtrip(default_value)) == linker.ResolveDescriptor("Lfixture/DefaultUid;"));
        const auto graph = vm.Model().NewObjectArray(linker.ResolveDescriptor("[Ljava/lang/Object;"), linker.ResolveDescriptor("Ljava/lang/Object;"), 3);
        vm.Model().SetObjectElement(graph, 0, graph);
        const auto shared_bytes = bytes("00ff010203");
        vm.Model().SetObjectElement(graph, 1, shared_bytes);
        vm.Model().SetObjectElement(graph, 2, shared_bytes);
        const auto graph_copy = roundtrip(graph);
        CHECK(vm.Model().GetObjectElement(graph_copy, 0) == graph_copy);
        CHECK(vm.Model().GetObjectElement(graph_copy, 1) == vm.Model().GetObjectElement(graph_copy, 2));
        CHECK(read(vm.Model().GetObjectElement(graph_copy, 1)) == read(bytes("00ff010203")));
        const auto uuid_original = direct("Ljava/util/UUID;", "fromString", "(Ljava/lang/String;)Ljava/util/UUID;",
            {VmValue::Ref(vm.NewStringUtf8("f81d4fae-7dec-11d0-a765-00a0c91e6bf6"))}).ref;
        const auto restored_uuid = roundtrip(uuid_original, "aced00057372000e6a6176612e7574696c2e55554944bc9903f7986d852f0200024a000c6c65617374536967426974734a000b6d6f7374536967426974737870a76500a0c91e6bf6f81d4fae7dec11d0");
        CHECK(invoke(restored_uuid, "version", "()I", {}).AsInt() == 1);
        CHECK(invoke(restored_uuid, "timestamp", "()J", {}).AsLong() == INT64_C(130742845922168750));
        for (const auto& [owner, expected] : std::array{
                 std::pair{"Lfixture/SerializationResolve;", "resolved"},
                 std::pair{"Lfixture/SerializationReplace;", "replaced"}}) {
            const auto instance = vm.NewIntrinsicInstance(owner);
            direct(owner, "<init>", "()V", {VmValue::Ref(instance)});
            CHECK(vm.StringUtf8(roundtrip(instance)) == expected);
        }
        for (const auto& [descriptor, kind] : std::array{
                 std::pair{"[Z", runtime::JniPrimitiveKind::boolean},
                 std::pair{"[C", runtime::JniPrimitiveKind::character},
                 std::pair{"[S", runtime::JniPrimitiveKind::short_integer},
                 std::pair{"[I", runtime::JniPrimitiveKind::integer},
                 std::pair{"[J", runtime::JniPrimitiveKind::long_integer},
                 std::pair{"[F", runtime::JniPrimitiveKind::float_value},
                 std::pair{"[D", runtime::JniPrimitiveKind::double_value}}) {
            const auto array = vm.Model().NewPrimitiveArray(linker.ResolveDescriptor(descriptor), kind, 2);
            vm.Model().SetPrimitiveElement(array, 0, 1);
            vm.Model().SetPrimitiveElement(array, 1, descriptor[1] == 'Z' ? 0 : UINT64_C(0x0102030405060708));
            const auto copy = roundtrip(array);
            CHECK(vm.Model().ObjectClass(copy) == linker.ResolveDescriptor(descriptor));
            CHECK(vm.Model().GetPrimitiveElement(copy, 0) == vm.Model().GetPrimitiveElement(array, 0));
            CHECK(vm.Model().GetPrimitiveElement(copy, 1) == vm.Model().GetPrimitiveElement(array, 1));
        }
        // Independent known answers, including binary input crossing many EVP blocks.
        for (const auto& vector : std::array{
            std::array{"MD5", "md5", "1.2.840.113549.2.5", "d41d8cd98f00b204e9800998ecf8427e", "900150983cd24fb0d6963f7d28e17f72", "3e2e51f419bcd80d9de0290be2de85ed"},
            std::array{"SHA-1", "SHA", "1.3.14.3.2.26", "da39a3ee5e6b4b0d3255bfef95601890afd80709", "a9993e364706816aba3e25717850c26c9cd0d89d", "4a2fb8a7e91751d887e656935210a7e4aeece658"},
            std::array{"SHA-256", "SHA256", "2.16.840.1.101.3.4.2.1", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "dd7e5c49d123e860c8bb7016bada722b5d0baa37ef8b19d5e270cf2a3000c31d"},
            std::array{"SHA-384", "SHA384", "2.16.840.1.101.3.4.2.2", "38b060a751ac96384cd9327eb1b1e36a21fdb71114be07434c0cc7bf63f6e1da274edebfe76f65fbd51ad2f14898b95b", "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7", "5514fa38835e9e484a4ae89a248545ef400b8c91d121952eb243a42f65fd68d084c9ac680f6f4a7164d183b3997a4cf5"},
            std::array{"SHA-512", "SHA512", "2.16.840.1.101.3.4.2.3", "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e", "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f", "2af1ca0a6a8f835b556c65eee6e7bbdda5e8251f5117351291a703f35f585e7aee225f6c21b07c9475e8ebbfbd210e120d97ddc1d8e81f10a97def1c31df9514"}}) {
            CAPTURE(vector[0]);
            const auto create_digest = [&](const char* algorithm) {
                return direct("Ljava/security/MessageDigest;", "getInstance",
                              "(Ljava/lang/String;)Ljava/security/MessageDigest;",
                              {VmValue::Ref(vm.NewStringUtf8(algorithm))}).ref;
            };
            const auto hash = create_digest(vector[0]);
            const auto hash_root = vm.ProtectReferences(std::array{hash});
            const auto length = static_cast<int>(std::string_view(vector[4]).size() / 2);
            CHECK(invoke(hash, "getDigestLength", "()I", {}).AsInt() == length);
            CHECK(read(invoke(hash, "digest", "()[B", {}).ref) == read(bytes(vector[3])));
            for (const auto* alias : {vector[1], vector[2]}) {
                CHECK(read(invoke(create_digest(alias), "digest", "([B)[B",
                                  {VmValue::Ref(bytes("616263"))}).ref) == read(bytes(vector[4])));
            }
            const auto provider_hash = direct("Ljava/security/MessageDigest;", "getInstance",
                "(Ljava/lang/String;Ljava/lang/String;)Ljava/security/MessageDigest;",
                {VmValue::Ref(vm.NewStringUtf8(vector[0])), VmValue::Ref(vm.NewStringUtf8("AndroidOpenSSL"))}).ref;
            CHECK(read(invoke(provider_hash, "digest", "([B)[B", {VmValue::Ref(bytes("616263"))}).ref) == read(bytes(vector[4])));
            invoke(hash, "update", "(B)V", {VmValue::Int(97)});
            const auto copy = invoke(hash, "clone", "()Ljava/lang/Object;", {}).ref;
            const auto copy_root = vm.ProtectReferences(std::array{copy});
            invoke(hash, "reset", "()V", {});
            CHECK(read(invoke(hash, "digest", "()[B", {}).ref) == read(bytes(vector[3])));
            invoke(copy, "update", "([BII)V", {VmValue::Ref(bytes("00626300")), VmValue::Int(1), VmValue::Int(2)});
            const auto output = vm.Model().NewPrimitiveArray(linker.ResolveDescriptor("[B"), runtime::JniPrimitiveKind::byte, length + 2);
            CHECK(invoke(copy, "digest", "([BII)I", {VmValue::Ref(output), VmValue::Int(1), VmValue::Int(length)}).AsInt() == length);
            CHECK(vm.Model().ReadByteRegion(output, 1, length) == read(bytes(vector[4])));
            CHECK(read(output).front() == std::byte{0});
            CHECK(read(output).back() == std::byte{0});
            for (int mode = 0; mode < 3; ++mode) {
                auto buffer = direct("Ljava/nio/ByteBuffer;", mode == 1 ? "allocateDirect" : "allocate",
                    "(I)Ljava/nio/ByteBuffer;", {VmValue::Int(5)}).ref;
                invoke(buffer, "put", "([B)Ljava/nio/ByteBuffer;", {VmValue::Ref(bytes("0061626300"))});
                invoke(buffer, "position", "(I)Ljava/nio/Buffer;", {VmValue::Int(1)});
                invoke(buffer, "limit", "(I)Ljava/nio/Buffer;", {VmValue::Int(4)});
                if (mode == 2) buffer = invoke(buffer, "asReadOnlyBuffer", "()Ljava/nio/ByteBuffer;", {}).ref;
                invoke(hash, "update", "(Ljava/nio/ByteBuffer;)V", {VmValue::Ref(buffer)});
                CHECK(invoke(buffer, "position", "()I", {}).AsInt() == 4);
                CHECK(read(invoke(hash, "digest", "()[B", {}).ref) == read(bytes(vector[4])));
            }
            // No total-message or single-update cap: native uses a bounded scratch buffer.
            std::vector<std::byte> binary(256 * 4097);
            for (std::size_t i = 0; i < binary.size(); ++i) binary[i] = static_cast<std::byte>(i & 255);
            const auto large = vm.Model().NewPrimitiveArray(linker.ResolveDescriptor("[B"), runtime::JniPrimitiveKind::byte, static_cast<runtime::JniSize>(binary.size()));
            vm.Model().WriteByteRegion(large, 0, binary);
            CHECK(read(invoke(hash, "digest", "([B)[B", {VmValue::Ref(large)}).ref) == read(bytes(vector[5])));
            for (const bool input : {true, false}) {
                const char* source_type = input ? "Ljava/io/ByteArrayInputStream;" : "Ljava/io/ByteArrayOutputStream;";
                const char* filter_type = input ? "Ljava/security/DigestInputStream;" : "Ljava/security/DigestOutputStream;";
                const auto source = vm.NewIntrinsicInstance(source_type);
                direct(source_type, "<init>", input ? "([B)V" : "()V",
                       input ? std::vector{VmValue::Ref(source), VmValue::Ref(bytes("61626378"))} : std::vector{VmValue::Ref(source)});
                const auto filter = vm.NewIntrinsicInstance(filter_type);
                const auto stream_roots = vm.ProtectReferences(std::array{source, filter});
                direct(filter_type, "<init>", input ? "(Ljava/io/InputStream;Ljava/security/MessageDigest;)V" : "(Ljava/io/OutputStream;Ljava/security/MessageDigest;)V",
                       {VmValue::Ref(filter), VmValue::Ref(source), VmValue::Ref(hash)});
                CHECK(invoke(filter, "getMessageDigest", "()Ljava/security/MessageDigest;", {}).ref == hash);
                if (input) {
                    CHECK(invoke(filter, "read", "()I", {}).AsInt() == 97);
                    CHECK(invoke(filter, "read", "([BII)I", {VmValue::Ref(bytes("0000")), VmValue::Int(0), VmValue::Int(2)}).AsInt() == 2);
                } else {
                    invoke(filter, "write", "(I)V", {VmValue::Int(97)});
                    invoke(filter, "write", "([BII)V", {VmValue::Ref(bytes("6263")), VmValue::Int(0), VmValue::Int(2)});
                }
                invoke(filter, "on", "(Z)V", {VmValue::Int(0)});
                if (input) {
                    CHECK(invoke(filter, "read", "()I", {}).AsInt() == 120);
                    CHECK(invoke(filter, "read", "()I", {}).AsInt() == -1);
                } else {
                    invoke(filter, "write", "(I)V", {VmValue::Int(120)});
                    CHECK(read(invoke(source, "toByteArray", "()[B", {}).ref) == read(bytes("61626378")));
                }
                CHECK(read(invoke(hash, "digest", "()[B", {}).ref) == read(bytes(vector[4])));
            }
            CHECK(vm.GuestNativeResourceCount() == 0);
        }
        const auto get_digest = linker.FindDirectMethod(linker.ResolveDescriptor("Ljava/security/MessageDigest;"), "getInstance", "(Ljava/lang/String;)Ljava/security/MessageDigest;");
        REQUIRE(get_digest);
        for (const auto* unavailable : {"SHA-224", "SHA3-256", "HmacSHA256", "unknown"})
            expect(vm.Call(*get_digest, std::array{VmValue::Ref(vm.NewStringUtf8(unavailable))}), "Ljava/security/NoSuchAlgorithmException;");
        for (const auto& [input, answer] : std::array{
            std::pair{"", "d41d8cd98f00b204e9800998ecf8427e"},
            std::pair{"61", "0cc175b9c0f1b6a831c399e269772661"},
            std::pair{"616263", "900150983cd24fb0d6963f7d28e17f72"}}) {
            CHECK(read(invoke(digest, "digest", "([B)[B", {VmValue::Ref(bytes(input))}).ref) == read(bytes(answer)));
            CHECK(vm.GuestNativeResourceCount() == 0);
        }
        invoke(digest, "update", "([B)V", {VmValue::Ref(bytes("61"))});
        CHECK(vm.GuestNativeResourceCount() == 1);
        const auto clone = invoke(digest, "clone", "()Ljava/lang/Object;", {}).ref;
        const auto clone_roots = vm.ProtectReferences(std::array{clone});
        CHECK(vm.GuestNativeResourceCount() == 2);
        invoke(digest, "update", "([B)V", {VmValue::Ref(bytes("6263"))});
        CHECK(read(invoke(digest, "digest", "()[B", {}).ref) == read(bytes("900150983cd24fb0d6963f7d28e17f72")));
        CHECK(read(invoke(clone, "digest", "()[B", {}).ref) == read(bytes("0cc175b9c0f1b6a831c399e269772661")));
        invoke(digest, "update", "(B)V", {VmValue::Int(97)});
        invoke(digest, "reset", "()V", {});
        CHECK(vm.GuestNativeResourceCount() == 0);
        CHECK(read(invoke(digest, "digest", "()[B", {}).ref) == read(bytes("d41d8cd98f00b204e9800998ecf8427e")));
        expect(raw(digest, "update", "([BII)V", {VmValue::Ref(bytes("61")), VmValue::Int(-1), VmValue::Int(1)}), "Ljava/lang/ArrayIndexOutOfBoundsException;");
        expect(raw(digest, "digest", "([BII)I", {VmValue::Ref(bytes("0000")), VmValue::Int(0), VmValue::Int(2)}), "Ljava/security/DigestException;");
        invoke(digest, "reset", "()V", {});
        // An abandoned shallow clone must not retire the original's shared token.
        invoke(digest, "update", "([B)V", {VmValue::Ref(bytes("61"))});
        static_cast<void>(vm.CloneObject(digest));
        static_cast<void>(vm.CollectGarbage("dvm108-shallow-clone"));
        CHECK(vm.GuestNativeResourceCount() == 1);
        CHECK(read(invoke(digest, "digest", "()[B", {}).ref) == read(bytes("0cc175b9c0f1b6a831c399e269772661")));
        const auto abandoned = direct("Ljava/security/MessageDigest;", "getInstance", "(Ljava/lang/String;)Ljava/security/MessageDigest;", {VmValue::Ref(vm.NewStringUtf8("MD5"))}).ref;
        invoke(abandoned, "update", "(B)V", {VmValue::Int(1)});
        CHECK(vm.GuestNativeResourceCount() == 1);
        static_cast<void>(vm.CollectGarbage("dvm108-digest-owner"));
        CHECK(vm.GuestNativeResourceCount() == 0);
        // Native entry points must reject opaque/stale tokens even when bypassing JCA.
        const auto native = linker.ResolveDescriptor("Lcom/android/org/conscrypt/NativeCrypto;");
        const auto lookup = *linker.FindDirectMethod(native, "EVP_get_digestbyname", "(Ljava/lang/String;)J");
        const auto size = *linker.FindDirectMethod(native, "EVP_MD_size", "(J)I");
        const auto init = *linker.FindDirectMethod(native, "EVP_DigestInit", "(J)J");
        const auto cleanup = *linker.FindDirectMethod(native, "EVP_MD_CTX_destroy", "(J)V");
        const auto copy_context = *linker.FindDirectMethod(native, "EVP_MD_CTX_copy", "(J)J");
        CHECK(vm.Call(lookup, std::array{VmValue::Ref(vm.NewStringUtf8("unknown"))}).value.AsLong() == 0);
        CHECK(vm.Call(size, std::array{VmValue::Long(0)}).exception.IsValid());
        CHECK(vm.Call(init, std::array{VmValue::Long(99)}).exception.IsValid());
        const auto algorithm = vm.Call(lookup, std::array{VmValue::Ref(vm.NewStringUtf8("sha512"))}).value.AsLong();
        const auto token = vm.Call(init, std::array{VmValue::Long(algorithm)}).value.AsLong();
        REQUIRE(token > 0);
        static_cast<void>(vm.Call(cleanup, std::array{VmValue::Long(token)}));
        CHECK(vm.Call(cleanup, std::array{VmValue::Long(token)}).exception.IsValid());
        CHECK(vm.Call(copy_context, std::array{VmValue::Long(token)}).exception.IsValid());
        const auto name_uuid = direct("Ljava/util/UUID;", "nameUUIDFromBytes", "([B)Ljava/util/UUID;", {VmValue::Ref(bytes("616263"))}).ref;
        CHECK(vm.StringUtf8(invoke(name_uuid, "toString", "()Ljava/lang/String;", {}).ref) == "90015098-3cd2-3fb0-9696-3f7d28e17f72");
        CHECK(invoke(name_uuid, "version", "()I", {}).AsInt() == 3);
        CHECK(invoke(name_uuid, "variant", "()I", {}).AsInt() == 2);
        const auto parsed = direct("Ljava/util/UUID;", "fromString", "(Ljava/lang/String;)Ljava/util/UUID;", {VmValue::Ref(vm.NewStringUtf8("f81d4fae-7dec-11d0-a765-00a0c91e6bf6"))}).ref;
        const auto uuid_roots = vm.ProtectReferences(std::array{parsed});
        CHECK(invoke(parsed, "version", "()I", {}).AsInt() == 1);
        CHECK(invoke(parsed, "timestamp", "()J", {}).AsLong() == INT64_C(130742845922168750));
        CHECK(invoke(parsed, "clockSequence", "()I", {}).AsInt() == 0x2765);
        CHECK(invoke(parsed, "node", "()J", {}).AsLong() == INT64_C(0x00a0c91e6bf6));
        const auto short_uuid = direct("Ljava/util/UUID;", "fromString", "(Ljava/lang/String;)Ljava/util/UUID;", {VmValue::Ref(vm.NewStringUtf8("1-1-1-1-1"))}).ref;
        CHECK(vm.StringUtf8(invoke(short_uuid, "toString", "()Ljava/lang/String;", {}).ref) == "00000001-0001-0001-0001-000000000001");
        std::set<std::string> randoms;
        for (int i = 0; i < 16; ++i) {
            const auto uuid = direct("Ljava/util/UUID;", "randomUUID", "()Ljava/util/UUID;", {}).ref;
            CHECK(invoke(uuid, "version", "()I", {}).AsInt() == 4);
            CHECK(invoke(uuid, "variant", "()I", {}).AsInt() == 2);
            randoms.insert(vm.StringUtf8(invoke(uuid, "toString", "()Ljava/lang/String;", {}).ref));
            expect(raw(uuid, "timestamp", "()J", {}), "Ljava/lang/UnsupportedOperationException;");
        }
        CHECK(randoms.size() == 16);
        CHECK(direct("Lfixture/UuidThreads;", "exercise", "()I", {}).AsInt() == 32);
        // Publish the real BootDex class to the JNI method registry.
        auto& bridge = app->DexVm();
        static_cast<void>(bridge.PublishLocal(vm.Model().ClassObject(linker.ResolveDescriptor("Ljava/util/UUID;"))));
        auto& process = app->NativeProcess();
        const auto uuid_class = process.Classes().FindClass("java/util/UUID");
        REQUIRE(uuid_class.has_value());
        const auto random_method = process.Classes().GetMethodId(*uuid_class, "randomUUID", "()Ljava/util/UUID;", true);
        REQUIRE(random_method.has_value());
        const auto jni_uuid = std::get<runtime::JniReference>(process.Invocations().InvokeStatic(1U, *uuid_class, *random_method, {}, runtime::JniArgumentSource::value_array));
        const auto actual = bridge.FromReference(jni_uuid);
        CHECK(vm.Model().ObjectClass(actual) == linker.ResolveDescriptor("Ljava/util/UUID;"));
        CHECK(invoke(actual, "version", "()I", {}).AsInt() == 4);
        const auto to_string = process.Classes().GetMethodId(*uuid_class, "toString", "()Ljava/lang/String;", false);
        REQUIRE(to_string.has_value());
        const auto jni_text = std::get<runtime::JniReference>(process.Invocations().InvokeVirtual(1U, jni_uuid, *uuid_class, *to_string, {}, runtime::JniArgumentSource::value_array));
        CHECK(vm.StringUtf8(bridge.FromReference(jni_text)) == vm.StringUtf8(invoke(actual, "toString", "()Ljava/lang/String;", {}).ref));
        invoke(digest, "update", "(B)V", {VmValue::Int(7)});
        vm.ReleaseGuestNativeResources(true);
        CHECK(vm.GuestNativeResourceCount() == 0);
        // The cleared field also makes a later Java reset/finalizer safe.
        invoke(digest, "reset", "()V", {});
        static_cast<void>(app->Stop());
    }
}
