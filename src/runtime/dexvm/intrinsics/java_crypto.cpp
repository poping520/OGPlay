// API 19 JCA configuration and explicit guest OpenSSL/OS entropy boundaries.
#include "catalog.h"
#include "shared.h"

namespace ogplay::runtime::dexvm::intrinsics {
namespace {
using namespace detail;
constexpr auto kNative = "Lcom/android/org/conscrypt/NativeCrypto;";
constexpr auto kVerificationNative = "Lorg/ogplay/security/NativeVerification;";
constexpr auto kKeyStoreNative = "Lorg/ogplay/security/NativeKeyStoreCrypto;";
constexpr auto kProvider = "Lcom/android/org/conscrypt/OpenSSLProvider;";

struct SignatureAlgorithm {
    const char* algorithm;
    const char* oid;
};
constexpr SignatureAlgorithm kSignatures[] = {
    {"SHA1withRSA", "1.2.840.113549.1.1.5"},    {"SHA224withRSA", "1.2.840.113549.1.1.14"},
    {"SHA256withRSA", "1.2.840.113549.1.1.11"}, {"SHA384withRSA", "1.2.840.113549.1.1.12"},
    {"SHA512withRSA", "1.2.840.113549.1.1.13"}, {"SHA1withECDSA", "1.2.840.10045.4.1"},
    {"SHA224withECDSA", "1.2.840.10045.4.3.1"}, {"SHA256withECDSA", "1.2.840.10045.4.3.2"},
    {"SHA384withECDSA", "1.2.840.10045.4.3.3"}, {"SHA512withECDSA", "1.2.840.10045.4.3.4"},
};

VmValue Direct(Interpreter& vm, const char* owner, const char* name, const char* signature,
               std::vector<VmValue> args) {
    const auto type = vm.Linker().FindClass(owner);
    if (!type) throw DexVmError(DexVmErrorReason::unresolved_reference, owner);
    const auto method = vm.Linker().FindDirectMethod(*type, name, signature);
    if (!method) throw DexVmError(DexVmErrorReason::unresolved_reference, name);
    const auto result = vm.Call(*method, args);
    if (result.exception.IsValid())
        throw VmJavaThrow{vm.Linker().Class(result.exception_class).descriptor,
                          result.exception_message, result.exception};
    return result.value;
}
VmObjectRef Construct(Interpreter& vm, const char* owner) {
    auto object = vm.NewIntrinsicInstance(owner);
    const auto roots = vm.ProtectReferences(std::array{object});
    Direct(vm, owner, "<init>", "()V", {VmValue::Ref(object)});
    return object;
}
void Put(Interpreter& vm, VmObjectRef object, const std::string& key, const std::string& value) {
    const auto k = vm.NewStringUtf8(key);
    const auto roots = vm.ProtectReferences(std::array{object, k});
    InvokeGuest(vm, object, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                {VmValue::Ref(k), VmValue::Ref(vm.NewStringUtf8(value))});
}
IntrinsicClassDecl SecurityConfiguration() {
    auto b = IntrinsicClassBuilder::Class("Ljava/security/Security;");
    b.ClassInitializer([](IntrinsicContext& c) {
        auto& vm = c.vm;
        const auto props = Construct(vm, "Ljava/util/Properties;");
        vm.SetIntrinsicStaticRef("Ljava/security/Security;", "secprops", "Ljava/util/Properties;",
                                 props);
        Put(vm, props, "security.provider.1", "com.android.org.conscrypt.OpenSSLProvider");
        Put(vm, props, "security.provider.2",
            "org.apache.harmony.security.provider.cert.DRLCertFactory");
        Put(vm, props, "security.provider.3", "org.ogplay.security.OgPlayKeyStoreProvider");
        Put(vm, props, "keystore.type", "BKS");
        const auto door = Construct(vm, "Ljava/security/Security$SecurityDoor;");
        vm.SetIntrinsicStaticRef("Lorg/apache/harmony/security/fortress/Engine;", "door",
                                 "Lorg/apache/harmony/security/fortress/SecurityAccess;", door);
        return VmValue::Void();
    });
    return std::move(b).Build();
}
IntrinsicClassDecl CryptoProvider() {
    auto b = IntrinsicClassBuilder::Class(kProvider, "Ljava/security/Provider;");
    b.Constructor("()V", [](IntrinsicContext& c) {
        auto& vm = c.vm;
        const auto name = vm.NewStringUtf8("AndroidOpenSSL");
        const auto roots = vm.ProtectReferences(std::array{name});
        Direct(vm, "Ljava/security/Provider;", "<init>", "(Ljava/lang/String;DLjava/lang/String;)V",
               {VmValue::Ref(c.receiver), VmValue::Ref(name), VmValue::Double(1.0),
                VmValue::Ref(vm.NewStringUtf8(
                    "OGPlay API 19 guest OpenSSL AES/digests/signature verification; OS entropy"))});
        for (const auto mode : {"ECB", "CBC", "CTR"}) {
            for (const auto padding : {"NoPadding", "PKCS5Padding"}) {
                if (std::string_view(mode) == "CTR" && std::string_view(padding) != "NoPadding")
                    continue;
                Put(vm, c.receiver, std::string("Cipher.AES/") + mode + "/" + padding,
                    std::string("com.android.org.conscrypt.OpenSSLCipher$AES$") + mode + "$" +
                        padding);
            }
        }
        Put(vm, c.receiver, "Cipher.AES", "org.ogplay.security.DefaultAes");
        Put(vm, c.receiver, "SecureRandom.OGPlayOS", "org.ogplay.security.OsRandom");
        Put(vm, c.receiver, "SecureRandom.SHA1PRNG",
            "com.android.org.conscrypt.OpenSSLRandom");
        Put(vm, c.receiver, "SecureRandom.SHA1PRNG ImplementedIn", "Software");
        Put(vm, c.receiver, "KeyGenerator.AES",
            "org.ogplay.security.AesKeyGenerator");
        Put(vm, c.receiver, "Mac.HmacSHA1",
            "com.android.org.conscrypt.OpenSSLMac$HmacSHA1");
        Put(vm, c.receiver, "Alg.Alias.Mac.1.2.840.113549.2.7", "HmacSHA1");
        Put(vm, c.receiver, "Alg.Alias.Mac.HMAC-SHA1", "HmacSHA1");
        Put(vm, c.receiver, "Alg.Alias.Mac.HMAC/SHA1", "HmacSHA1");
        // Match API 19 OpenSSLProvider names, aliases and OIDs.
        for (const auto& entry : std::array{
                 std::array{"MD5", "MD5", "1.2.840.113549.2.5"},
                 std::array{"SHA-1", "SHA1", "1.3.14.3.2.26"},
                 std::array{"SHA-256", "SHA256", "2.16.840.1.101.3.4.2.1"},
                 std::array{"SHA-384", "SHA384", "2.16.840.1.101.3.4.2.2"},
                 std::array{"SHA-512", "SHA512", "2.16.840.1.101.3.4.2.3"}}) {
            Put(vm, c.receiver, std::string("MessageDigest.") + entry[0],
                std::string("com.android.org.conscrypt.OpenSSLMessageDigestJDK$") + entry[1]);
            if (std::string_view(entry[0]) != entry[1])
                Put(vm, c.receiver, std::string("Alg.Alias.MessageDigest.") + entry[1], entry[0]);
            Put(vm, c.receiver, std::string("Alg.Alias.MessageDigest.") + entry[2], entry[0]);
        }
        Put(vm, c.receiver, "Alg.Alias.MessageDigest.SHA", "SHA-1");
        for (const auto& entry : kSignatures) {
            Put(vm, c.receiver, std::string("Signature.") + entry.algorithm,
                std::string("org.ogplay.security.Verify") + entry.algorithm);
            Put(vm, c.receiver, std::string("Alg.Alias.Signature.") + entry.oid, entry.algorithm);
            Put(vm, c.receiver, std::string("Alg.Alias.Signature.OID.") + entry.oid,
                entry.algorithm);
        }
        return VmValue::Void();
    });
    return std::move(b).Build();
}
// The bare AES service must not let JCA's fallback silently widen the
// configured transformation set via engineSetMode on the generic AES SPI.
IntrinsicClassDecl DefaultAes() {
    constexpr auto parent = "Lcom/android/org/conscrypt/OpenSSLCipher$AES$ECB$PKCS5Padding;";
    auto b = IntrinsicClassBuilder::Class("Lorg/ogplay/security/DefaultAes;", parent);
    b.Constructor("()V", [](IntrinsicContext& c) {
        Direct(c.vm, parent, "<init>", "()V", {VmValue::Ref(c.receiver)});
        return VmValue::Void();
    });
    b.OverrideMethod(
        "checkSupportedMode", "(Lcom/android/org/conscrypt/OpenSSLCipher$Mode;)V",
        [](IntrinsicContext& c) {
            const auto mode = InvokeGuest(c.vm, IntrinsicCall(c).NonNullRef(0, "mode"), "name",
                                          "()Ljava/lang/String;");
            if (c.vm.StringUtf8(mode.ref) != "ECB")
                throw VmJavaThrow{"Ljava/security/NoSuchAlgorithmException;",
                                  "AES default service only supports ECB"};
            return VmValue::Void();
        },
        kAccProtected);
    return std::move(b).Build();
}
IntrinsicClassDecl OsRandom(const CoreIntrinsicServices& services) {
    auto b = IntrinsicClassBuilder::Class("Lorg/ogplay/security/OsRandom;",
                                          "Ljava/security/SecureRandomSpi;");
    b.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); });
    const auto fill = [random = services.secure_random](Interpreter& vm, VmObjectRef array) {
        if (!random)
            throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                              "OS secure random is not connected"};
        std::vector<std::byte> bytes(static_cast<std::size_t>(vm.Model().ArrayLength(array)));
        try {
            random(bytes);
        } catch (const std::exception&) {
            throw VmJavaThrow{"Ljava/security/ProviderException;", "OS secure random failed"};
        }
        vm.Model().WriteByteRegion(array, 0, bytes);
    };
    b.VirtualMethod(
        "engineNextBytes", "([B)V",
        [fill](IntrinsicContext& c) {
            fill(c.vm, IntrinsicCall(c).NonNullRef(0, "bytes"));
            return VmValue::Void();
        },
        kAccProtected);
    b.VirtualMethod(
        "engineGenerateSeed", "(I)[B",
        [fill](IntrinsicContext& c) {
            const auto count = IntrinsicCall(c).Int(0);
            if (count < 0)
                throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "negative seed size"};
            auto array = c.vm.Model().NewPrimitiveArray(c.vm.Linker().ResolveDescriptor("[B"),
                                                        JniPrimitiveKind::byte, count);
            const auto roots = c.vm.ProtectReferences(std::array{array});
            fill(c.vm, array);
            return VmValue::Ref(array);
        },
        kAccProtected);
    b.UnimplementedVirtual("engineSetSeed", "([B)V", kAccProtected);
    return std::move(b).Build();
}
IntrinsicClassDecl OpenSslRandom(const CoreIntrinsicServices& services) {
    auto b = IntrinsicClassBuilder::Class(
        "Lcom/android/org/conscrypt/OpenSSLRandom;",
        "Ljava/security/SecureRandomSpi;", {"Ljava/io/Serializable;"});
    const auto seeded = b.BoundInstanceField("seeded", "Z", kAccPrivate);
    const auto ensure_seeded = [random = services.secure_random, seeded](IntrinsicContext& c) {
        IntrinsicCall call(c);
        if (call.GetInt(seeded)) return;
        if (!random)
            throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                              "OS secure random is not connected"};
        std::vector<std::byte> bytes(32);
        try {
            random(bytes);
        } catch (const std::exception&) {
            throw VmJavaThrow{"Ljava/security/ProviderException;", "OS secure random failed"};
        }
        const auto seed = c.vm.Model().NewPrimitiveArray(
            c.vm.Linker().ResolveDescriptor("[B"), JniPrimitiveKind::byte,
            static_cast<JniSize>(bytes.size()));
        const auto roots = c.vm.ProtectReferences(std::array{seed});
        c.vm.Model().WriteByteRegion(seed, 0, bytes);
        Direct(c.vm, kNative, "RAND_seed", "([B)V", {VmValue::Ref(seed)});
        call.SetInt(seeded, 1);
    };
    b.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); });
    b.VirtualMethod("engineSetSeed", "([B)V", [ensure_seeded](IntrinsicContext& c) {
        ensure_seeded(c);
        return Direct(c.vm, kNative, "RAND_seed", "([B)V",
                      {VmValue::Ref(IntrinsicCall(c).NonNullRef(0, "seed"))});
    }, kAccProtected);
    b.VirtualMethod("engineNextBytes", "([B)V", [ensure_seeded](IntrinsicContext& c) {
        ensure_seeded(c);
        return Direct(c.vm, kNative, "RAND_bytes", "([B)V",
                      {VmValue::Ref(IntrinsicCall(c).NonNullRef(0, "output"))});
    }, kAccProtected);
    b.VirtualMethod("engineGenerateSeed", "(I)[B", [ensure_seeded](IntrinsicContext& c) {
        const auto count = IntrinsicCall(c).Int(0);
        if (count < 0) {
            throw VmJavaThrow{"Ljava/lang/NegativeArraySizeException;",
                              std::to_string(count)};
        }
        const auto output = c.vm.Model().NewPrimitiveArray(
            c.vm.Linker().ResolveDescriptor("[B"), JniPrimitiveKind::byte,
            count);
        const auto roots = c.vm.ProtectReferences(std::array{output});
        ensure_seeded(c);
        Direct(c.vm, kNative, "RAND_bytes", "([B)V",
               {VmValue::Ref(output)});
        return VmValue::Ref(output);
    }, kAccProtected);
    return std::move(b).Build();
}
IntrinsicClassDecl AesKeyGenerator() {
    auto b = IntrinsicClassBuilder::Class(
        "Lorg/ogplay/security/AesKeyGenerator;", "Ljavax/crypto/KeyGeneratorSpi;");
    const auto bits = b.BoundInstanceField("keySize", "I", kAccPrivate);
    const auto random = b.BoundInstanceField(
        "random", "Ljava/security/SecureRandom;", kAccPrivate);
    b.Constructor("()V", [bits](IntrinsicContext& c) {
        IntrinsicCall(c).SetInt(bits, 128);
        return VmValue::Void();
    });
    const auto require_random = [](IntrinsicContext& c, VmObjectRef value) {
        if (value.IsValid()) return value;
        auto& vm = c.vm;
        const auto type = vm.Linker().ResolveDescriptor("Ljava/security/SecureRandom;");
        const auto value_object = vm.Model().NewInstance(
            type, vm.Linker().Class(type).instance_slots);
        const auto roots = vm.ProtectReferences(std::array{value_object});
        Direct(vm, "Ljava/security/SecureRandom;", "<init>", "()V",
               {VmValue::Ref(value_object)});
        return value_object;
    };
    b.VirtualMethod(
        "engineInit", "(ILjava/security/SecureRandom;)V",
        [bits, random, require_random](IntrinsicContext& c) {
            IntrinsicCall call(c);
            const auto size = call.Int(0);
            if (size != 128 && size != 192 && size != 256) {
                throw VmJavaThrow{"Ljava/security/InvalidParameterException;",
                                  "AES key size must be 128, 192, or 256 bits"};
            }
            call.SetInt(bits, size);
            call.SetRef(random, require_random(c, call.Ref(1)));
            return VmValue::Void();
        }, kAccProtected);
    b.VirtualMethod(
        "engineInit", "(Ljava/security/SecureRandom;)V",
        [random, require_random](IntrinsicContext& c) {
            IntrinsicCall call(c);
            call.SetRef(random, require_random(c, call.Ref(0)));
            return VmValue::Void();
        }, kAccProtected);
    b.VirtualMethod(
        "engineInit", "(Ljava/security/spec/AlgorithmParameterSpec;Ljava/security/SecureRandom;)V",
        [](IntrinsicContext&) -> VmValue {
            throw VmJavaThrow{"Ljava/security/InvalidAlgorithmParameterException;",
                              "AES KeyGenerator parameters are not supported"};
        }, kAccProtected);
    b.VirtualMethod(
        "engineGenerateKey", "()Ljavax/crypto/SecretKey;",
        [bits, random, require_random](IntrinsicContext& c) {
            IntrinsicCall call(c);
            const auto source = require_random(c, call.GetRef(random));
            call.SetRef(random, source);
            const auto array = c.vm.Model().NewPrimitiveArray(
                c.vm.Linker().ResolveDescriptor("[B"), JniPrimitiveKind::byte,
                call.GetInt(bits) / 8);
            const auto roots = c.vm.ProtectReferences(std::array{source, array});
            InvokeGuest(c.vm, source, "nextBytes", "([B)V",
                        {VmValue::Ref(array)});
            const auto key_type = c.vm.Linker().ResolveDescriptor(
                "Ljavax/crypto/spec/SecretKeySpec;");
            const auto key = c.vm.Model().NewInstance(
                key_type, c.vm.Linker().Class(key_type).instance_slots);
            const auto algorithm = c.vm.NewStringUtf8("AES");
            const auto key_roots = c.vm.ProtectReferences(
                std::array{source, array, key, algorithm});
            Direct(c.vm, "Ljavax/crypto/spec/SecretKeySpec;", "<init>",
                   "([BLjava/lang/String;)V",
                   {VmValue::Ref(key), VmValue::Ref(array),
                    VmValue::Ref(algorithm)});
            return VmValue::Ref(key);
        }, kAccProtected);
    return std::move(b).Build();
}
IntrinsicClassDecl CipherContext() {
    auto b = IntrinsicClassBuilder::Class("Lcom/android/org/conscrypt/OpenSSLCipherContext;");
    const auto field = b.BoundInstanceField("context", "J", kAccPrivate | kAccFinal);
    b.Constructor(
        "(J)V",
        [field](IntrinsicContext& c) {
            IntrinsicCall call(c);
            const auto token = call.Long(0);
            if (token == 0) throw VmJavaThrow{"Ljava/lang/NullPointerException;", "ctx == 0"};
            call.SetLong(field, token);
            const auto native = c.vm.Linker().FindClass(kNative);
            const auto cleanup =
                c.vm.Linker().FindDirectMethod(*native, "EVP_CIPHER_CTX_cleanup", "(J)V");
            c.vm.TrackGuestNativeResource(c.receiver, *cleanup, token);
            return VmValue::Void();
        },
        0);
    return std::move(b).Build();
}
IntrinsicClassDecl NativeVerificationBoundary() {
    auto b = IntrinsicClassBuilder::Class(kVerificationNative);
    b.GuestNativeStatic("verify", "([B[B[BLjava/lang/String;)Z");
    return std::move(b).Build();
}
IntrinsicClassDecl NativeKeyStoreBoundary() {
    auto b = IntrinsicClassBuilder::Class(kKeyStoreNative);
    b.GuestNativeStatic("desEdeCbc", "(Z[B[B[B)[B");
    b.GuestNativeStatic("decodePrivateKey", "([B)J");
    b.GuestNativeStatic("encodePrivateKey", "(J)[B");
    b.GuestNativeStatic("privateKeyType", "(J)I");
    b.GuestNativeStatic("freePrivateKey", "(J)V");
    return std::move(b).Build();
}
IntrinsicClassDecl NativeCryptoGuestAdmission() {
    auto b = IntrinsicClassBuilder::Class(kNative);
    b.AdmitBootNativeMethods();
    return std::move(b).Build();
}
IntrinsicClassDecl VerificationSpi(const std::string& algorithm) {
    auto b = IntrinsicClassBuilder::Class("Lorg/ogplay/security/Verify" + algorithm + ";",
                                          "Ljava/security/SignatureSpi;");
    const auto key = b.BoundInstanceField("encodedKey", "[B", kAccPrivate);
    const auto buffer =
        b.BoundInstanceField("message", "Ljava/io/ByteArrayOutputStream;", kAccPrivate);
    b.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); });
    b.VirtualMethod(
        "engineInitVerify", "(Ljava/security/PublicKey;)V",
        [key, buffer, algorithm](IntrinsicContext& c) {
            IntrinsicCall call(c);
            const auto pub = call.NonNullRef(0, "public key");
            const auto data = InvokeGuest(c.vm, pub, "getEncoded", "()[B").ref;
            if (!data.IsValid())
                throw VmJavaThrow{"Ljava/security/InvalidKeyException;",
                                  "public key has no encoding"};
            const auto roots = c.vm.ProtectReferences(std::array{data});
            Direct(c.vm, kVerificationNative, "verify", "([B[B[BLjava/lang/String;)Z",
                   {VmValue::Ref(data), VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{}),
                    VmValue::Ref(c.vm.NewStringUtf8(algorithm))});
            call.SetRef(key, c.vm.Model().CloneObject(data));
            call.SetRef(buffer, Construct(c.vm, "Ljava/io/ByteArrayOutputStream;"));
            return VmValue::Void();
        },
        kAccProtected);
    const auto update = [buffer](IntrinsicContext& c) {
        IntrinsicCall call(c);
        const auto stream = call.GetRef(buffer);
        if (!stream.IsValid())
            throw VmJavaThrow{"Ljava/security/SignatureException;", "signature is not initialized"};
        const auto size = InvokeGuest(c.vm, stream, "size", "()I").AsInt();
        const auto count = c.arguments.size() == 1 ? 1 : call.Int(2);
        if (count > 1048576 - size)
            throw VmJavaThrow{"Ljava/security/SignatureException;",
                              "signature input exceeds 1 MiB limit"};
        if (c.arguments.size() == 1)
            InvokeGuest(c.vm, stream, "write", "(I)V", {VmValue::Int(call.Int(0))});
        else
            InvokeGuest(c.vm, stream, "write", "([BII)V",
                        {c.arguments[0], c.arguments[1], c.arguments[2]});
        return VmValue::Void();
    };
    b.VirtualMethod("engineUpdate", "(B)V", update, kAccProtected);
    b.VirtualMethod("engineUpdate", "([BII)V", update, kAccProtected);
    b.VirtualMethod(
        "engineVerify", "([B)Z",
        [key, buffer, algorithm](IntrinsicContext& c) {
            IntrinsicCall call(c);
            const auto signature = call.NonNullRef(0, "signature");
            const auto stream = call.GetRef(buffer);
            if (!stream.IsValid())
                throw VmJavaThrow{"Ljava/security/SignatureException;",
                                  "signature is not initialized"};
            const auto bytes = InvokeGuest(c.vm, stream, "toByteArray", "()[B").ref;
            const auto roots = c.vm.ProtectReferences(std::array{bytes});
            InvokeGuest(c.vm, stream, "reset", "()V");
            return Direct(c.vm, kVerificationNative, "verify", "([B[B[BLjava/lang/String;)Z",
                          {VmValue::Ref(call.GetRef(key)), VmValue::Ref(bytes),
                           VmValue::Ref(signature), VmValue::Ref(c.vm.NewStringUtf8(algorithm))});
        },
        kAccProtected);
    b.UnimplementedVirtual("engineInitSign", "(Ljava/security/PrivateKey;)V", kAccProtected);
    b.UnimplementedVirtual("engineSign", "()[B", kAccProtected);
    b.UnimplementedVirtual("engineSetParameter", "(Ljava/lang/String;Ljava/lang/Object;)V",
                           kAccProtected);
    b.UnimplementedVirtual("engineGetParameter", "(Ljava/lang/String;)Ljava/lang/Object;",
                           kAccProtected);
    return std::move(b).Build();
}
}  // namespace
void AppendJavaCrypto(std::vector<IntrinsicClassDecl>& catalog,
                      const CoreIntrinsicServices& services) {
    catalog.push_back(SecurityConfiguration());
    catalog.push_back(CryptoProvider());
    catalog.push_back(DefaultAes());
    catalog.push_back(OsRandom(services));
    catalog.push_back(OpenSslRandom(services));
    catalog.push_back(AesKeyGenerator());
    catalog.push_back(NativeCryptoGuestAdmission());
    catalog.push_back(NativeVerificationBoundary());
    catalog.push_back(NativeKeyStoreBoundary());
    catalog.push_back(CipherContext());
    for (const auto& entry : kSignatures) catalog.push_back(VerificationSpi(entry.algorithm));
}
}  // namespace ogplay::runtime::dexvm::intrinsics
