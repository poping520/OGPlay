// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from java_net_MalformedURLException.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics::dvm80_java_net_MalformedURLException {
using namespace detail;

IntrinsicClassDecl Declare_java_net_MalformedURLException() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/net/MalformedURLException;", "Ljava/io/IOException;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics

// ---- migrated from dexvm_android java.net / javax.net.ssl ----

#include <array>
#include <cctype>

namespace ogplay::runtime::dexvm::intrinsics {

namespace {

IntrinsicHandler NetworkUnsupported() {
    return [](IntrinsicContext&) -> VmValue {
        throw VmJavaThrow{
            "Ljava/lang/UnsupportedOperationException;",
            "SMS/network actions are outside the compatibility scope"};
    };
}

IntrinsicHandler NoopVoid() {
    return [](IntrinsicContext&) { return VmValue::Void(); };
}

IntrinsicClassDecl DeclarePlatformHttpURLConnection() {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljava/net/HttpURLConnection;", "Ljava/net/URLConnection;");
    builder.FinalMethod("connect", "()V", NetworkUnsupported());
    builder.FinalMethod("disconnect", "()V", NetworkUnsupported());
    builder.FinalMethod("getInputStream", "()Ljava/io/InputStream;",
                        NetworkUnsupported());
    builder.FinalMethod("setConnectTimeout", "(I)V", NetworkUnsupported());
    return std::move(builder).Build();
}

IntrinsicClassDecl DeclarePlatformUrl() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/net/URL;",
                                                "Ljava/lang/Object;");
    builder.Constructor("(Ljava/lang/String;)V", NetworkUnsupported());
    builder.FinalMethod("openConnection", "()Ljava/net/URLConnection;",
                        NetworkUnsupported());
    return std::move(builder).Build();
}

IntrinsicClassDecl DeclarePlatformUrlConnection() {
    return std::move(IntrinsicClassBuilder::Class(
                         "Ljava/net/URLConnection;", "Ljava/lang/Object;"))
        .Build();
}

IntrinsicClassDecl DeclarePlatformUrlEncoder() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/net/URLEncoder;",
                                                "Ljava/lang/Object;");
    builder.StaticMethod(
        "encode", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        [](IntrinsicContext& call) {
            auto charset = call.vm.StringUtf8(call.arguments[1].ref);
            for (auto& byte : charset) {
                byte = static_cast<char>(
                    std::toupper(static_cast<unsigned char>(byte)));
            }
            if (charset != "UTF-8" && charset != "UTF8") {
                throw VmJavaThrow{
                    "Ljava/io/UnsupportedEncodingException;", charset};
            }
            constexpr std::array<char, 16> kHex{
                '0', '1', '2', '3', '4', '5', '6', '7',
                '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
            std::string encoded;
            for (const auto byte :
                 call.vm.StringUtf8(call.arguments[0].ref)) {
                const auto value = static_cast<unsigned char>(byte);
                const bool safe = (value >= 'a' && value <= 'z') ||
                                  (value >= 'A' && value <= 'Z') ||
                                  (value >= '0' && value <= '9') ||
                                  value == '-' || value == '_' ||
                                  value == '.' || value == '*';
                if (safe) {
                    encoded.push_back(static_cast<char>(value));
                } else if (value == ' ') {
                    encoded.push_back('+');
                } else {
                    encoded.push_back('%');
                    encoded.push_back(kHex[value >> 4U]);
                    encoded.push_back(kHex[value & 0x0FU]);
                }
            }
            return VmValue::Ref(call.vm.NewStringUtf8(encoded));
        });
    return std::move(builder).Build();
}

IntrinsicClassDecl DeclareHostnameVerifier() {
    return std::move(IntrinsicClassBuilder::Interface(
                         "Ljavax/net/ssl/HostnameVerifier;"))
        .Build();
}

IntrinsicClassDecl DeclareHttpsURLConnection() {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljavax/net/ssl/HttpsURLConnection;",
        "Ljava/net/HttpURLConnection;");
    builder.StaticMethod("setDefaultHostnameVerifier",
                         "(Ljavax/net/ssl/HostnameVerifier;)V", NoopVoid());
    builder.StaticMethod("setDefaultSSLSocketFactory",
                         "(Ljavax/net/ssl/SSLSocketFactory;)V", NoopVoid());
    builder.FinalMethod("setRequestMethod", "(Ljava/lang/String;)V",
                        NetworkUnsupported());
    builder.FinalMethod(
        "setRequestProperty",
        "(Ljava/lang/String;Ljava/lang/String;)V", NetworkUnsupported());
    builder.FinalMethod("getResponseCode", "()I", NetworkUnsupported());
    return std::move(builder).Build();
}

IntrinsicClassDecl DeclareKeyManager() {
    return std::move(IntrinsicClassBuilder::Interface(
                         "Ljavax/net/ssl/KeyManager;"))
        .Build();
}

