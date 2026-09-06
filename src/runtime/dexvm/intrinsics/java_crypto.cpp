// API 19 JCA configuration and explicit guest OpenSSL/OS entropy boundaries.
#include "catalog.h"
#include "shared.h"

namespace ogplay::runtime::dexvm::intrinsics {
namespace {
using namespace detail;
constexpr auto kNative = "Lcom/android/org/conscrypt/NativeCrypto;";
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
                    "OGPlay API 19 guest OpenSSL AES/signature verification; OS entropy"))});
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
IntrinsicClassDecl NativeCryptoBoundary() {
    auto b = IntrinsicClassBuilder::Class(kNative);
    b.ClassInitializer([](IntrinsicContext& c) {
        Direct(c.vm, "Ljava/lang/System;", "loadLibrary", "(Ljava/lang/String;)V",
               {VmValue::Ref(c.vm.NewStringUtf8("ogplay_cipher"))});
        return VmValue::Void();
    });
    for (const auto& [name, signature] : std::array{
             std::pair{"EVP_get_cipherbyname", "(Ljava/lang/String;)J"},
             std::pair{"EVP_CIPHER_CTX_new", "()J"}, std::pair{"EVP_CIPHER_CTX_cleanup", "(J)V"},
             std::pair{"EVP_CIPHER_CTX_block_size", "(J)I"},
             std::pair{"get_EVP_CIPHER_CTX_buf_len", "(J)I"},
             std::pair{"EVP_CIPHER_CTX_set_padding", "(JZ)V"},
             std::pair{"EVP_CIPHER_CTX_set_key_length", "(JI)V"},
             std::pair{"EVP_CIPHER_iv_length", "(J)I"},
             std::pair{"EVP_CipherInit_ex", "(JJ[B[BZ)V"},
             std::pair{"EVP_CipherUpdate", "(J[BI[BII)I"},
             std::pair{"EVP_CipherFinal_ex", "(J[BI)I"},
             std::pair{"verify_signature", "([B[B[BLjava/lang/String;)Z"}}) {
        b.GuestNativeStatic(name, signature);
    }
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
            Direct(c.vm, kNative, "verify_signature", "([B[B[BLjava/lang/String;)Z",
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
            return Direct(c.vm, kNative, "verify_signature", "([B[B[BLjava/lang/String;)Z",
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
    catalog.push_back(NativeCryptoBoundary());
    catalog.push_back(CipherContext());
    for (const auto& entry : kSignatures) catalog.push_back(VerificationSpi(entry.algorithm));
}
}  // namespace ogplay::runtime::dexvm::intrinsics
