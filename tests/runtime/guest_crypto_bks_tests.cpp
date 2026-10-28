#include <tuple>
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

TEST_CASE("DVM-105/169/175-180 crypto and BKS use BootDex and real guest libcrypto") {
    using namespace ogplay;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        CAPTURE(backend == InterpreterBackend::threaded ? "threaded" : "switch");
        const auto sandbox_root =
            std::filesystem::temp_directory_path() /
            ("ogplay-keystore-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

        struct Cleanup {
            std::filesystem::path path;

            ~Cleanup() {
                std::error_code ec;
                std::filesystem::remove_all(path, ec);
            }
        } sandbox_cleanup{sandbox_root};

        std::filesystem::create_directories(sandbox_root);
        auto sandbox = runtime::SandboxStore::Open(sandbox_root, "fixture", "fixture");
        const std::array<std::string, 1> sandbox_roots{"/data/data/fixture"};
        runtime::VirtualFileSystem filesystem;
        filesystem.AttachSandbox(*sandbox, sandbox_roots);
        core::CapabilityLedger ledger;
        core::Logger logger;
        std::vector<std::vector<std::byte>> contents;
        std::vector<runtime::BionicModuleSource> libraries;
        for (const auto name :
             {"libc.so", "libm.so", "libdl.so", "libstdc++.so", "libz.so", "libcrypto.so", "libssl.so", "libgabi++.so",
              "libicui18n.so", "libicuuc.so", "libstlport.so", "libogplay_jni.so"}) {
            std::ifstream stream(std::string(OGPLAY_SOURCE_DIR) + "/data/android/19/lib/" + name, std::ios::binary);
            REQUIRE_MESSAGE(stream.good(), name);
            std::vector<char> data{std::istreambuf_iterator<char>(stream), {}};
            contents.emplace_back(data.size());
            std::transform(data.begin(), data.end(), contents.back().begin(),
                           [](char c) { return static_cast<std::byte>(c); });
            libraries.push_back({name, contents.back()});
        }
        auto context = std::make_shared<runtime::DexVmAndroidContext>();
        test::BindBootDexArchive(*context);
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
        {
            auto app = session::AndroidAppProcess::Create(std::move(request));
            auto& vm = app->DexVm().Vm();
            auto& linker = vm.Linker();
            const auto direct = [&](const char* owner, const char* name, const char* desc, std::vector<VmValue> args) {
                auto type = linker.FindClass(owner);
                REQUIRE(type.has_value());
                auto method = linker.FindDirectMethod(*type, name, desc);
                REQUIRE_MESSAGE(method.has_value(), name);
                auto result = vm.Call(*method, args);
                REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
                return result.value;
            };
            const auto invoke = [&](VmObjectRef obj, const char* name, const char* desc, std::vector<VmValue> args) {
                auto type = vm.Model().ObjectClass(obj);
                auto slot = linker.FindVtableIndex(type, name, desc);
                REQUIRE_MESSAGE(slot.has_value(), name);
                args.insert(args.begin(), VmValue::Ref(obj));
                auto result = vm.Call(linker.Class(type).vtable[*slot], args);
                REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
                return result.value;
            };
            const auto invoke_result = [&](VmObjectRef obj, const char* name, const char* desc,
                                           std::vector<VmValue> args) {
                auto type = vm.Model().ObjectClass(obj);
                auto slot = linker.FindVtableIndex(type, name, desc);
                REQUIRE_MESSAGE(slot.has_value(), name);
                args.insert(args.begin(), VmValue::Ref(obj));
                return vm.Call(linker.Class(type).vtable[*slot], args);
            };
            const auto bytes = [&](const char* hex) {
                std::vector<std::byte> data;
                for (std::size_t i = 0; hex[i]; i += 2)
                    data.push_back(static_cast<std::byte>(std::stoul(std::string(hex + i, 2), nullptr, 16)));
                auto array =
                    vm.Model().NewPrimitiveArray(linker.ResolveDescriptor("[B"), runtime::JniPrimitiveKind::byte,
                                                 static_cast<runtime::JniSize>(data.size()));
                vm.Model().WriteByteRegion(array, 0, data);
                return array;
            };
            const auto chars = [&](std::string_view text) {
                auto array =
                    vm.Model().NewPrimitiveArray(linker.ResolveDescriptor("[C"), runtime::JniPrimitiveKind::character,
                                                 static_cast<runtime::JniSize>(text.size()));
                for (std::size_t index = 0; index < text.size(); ++index)
                    vm.Model().SetPrimitiveElement(array, static_cast<runtime::JniSize>(index),
                                                   static_cast<std::uint16_t>(text[index]));
                return array;
            };
            // DVM-199: public JCA dispatch and PKCS12/SHA1 vectors from isolated BC 1.50.
            {
                constexpr const char* factory_type = "Ljavax/crypto/SecretKeyFactory;";
                constexpr const char* spec_type = "Ljavax/crypto/spec/PBEKeySpec;";
                const auto factory =
                    direct(factory_type, "getInstance", "(Ljava/lang/String;)Ljavax/crypto/SecretKeyFactory;",
                           {VmValue::Ref(vm.NewStringUtf8("PBEWITHSHAAND256BITAES-CBC-BC"))})
                        .ref;
                const auto factory_root = vm.ProtectReferences(std::array{factory});
                const auto provider = invoke(factory, "getProvider", "()Ljava/security/Provider;", {}).ref;
                CHECK(vm.StringUtf8(invoke(provider, "getName", "()Ljava/lang/String;", {}).ref) == "OGPlayCrypto");
                const std::array<std::u16string, 3> passwords{
                    u"password", u"", std::u16string{u'\u5bc6', u'\u7801', u'\0', 0xd83d, 0xde00}};
                const std::array<const char*, 6> vectors{
                    "be7f5f21b43a293a45aa36715521dcc91d0554d5b1b1a5d6fdf5b9d110b276eb",
                    "00191210ddc2ed6768d2095bc40c2fad2305b914cd7c06ddba975183a3ca71f6",
                    "39d3776b5e1e17a1db1cb6db5ebea05d6ea2fc0edf0c48cfac68d756dea8c4e4",
                    "71fc9f3c005dcb294b766c8461072b96b56a357fa30cf074e3c78a4c7d8f13c3",
                    "43b902d0b714c6757aaa58e5fc616eb597aae2e1201f6162b0530e767c03b520",
                    "6180ffff036ea371846dc7fa7890fb35134b0b7d9caea7b882017f97e18cce66"};
                std::size_t vector_index = 0;
                for (const auto& password : passwords) {
                    for (const int iterations : {1, 1024}) {
                        const auto pass = vm.Model().NewPrimitiveArray(linker.ResolveDescriptor("[C"),
                                                                       runtime::JniPrimitiveKind::character,
                                                                       static_cast<runtime::JniSize>(password.size()));
                        for (std::size_t i = 0; i < password.size(); ++i)
                            vm.Model().SetPrimitiveElement(pass, static_cast<runtime::JniSize>(i), password[i]);
                        const auto spec = vm.NewIntrinsicInstance(spec_type);
                        const auto spec_roots = vm.ProtectReferences(std::array{pass, spec});
                        direct(spec_type, "<init>", "([C[BII)V",
                               {VmValue::Ref(spec), VmValue::Ref(pass), VmValue::Ref(bytes("0102030405060708")),
                                VmValue::Int(iterations), VmValue::Int(128)});
                        const auto secret =
                            invoke(factory, "generateSecret", "(Ljava/security/spec/KeySpec;)Ljavax/crypto/SecretKey;",
                                   {VmValue::Ref(spec)})
                                .ref;
                        const auto secret_root = vm.ProtectReferences(std::array{secret});
                        const auto encoded = invoke(secret, "getEncoded", "()[B", {}).ref;
                        const auto encoded_root = vm.ProtectReferences(std::array{encoded});
                        CHECK(vm.Model().ArrayLength(encoded) == 32);  // named algorithm ignores requested 128 bits
                        CHECK(vm.Model().ReadByteRegion(encoded, 0, 32) ==
                              vm.Model().ReadByteRegion(bytes(vectors[vector_index++]), 0, 32));
                        CHECK(vm.StringUtf8(invoke(secret, "getAlgorithm", "()Ljava/lang/String;", {}).ref) ==
                              "PBEWithSHA1And256BitAES-CBC-BC");
                        CHECK(vm.StringUtf8(invoke(secret, "getFormat", "()Ljava/lang/String;", {}).ref) == "RAW");
                        CHECK(invoke(secret, "getIterationCount", "()I", {}).AsInt() == iterations);
                        const auto translated =
                            invoke(factory, "translateKey", "(Ljavax/crypto/SecretKey;)Ljavax/crypto/SecretKey;",
                                   {VmValue::Ref(secret)})
                                .ref;
                        CHECK(vm.Model().ReadByteRegion(invoke(translated, "getEncoded", "()[B", {}).ref, 0, 32) ==
                              vm.Model().ReadByteRegion(encoded, 0, 32));
                        for (const auto* target :
                             {"Ljavax/crypto/spec/SecretKeySpec;", "Ljavax/crypto/spec/DESKeySpec;",
                              "Ljavax/crypto/spec/DESedeKeySpec;"}) {
                            const auto converted =
                                invoke(factory, "getKeySpec",
                                       "(Ljavax/crypto/SecretKey;Ljava/lang/Class;)Ljava/security/spec/KeySpec;",
                                       {VmValue::Ref(secret),
                                        VmValue::Ref(vm.Model().ClassObject(linker.ResolveDescriptor(target)))})
                                    .ref;
                            const bool raw = std::string_view(target).find("SecretKeySpec") != std::string_view::npos;
                            const auto converted_bytes =
                                invoke(converted, raw ? "getEncoded" : "getKey", "()[B", {}).ref;
                            const auto length = vm.Model().ArrayLength(converted_bytes);
                            CHECK(vm.Model().ReadByteRegion(converted_bytes, 0, length) ==
                                  vm.Model().ReadByteRegion(encoded, 0, length));
                        }
                        const auto invalid_spec =
                            invoke_result(factory, "getKeySpec",
                                          "(Ljavax/crypto/SecretKey;Ljava/lang/Class;)Ljava/security/spec/KeySpec;",
                                          {VmValue::Ref(secret),
                                           VmValue::Ref(vm.Model().ClassObject(linker.ResolveDescriptor(spec_type)))});
                        REQUIRE(invalid_spec.exception.IsValid());
                        CHECK(linker.Class(invalid_spec.exception_class).descriptor ==
                              "Ljava/security/spec/InvalidKeySpecException;");
                        invoke(spec, "clearPassword", "()V", {});
                        CHECK(invoke_result(secret, "getPassword", "()[C", {}).exception.IsValid());
                        CHECK(vm.Model().ArrayLength(invoke(secret, "getEncoded", "()[B", {}).ref) == 32);
                    }
                }
                const auto unsalted = vm.NewIntrinsicInstance(spec_type);
                const auto unsalted_root = vm.ProtectReferences(std::array{unsalted});
                direct(spec_type, "<init>", "([C)V", {VmValue::Ref(unsalted), VmValue::Ref(chars("ab"))});
                const auto deferred =
                    invoke(factory, "generateSecret", "(Ljava/security/spec/KeySpec;)Ljavax/crypto/SecretKey;",
                           {VmValue::Ref(unsalted)})
                        .ref;
                const auto deferred_root = vm.ProtectReferences(std::array{deferred});
                CHECK(vm.Model().ReadByteRegion(invoke(deferred, "getEncoded", "()[B", {}).ref, 0, 6) ==
                      vm.Model().ReadByteRegion(bytes("006100620000"), 0, 6));
                const auto invalid =
                    invoke_result(factory, "generateSecret", "(Ljava/security/spec/KeySpec;)Ljavax/crypto/SecretKey;",
                                  {VmValue::Ref(VmObjectRef{})});
                REQUIRE(invalid.exception.IsValid());
                CHECK(linker.Class(invalid.exception_class).descriptor ==
                      "Ljava/security/spec/InvalidKeySpecException;");
                direct(spec_type, "<init>", "([C[BII)V",
                       {VmValue::Ref(unsalted), VmValue::Ref(chars("p")), VmValue::Ref(bytes("01")),
                        VmValue::Int(1000001), VmValue::Int(256)});
                const auto over_limit =
                    invoke_result(factory, "generateSecret", "(Ljava/security/spec/KeySpec;)Ljavax/crypto/SecretKey;",
                                  {VmValue::Ref(unsalted)});
                REQUIRE(over_limit.exception.IsValid());
                CHECK(linker.Class(over_limit.exception_class).descriptor ==
                      "Ljava/security/spec/InvalidKeySpecException;");
                for (const auto* alias : {"PBEWITHSHA1AND256BITAES-CBC-BC", "pbewithsha-1and256bitaes-cbc-bc"}) {
                    const auto other =
                        direct(factory_type, "getInstance",
                               "(Ljava/lang/String;Ljava/security/Provider;)Ljavax/crypto/SecretKeyFactory;",
                               {VmValue::Ref(vm.NewStringUtf8(alias)), VmValue::Ref(provider)})
                            .ref;
                    CHECK(other.IsValid());
                    CHECK(
                        direct(factory_type, "getInstance",
                               "(Ljava/lang/String;Ljava/lang/String;)Ljavax/crypto/SecretKeyFactory;",
                               {VmValue::Ref(vm.NewStringUtf8(alias)), VmValue::Ref(vm.NewStringUtf8("OGPlayCrypto"))})
                            .ref.IsValid());
                }
                const auto wrong_key = vm.NewIntrinsicInstance("Ljavax/crypto/spec/SecretKeySpec;");
                const auto wrong_key_root = vm.ProtectReferences(std::array{wrong_key});
                direct("Ljavax/crypto/spec/SecretKeySpec;", "<init>", "([BLjava/lang/String;)V",
                       {VmValue::Ref(wrong_key), VmValue::Ref(bytes("0102030405060708")),
                        VmValue::Ref(vm.NewStringUtf8("AES"))});
                for (const auto bad_key : {VmObjectRef{}, wrong_key}) {
                    const auto rejected =
                        invoke_result(factory, "translateKey", "(Ljavax/crypto/SecretKey;)Ljavax/crypto/SecretKey;",
                                      {VmValue::Ref(bad_key)});
                    REQUIRE(rejected.exception.IsValid());
                    CHECK(linker.Class(rejected.exception_class).descriptor == "Ljava/security/InvalidKeyException;");
                }
                const auto huge_password = vm.Model().NewPrimitiveArray(linker.ResolveDescriptor("[C"),
                                                                        runtime::JniPrimitiveKind::character, 65537);
                direct(spec_type, "<init>", "([C)V", {VmValue::Ref(unsalted), VmValue::Ref(huge_password)});
                CHECK(invoke_result(factory, "generateSecret", "(Ljava/security/spec/KeySpec;)Ljavax/crypto/SecretKey;",
                                    {VmValue::Ref(unsalted)})
                          .exception.IsValid());
                const auto factory_class = linker.FindClass(factory_type);
                const auto get_instance = linker.FindDirectMethod(
                    *factory_class, "getInstance", "(Ljava/lang/String;)Ljavax/crypto/SecretKeyFactory;");
                const auto unsupported =
                    vm.Call(*get_instance, std::array{VmValue::Ref(vm.NewStringUtf8("PBKDF2WithHmacSHA1"))});
                REQUIRE(unsupported.exception.IsValid());
                CHECK(linker.Class(unsupported.exception_class).descriptor ==
                      "Ljava/security/NoSuchAlgorithmException;");
            }
            const auto kdf_password =
                vm.Model().NewPrimitiveArray(linker.ResolveDescriptor("[C"), runtime::JniPrimitiveKind::character, 8);
            constexpr std::string_view password_text = "password";
            for (std::size_t index = 0; index < password_text.size(); ++index) {
                vm.Model().SetPrimitiveElement(kdf_password, static_cast<runtime::JniSize>(index),
                                               static_cast<std::uint16_t>(password_text[index]));
            }
            const auto derived = direct("Lorg/ogplay/security/Pkcs12Kdf;", "derive", "([C[BIII)[B",
                                        {VmValue::Ref(kdf_password), VmValue::Ref(bytes("0102030405060708")),
                                         VmValue::Int(2048), VmValue::Int(3), VmValue::Int(20)})
                                     .ref;
            CHECK(vm.Model().ReadByteRegion(derived, 0, 20) ==
                  vm.Model().ReadByteRegion(bytes("e41bf25c2ea77685923a7b4bb231021e7e045ee4"), 0, 20));
            const auto oracle_path =
                std::filesystem::path(OGPLAY_SOURCE_DIR) / "tests/fixtures/keystore/mixed-api19.bks.b64";
            REQUIRE(std::filesystem::is_regular_file(oracle_path));
            std::ifstream oracle_stream(oracle_path);
            const std::string oracle_base64{std::istreambuf_iterator<char>(oracle_stream), {}};
            const auto decoded_oracle = ogplay::core::DecodeBase64(oracle_base64);
            REQUIRE(decoded_oracle.has_value());
            const auto& oracle_bytes = *decoded_oracle;
            const auto oracle_array =
                vm.Model().NewPrimitiveArray(linker.ResolveDescriptor("[B"), runtime::JniPrimitiveKind::byte,
                                             static_cast<runtime::JniSize>(oracle_bytes.size()));
            vm.Model().WriteByteRegion(oracle_array, 0, oracle_bytes);
            const auto public_input = vm.NewIntrinsicInstance("Ljava/io/ByteArrayInputStream;");
            direct("Ljava/io/ByteArrayInputStream;", "<init>", "([B)V",
                   {VmValue::Ref(public_input), VmValue::Ref(oracle_array)});
            const auto public_store =
                direct("Ljava/security/KeyStore;", "getInstance", "(Ljava/lang/String;)Ljava/security/KeyStore;",
                       {VmValue::Ref(vm.NewStringUtf8("BKS"))})
                    .ref;
            const auto public_password = chars("storepass");
            const auto public_key_password = chars("keypass");
            const auto public_roots =
                vm.ProtectReferences(std::array{public_input, public_store, public_password, public_key_password});
            invoke(public_store, "load", "(Ljava/io/InputStream;[C)V",
                   {VmValue::Ref(public_input), VmValue::Ref(public_password)});
            CHECK(invoke(public_store, "size", "()I", {}).AsInt() == 4);
            CHECK(
                invoke(public_store, "isKeyEntry", "(Ljava/lang/String;)Z", {VmValue::Ref(vm.NewStringUtf8("secret"))})
                    .AsInt() != 0);
            const auto password_protection = vm.NewIntrinsicInstance("Ljava/security/KeyStore$PasswordProtection;");
            direct("Ljava/security/KeyStore$PasswordProtection;", "<init>", "([C)V",
                   {VmValue::Ref(password_protection), VmValue::Ref(public_key_password)});
            const auto public_entry =
                invoke(public_store, "getEntry",
                       "(Ljava/lang/String;Ljava/security/KeyStore$ProtectionParameter;)Ljava/security/KeyStore$Entry;",
                       {VmValue::Ref(vm.NewStringUtf8("secret")), VmValue::Ref(password_protection)})
                    .ref;
            CHECK(linker.Class(vm.Model().ObjectClass(public_entry)).descriptor ==
                  "Ljava/security/KeyStore$SecretKeyEntry;");
            const auto builder =
                direct(
                    "Ljava/security/KeyStore$Builder;", "newInstance",
                    "(Ljava/security/KeyStore;Ljava/security/KeyStore$ProtectionParameter;)Ljava/security/KeyStore$Builder;",
                    {VmValue::Ref(public_store), VmValue::Ref(password_protection)})
                    .ref;
            const auto builder_roots = vm.ProtectReferences(std::array{password_protection, public_entry, builder});
            CHECK(invoke(builder, "getKeyStore", "()Ljava/security/KeyStore;", {}).ref == public_store);
            CHECK(invoke(builder, "getProtectionParameter",
                         "(Ljava/lang/String;)Ljava/security/KeyStore$ProtectionParameter;",
                         {VmValue::Ref(vm.NewStringUtf8("secret"))})
                      .ref == password_protection);
            // API 19 KeyStoreSpi resolves the configured default CallbackHandler.
            direct("Ljava/security/Security;", "setProperty", "(Ljava/lang/String;Ljava/lang/String;)V",
                   {VmValue::Ref(vm.NewStringUtf8("auth.login.defaultCallbackHandler")),
                    VmValue::Ref(vm.NewStringUtf8("fixture.KeyPasswordHandler"))});
            const auto handler = vm.NewIntrinsicInstance("Lfixture/KeyPasswordHandler;");
            direct("Lfixture/KeyPasswordHandler;", "<init>", "()V", {VmValue::Ref(handler)});
            const auto protection = vm.NewIntrinsicInstance("Ljava/security/KeyStore$CallbackHandlerProtection;");
            const auto callback_roots = vm.ProtectReferences(std::array{handler, protection});
            direct("Ljava/security/KeyStore$CallbackHandlerProtection;", "<init>",
                   "(Ljavax/security/auth/callback/CallbackHandler;)V",
                   {VmValue::Ref(protection), VmValue::Ref(handler)});
            const auto callback_entry =
                invoke(public_store, "getEntry",
                       "(Ljava/lang/String;Ljava/security/KeyStore$ProtectionParameter;)Ljava/security/KeyStore$Entry;",
                       {VmValue::Ref(vm.NewStringUtf8("secret")), VmValue::Ref(protection)})
                    .ref;
            const auto callback_entry_roots = vm.ProtectReferences(std::array{callback_entry});
            const auto callback_key = invoke(callback_entry, "getSecretKey", "()Ljavax/crypto/SecretKey;", {}).ref;
            CHECK(vm.Model().ReadByteRegion(invoke(callback_key, "getEncoded", "()[B", {}).ref, 0, 16) ==
                  vm.Model().ReadByteRegion(bytes("000102030405060708090a0b0c0d0e0f"), 0, 16));
            CHECK(direct("Lfixture/KeyStoreWorkers;", "exercise", "(Ljava/security/KeyStore;)I",
                         {VmValue::Ref(public_store)})
                      .AsInt() == 40);
            CHECK(invoke(public_store, "size", "()I", {}).AsInt() == 4);
            const auto session_output = vm.NewIntrinsicInstance("Ljava/io/FileOutputStream;");
            direct("Ljava/io/FileOutputStream;", "<init>", "(Ljava/lang/String;)V",
                   {VmValue::Ref(session_output),
                    VmValue::Ref(vm.NewStringUtf8("/data/data/fixture/keystore-session.bks"))});
            const auto session_output_roots = vm.ProtectReferences(std::array{session_output});
            invoke(public_store, "store", "(Ljava/io/OutputStream;[C)V",
                   {VmValue::Ref(session_output), VmValue::Ref(public_password)});
            invoke(session_output, "close", "()V", {});
            const auto oracle_input = vm.NewIntrinsicInstance("Ljava/io/ByteArrayInputStream;");
            direct("Ljava/io/ByteArrayInputStream;", "<init>", "([B)V",
                   {VmValue::Ref(oracle_input), VmValue::Ref(oracle_array)});
            const auto store = vm.NewIntrinsicInstance("Lorg/ogplay/security/BksKeyStoreSpi;");
            direct("Lorg/ogplay/security/BksKeyStoreSpi;", "<init>", "()V", {VmValue::Ref(store)});
            const auto store_password = chars("storepass");
            const auto key_password = chars("keypass");
            const auto store_roots =
                vm.ProtectReferences(std::array{oracle_array, oracle_input, store, store_password, key_password});
            invoke(store, "engineLoad", "(Ljava/io/InputStream;[C)V",
                   {VmValue::Ref(oracle_input), VmValue::Ref(store_password)});
            CHECK(invoke(store, "engineSize", "()I", {}).AsInt() == 4);
            const auto rejected_load = [&](std::vector<std::byte> candidate, VmObjectRef password) {
                const auto encoded =
                    vm.Model().NewPrimitiveArray(linker.ResolveDescriptor("[B"), runtime::JniPrimitiveKind::byte,
                                                 static_cast<runtime::JniSize>(candidate.size()));
                vm.Model().WriteByteRegion(encoded, 0, candidate);
                const auto input = vm.NewIntrinsicInstance("Ljava/io/ByteArrayInputStream;");
                direct("Ljava/io/ByteArrayInputStream;", "<init>", "([B)V",
                       {VmValue::Ref(input), VmValue::Ref(encoded)});
                const auto target = vm.NewIntrinsicInstance("Lorg/ogplay/security/BksKeyStoreSpi;");
                direct("Lorg/ogplay/security/BksKeyStoreSpi;", "<init>", "()V", {VmValue::Ref(target)});
                const auto roots = vm.ProtectReferences(std::array{encoded, input, target});
                const auto result = invoke_result(target, "engineLoad", "(Ljava/io/InputStream;[C)V",
                                                  {VmValue::Ref(input), VmValue::Ref(password)});
                CHECK(result.exception.IsValid());
                CHECK(invoke(target, "engineSize", "()I", {}).AsInt() == 0);
            };
            rejected_load(oracle_bytes, chars("wrong"));
            auto tampered = oracle_bytes;
            tampered[tampered.size() / 2] ^= std::byte{1};
            rejected_load(std::move(tampered), store_password);
            auto truncated = oracle_bytes;
            truncated.resize(truncated.size() - 7);
            rejected_load(std::move(truncated), store_password);
            for (const auto no_password : {VmObjectRef{}, chars("")}) {
                const auto encoded =
                    vm.Model().NewPrimitiveArray(linker.ResolveDescriptor("[B"), runtime::JniPrimitiveKind::byte,
                                                 static_cast<runtime::JniSize>(oracle_bytes.size()));
                vm.Model().WriteByteRegion(encoded, 0, oracle_bytes);
                const auto input = vm.NewIntrinsicInstance("Ljava/io/ByteArrayInputStream;");
                direct("Ljava/io/ByteArrayInputStream;", "<init>", "([B)V",
                       {VmValue::Ref(input), VmValue::Ref(encoded)});
                const auto unauthenticated = vm.NewIntrinsicInstance("Lorg/ogplay/security/BksKeyStoreSpi;");
                direct("Lorg/ogplay/security/BksKeyStoreSpi;", "<init>", "()V", {VmValue::Ref(unauthenticated)});
                const auto roots = vm.ProtectReferences(std::array{encoded, input, unauthenticated, no_password});
                invoke(unauthenticated, "engineLoad", "(Ljava/io/InputStream;[C)V",
                       {VmValue::Ref(input), VmValue::Ref(no_password)});
                CHECK(invoke(unauthenticated, "engineSize", "()I", {}).AsInt() == 4);
                const auto protected_key =
                    invoke_result(unauthenticated, "engineGetKey", "(Ljava/lang/String;[C)Ljava/security/Key;",
                                  {VmValue::Ref(vm.NewStringUtf8("secret")), VmValue::Ref(chars("wrong"))});
                CHECK(protected_key.exception.IsValid());

                auto mac_tampered = oracle_bytes;
                mac_tampered.back() ^= std::byte{1};
                const auto tampered_array =
                    vm.Model().NewPrimitiveArray(linker.ResolveDescriptor("[B"), runtime::JniPrimitiveKind::byte,
                                                 static_cast<runtime::JniSize>(mac_tampered.size()));
                vm.Model().WriteByteRegion(tampered_array, 0, mac_tampered);
                const auto tampered_input = vm.NewIntrinsicInstance("Ljava/io/ByteArrayInputStream;");
                direct("Ljava/io/ByteArrayInputStream;", "<init>", "([B)V",
                       {VmValue::Ref(tampered_input), VmValue::Ref(tampered_array)});
                const auto tampered_store = vm.NewIntrinsicInstance("Lorg/ogplay/security/BksKeyStoreSpi;");
                direct("Lorg/ogplay/security/BksKeyStoreSpi;", "<init>", "()V", {VmValue::Ref(tampered_store)});
                const auto tampered_roots =
                    vm.ProtectReferences(std::array{tampered_array, tampered_input, tampered_store});
                invoke(tampered_store, "engineLoad", "(Ljava/io/InputStream;[C)V",
                       {VmValue::Ref(tampered_input), VmValue::Ref(no_password)});
                CHECK(invoke(tampered_store, "engineSize", "()I", {}).AsInt() == 4);
            }
            const auto wrong_key =
                invoke_result(store, "engineGetKey", "(Ljava/lang/String;[C)Ljava/security/Key;",
                              {VmValue::Ref(vm.NewStringUtf8("secret")), VmValue::Ref(chars("wrong"))});
            CHECK(wrong_key.exception.IsValid());
            const auto legacy_path =
                std::filesystem::path(OGPLAY_SOURCE_DIR) /
                ".local/aosp/libcore/luni/src/test/resources/org/apache/harmony/luni/tests/key_store.bks";
            std::ifstream legacy_stream(legacy_path, std::ios::binary);
            REQUIRE(legacy_stream.good());
            const std::vector<char> legacy_chars{std::istreambuf_iterator<char>(legacy_stream), {}};
            std::vector<std::byte> legacy_bytes(legacy_chars.size());
            std::transform(legacy_chars.begin(), legacy_chars.end(), legacy_bytes.begin(),
                           [](char value) { return static_cast<std::byte>(value); });
            const auto legacy_array =
                vm.Model().NewPrimitiveArray(linker.ResolveDescriptor("[B"), runtime::JniPrimitiveKind::byte,
                                             static_cast<runtime::JniSize>(legacy_bytes.size()));
            vm.Model().WriteByteRegion(legacy_array, 0, legacy_bytes);
            const auto legacy_input = vm.NewIntrinsicInstance("Ljava/io/ByteArrayInputStream;");
            direct("Ljava/io/ByteArrayInputStream;", "<init>", "([B)V",
                   {VmValue::Ref(legacy_input), VmValue::Ref(legacy_array)});
            const auto legacy_store = vm.NewIntrinsicInstance("Lorg/ogplay/security/BksKeyStoreSpi;");
            direct("Lorg/ogplay/security/BksKeyStoreSpi;", "<init>", "()V", {VmValue::Ref(legacy_store)});
            const auto legacy_password = chars("password");
            const auto legacy_roots =
                vm.ProtectReferences(std::array{legacy_array, legacy_input, legacy_store, legacy_password});
            invoke(legacy_store, "engineLoad", "(Ljava/io/InputStream;[C)V",
                   {VmValue::Ref(legacy_input), VmValue::Ref(legacy_password)});
            CHECK(invoke(legacy_store, "engineSize", "()I", {}).AsInt() == 1);
            const auto legacy_key =
                invoke(legacy_store, "engineGetKey", "(Ljava/lang/String;[C)Ljava/security/Key;",
                       {VmValue::Ref(vm.NewStringUtf8("ssl_test_store")), VmValue::Ref(legacy_password)})
                    .ref;
            CHECK(vm.StringUtf8(invoke(legacy_key, "getAlgorithm", "()Ljava/lang/String;", {}).ref) == "RSA");
            const auto v0_path = std::filesystem::path(OGPLAY_SOURCE_DIR) / "tests/fixtures/keystore/v0-api19.bks.b64";
            std::ifstream v0_stream(v0_path);
            REQUIRE(v0_stream.good());
            const std::string v0_base64{std::istreambuf_iterator<char>(v0_stream), {}};
            const auto decoded_v0 = ogplay::core::DecodeBase64(v0_base64);
            REQUIRE(decoded_v0.has_value());
            const auto v0_array =
                vm.Model().NewPrimitiveArray(linker.ResolveDescriptor("[B"), runtime::JniPrimitiveKind::byte,
                                             static_cast<runtime::JniSize>(decoded_v0->size()));
            vm.Model().WriteByteRegion(v0_array, 0, *decoded_v0);
            const auto v0_input = vm.NewIntrinsicInstance("Ljava/io/ByteArrayInputStream;");
            direct("Ljava/io/ByteArrayInputStream;", "<init>", "([B)V",
                   {VmValue::Ref(v0_input), VmValue::Ref(v0_array)});
            const auto v0_store = vm.NewIntrinsicInstance("Lorg/ogplay/security/BksKeyStoreSpi;");
            direct("Lorg/ogplay/security/BksKeyStoreSpi;", "<init>", "()V", {VmValue::Ref(v0_store)});
            const auto v0_roots = vm.ProtectReferences(std::array{v0_array, v0_input, v0_store});
            invoke(v0_store, "engineLoad", "(Ljava/io/InputStream;[C)V",
                   {VmValue::Ref(v0_input), VmValue::Ref(legacy_password)});
            CHECK(invoke(v0_store, "engineSize", "()I", {}).AsInt() == 1);
            CHECK(invoke(v0_store, "engineContainsAlias", "(Ljava/lang/String;)Z",
                         {VmValue::Ref(vm.NewStringUtf8("cert"))})
                      .AsInt() != 0);
            const auto unicode_path =
                std::filesystem::path(OGPLAY_SOURCE_DIR) / "tests/fixtures/keystore/unicode-api19.bks.b64";
            std::ifstream unicode_stream(unicode_path);
            REQUIRE(unicode_stream.good());
            const std::string unicode_base64{std::istreambuf_iterator<char>(unicode_stream), {}};
            const auto decoded_unicode = ogplay::core::DecodeBase64(unicode_base64);
            REQUIRE(decoded_unicode.has_value());
            const auto unicode_array =
                vm.Model().NewPrimitiveArray(linker.ResolveDescriptor("[B"), runtime::JniPrimitiveKind::byte,
                                             static_cast<runtime::JniSize>(decoded_unicode->size()));
            vm.Model().WriteByteRegion(unicode_array, 0, *decoded_unicode);
            const auto unicode_input = vm.NewIntrinsicInstance("Ljava/io/ByteArrayInputStream;");
            direct("Ljava/io/ByteArrayInputStream;", "<init>", "([B)V",
                   {VmValue::Ref(unicode_input), VmValue::Ref(unicode_array)});
            const auto unicode_store = vm.NewIntrinsicInstance("Lorg/ogplay/security/BksKeyStoreSpi;");
            direct("Lorg/ogplay/security/BksKeyStoreSpi;", "<init>", "()V", {VmValue::Ref(unicode_store)});
            const auto unicode_password =
                vm.Model().NewPrimitiveArray(linker.ResolveDescriptor("[C"), runtime::JniPrimitiveKind::character, 4);
            for (std::size_t i = 0; i < 4; ++i)
                vm.Model().SetPrimitiveElement(unicode_password, static_cast<runtime::JniSize>(i),
                                               std::array<std::uint16_t, 4>{0x5bc6, 0x7801, 0xd83d, 0xdd11}[i]);
            const auto unicode_roots =
                vm.ProtectReferences(std::array{unicode_array, unicode_input, unicode_store, unicode_password});
            invoke(unicode_store, "engineLoad", "(Ljava/io/InputStream;[C)V",
                   {VmValue::Ref(unicode_input), VmValue::Ref(unicode_password)});
            CHECK(invoke(unicode_store, "engineSize", "()I", {}).AsInt() == 1);
            const auto opaque_store = vm.NewIntrinsicInstance("Lorg/ogplay/security/BksKeyStoreSpi;");
            direct("Lorg/ogplay/security/BksKeyStoreSpi;", "<init>", "()V", {VmValue::Ref(opaque_store)});
            invoke(opaque_store, "engineLoad", "(Ljava/io/InputStream;[C)V",
                   {VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{})});
            const auto opaque_bytes = bytes("010203");
            const auto opaque_roots = vm.ProtectReferences(std::array{opaque_store, opaque_bytes});
            invoke(opaque_store, "engineSetKeyEntry", "(Ljava/lang/String;[B[Ljava/security/cert/Certificate;)V",
                   {VmValue::Ref(vm.NewStringUtf8("opaque")), VmValue::Ref(opaque_bytes), VmValue::Ref(VmObjectRef{})});
            CHECK(invoke(opaque_store, "engineIsKeyEntry", "(Ljava/lang/String;)Z",
                         {VmValue::Ref(vm.NewStringUtf8("opaque"))})
                      .AsInt() != 0);
            const auto opaque_get =
                invoke_result(opaque_store, "engineGetKey", "(Ljava/lang/String;[C)Ljava/security/Key;",
                              {VmValue::Ref(vm.NewStringUtf8("opaque")), VmValue::Ref(key_password)});
            CHECK(opaque_get.exception.IsValid());
            CHECK(linker.Class(opaque_get.exception_class).descriptor == "Ljava/lang/RuntimeException;");
            const auto opaque_output = vm.NewIntrinsicInstance("Ljava/io/ByteArrayOutputStream;");
            direct("Ljava/io/ByteArrayOutputStream;", "<init>", "()V", {VmValue::Ref(opaque_output)});
            invoke(opaque_store, "engineStore", "(Ljava/io/OutputStream;[C)V",
                   {VmValue::Ref(opaque_output), VmValue::Ref(store_password)});
            const auto opaque_encoded = invoke(opaque_output, "toByteArray", "()[B", {}).ref;
            CHECK(vm.Model().ReadByteRegion(opaque_encoded, 32, 1)[0] == std::byte{3});
            const auto opaque_input = vm.NewIntrinsicInstance("Ljava/io/ByteArrayInputStream;");
            direct("Ljava/io/ByteArrayInputStream;", "<init>", "([B)V",
                   {VmValue::Ref(opaque_input), VmValue::Ref(opaque_encoded)});
            const auto opaque_reloaded = vm.NewIntrinsicInstance("Lorg/ogplay/security/BksKeyStoreSpi;");
            direct("Lorg/ogplay/security/BksKeyStoreSpi;", "<init>", "()V", {VmValue::Ref(opaque_reloaded)});
            const auto opaque_reload_roots =
                vm.ProtectReferences(std::array{opaque_output, opaque_encoded, opaque_input, opaque_reloaded});
            invoke(opaque_reloaded, "engineLoad", "(Ljava/io/InputStream;[C)V",
                   {VmValue::Ref(opaque_input), VmValue::Ref(store_password)});
            CHECK(invoke(opaque_reloaded, "engineIsKeyEntry", "(Ljava/lang/String;)Z",
                         {VmValue::Ref(vm.NewStringUtf8("opaque"))})
                      .AsInt() != 0);
            const auto failing_output = vm.NewIntrinsicInstance("Lfixture/FailingOutputStream;");
            direct("Lfixture/FailingOutputStream;", "<init>", "(I)V", {VmValue::Ref(failing_output), VmValue::Int(8)});
            const auto failing_roots = vm.ProtectReferences(std::array{failing_output});
            const auto failed_write = invoke_result(opaque_store, "engineStore", "(Ljava/io/OutputStream;[C)V",
                                                    {VmValue::Ref(failing_output), VmValue::Ref(store_password)});
            CHECK(failed_write.exception.IsValid());
            CHECK(linker.Class(failed_write.exception_class).descriptor == "Ljava/io/IOException;");
            const auto oversized_store = vm.NewIntrinsicInstance("Lorg/ogplay/security/BksKeyStoreSpi;");
            direct("Lorg/ogplay/security/BksKeyStoreSpi;", "<init>", "()V", {VmValue::Ref(oversized_store)});
            invoke(oversized_store, "engineLoad", "(Ljava/io/InputStream;[C)V",
                   {VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{})});
            const auto one_megabyte = vm.Model().NewPrimitiveArray(linker.ResolveDescriptor("[B"),
                                                                   runtime::JniPrimitiveKind::byte, 1024 * 1024);
            const auto oversized_roots = vm.ProtectReferences(std::array{oversized_store, one_megabyte});
            for (int index = 0; index < 17; ++index) {
                invoke(oversized_store, "engineSetKeyEntry", "(Ljava/lang/String;[B[Ljava/security/cert/Certificate;)V",
                       {VmValue::Ref(vm.NewStringUtf8("blob" + std::to_string(index))), VmValue::Ref(one_megabyte),
                        VmValue::Ref(VmObjectRef{})});
            }
            const auto oversized_output = vm.NewIntrinsicInstance("Ljava/io/ByteArrayOutputStream;");
            direct("Ljava/io/ByteArrayOutputStream;", "<init>", "()V", {VmValue::Ref(oversized_output)});
            const auto oversized_result = invoke_result(oversized_store, "engineStore", "(Ljava/io/OutputStream;[C)V",
                                                        {VmValue::Ref(oversized_output), VmValue::Ref(store_password)});
            CHECK(oversized_result.exception.IsValid());
            CHECK(linker.Class(oversized_result.exception_class).descriptor == "Ljava/io/IOException;");
            CHECK(vm.Model().ArrayLength(invoke(oversized_output, "toByteArray", "()[B", {}).ref) == 0);
            const auto load_fixture = [&](const char* relative_path, VmObjectRef password) {
                std::ifstream stream(std::filesystem::path(OGPLAY_SOURCE_DIR) / relative_path);
                REQUIRE(stream.good());
                const std::string base64{std::istreambuf_iterator<char>(stream), {}};
                const auto decoded = ogplay::core::DecodeBase64(base64);
                REQUIRE(decoded.has_value());
                const auto encoded =
                    vm.Model().NewPrimitiveArray(linker.ResolveDescriptor("[B"), runtime::JniPrimitiveKind::byte,
                                                 static_cast<runtime::JniSize>(decoded->size()));
                vm.Model().WriteByteRegion(encoded, 0, *decoded);
                const auto input = vm.NewIntrinsicInstance("Ljava/io/ByteArrayInputStream;");
                direct("Ljava/io/ByteArrayInputStream;", "<init>", "([B)V",
                       {VmValue::Ref(input), VmValue::Ref(encoded)});
                const auto loaded = vm.NewIntrinsicInstance("Lorg/ogplay/security/BksKeyStoreSpi;");
                direct("Lorg/ogplay/security/BksKeyStoreSpi;", "<init>", "()V", {VmValue::Ref(loaded)});
                invoke(loaded, "engineLoad", "(Ljava/io/InputStream;[C)V",
                       {VmValue::Ref(input), VmValue::Ref(password)});
                return loaded;
            };
            for (const auto* historical : {"tests/fixtures/keystore/old-pbe-bc146.bks.b64",
                                           "tests/fixtures/keystore/broken-pbe-bc146.bks.b64"}) {
                CAPTURE(historical);
                const auto historical_store = load_fixture(historical, store_password);
                const auto historical_roots = vm.ProtectReferences(std::array{historical_store});
                const auto historical_key =
                    invoke(historical_store, "engineGetKey", "(Ljava/lang/String;[C)Ljava/security/Key;",
                           {VmValue::Ref(vm.NewStringUtf8("secret")), VmValue::Ref(key_password)})
                        .ref;
                CHECK(vm.StringUtf8(invoke(historical_key, "getAlgorithm", "()Ljava/lang/String;", {}).ref) == "AES");
                CHECK(vm.Model().ArrayLength(invoke(historical_key, "getEncoded", "()[B", {}).ref) == 16);
            }
            const auto rebuilt_store = vm.NewIntrinsicInstance("Lorg/ogplay/security/BksKeyStoreSpi;");
            direct("Lorg/ogplay/security/BksKeyStoreSpi;", "<init>", "()V", {VmValue::Ref(rebuilt_store)});
            invoke(rebuilt_store, "engineLoad", "(Ljava/io/InputStream;[C)V",
                   {VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{})});
            const auto rebuilt_roots = vm.ProtectReferences(std::array{rebuilt_store});
            for (const auto& expected_key :
                 std::array{std::pair{"secret", "AES"}, std::pair{"rsa", "RSA"}, std::pair{"ec", "EC"}}) {
                const auto restored =
                    invoke(store, "engineGetKey", "(Ljava/lang/String;[C)Ljava/security/Key;",
                           {VmValue::Ref(vm.NewStringUtf8(expected_key.first)), VmValue::Ref(key_password)})
                        .ref;
                const auto restored_roots = vm.ProtectReferences(std::array{restored});
                CHECK(vm.StringUtf8(invoke(restored, "getAlgorithm", "()Ljava/lang/String;", {}).ref) ==
                      expected_key.second);
                CHECK(invoke(restored, "getEncoded", "()[B", {}).ref.IsValid());
                const auto chain =
                    invoke(store, "engineGetCertificateChain", "(Ljava/lang/String;)[Ljava/security/cert/Certificate;",
                           {VmValue::Ref(vm.NewStringUtf8(expected_key.first))})
                        .ref;
                invoke(rebuilt_store, "engineSetKeyEntry",
                       "(Ljava/lang/String;Ljava/security/Key;[C[Ljava/security/cert/Certificate;)V",
                       {VmValue::Ref(vm.NewStringUtf8(expected_key.first)), VmValue::Ref(restored),
                        VmValue::Ref(key_password), VmValue::Ref(chain)});
            }
            const auto trusted =
                invoke(store, "engineGetCertificate", "(Ljava/lang/String;)Ljava/security/cert/Certificate;",
                       {VmValue::Ref(vm.NewStringUtf8("cert"))})
                    .ref;
            invoke(rebuilt_store, "engineSetCertificateEntry", "(Ljava/lang/String;Ljava/security/cert/Certificate;)V",
                   {VmValue::Ref(vm.NewStringUtf8("cert")), VmValue::Ref(trusted)});
            const auto bks_output = vm.NewIntrinsicInstance("Ljava/io/ByteArrayOutputStream;");
            direct("Ljava/io/ByteArrayOutputStream;", "<init>", "()V", {VmValue::Ref(bks_output)});
            const auto bks_output_roots = vm.ProtectReferences(std::array{bks_output});
            invoke(rebuilt_store, "engineStore", "(Ljava/io/OutputStream;[C)V",
                   {VmValue::Ref(bks_output), VmValue::Ref(store_password)});
            const auto ogplay_store = invoke(bks_output, "toByteArray", "()[B", {}).ref;
            const auto encoded_store = vm.Model().ReadByteRegion(ogplay_store, 0, vm.Model().ArrayLength(ogplay_store));
            std::ofstream roundtrip(std::filesystem::temp_directory_path() / "ogplay-keystore-api19-roundtrip.bks",
                                    std::ios::binary);
            roundtrip.write(reinterpret_cast<const char*>(encoded_store.data()),
                            static_cast<std::streamsize>(encoded_store.size()));
            REQUIRE(roundtrip.good());
            const auto secure_random =
                direct("Ljava/security/SecureRandom;", "getInstance",
                       "(Ljava/lang/String;)Ljava/security/SecureRandom;", {VmValue::Ref(vm.NewStringUtf8("SHA1PRNG"))})
                    .ref;
            const auto random_output = bytes("00000000000000000000000000000000");
            const auto random_roots = vm.ProtectReferences(std::array{secure_random, random_output});
            invoke(secure_random, "setSeed", "([B)V", {VmValue::Ref(bytes("01020304"))});
            invoke(secure_random, "nextBytes", "([B)V", {VmValue::Ref(random_output)});
            CHECK(vm.Model().ReadByteRegion(random_output, 0, 16) != std::vector<std::byte>(16, std::byte{0}));

            const auto key_generator =
                direct("Ljavax/crypto/KeyGenerator;", "getInstance", "(Ljava/lang/String;)Ljavax/crypto/KeyGenerator;",
                       {VmValue::Ref(vm.NewStringUtf8("AES"))})
                    .ref;
            const auto generator_roots = vm.ProtectReferences(std::array{key_generator});
            for (const auto bits : {128, 192, 256}) {
                invoke(key_generator, "init", "(ILjava/security/SecureRandom;)V",
                       {VmValue::Int(bits), VmValue::Ref(secure_random)});
                const auto generated_key = invoke(key_generator, "generateKey", "()Ljavax/crypto/SecretKey;", {}).ref;
                const auto generated_roots = vm.ProtectReferences(std::array{generated_key});
                const auto generated_bytes = invoke(generated_key, "getEncoded", "()[B", {}).ref;
                CHECK(vm.Model().ArrayLength(generated_bytes) == bits / 8);
            }

            const auto key_bytes = bytes("000102030405060708090a0b0c0d0e0f");
            const auto key = vm.NewIntrinsicInstance("Ljavax/crypto/spec/SecretKeySpec;");
            const auto key_roots = vm.ProtectReferences(std::array{key, key_bytes});
            direct("Ljavax/crypto/spec/SecretKeySpec;", "<init>", "([BLjava/lang/String;)V",
                   {VmValue::Ref(key), VmValue::Ref(key_bytes), VmValue::Ref(vm.NewStringUtf8("AES"))});
            const auto resources_before_cipher = vm.GuestNativeResourceCount();
            const auto cipher =
                direct("Ljavax/crypto/Cipher;", "getInstance", "(Ljava/lang/String;)Ljavax/crypto/Cipher;",
                       {VmValue::Ref(vm.NewStringUtf8("AES/ECB/NoPadding"))})
                    .ref;
            const auto cipher_roots = vm.ProtectReferences(std::array{cipher});
            invoke(cipher, "init", "(ILjava/security/Key;)V", {VmValue::Int(1), VmValue::Ref(key)});
            const auto result =
                invoke(cipher, "doFinal", "([B)[B", {VmValue::Ref(bytes("00112233445566778899aabbccddeeff"))}).ref;
            const auto expected = bytes("69c4e0d86a7b0430d8cdb78070b4c55a");
            CHECK(vm.Model().ReadByteRegion(result, 0, 16) == vm.Model().ReadByteRegion(expected, 0, 16));
            CHECK(vm.GuestNativeResourceCount() == resources_before_cipher + 1);
            invoke(cipher, "init", "(ILjava/security/Key;)V", {VmValue::Int(2), VmValue::Ref(key)});
            const auto plain = invoke(cipher, "doFinal", "([B)[B", {VmValue::Ref(expected)}).ref;
            CHECK(vm.Model().ReadByteRegion(plain, 0, 16) ==
                  vm.Model().ReadByteRegion(bytes("00112233445566778899aabbccddeeff"), 0, 16));
            const auto raw_invoke = [&](VmObjectRef obj, const char* name, const char* desc,
                                        std::vector<VmValue> args) {
                const auto type = vm.Model().ObjectClass(obj);
                const auto slot = linker.FindVtableIndex(type, name, desc);
                REQUIRE(slot.has_value());
                args.insert(args.begin(), VmValue::Ref(obj));
                return vm.Call(linker.Class(type).vtable[*slot], args);
            };
            const auto expect_exception = [&](const VmCallOutcome& result, const char* descriptor) {
                INFO(result.exception_message);
                REQUIRE(result.exception.IsValid());
                CHECK(linker.Class(result.exception_class).descriptor == descriptor);
            };
            const auto make_cipher = [&](const std::string& transformation) {
                return direct("Ljavax/crypto/Cipher;", "getInstance", "(Ljava/lang/String;)Ljavax/crypto/Cipher;",
                              {VmValue::Ref(vm.NewStringUtf8(transformation))})
                    .ref;
            };
            const auto make_key = [&](const char* hex) {
                const auto object = vm.NewIntrinsicInstance("Ljavax/crypto/spec/SecretKeySpec;");
                const auto roots = vm.ProtectReferences(std::array{object});
                const auto data = bytes(hex);
                const auto data_roots = vm.ProtectReferences(std::array{data});
                direct("Ljavax/crypto/spec/SecretKeySpec;", "<init>", "([BLjava/lang/String;)V",
                       {VmValue::Ref(object), VmValue::Ref(data), VmValue::Ref(vm.NewStringUtf8("AES"))});
                return object;
            };
            const auto make_iv = [&](const char* hex) {
                const auto object = vm.NewIntrinsicInstance("Ljavax/crypto/spec/IvParameterSpec;");
                const auto roots = vm.ProtectReferences(std::array{object});
                direct("Ljavax/crypto/spec/IvParameterSpec;", "<init>", "([B)V",
                       {VmValue::Ref(object), VmValue::Ref(bytes(hex))});
                return object;
            };
            const auto read = [&](VmObjectRef object) {
                return object.IsValid() ? vm.Model().ReadByteRegion(object, 0, vm.Model().ArrayLength(object))
                                        : std::vector<std::byte>{};
            };

            struct Vector {
                const char* key;
                const char* ecb;
                const char* cbc;
                const char* ctr;
            };

            // NIST SP 800-38A F.1/F.2/F.5, first block, all three AES key lengths.
            for (const auto& vector : std::array{
                     Vector{"2b7e151628aed2a6abf7158809cf4f3c", "3ad77bb40d7a3660a89ecaf32466ef97",
                            "7649abac8119b246cee98e9b12e9197d", "874d6191b620e3261bef6864990db6ce"},
                     Vector{"8e73b0f7da0e6452c810f32b809079e562f8ead2522c6b7b", "bd334f1d6e45f25ff712a214571fa5cc",
                            "4f021db243bc633d7178183a9fa071e8", "1abc932417521ca24f2b0459fe7e6e0b"},
                     Vector{"603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4",
                            "f3eed1bdb5d2a03c064b5a7e3db181f8", "f58c4c04d6e5f1ba779eabfb5f7bfbd6",
                            "601ec313775789a5b7a7f504bbf3d228"}}) {
                const auto vector_key = make_key(vector.key);
                const auto vector_roots = vm.ProtectReferences(std::array{vector_key});
                for (const auto mode : {"ECB", "CBC", "CTR"}) {
                    CAPTURE(mode);
                    CAPTURE(vector.key);
                    const auto aes = make_cipher(std::string("AES/") + mode + "/NoPadding");
                    const auto roots = vm.ProtectReferences(std::array{aes});
                    const auto iv = make_iv(std::string_view(mode) == "CTR" ? "f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff"
                                                                            : "000102030405060708090a0b0c0d0e0f");
                    const auto iv_roots = vm.ProtectReferences(std::array{iv});
                    const auto initialize = [&](int operation) {
                        if (std::string_view(mode) == "ECB")
                            invoke(aes, "init", "(ILjava/security/Key;)V",
                                   {VmValue::Int(operation), VmValue::Ref(vector_key)});
                        else
                            invoke(aes, "init", "(ILjava/security/Key;Ljava/security/spec/AlgorithmParameterSpec;)V",
                                   {VmValue::Int(operation), VmValue::Ref(vector_key), VmValue::Ref(iv)});
                    };
                    initialize(1);
                    const auto input = bytes("6bc1bee22e409f96e93d7e117393172a");
                    const auto input_roots = vm.ProtectReferences(std::array{input});
                    const auto output = invoke(aes, "doFinal", "([B)[B", {VmValue::Ref(input)}).ref;
                    const auto output_roots = vm.ProtectReferences(std::array{output});
                    const auto answer = bytes(std::string_view(mode) == "ECB"   ? vector.ecb
                                              : std::string_view(mode) == "CBC" ? vector.cbc
                                                                                : vector.ctr);
                    CHECK(read(output) == read(answer));
                    // doFinal resets the key/IV and allows reuse.
                    CHECK(read(invoke(aes, "doFinal", "([B)[B", {VmValue::Ref(input)}).ref) == read(output));
                    initialize(2);
                    CHECK(read(invoke(aes, "doFinal", "([B)[B", {VmValue::Ref(output)}).ref) == read(input));
                    initialize(1);
                    auto split = read(
                        invoke(aes, "update", "([BII)[B", {VmValue::Ref(input), VmValue::Int(0), VmValue::Int(5)}).ref);
                    auto tail = read(
                        invoke(aes, "doFinal", "([BII)[B", {VmValue::Ref(input), VmValue::Int(5), VmValue::Int(11)})
                            .ref);
                    split.insert(split.end(), tail.begin(), tail.end());
                    CHECK(split == read(output));
                    const auto provider = invoke(aes, "getProvider", "()Ljava/security/Provider;", {}).ref;
                    CHECK(vm.StringUtf8(invoke(provider, "getName", "()Ljava/lang/String;", {}).ref) ==
                          "AndroidOpenSSL");
                }
            }
            for (const auto mode : {"ECB", "CBC"}) {
                const auto aes = make_cipher(std::string("AES/") + mode + "/PKCS5Padding");
                const auto roots = vm.ProtectReferences(std::array{aes});
                invoke(aes, "init", "(ILjava/security/Key;)V", {VmValue::Int(1), VmValue::Ref(key)});
                const auto iv_bytes = invoke(aes, "getIV", "()[B", {}).ref;
                const auto iv_roots = vm.ProtectReferences(std::array{iv_bytes});
                for (const auto length : {0, 1, 15, 16, 17, 31, 32, 65}) {
                    CAPTURE(mode);
                    CAPTURE(length);
                    const auto input = vm.Model().NewPrimitiveArray(linker.ResolveDescriptor("[B"),
                                                                    runtime::JniPrimitiveKind::byte, length);
                    const auto input_roots = vm.ProtectReferences(std::array{input});
                    const auto encrypted = invoke(aes, "doFinal", "([B)[B", {VmValue::Ref(input)}).ref;
                    const auto encrypted_roots = vm.ProtectReferences(std::array{encrypted});
                    CHECK(vm.Model().ArrayLength(encrypted) == (length / 16 + 1) * 16);
                    const auto decrypt = make_cipher(std::string("AES/") + mode + "/PKCS5Padding");
                    const auto decrypt_roots = vm.ProtectReferences(std::array{decrypt});
                    if (iv_bytes.IsValid()) {
                        const auto spec = vm.NewIntrinsicInstance("Ljavax/crypto/spec/IvParameterSpec;");
                        const auto spec_roots = vm.ProtectReferences(std::array{spec});
                        direct("Ljavax/crypto/spec/IvParameterSpec;", "<init>", "([B)V",
                               {VmValue::Ref(spec), VmValue::Ref(iv_bytes)});
                        invoke(decrypt, "init", "(ILjava/security/Key;Ljava/security/spec/AlgorithmParameterSpec;)V",
                               {VmValue::Int(2), VmValue::Ref(key), VmValue::Ref(spec)});
                    } else
                        invoke(decrypt, "init", "(ILjava/security/Key;)V", {VmValue::Int(2), VmValue::Ref(key)});
                    auto restored = read(invoke(decrypt, "update", "([BII)[B",
                                                {VmValue::Ref(encrypted), VmValue::Int(0), VmValue::Int(7)})
                                             .ref);
                    auto tail = read(invoke(decrypt, "doFinal", "([BII)[B",
                                            {VmValue::Ref(encrypted), VmValue::Int(7),
                                             VmValue::Int(vm.Model().ArrayLength(encrypted) - 7)})
                                         .ref);
                    restored.insert(restored.end(), tail.begin(), tail.end());
                    CHECK(restored == read(input));
                }
                if (iv_bytes.IsValid()) {
                    invoke(aes, "init", "(ILjava/security/Key;)V", {VmValue::Int(1), VmValue::Ref(key)});
                    const auto second_iv = invoke(aes, "getIV", "()[B", {}).ref;
                    CHECK(read(second_iv) != read(iv_bytes));
                    CHECK(read(second_iv) != std::vector<std::byte>(16));
                }
            }
            {
                const auto zero_key = make_key("2b7e151628aed2a6abf7158809cf4f3c");
                const auto zero_iv = make_iv("000102030405060708090a0b0c0d0e0f");
                const auto zero = make_cipher("AES/CBC/ZeroBytePadding");
                const auto roots = vm.ProtectReferences(std::array{zero_key, zero_iv, zero});
                const auto initialize = [&](const int operation) {
                    invoke(zero, "init", "(ILjava/security/Key;Ljava/security/spec/AlgorithmParameterSpec;)V",
                           {VmValue::Int(operation), VmValue::Ref(zero_key), VmValue::Ref(zero_iv)});
                };
                const auto input = bytes("42757273746c7920646576696365");
                const auto zero_expected = bytes("3d90d0108a2b72a3f9b7bf26866ed764");
                const auto input_roots = vm.ProtectReferences(std::array{input, zero_expected});
                initialize(1);
                CHECK(invoke(zero, "getOutputSize", "(I)I", {VmValue::Int(14)}).AsInt() == 16);
                CHECK(read(invoke(zero, "doFinal", "([B)[B", {VmValue::Ref(input)}).ref) == read(zero_expected));

                initialize(1);
                CHECK(
                    read(
                        invoke(zero, "update", "([BII)[B", {VmValue::Ref(input), VmValue::Int(0), VmValue::Int(5)}).ref)
                        .empty());
                CHECK(invoke(zero, "getOutputSize", "(I)I", {VmValue::Int(9)}).AsInt() == 16);
                CHECK(read(invoke(zero, "doFinal", "([BII)[B", {VmValue::Ref(input), VmValue::Int(5), VmValue::Int(9)})
                               .ref) == read(zero_expected));

                initialize(2);
                CHECK(read(invoke(zero, "doFinal", "([B)[B", {VmValue::Ref(zero_expected)}).ref) == read(input));
                initialize(1);
                CHECK(read(invoke(zero, "doFinal", "()[B", {}).ref).empty());
                initialize(2);
                CHECK(read(invoke(zero, "doFinal", "()[B", {}).ref).empty());
                initialize(1);
                CHECK(read(invoke(zero, "doFinal", "([B)[B", {VmValue::Ref(bytes("6bc1bee22e409f96e93d7e117393172a"))})
                               .ref) == read(bytes("7649abac8119b246cee98e9b12e9197d")));
                initialize(1);
                expect_exception(raw_invoke(zero, "doFinal", "([BII[BI)I",
                                            {VmValue::Ref(input), VmValue::Int(0), VmValue::Int(14),
                                             VmValue::Ref(bytes("000000000000000000000000000000")), VmValue::Int(0)}),
                                 "Ljavax/crypto/ShortBufferException;");
            }
            const auto bad_key = make_key("000102030405060708090a0b0c0d0e");
            expect_exception(
                raw_invoke(cipher, "init", "(ILjava/security/Key;)V", {VmValue::Int(1), VmValue::Ref(bad_key)}),
                "Ljava/security/InvalidKeyException;");
            invoke(cipher, "init", "(ILjava/security/Key;)V", {VmValue::Int(1), VmValue::Ref(key)});
            expect_exception(raw_invoke(cipher, "doFinal", "([B)[B", {VmValue::Ref(bytes("00"))}),
                             "Ljavax/crypto/IllegalBlockSizeException;");
            const auto bad_padding = make_cipher("AES/ECB/PKCS5Padding");
            const auto bad_roots = vm.ProtectReferences(std::array{bad_padding});
            invoke(bad_padding, "init", "(ILjava/security/Key;)V", {VmValue::Int(2), VmValue::Ref(key)});
            expect_exception(
                raw_invoke(bad_padding, "doFinal", "([B)[B", {VmValue::Ref(bytes("69c4e0d86a7b0430d8cdb78070b4c55a"))}),
                "Ljavax/crypto/BadPaddingException;");
            invoke(cipher, "init", "(ILjava/security/Key;)V", {VmValue::Int(1), VmValue::Ref(key)});
            const auto block = bytes("00112233445566778899aabbccddeeff");
            const auto block_roots = vm.ProtectReferences(std::array{block});
            expect_exception(raw_invoke(cipher, "doFinal", "([BII[BI)I",
                                        {VmValue::Ref(block), VmValue::Int(0), VmValue::Int(16),
                                         VmValue::Ref(bytes("00")), VmValue::Int(0)}),
                             "Ljavax/crypto/ShortBufferException;");
            CHECK(invoke(cipher, "doFinal", "([BII[BI)I",
                         {VmValue::Ref(block), VmValue::Int(0), VmValue::Int(16), VmValue::Ref(block), VmValue::Int(0)})
                      .AsInt() == 16);
            CHECK(read(block) == read(bytes("69c4e0d86a7b0430d8cdb78070b4c55a")));
            const auto cbc = make_cipher("AES/CBC/NoPadding");
            const auto cbc_roots = vm.ProtectReferences(std::array{cbc});
            expect_exception(raw_invoke(cbc, "init",
                                        "(ILjava/security/Key;Ljava/security/spec/AlgorithmParameterSpec;)V",
                                        {VmValue::Int(1), VmValue::Ref(key), VmValue::Ref(make_iv("00"))}),
                             "Ljava/security/InvalidAlgorithmParameterException;");
            const auto cipher_type = *linker.FindClass("Ljavax/crypto/Cipher;");
            const auto factory =
                *linker.FindDirectMethod(cipher_type, "getInstance", "(Ljava/lang/String;)Ljavax/crypto/Cipher;");
            for (const auto name :
                 {"AES/GCM/NoPadding", "AES/CTR/PKCS5Padding", "DES/ECB/NoPadding", "RSA/ECB/PKCS1Padding"}) {
                CAPTURE(std::string(name));
                expect_exception(vm.Call(factory, std::array{VmValue::Ref(vm.NewStringUtf8(name))}),
                                 "Ljava/security/NoSuchAlgorithmException;");
            }
            const auto alias = make_cipher("AES");
            const auto alias_roots = vm.ProtectReferences(std::array{alias});
            invoke(alias, "init", "(ILjava/security/Key;)V", {VmValue::Int(1), VmValue::Ref(key)});
            CHECK(read(invoke(alias, "doFinal", "([B)[B", {VmValue::Ref(bytes(""))}).ref).size() == 16);
            // Tokens stay private to guest libcrypto and stale/foreign tokens fail explicitly.
            const auto native = *linker.FindClass("Lcom/android/org/conscrypt/NativeCrypto;");
            const auto initialized_once = vm.EnsureClassInitialized(native);
            REQUIRE_MESSAGE(!initialized_once.exception.IsValid(), initialized_once.exception_message);
            const auto initialized_twice = vm.EnsureClassInitialized(native);
            REQUIRE_MESSAGE(!initialized_twice.exception.IsValid(), initialized_twice.exception_message);
            const auto supported_protocols =
                direct("Lcom/android/org/conscrypt/NativeCrypto;", "getSupportedProtocols", "()[Ljava/lang/String;", {})
                    .ref;
            CHECK(vm.Model().ArrayLength(supported_protocols) == 4);
            const auto unsupported_engine = *linker.FindDirectMethod(native, "ENGINE_load_dynamic", "()V");
            CHECK(vm.Call(unsupported_engine, {}).exception.IsValid());
            const auto allocate = *linker.FindDirectMethod(native, "EVP_CIPHER_CTX_new", "()J");
            const auto cleanup = *linker.FindDirectMethod(native, "EVP_CIPHER_CTX_cleanup", "(J)V");
            const auto size = *linker.FindDirectMethod(native, "EVP_CIPHER_CTX_block_size", "(J)I");
            CHECK(linker.Method(allocate).kind == MethodKind::native);
            CHECK_FALSE(static_cast<bool>(linker.Method(allocate).implementation));
            const auto token = vm.Call(allocate, {}).value.AsLong();
            CHECK(vm.Call(size, std::array{VmValue::Long(token)}).exception.IsValid());
            static_cast<void>(vm.Call(cleanup, std::array{VmValue::Long(token)}));
            CHECK(vm.Call(cleanup, std::array{VmValue::Long(token)}).exception.IsValid());
            CHECK(vm.Call(size, std::array{VmValue::Long(0x123456789LL)}).exception.IsValid());
            CHECK(direct("Lfixture/CipherThreads;", "exercise", "()I", {}).AsInt() == 32);
            const auto before_gc = vm.GuestNativeResourceCount();
            CHECK(before_gc > 3);
            static_cast<void>(vm.CollectGarbage("cipher-test"));
            try {
                vm.RunFinalization();
            } catch (const VmJavaThrow& error) {
                FAIL(error.descriptor << ": " << error.message);
            }
            static_cast<void>(vm.CollectGarbage("crypto-finalizers-complete"));
            CHECK(vm.GuestNativeResourceCount() < before_gc);
            CHECK(vm.GuestNativeResourceCount() >= 2);
            invoke(cipher, "init", "(ILjava/security/Key;)V", {VmValue::Int(1), VmValue::Ref(key)});
            CHECK(read(invoke(cipher, "doFinal", "([B)[B", {VmValue::Ref(bytes("00112233445566778899aabbccddeeff"))})
                           .ref) == read(bytes("69c4e0d86a7b0430d8cdb78070b4c55a")));
            // String.foldCase is lower(upper(char)), rather than a full string
            // conversion: sharp-s must not expand, and Turkish I/final sigma fold.
            for (const auto& [left, right, matches] : std::vector<std::tuple<std::u16string, std::u16string, bool>>{
                     {u"\u00c4", u"\u00e4", true},
                     {u"\u0130", u"i", true},
                     {u"\u0131", u"I", true},
                     {u"\u03c2", u"\u03c3", true},
                     {u"\u212a", u"k", true},
                     {u"\u00df", u"s", false},
                     {std::u16string(1, char16_t{0xd800}), std::u16string(1, char16_t{0xd801}), false}}) {
                const auto a = vm.Model().NewString(left);
                const auto b = vm.Model().NewString(right);
                const auto roots = vm.ProtectReferences(std::array{a, b});
                CHECK(invoke(a, "regionMatches", "(ZILjava/lang/String;II)Z",
                             {VmValue::Int(1), VmValue::Int(0), VmValue::Ref(b), VmValue::Int(0), VmValue::Int(1)})
                          .AsInt() == matches);
            }
            CHECK(direct("Ljava/lang/Character;", "toUpperCase", "(I)I", {VmValue::Int(0x10428)}).AsInt() == 0x10400);
            CHECK(direct("Ljava/lang/Character;", "toLowerCase", "(I)I", {VmValue::Int(0x10400)}).AsInt() == 0x10428);
            const auto iso = direct("Llibcore/icu/ICU;", "getISOLanguagesNative", "()[Ljava/lang/String;", {}).ref;
            CHECK(vm.Model().ArrayLength(iso) == 559);
            const auto countries =
                direct("Llibcore/icu/ICU;", "getISOCountriesNative", "()[Ljava/lang/String;", {}).ref;
            CHECK(vm.Model().ArrayLength(countries) == 249);
            const auto locale = vm.NewStringUtf8("en_US");
            const auto usd = vm.NewStringUtf8("USD");
            const auto icu_roots = vm.ProtectReferences(std::array{locale, usd});
            CHECK_FALSE(vm.StringUtf8(direct("Llibcore/icu/ICU;", "getBestDateTimePatternNative",
                                             "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                                             {VmValue::Ref(vm.NewStringUtf8("yMd")), VmValue::Ref(locale)})
                                          .ref)
                            .empty());
            CHECK(vm.StringUtf8(direct("Llibcore/icu/ICU;", "getCurrencyCode", "(Ljava/lang/String;)Ljava/lang/String;",
                                       {VmValue::Ref(vm.NewStringUtf8("US"))})
                                    .ref) == "USD");
            CHECK_FALSE(vm.StringUtf8(direct("Llibcore/icu/ICU;", "getCurrencyDisplayName",
                                             "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                                             {VmValue::Ref(locale), VmValue::Ref(usd)})
                                          .ref)
                            .empty());
            CHECK(direct("Llibcore/icu/ICU;", "getCurrencyFractionDigits", "(Ljava/lang/String;)I", {VmValue::Ref(usd)})
                      .AsInt() == 2);
            CHECK_FALSE(vm.StringUtf8(direct("Llibcore/icu/ICU;", "getCurrencySymbol",
                                             "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                                             {VmValue::Ref(locale), VmValue::Ref(usd)})
                                          .ref)
                            .empty());
            const auto calendar = direct("Ljava/util/Calendar;", "getInstance", "()Ljava/util/Calendar;", {}).ref;
            const auto calendar_roots = vm.ProtectReferences(std::array{calendar});
            REQUIRE(calendar.IsValid());
            CHECK(linker.Class(vm.Model().ObjectClass(calendar)).descriptor == "Ljava/util/GregorianCalendar;");
            CHECK(invoke(calendar, "getFirstDayOfWeek", "()I", {}).AsInt() == 1);
            CHECK(invoke(calendar, "getMinimalDaysInFirstWeek", "()I", {}).AsInt() == 1);
            // ICU-dependent Calendar and custom-zone parsing belong in the real
            // guest JNI fixture; core-only tests validate the missing-bridge boundary.
            invoke(calendar, "setTimeInMillis", "(J)V", {VmValue::Long(1704067200000LL)});
            CHECK(invoke(calendar, "get", "(I)I", {VmValue::Int(1)}).AsInt() == 2024);
            CHECK(invoke(calendar, "get", "(I)I", {VmValue::Int(2)}).AsInt() == 0);
            for (const auto& [id, offset] :
                 std::array{std::pair{"GMT+08:00", 28800000}, std::pair{"GMT-03:30", -12600000}}) {
                const auto zone =
                    direct("Ljava/util/TimeZone;", "getTimeZone", "(Ljava/lang/String;)Ljava/util/TimeZone;",
                           {VmValue::Ref(vm.NewStringUtf8(id))})
                        .ref;
                const auto zone_root = vm.ProtectReferences(std::array{zone});
                CHECK(invoke(zone, "getRawOffset", "()I", {}).AsInt() == offset);
                direct("Ljava/util/TimeZone;", "setDefault", "(Ljava/util/TimeZone;)V", {VmValue::Ref(zone)});
                const auto cached = direct("Ljava/util/TimeZone;", "getDefault", "()Ljava/util/TimeZone;", {}).ref;
                CHECK(invoke(cached, "getRawOffset", "()I", {}).AsInt() == offset);
                direct("Ljava/util/TimeZone;", "setDefault", "(Ljava/util/TimeZone;)V", {VmValue::Ref(VmObjectRef{})});
                const auto reset = direct("Ljava/util/TimeZone;", "getDefault", "()Ljava/util/TimeZone;", {}).ref;
                CHECK(invoke(reset, "getRawOffset", "()I", {}).AsInt() == 0);
            }
            const auto choice = vm.NewIntrinsicInstance("Ljava/text/ChoiceFormat;");
            const auto choice_root = vm.ProtectReferences(std::array{choice});
            direct("Ljava/text/ChoiceFormat;", "<init>", "(Ljava/lang/String;)V",
                   {VmValue::Ref(choice), VmValue::Ref(vm.NewStringUtf8("0#none|1#one|1<many"))});
            CHECK(vm.StringUtf8(invoke(choice, "format", "(J)Ljava/lang/String;", {VmValue::Long(2)}).ref) == "many");
            const auto serialized_bytes = vm.NewIntrinsicInstance("Ljava/io/ByteArrayOutputStream;");
            const auto object_output = vm.NewIntrinsicInstance("Ljava/io/ObjectOutputStream;");
            const auto serialization_roots = vm.ProtectReferences(std::array{serialized_bytes, object_output});
            direct("Ljava/io/ByteArrayOutputStream;", "<init>", "()V", {VmValue::Ref(serialized_bytes)});
            direct("Ljava/io/ObjectOutputStream;", "<init>", "(Ljava/io/OutputStream;)V",
                   {VmValue::Ref(object_output), VmValue::Ref(serialized_bytes)});
            expect_exception(
                invoke_result(object_output, "writeObject", "(Ljava/lang/Object;)V", {VmValue::Ref(object_output)}),
                "Ljava/io/NotSerializableException;");

            const auto unknown_currency = vm.NewStringUtf8("ZZZ");
            const auto unknown_roots = vm.ProtectReferences(std::array{unknown_currency});
            CHECK_FALSE(direct("Llibcore/icu/ICU;", "getCurrencySymbol",
                               "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                               {VmValue::Ref(locale), VmValue::Ref(unknown_currency)})
                            .ref.IsValid());
            CHECK(vm.StringUtf8(direct("Llibcore/icu/ICU;", "getCurrencyDisplayName",
                                       "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                                       {VmValue::Ref(locale), VmValue::Ref(unknown_currency)})
                                    .ref) == "ZZZ");
            CHECK(vm.StringUtf8(direct("Lfixture/IcuRegression;", "upperSharpS", "()Ljava/lang/String;", {}).ref) ==
                  "SS");
            CHECK(vm.StringUtf8(direct("Lfixture/IcuRegression;", "turkishUpperI", "()Ljava/lang/String;", {}).ref) ==
                  "İ");
            CHECK(vm.StringUtf8(direct("Lfixture/IcuRegression;", "zhCurrencyCode", "()Ljava/lang/String;", {}).ref) ==
                  "XXX");
            CHECK(direct("Lfixture/IcuRegression;", "fractionFieldPosition", "()I", {}).AsInt() == 35);
            CHECK(direct("Lfixture/IcuRegression;", "groupingAttributeCount", "()I", {}).AsInt() == 2);
            CHECK(direct("Lfixture/IcuRegression;", "parsePositionSemantics", "()I", {}).AsInt() == 1);
            const auto string_class = linker.ResolveDescriptor("Ljava/lang/String;");
            const auto string_array_class = linker.ResolveDescriptor("[Ljava/lang/String;");
            const auto rows_class = linker.ResolveDescriptor("[[Ljava/lang/String;");
            const auto row = vm.Model().NewObjectArray(string_array_class, string_class, 5);
            vm.Model().SetObjectElement(row, 0, vm.NewStringUtf8("GMT"));
            const auto rows = vm.Model().NewObjectArray(rows_class, string_array_class, 1);
            vm.Model().SetObjectElement(rows, 0, row);
            const auto zone_roots = vm.ProtectReferences(std::array{row, rows});
            static_cast<void>(direct("Llibcore/icu/TimeZoneNames;", "fillZoneStrings",
                                     "(Ljava/lang/String;[[Ljava/lang/String;)V",
                                     {VmValue::Ref(locale), VmValue::Ref(rows)}));
            CHECK(vm.Model().GetObjectElement(row, 1).IsValid());
            const auto decimal = vm.NewIntrinsicInstance("Ljava/text/DecimalFormat;");
            const auto decimal_roots = vm.ProtectReferences(std::array{decimal});
            static_cast<void>(direct("Ljava/text/DecimalFormat;", "<init>", "(Ljava/lang/String;)V",
                                     {VmValue::Ref(decimal), VmValue::Ref(vm.NewStringUtf8("0.00"))}));
            CHECK(vm.StringUtf8(invoke(decimal, "format", "(J)Ljava/lang/String;", {VmValue::Long(12)}).ref) ==
                  "12.00");
            static_cast<void>(
                invoke(decimal, "applyPattern", "(Ljava/lang/String;)V", {VmValue::Ref(vm.NewStringUtf8("000"))}));
            CHECK(vm.StringUtf8(invoke(decimal, "toPattern", "()Ljava/lang/String;", {}).ref) == "#000");
            CHECK(vm.StringUtf8(invoke(decimal, "format", "(J)Ljava/lang/String;", {VmValue::Long(12)}).ref) == "012");
            const auto parsed = invoke(decimal, "parse", "(Ljava/lang/String;)Ljava/lang/Number;",
                                       {VmValue::Ref(vm.NewStringUtf8("034"))})
                                    .ref;
            CHECK(invoke(parsed, "longValue", "()J", {}).AsLong() == 34);
            const auto decimal_fraction = vm.NewIntrinsicInstance("Ljava/text/DecimalFormat;");
            const auto fraction_roots = vm.ProtectReferences(std::array{decimal_fraction});
            static_cast<void>(direct("Ljava/text/DecimalFormat;", "<init>", "(Ljava/lang/String;)V",
                                     {VmValue::Ref(decimal_fraction), VmValue::Ref(vm.NewStringUtf8("0.0"))}));
            const auto parsed_fraction = invoke(decimal_fraction, "parse", "(Ljava/lang/String;)Ljava/lang/Number;",
                                                {VmValue::Ref(vm.NewStringUtf8("1.5"))})
                                             .ref;
            CHECK(linker.Class(vm.Model().ObjectClass(parsed_fraction)).descriptor == "Ljava/lang/Double;");
            CHECK(invoke(parsed_fraction, "doubleValue", "()D", {}).AsDouble() == doctest::Approx(1.5));
            const auto cloned = invoke(decimal, "clone", "()Ljava/lang/Object;", {}).ref;
            const auto clone_roots = vm.ProtectReferences(std::array{cloned});
            CHECK(vm.StringUtf8(invoke(cloned, "format", "(J)Ljava/lang/String;", {VmValue::Long(7)}).ref) == "007");
            const auto date_format = vm.NewIntrinsicInstance("Ljava/text/SimpleDateFormat;");
            const auto date = vm.NewIntrinsicInstance("Ljava/util/Date;");
            const auto date_roots = vm.ProtectReferences(std::array{date_format, date});
            static_cast<void>(direct("Ljava/text/SimpleDateFormat;", "<init>", "(Ljava/lang/String;)V",
                                     {VmValue::Ref(date_format), VmValue::Ref(vm.NewStringUtf8("yyyy-MM-dd"))}));
            static_cast<void>(direct("Ljava/util/Date;", "<init>", "(J)V", {VmValue::Ref(date), VmValue::Long(0)}));
            CHECK(
                vm.StringUtf8(
                    invoke(date_format, "format", "(Ljava/util/Date;)Ljava/lang/String;", {VmValue::Ref(date)}).ref) ==
                "1970-01-01");
            const auto time_zone = linker.ResolveDescriptor("Ljava/util/TimeZone;");
            const auto get_time_zone =
                linker.FindDirectMethod(time_zone, "getTimeZone", "(Ljava/lang/String;)Ljava/util/TimeZone;");
            REQUIRE(get_time_zone.has_value());
            expect_exception(vm.Call(*get_time_zone, std::array{VmValue::Ref(vm.NewStringUtf8("PST"))}),
                             "Ljava/lang/UnsupportedOperationException;");
            const auto hmac_key_bytes = bytes("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b");
            const auto hmac_key = vm.NewIntrinsicInstance("Ljavax/crypto/spec/SecretKeySpec;");
            const auto hmac_key_roots = vm.ProtectReferences(std::array{hmac_key_bytes, hmac_key});
            direct("Ljavax/crypto/spec/SecretKeySpec;", "<init>", "([BLjava/lang/String;)V",
                   {VmValue::Ref(hmac_key), VmValue::Ref(hmac_key_bytes), VmValue::Ref(vm.NewStringUtf8("HmacSHA1"))});
            const auto mac = direct("Ljavax/crypto/Mac;", "getInstance", "(Ljava/lang/String;)Ljavax/crypto/Mac;",
                                    {VmValue::Ref(vm.NewStringUtf8("HmacSHA1"))})
                                 .ref;
            const auto mac_roots = vm.ProtectReferences(std::array{mac});
            for (const auto alias_name : {"HMAC-SHA1", "HMAC/SHA1", "1.2.840.113549.2.7"}) {
                const auto alias_mac =
                    direct("Ljavax/crypto/Mac;", "getInstance", "(Ljava/lang/String;)Ljavax/crypto/Mac;",
                           {VmValue::Ref(vm.NewStringUtf8(alias_name))})
                        .ref;
                const auto hmac_alias_roots = vm.ProtectReferences(std::array{alias_mac});
                CHECK(alias_mac.IsValid());
            }
            invoke(mac, "init", "(Ljava/security/Key;)V", {VmValue::Ref(hmac_key)});
            const auto hi_there = bytes("4869205468657265");
            const auto hmac_input_roots = vm.ProtectReferences(std::array{hi_there});
            invoke(mac, "update", "([BII)V", {VmValue::Ref(hi_there), VmValue::Int(0), VmValue::Int(3)});
            const auto hmac = invoke(mac, "doFinal", "([B)[B", {VmValue::Ref(bytes("5468657265"))}).ref;
            const auto hmac_roots = vm.ProtectReferences(std::array{hmac});
            const auto hmac_hex = core::EncodeHex(read(hmac), core::HexCase::lower);
            INFO(hmac_hex);
            CHECK(read(hmac) == read(bytes("b617318655057264e28bc0b6fb378c8ef146be00")));
            CHECK(read(invoke(mac, "doFinal", "([B)[B", {VmValue::Ref(hi_there)}).ref) == read(hmac));
            invoke(mac, "update", "([B)V", {VmValue::Ref(hi_there)});
            invoke(mac, "reset", "()V", {});
            CHECK(read(invoke(mac, "doFinal", "([B)[B", {VmValue::Ref(hi_there)}).ref) == read(hmac));
            const auto mac_provider = invoke(mac, "getProvider", "()Ljava/security/Provider;", {}).ref;
            CHECK(vm.StringUtf8(invoke(mac_provider, "getName", "()Ljava/lang/String;", {}).ref) == "AndroidOpenSSL");
            CHECK(vm.StringUtf8(invoke(mac_provider, "getProperty", "(Ljava/lang/String;)Ljava/lang/String;",
                                       {VmValue::Ref(vm.NewStringUtf8("Mac.HmacSHA1"))})
                                    .ref) == "com.android.org.conscrypt.OpenSSLMac$HmacSHA1");
            // Cleanup must work with the original native pending exception, while
            // the normal JNI gate remains strict before and after the scope.
            auto& cleanup_env = app->DexVm().Session().Environment();
            const auto throwable = vm.MakeThrowable("Ljava/lang/IllegalStateException;", "original failure");
            const auto original_ref = app->DexVm().PublishLocal(throwable);
            cleanup_env.Throw(1, original_ref);
            const auto original_pending = cleanup_env.PendingExceptionMetadata(1);
            REQUIRE(original_pending.has_value());
            CHECK_THROWS_AS(static_cast<void>(cleanup_env.NewGlobalRef(1, original_ref)), runtime::JniExceptionError);
            // Remove the caller's root: the cleanup guard alone must protect it.
            cleanup_env.DeleteLocalRef(1, original_ref);
            {
                const auto scope = app->DexVm().EnterResourceCleanup();
                CHECK_FALSE(cleanup_env.ExceptionCheck(1));
                static_cast<void>(vm.CollectGarbage("pending exception cleanup"));
                const auto second = vm.MakeThrowable("Ljava/lang/IllegalArgumentException;", "cleanup failure");
                cleanup_env.Throw(1, app->DexVm().PublishLocal(second));
            }
            REQUIRE(cleanup_env.PendingExceptionMetadata(1).has_value());
            CHECK(cleanup_env.PendingExceptionMetadata(1)->throwable == original_pending->throwable);
            CHECK(vm.StringUtf8(vm.ThrowableMessage(throwable)) == "original failure");
            vm.ReleaseGuestNativeResources(true);
            CHECK(cleanup_env.PendingExceptionMetadata(1)->throwable == original_pending->throwable);
            const auto restored_ref = cleanup_env.ExceptionOccurred(1);
            CHECK_THROWS_AS(static_cast<void>(cleanup_env.NewGlobalRef(1, restored_ref)), runtime::JniExceptionError);
            cleanup_env.ExceptionClear(1);
            cleanup_env.DeleteLocalRef(1, restored_ref);
            CHECK(vm.GuestNativeResourceCount() == 0);
            static_cast<void>(app->Stop());
        }
        filesystem.FlushAll();
        auto reopened = runtime::SandboxStore::Open(sandbox_root, "fixture", "fixture");
        runtime::VirtualFileSystem reload_filesystem;
        reload_filesystem.AttachSandbox(*reopened, sandbox_roots);
        session::AndroidAppProcessRequest reload_request;
        reload_request.manifest = AppManifest("fixture.MainActivity");
        reload_request.system_libraries = libraries;
        reload_request.dex_bytes = ReadDexFixture("cipher.dex");
        reload_request.icu_data = ReadPayloadBytes("icu/icudt51l.dat");
        reload_request.boot_dex_bytes = test::ReadBootDex();
        reload_request.context = std::make_shared<runtime::DexVmAndroidContext>();
        test::BindBootDexArchive(*reload_request.context);
        reload_request.context->apk_bytes = {std::byte{0x50}, std::byte{0x4b}, std::byte{3}, std::byte{4}};
        reload_request.dexvm.interpreter.backend = backend;
        reload_request.surface_width = 64;
        reload_request.surface_height = 36;
        reload_request.maximum_ticks_per_call = UINT64_C(100000000);
#if defined(_WIN32)
        reload_request.backend = {gles::AngleRenderer::d3d11, gles::AngleDevice::hardware};
#elif defined(__APPLE__)
        reload_request.backend = {gles::AngleRenderer::metal, gles::AngleDevice::hardware};
#else
        reload_request.backend = {gles::AngleRenderer::vulkan, gles::AngleDevice::hardware};
#endif
        reload_request.filesystem = &reload_filesystem;
        reload_request.ledger = &ledger;
        reload_request.logger = &logger;
        auto reloaded_app = session::AndroidAppProcess::Create(std::move(reload_request));
        auto& reloaded_vm = reloaded_app->DexVm().Vm();
        auto& reloaded_linker = reloaded_vm.Linker();
        const auto reload_direct = [&](const char* owner, const char* name, const char* desc,
                                       std::vector<VmValue> args) {
            const auto type = reloaded_linker.FindClass(owner);
            REQUIRE(type.has_value());
            const auto method = reloaded_linker.FindDirectMethod(*type, name, desc);
            REQUIRE(method.has_value());
            const auto result = reloaded_vm.Call(*method, args);
            REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
            return result.value;
        };
        const auto reload_invoke = [&](VmObjectRef object, const char* name, const char* desc,
                                       std::vector<VmValue> args) {
            const auto type = reloaded_vm.Model().ObjectClass(object);
            const auto slot = reloaded_linker.FindVtableIndex(type, name, desc);
            REQUIRE(slot.has_value());
            args.insert(args.begin(), VmValue::Ref(object));
            const auto result = reloaded_vm.Call(reloaded_linker.Class(type).vtable[*slot], args);
            REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
            return result.value;
        };
        const auto session_input = reloaded_vm.NewIntrinsicInstance("Ljava/io/FileInputStream;");
        reload_direct("Ljava/io/FileInputStream;", "<init>", "(Ljava/lang/String;)V",
                      {VmValue::Ref(session_input),
                       VmValue::Ref(reloaded_vm.NewStringUtf8("/data/data/fixture/keystore-session.bks"))});
        const auto session_store =
            reload_direct("Ljava/security/KeyStore;", "getInstance", "(Ljava/lang/String;)Ljava/security/KeyStore;",
                          {VmValue::Ref(reloaded_vm.NewStringUtf8("BKS"))})
                .ref;
        const auto reload_password = reloaded_vm.Model().NewPrimitiveArray(reloaded_linker.ResolveDescriptor("[C"),
                                                                           runtime::JniPrimitiveKind::character, 9);
        constexpr std::string_view reload_password_text = "storepass";
        for (std::size_t index = 0; index < reload_password_text.size(); ++index)
            reloaded_vm.Model().SetPrimitiveElement(reload_password, static_cast<runtime::JniSize>(index),
                                                    static_cast<std::uint16_t>(reload_password_text[index]));
        const auto reload_roots =
            reloaded_vm.ProtectReferences(std::array{session_input, session_store, reload_password});
        reload_invoke(session_store, "load", "(Ljava/io/InputStream;[C)V",
                      {VmValue::Ref(session_input), VmValue::Ref(reload_password)});
        CHECK(reload_invoke(session_store, "size", "()I", {}).AsInt() == 4);
        CHECK(reload_invoke(session_store, "containsAlias", "(Ljava/lang/String;)Z",
                            {VmValue::Ref(reloaded_vm.NewStringUtf8("rsa"))})
                  .AsInt() != 0);
        reload_invoke(session_input, "close", "()V", {});
        const auto restored_key_password = reloaded_vm.Model().NewPrimitiveArray(
            reloaded_linker.ResolveDescriptor("[C"), runtime::JniPrimitiveKind::character, 7);
        for (std::size_t i = 0; i < 7; ++i)
            reloaded_vm.Model().SetPrimitiveElement(restored_key_password, static_cast<runtime::JniSize>(i),
                                                    static_cast<std::uint16_t>("keypass"[i]));
        const auto restored_password_root = reloaded_vm.ProtectReferences(std::array{restored_key_password});
        for (const auto* alias : {"secret", "rsa", "ec"}) {
            const auto key =
                reload_invoke(session_store, "getKey", "(Ljava/lang/String;[C)Ljava/security/Key;",
                              {VmValue::Ref(reloaded_vm.NewStringUtf8(alias)), VmValue::Ref(restored_key_password)})
                    .ref;
            const auto key_root = reloaded_vm.ProtectReferences(std::array{key});
            CHECK(reload_invoke(key, "getEncoded", "()[B", {}).ref.IsValid());
        }
        auto other = runtime::SandboxStore::Open(sandbox_root, "fixture.other", "fixture.other");
        runtime::VirtualFileSystem isolated;
        const std::array<std::string, 1> other_roots{"/data/data/fixture.other"};
        isolated.AttachSandbox(*other, other_roots);
        CHECK_THROWS_AS(static_cast<void>(isolated.Stat("/data/data/fixture/keystore-session.bks")), runtime::VfsError);
        CHECK_THROWS_AS(static_cast<void>(isolated.Open("/data/data/fixture/keystore-session.bks", {.read = true})),
                        runtime::VfsError);
        const auto original_file = reopened->ReadFile("/data/data/fixture/keystore-session.bks");
        const auto isolated_fd =
            isolated.Open("/data/data/fixture.other/keystore-session.bks", {.write = true, .create = true});
        const std::array<std::byte, 3> other_data{std::byte{1}, std::byte{2}, std::byte{3}};
        CHECK(isolated.Write(isolated_fd, other_data) == other_data.size());
        isolated.Close(isolated_fd);
        CHECK(reopened->ReadFile("/data/data/fixture/keystore-session.bks") == original_file);
        CHECK(other->ReadFile("/data/data/fixture.other/keystore-session.bks").size() == 3);
        static_cast<void>(reloaded_app->Stop());
    }
}