IntrinsicClassDecl DeclareSslContext(
    const CoreIntrinsicServices& services) {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljavax/net/ssl/SSLContext;", "Ljava/lang/Object;");
    builder.StaticMethod(
        "getInstance",
        "(Ljava/lang/String;)Ljavax/net/ssl/SSLContext;",
        [](IntrinsicContext& call) {
            return VmValue::Ref(call.vm.NewIntrinsicInstance(
                "Ljavax/net/ssl/SSLContext;"));
        });
    builder.FinalMethod(
        "init",
        "([Ljavax/net/ssl/KeyManager;[Ljavax/net/ssl/TrustManager;"
        "Ljava/security/SecureRandom;)V",
        NoopVoid());
    builder.FinalMethod(
        "getSocketFactory", "()Ljavax/net/ssl/SSLSocketFactory;",
        [services](IntrinsicContext& call) {
            if (services.singleton) {
                return VmValue::Ref(services.singleton(
                    call.vm, "ssl_socket_factory",
                    "Ljavax/net/ssl/SSLSocketFactory;"));
            }
            return VmValue::Ref(call.vm.NewIntrinsicInstance(
                "Ljavax/net/ssl/SSLSocketFactory;"));
        });
    return std::move(builder).Build();
}

IntrinsicClassDecl DeclareSslSocketFactory() {
    return std::move(IntrinsicClassBuilder::Class(
                         "Ljavax/net/ssl/SSLSocketFactory;",
                         "Ljava/lang/Object;"))
        .Build();
}

IntrinsicClassDecl DeclareTrustManager() {
    return std::move(IntrinsicClassBuilder::Interface(
                         "Ljavax/net/ssl/TrustManager;"))
        .Build();
}

IntrinsicClassDecl DeclareX509TrustManager() {
    return std::move(IntrinsicClassBuilder::Interface(
                         "Ljavax/net/ssl/X509TrustManager;"))
        .Build();
}

}  // namespace

void AppendJavaNetPlatform(std::vector<IntrinsicClassDecl>& catalog,
                           const CoreIntrinsicServices& services) {
    catalog.push_back(DeclarePlatformHttpURLConnection());
    catalog.push_back(DeclarePlatformUrl());
    catalog.push_back(DeclarePlatformUrlConnection());
    catalog.push_back(DeclarePlatformUrlEncoder());
    catalog.push_back(DeclareHostnameVerifier());
    catalog.push_back(DeclareHttpsURLConnection());
    catalog.push_back(DeclareKeyManager());
    catalog.push_back(DeclareSslContext(services));
    catalog.push_back(DeclareSslSocketFactory());
    catalog.push_back(DeclareTrustManager());
    catalog.push_back(DeclareX509TrustManager());
}

}  // namespace ogplay::runtime::dexvm::intrinsics

namespace ogplay::runtime::dexvm::intrinsics {
IntrinsicClassDecl Declare_java_net_MalformedURLException() {
    return dvm80_java_net_MalformedURLException::Declare_java_net_MalformedURLException();
}
}  // namespace ogplay::runtime::dexvm::intrinsics

// ---- migrated from java_net_SocketException.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics::dvm80_java_net_SocketException {
using namespace detail;

IntrinsicClassDecl Declare_java_net_SocketException() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/net/SocketException;", "Ljava/io/IOException;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics

namespace ogplay::runtime::dexvm::intrinsics {
IntrinsicClassDecl Declare_java_net_SocketException() {
    return dvm80_java_net_SocketException::Declare_java_net_SocketException();
}
}  // namespace ogplay::runtime::dexvm::intrinsics

// ---- migrated from java_net_SocketTimeoutException.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics::dvm80_java_net_SocketTimeoutException {
using namespace detail;

IntrinsicClassDecl Declare_java_net_SocketTimeoutException() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/net/SocketTimeoutException;", "Ljava/io/IOException;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics

namespace ogplay::runtime::dexvm::intrinsics {
IntrinsicClassDecl Declare_java_net_SocketTimeoutException() {
    return dvm80_java_net_SocketTimeoutException::Declare_java_net_SocketTimeoutException();
}
}  // namespace ogplay::runtime::dexvm::intrinsics

// ---- migrated from java_net_UnknownHostException.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics::dvm80_java_net_UnknownHostException {
using namespace detail;

IntrinsicClassDecl Declare_java_net_UnknownHostException() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/net/UnknownHostException;", "Ljava/io/IOException;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics

namespace ogplay::runtime::dexvm::intrinsics {
IntrinsicClassDecl Declare_java_net_UnknownHostException() {
    return dvm80_java_net_UnknownHostException::Declare_java_net_UnknownHostException();
}
}  // namespace ogplay::runtime::dexvm::intrinsics
