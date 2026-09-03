// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from java_net_MalformedURLException.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;

IntrinsicClassDecl Declare_java_net_MalformedURLException() {
    return DeclareSimpleThrowable("Ljava/net/MalformedURLException;", "Ljava/io/IOException;");
}

}  // namespace ogplay::runtime::dexvm::intrinsics

// ---- migrated from dexvm_android java.net / javax.net.ssl ----

#include <array>
#include <cctype>
#include <optional>
#include <string_view>

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

struct ParsedUri final {
    std::string spec;
    std::optional<std::string> scheme;
    std::string scheme_specific_part;
    std::optional<std::string> authority;
    std::optional<std::string> path;
    std::optional<std::string> query;
    std::optional<std::string> fragment;
    bool absolute{};
    bool opaque{};
};

[[noreturn]] void UriSyntax(const std::string_view spec,
                            const std::string_view reason) {
    throw VmJavaThrow{"Ljava/net/URISyntaxException;",
                      std::string(reason) + ": " + std::string(spec)};
}

void ValidateUriComponent(const std::string_view spec,
                          const std::string_view component) {
    const auto hex = [](const unsigned char value) {
        return std::isxdigit(value) != 0;
    };
    for (std::size_t index = 0; index < component.size(); ++index) {
        const auto value = static_cast<unsigned char>(component[index]);
        if (value <= 0x20U || value == 0x7fU)
            UriSyntax(spec, "Illegal character in URI");
        if (value == '%') {
            if (index + 2U >= component.size() ||
                !hex(static_cast<unsigned char>(component[index + 1U])) ||
                !hex(static_cast<unsigned char>(component[index + 2U]))) {
                UriSyntax(spec, "Invalid percent escape");
            }
            index += 2U;
        }
    }
}

ParsedUri ParseUri(std::string spec) {
    ParsedUri result;
    result.spec = std::move(spec);
    const auto view = std::string_view(result.spec);
    const auto fragment_start = view.find('#');
    const auto main_end = fragment_start == std::string_view::npos
        ? view.size() : fragment_start;
    if (fragment_start != std::string_view::npos) {
        result.fragment = std::string(view.substr(fragment_start + 1U));
        ValidateUriComponent(view, *result.fragment);
    }

    std::size_t start{};
    const auto colon = view.substr(0, main_end).find(':');
    const auto first_delimiter = view.substr(0, main_end).find_first_of("/?");
    if (colon != std::string_view::npos &&
        (first_delimiter == std::string_view::npos || colon < first_delimiter)) {
        if (colon == 0U ||
            std::isalpha(static_cast<unsigned char>(view[0])) == 0) {
            UriSyntax(view, "Invalid URI scheme");
        }
        for (std::size_t index = 1; index < colon; ++index) {
            const auto value = static_cast<unsigned char>(view[index]);
            if (std::isalnum(value) == 0 && value != '+' && value != '-' &&
                value != '.') {
                UriSyntax(view, "Invalid URI scheme");
            }
        }
        result.absolute = true;
        result.scheme = std::string(view.substr(0, colon));
        start = colon + 1U;
        if (start == main_end)
            UriSyntax(view, "Scheme-specific part expected");
    }

    result.scheme_specific_part =
        std::string(view.substr(start, main_end - start));
    ValidateUriComponent(view, result.scheme_specific_part);
    result.opaque = result.absolute &&
        (result.scheme_specific_part.empty() ||
         result.scheme_specific_part.front() != '/');
    if (result.opaque) return result;

    std::size_t path_start = start;
    if (start + 1U < main_end && view.substr(start, 2U) == "//") {
        const auto authority_start = start + 2U;
        auto authority_end = view.find_first_of("/?", authority_start);
        if (authority_end == std::string_view::npos || authority_end > main_end)
            authority_end = main_end;
        if (authority_start == main_end)
            UriSyntax(view, "Authority expected");
        if (authority_start < authority_end) {
            result.authority = std::string(
                view.substr(authority_start, authority_end - authority_start));
            ValidateUriComponent(view, *result.authority);
        }
        path_start = authority_end;
    }
    auto query_start = view.find('?', path_start);
    if (query_start == std::string_view::npos || query_start > main_end)
        query_start = main_end;
    result.path = std::string(view.substr(path_start, query_start - path_start));
    ValidateUriComponent(view, *result.path);
    if (query_start < main_end) {
        result.query = std::string(
            view.substr(query_start + 1U, main_end - query_start - 1U));
        ValidateUriComponent(view, *result.query);
    }
    return result;
}

std::string DecodeUriComponent(const std::string_view raw) {
    const auto nibble = [](const unsigned char value) -> unsigned char {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10U;
        return value - 'A' + 10U;
    };
    std::string decoded;
    decoded.reserve(raw.size());
    for (std::size_t index = 0; index < raw.size(); ++index) {
        if (raw[index] != '%') {
            decoded.push_back(raw[index]);
            continue;
        }
        decoded.push_back(static_cast<char>(
            (nibble(static_cast<unsigned char>(raw[index + 1U])) << 4U) |
            nibble(static_cast<unsigned char>(raw[index + 2U]))));
        index += 2U;
    }
    return decoded;
}

IntrinsicClassDecl DeclareUriSyntaxException() {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljava/net/URISyntaxException;", "Ljava/lang/Exception;");
    const auto input = builder.BoundInstanceField("input", "Ljava/lang/String;",
                                                   kAccPrivate);
    const auto index = builder.BoundInstanceField("index", "I", kAccPrivate);
    const auto construct = [input, index](IntrinsicContext& context) {
        IntrinsicCall call(context);
        const auto input_value = call.NonNullRef(0, "input");
        const auto reason = call.NonNullRef(1, "reason");
        const auto error_index = context.arguments.size() == 3U
            ? call.Int(2) : -1;
        if (error_index < -1) {
            throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                              "URI syntax index < -1"};
        }
        call.SetRef(input, input_value);
        call.SetInt(index, error_index);
        call.Vm().SetThrowableMessage(call.Receiver(), reason);
        return VmValue::Void();
    };
    builder.Constructor("(Ljava/lang/String;Ljava/lang/String;)V", construct);
    builder.Constructor("(Ljava/lang/String;Ljava/lang/String;I)V", construct);
    builder.FinalMethod("getIndex", "()I", [index](IntrinsicContext& context) {
        return VmValue::Int(IntrinsicCall(context).GetInt(index));
    });
    builder.FinalMethod("getReason", "()Ljava/lang/String;",
        [](IntrinsicContext& context) {
            return VmValue::Ref(
                context.vm.ThrowableMessage(context.receiver));
        });
    builder.FinalMethod("getInput", "()Ljava/lang/String;",
        [input](IntrinsicContext& context) {
            return VmValue::Ref(IntrinsicCall(context).GetRef(input));
        });
    return std::move(builder).Build();
}

IntrinsicClassDecl DeclareUri() {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljava/net/URI;", "Ljava/lang/Object;",
        {"Ljava/io/Serializable;", "Ljava/lang/Comparable;"},
        kAccPublic | kAccFinal);
    const auto spec = builder.BoundInstanceField("string", "Ljava/lang/String;",
                                                 kAccPrivate);
    const auto scheme = builder.BoundInstanceField("scheme", "Ljava/lang/String;",
                                                   kAccPrivate);
    const auto ssp = builder.BoundInstanceField(
        "schemeSpecificPart", "Ljava/lang/String;", kAccPrivate);
    const auto authority = builder.BoundInstanceField(
        "authority", "Ljava/lang/String;", kAccPrivate);
    const auto path = builder.BoundInstanceField("path", "Ljava/lang/String;",
                                                 kAccPrivate);
    const auto query = builder.BoundInstanceField("query", "Ljava/lang/String;",
                                                  kAccPrivate);
    const auto fragment = builder.BoundInstanceField(
        "fragment", "Ljava/lang/String;", kAccPrivate);
    const auto absolute =
        builder.BoundInstanceField("absolute", "Z", kAccPrivate);
    const auto opaque = builder.BoundInstanceField("opaque", "Z", kAccPrivate);
    builder.Constructor("(Ljava/lang/String;)V",
        [=](IntrinsicContext& context) {
            IntrinsicCall call(context);
            const auto input = call.NonNullRef(0, "uri");
            const auto parsed = ParseUri(call.Vm().StringUtf8(input));
            const auto string_ref = [&](const std::optional<std::string>& value) {
                return value.has_value() ? call.Vm().NewStringUtf8(*value)
                                         : VmObjectRef{};
            };
            call.SetRef(spec, input);
            call.SetRef(scheme, string_ref(parsed.scheme));
            call.SetRef(ssp, call.Vm().NewStringUtf8(
                parsed.scheme_specific_part));
            call.SetRef(authority, string_ref(parsed.authority));
            call.SetRef(path, string_ref(parsed.path));
            call.SetRef(query, string_ref(parsed.query));
            call.SetRef(fragment, string_ref(parsed.fragment));
            call.SetInt(absolute, parsed.absolute ? 1 : 0);
            call.SetInt(opaque, parsed.opaque ? 1 : 0);
            return VmValue::Void();
        });
    const auto raw = [](const IntrinsicFieldHandle field) {
        return [field](IntrinsicContext& context) {
            return VmValue::Ref(IntrinsicCall(context).GetRef(field));
        };
    };
    builder.FinalMethod("getScheme", "()Ljava/lang/String;", raw(scheme));
    builder.FinalMethod("getRawSchemeSpecificPart", "()Ljava/lang/String;",
                        raw(ssp));
    builder.FinalMethod("getRawAuthority", "()Ljava/lang/String;",
                        raw(authority));
    builder.FinalMethod("getRawPath", "()Ljava/lang/String;", raw(path));
    builder.FinalMethod("getRawQuery", "()Ljava/lang/String;", raw(query));
    builder.FinalMethod("getRawFragment", "()Ljava/lang/String;",
                        raw(fragment));
    builder.FinalMethod("getPath", "()Ljava/lang/String;",
        [path](IntrinsicContext& context) {
            IntrinsicCall call(context);
            const auto raw_path = call.GetRef(path);
            if (!raw_path.IsValid()) return VmValue::Ref(VmObjectRef{});
            return VmValue::Ref(call.Vm().NewStringUtf8(
                DecodeUriComponent(call.Vm().StringUtf8(raw_path))));
        });
    builder.FinalMethod("isAbsolute", "()Z",
        [absolute](IntrinsicContext& context) {
            return VmValue::Int(IntrinsicCall(context).GetInt(absolute));
        });
    builder.FinalMethod("isOpaque", "()Z",
        [opaque](IntrinsicContext& context) {
            return VmValue::Int(IntrinsicCall(context).GetInt(opaque));
        });
    builder.FinalOverrideMethod("toString", "()Ljava/lang/String;", raw(spec));
    return std::move(builder).Build();
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

[[noreturn]] void ThrowNetwork(const NetworkRuntimeError& error);
NetworkRuntime::Endpoint HostEndpoint(IntrinsicContext& call,
                                      VmObjectRef host_ref,
                                      std::int32_t port);

IntrinsicClassDecl DeclareSslSocketFactory(
    const CoreIntrinsicServices& services) {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljavax/net/ssl/SSLSocketFactory;", "Ljavax/net/SocketFactory;");
    builder.StaticMethod("getDefault", "()Ljavax/net/SocketFactory;",
        [services](IntrinsicContext& call) {
            if (services.singleton) {
                return VmValue::Ref(services.singleton(
                    call.vm, "ssl_socket_factory",
                    "Ljavax/net/ssl/SSLSocketFactory;"));
            }
            return VmValue::Ref(call.vm.NewIntrinsicInstance(
                "Ljavax/net/ssl/SSLSocketFactory;"));
        });
    builder.FinalOverrideMethod("createSocket", "()Ljava/net/Socket;",
        [](IntrinsicContext& call) {
            const auto socket = call.vm.NewIntrinsicInstance(
                "Ljavax/net/ssl/SSLSocket;");
            call.vm.Network().CreateSocket(socket, true);
            return VmValue::Ref(socket);
        });
    builder.FinalOverrideMethod("createSocket",
        "(Ljava/lang/String;I)Ljava/net/Socket;",
        [](IntrinsicContext& call) {
            const auto socket = call.vm.NewIntrinsicInstance(
                "Ljavax/net/ssl/SSLSocket;");
            call.vm.Network().CreateSocket(socket, true);
            try { call.vm.Network().Connect(socket,
                HostEndpoint(call, call.arguments[0].ref,
                             call.arguments[1].AsInt())); }
            catch (const NetworkRuntimeError& error) { ThrowNetwork(error); }
            return VmValue::Ref(socket);
        });
    return std::move(builder).Build();
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

[[noreturn]] void ThrowNetwork(const NetworkRuntimeError& error) {
    throw VmJavaThrow{"Ljava/net/SocketException;", error.what()};
}

NetworkRuntime::Endpoint EndpointFrom(IntrinsicContext& call,
                                      const VmObjectRef address,
                                      const std::int32_t port) {
    if (port < 0 || port > 65535)
        throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                          "port out of range"};
    if (!address.IsValid())
        throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                          "address is null"};
    try {
        auto endpoint = call.vm.Network().Address(address);
        endpoint.port = static_cast<std::uint16_t>(port);
        return endpoint;
    } catch (const NetworkRuntimeError& error) { ThrowNetwork(error); }
}

NetworkRuntime::Endpoint HostEndpoint(IntrinsicContext& call,
                                      const VmObjectRef host_ref,
                                      const std::int32_t port) {
    if (!host_ref.IsValid())
        throw VmJavaThrow{"Ljava/lang/NullPointerException;", "host is null"};
    if (port < 0 || port > 65535)
        throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                          "port out of range"};
    const auto host = call.vm.StringUtf8(host_ref);
    try {
        const auto addresses = call.vm.Network().Resolve(host);
        return {host, addresses.front(), static_cast<std::uint16_t>(port)};
    } catch (const NetworkRuntimeError& error) {
        throw VmJavaThrow{"Ljava/net/UnknownHostException;", error.what()};
    }
}

IntrinsicClassDecl DeclareInetAddress() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/net/InetAddress;",
                                                "Ljava/lang/Object;");
    builder.StaticMethod("getByName",
                         "(Ljava/lang/String;)Ljava/net/InetAddress;",
        [](IntrinsicContext& call) {
            const auto host = call.vm.StringUtf8(call.arguments[0].ref);
            try {
                const auto addresses = call.vm.Network().Resolve(host);
                const auto result = call.vm.NewIntrinsicInstance(
                    "Ljava/net/InetAddress;");
                call.vm.Network().SetAddress(result, host, addresses.front());
                return VmValue::Ref(result);
            } catch (const NetworkRuntimeError& error) {
                throw VmJavaThrow{"Ljava/net/UnknownHostException;",
                                  error.what()};
            }
        });
    builder.FinalMethod("getHostName", "()Ljava/lang/String;",
        [](IntrinsicContext& call) {
            try { return VmValue::Ref(call.vm.NewStringUtf8(
                call.vm.Network().Address(call.receiver).host)); }
            catch (const NetworkRuntimeError& error) { ThrowNetwork(error); }
        });
    builder.FinalMethod("getHostAddress", "()Ljava/lang/String;",
        [](IntrinsicContext& call) {
            try { return VmValue::Ref(call.vm.NewStringUtf8(
                call.vm.Network().Address(call.receiver).address)); }
            catch (const NetworkRuntimeError& error) { ThrowNetwork(error); }
        });
    return std::move(builder).Build();
}

IntrinsicClassDecl DeclareSocketAddress() {
    return std::move(IntrinsicClassBuilder::Class(
        "Ljava/net/SocketAddress;", "Ljava/lang/Object;")).Build();
}

IntrinsicClassDecl DeclareInetSocketAddress() {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljava/net/InetSocketAddress;", "Ljava/net/SocketAddress;");
    builder.Constructor("(Ljava/lang/String;I)V", [](IntrinsicContext& call) {
        call.vm.Network().SetEndpoint(
            call.receiver,
            HostEndpoint(call, call.arguments[0].ref,
                         call.arguments[1].AsInt()));
        return VmValue::Void();
    });
    builder.Constructor("(Ljava/net/InetAddress;I)V",
        [](IntrinsicContext& call) {
            call.vm.Network().SetEndpoint(
                call.receiver,
                EndpointFrom(call, call.arguments[0].ref,
                             call.arguments[1].AsInt()));
            return VmValue::Void();
        });
    builder.FinalMethod("getPort", "()I", [](IntrinsicContext& call) {
        try { return VmValue::Int(call.vm.Network().GetEndpoint(
            call.receiver).port); }
        catch (const NetworkRuntimeError& error) { ThrowNetwork(error); }
    });
    builder.FinalMethod("getHostName", "()Ljava/lang/String;",
        [](IntrinsicContext& call) {
            try { return VmValue::Ref(call.vm.NewStringUtf8(
                call.vm.Network().GetEndpoint(call.receiver).host)); }
            catch (const NetworkRuntimeError& error) { ThrowNetwork(error); }
        });
    return std::move(builder).Build();
}

IntrinsicClassDecl DeclareSocketInputStream() {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljava/net/SocketInputStream;", "Ljava/io/InputStream;");
    builder.FinalOverrideMethod("read", "([BII)I", [](IntrinsicContext& call) {
        const auto array = call.arguments[0].ref;
        const auto offset = call.arguments[1].AsInt();
        const auto length = call.arguments[2].AsInt();
        detail::CheckRegion(call.vm.Model().ArrayLength(array), offset, length);
        try {
            const auto bytes = call.vm.Network().ReadStream(
                call.receiver, static_cast<std::size_t>(length));
            if (bytes.empty()) return VmValue::Int(-1);
            call.vm.Model().WriteByteRegion(array, offset, bytes);
            return VmValue::Int(static_cast<std::int32_t>(bytes.size()));
        } catch (const NetworkRuntimeError& error) { ThrowNetwork(error); }
    });
    builder.FinalOverrideMethod("read", "()I", [](IntrinsicContext& call) {
        try {
            const auto bytes = call.vm.Network().ReadStream(call.receiver, 1);
            return VmValue::Int(bytes.empty() ? -1 :
                static_cast<std::uint8_t>(bytes.front()));
        } catch (const NetworkRuntimeError& error) { ThrowNetwork(error); }
    });
    return std::move(builder).Build();
}

IntrinsicClassDecl DeclareSocketOutputStream() {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljava/net/SocketOutputStream;", "Ljava/io/OutputStream;");
    builder.FinalOverrideMethod("write", "([BII)V", [](IntrinsicContext& call) {
        const auto array = call.arguments[0].ref;
        const auto offset = call.arguments[1].AsInt();
        const auto length = call.arguments[2].AsInt();
        detail::CheckRegion(call.vm.Model().ArrayLength(array), offset, length);
        try {
            call.vm.Network().WriteStream(call.receiver,
                call.vm.Model().ReadByteRegion(array, offset, length));
            return VmValue::Void();
        } catch (const NetworkRuntimeError& error) { ThrowNetwork(error); }
    });
    builder.FinalOverrideMethod("write", "(I)V", [](IntrinsicContext& call) {
        const auto byte = static_cast<std::byte>(call.arguments[0].AsInt());
        try {
            call.vm.Network().WriteStream(call.receiver,
                                          std::span(&byte, 1));
            return VmValue::Void();
        } catch (const NetworkRuntimeError& error) { ThrowNetwork(error); }
    });
    builder.FinalOverrideMethod("flush", "()V", NoopVoid());
    return std::move(builder).Build();
}

IntrinsicClassDecl DeclareSocket(const bool tls) {
    const auto descriptor = tls ? "Ljavax/net/ssl/SSLSocket;" :
                                  "Ljava/net/Socket;";
    const auto superclass = tls ? "Ljava/net/Socket;" : "Ljava/lang/Object;";
    auto builder = IntrinsicClassBuilder::Class(descriptor, superclass);
    if (tls) {
        // SSLSocket inherits the bounded Socket surface. Instances are created
        // by SSLSocketFactory with the TLS bit already set in NetworkRuntime.
        return std::move(builder).Build();
    }
    builder.Constructor("()V", [tls](IntrinsicContext& call) {
        call.vm.Network().CreateSocket(call.receiver, tls);
        return VmValue::Void();
    });
    builder.Constructor("(Ljava/lang/String;I)V", [tls](IntrinsicContext& call) {
        call.vm.Network().CreateSocket(call.receiver, tls);
        try { call.vm.Network().Connect(call.receiver,
            HostEndpoint(call, call.arguments[0].ref,
                         call.arguments[1].AsInt())); }
        catch (const NetworkRuntimeError& error) { ThrowNetwork(error); }
        return VmValue::Void();
    });
    builder.FinalMethod("connect", "(Ljava/net/SocketAddress;)V",
        [](IntrinsicContext& call) {
            try { call.vm.Network().Connect(call.receiver,
                call.vm.Network().GetEndpoint(call.arguments[0].ref)); }
            catch (const NetworkRuntimeError& error) { ThrowNetwork(error); }
            return VmValue::Void();
        });
    builder.FinalMethod("getInputStream", "()Ljava/io/InputStream;",
        [](IntrinsicContext& call) {
            const auto stream = call.vm.NewIntrinsicInstance(
                "Ljava/net/SocketInputStream;");
            try { call.vm.Network().BindStream(stream, call.receiver, false); }
            catch (const NetworkRuntimeError& error) { ThrowNetwork(error); }
            return VmValue::Ref(stream);
        });
    builder.FinalMethod("getOutputStream", "()Ljava/io/OutputStream;",
        [](IntrinsicContext& call) {
            const auto stream = call.vm.NewIntrinsicInstance(
                "Ljava/net/SocketOutputStream;");
            try { call.vm.Network().BindStream(stream, call.receiver, true); }
            catch (const NetworkRuntimeError& error) { ThrowNetwork(error); }
            return VmValue::Ref(stream);
        });
    builder.FinalMethod("isConnected", "()Z", [](IntrinsicContext& call) {
        try { return VmValue::Int(call.vm.Network().GetSocket(
            call.receiver).connected ? 1 : 0); }
        catch (const NetworkRuntimeError& error) { ThrowNetwork(error); }
    });
    builder.FinalMethod("isClosed", "()Z", [](IntrinsicContext& call) {
        try { return VmValue::Int(call.vm.Network().GetSocket(
            call.receiver).closed ? 1 : 0); }
        catch (const NetworkRuntimeError& error) { ThrowNetwork(error); }
    });
    builder.FinalMethod("close", "()V", [](IntrinsicContext& call) {
        call.vm.Network().CloseSocket(call.receiver);
        return VmValue::Void();
    });
    return std::move(builder).Build();
}

IntrinsicClassDecl DeclareDatagramPacket() {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljava/net/DatagramPacket;", "Ljava/lang/Object;");
    builder.Constructor("([BII)V", [](IntrinsicContext& call) {
        detail::CheckRegion(call.vm.Model().ArrayLength(call.arguments[0].ref),
                            call.arguments[1].AsInt(),
                            call.arguments[2].AsInt());
        call.vm.Network().SetPacket(call.receiver,
            {call.arguments[0].ref, call.arguments[1].AsInt(),
             call.arguments[2].AsInt(), {}});
        return VmValue::Void();
    });
    builder.Constructor("([BIILjava/net/InetAddress;I)V",
        [](IntrinsicContext& call) {
            detail::CheckRegion(call.vm.Model().ArrayLength(call.arguments[0].ref),
                                call.arguments[1].AsInt(),
                                call.arguments[2].AsInt());
            call.vm.Network().SetPacket(call.receiver,
                {call.arguments[0].ref, call.arguments[1].AsInt(),
                 call.arguments[2].AsInt(), EndpointFrom(
                     call, call.arguments[3].ref, call.arguments[4].AsInt())});
            return VmValue::Void();
        });
    builder.FinalMethod("getLength", "()I", [](IntrinsicContext& call) {
        try { return VmValue::Int(call.vm.Network().Packet(call.receiver).length); }
        catch (const NetworkRuntimeError& error) { ThrowNetwork(error); }
    });
    builder.FinalMethod("getPort", "()I", [](IntrinsicContext& call) {
        try { return VmValue::Int(call.vm.Network().Packet(
            call.receiver).endpoint.port); }
        catch (const NetworkRuntimeError& error) { ThrowNetwork(error); }
    });
    return std::move(builder).Build();
}

IntrinsicClassDecl DeclareDatagramSocket() {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljava/net/DatagramSocket;", "Ljava/lang/Object;");
    builder.Constructor("()V", NoopVoid());
    builder.FinalMethod("send", "(Ljava/net/DatagramPacket;)V",
        [](IntrinsicContext& call) {
            try {
                const auto& packet = call.vm.Network().Packet(call.arguments[0].ref);
                call.vm.Network().SendPacket(call.arguments[0].ref,
                    call.vm.Model().ReadByteRegion(packet.array, packet.offset,
                                                   packet.length));
                return VmValue::Void();
            } catch (const NetworkRuntimeError& error) { ThrowNetwork(error); }
        });
    builder.FinalMethod("receive", "(Ljava/net/DatagramPacket;)V",
        [](IntrinsicContext& call) {
            try {
                auto& packet = call.vm.Network().Packet(call.arguments[0].ref);
                const auto datagram = call.vm.Network().ReceivePacket(packet.length);
                const auto amount = std::min<std::size_t>(
                    datagram.payload.size(), packet.length);
                call.vm.Model().WriteByteRegion(packet.array, packet.offset,
                    std::span(datagram.payload).first(amount));
                packet.length = static_cast<std::int32_t>(amount);
                packet.endpoint = {datagram.host, {}, datagram.port};
                return VmValue::Void();
            } catch (const NetworkRuntimeError& error) { ThrowNetwork(error); }
        });
    builder.FinalMethod("close", "()V", NoopVoid());
    return std::move(builder).Build();
}

IntrinsicClassDecl DeclareSocketFactory(const CoreIntrinsicServices& services) {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljavax/net/SocketFactory;", "Ljava/lang/Object;");
    builder.StaticMethod("getDefault", "()Ljavax/net/SocketFactory;",
        [services](IntrinsicContext& call) {
            if (services.singleton) {
                return VmValue::Ref(services.singleton(
                    call.vm, "socket_factory", "Ljavax/net/SocketFactory;"));
            }
            return VmValue::Ref(call.vm.NewIntrinsicInstance(
                "Ljavax/net/SocketFactory;"));
        });
    builder.VirtualMethod("createSocket", "()Ljava/net/Socket;",
        [](IntrinsicContext& call) {
            const auto socket = call.vm.NewIntrinsicInstance("Ljava/net/Socket;");
            call.vm.Network().CreateSocket(socket, false);
            return VmValue::Ref(socket);
        });
    builder.VirtualMethod("createSocket",
        "(Ljava/lang/String;I)Ljava/net/Socket;",
        [](IntrinsicContext& call) {
            const auto socket = call.vm.NewIntrinsicInstance("Ljava/net/Socket;");
            call.vm.Network().CreateSocket(socket, false);
            try {
                call.vm.Network().Connect(socket,
                    HostEndpoint(call, call.arguments[0].ref,
                                 call.arguments[1].AsInt()));
            } catch (const NetworkRuntimeError& error) { ThrowNetwork(error); }
            return VmValue::Ref(socket);
        });
    return std::move(builder).Build();
}

}  // namespace

void AppendJavaNetPlatform(std::vector<IntrinsicClassDecl>& catalog,
                           const CoreIntrinsicServices& services) {
    catalog.push_back(DeclareUriSyntaxException());
    catalog.push_back(DeclareUri());
    catalog.push_back(DeclarePlatformHttpURLConnection());
    catalog.push_back(DeclarePlatformUrl());
    catalog.push_back(DeclarePlatformUrlConnection());
    catalog.push_back(DeclarePlatformUrlEncoder());
    catalog.push_back(DeclareHostnameVerifier());
    catalog.push_back(DeclareHttpsURLConnection());
    catalog.push_back(DeclareKeyManager());
    catalog.push_back(DeclareSslContext(services));
    catalog.push_back(DeclareSslSocketFactory(services));
    catalog.push_back(DeclareTrustManager());
    catalog.push_back(DeclareX509TrustManager());
    catalog.push_back(DeclareInetAddress());
    catalog.push_back(DeclareSocketAddress());
    catalog.push_back(DeclareInetSocketAddress());
    catalog.push_back(DeclareSocketInputStream());
    catalog.push_back(DeclareSocketOutputStream());
    catalog.push_back(DeclareSocket(false));
    catalog.push_back(DeclareDatagramPacket());
    catalog.push_back(DeclareDatagramSocket());
    catalog.push_back(DeclareSocketFactory(services));
    catalog.push_back(DeclareSocket(true));
}

}  // namespace ogplay::runtime::dexvm::intrinsics


// ---- migrated from java_net_SocketException.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;

IntrinsicClassDecl Declare_java_net_SocketException() {
    return DeclareSimpleThrowable("Ljava/net/SocketException;", "Ljava/io/IOException;");
}

}  // namespace ogplay::runtime::dexvm::intrinsics


// ---- migrated from java_net_SocketTimeoutException.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;

IntrinsicClassDecl Declare_java_net_SocketTimeoutException() {
    return DeclareSimpleThrowable("Ljava/net/SocketTimeoutException;", "Ljava/io/IOException;");
}

}  // namespace ogplay::runtime::dexvm::intrinsics


// ---- migrated from java_net_UnknownHostException.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;

IntrinsicClassDecl Declare_java_net_UnknownHostException() {
    return DeclareSimpleThrowable("Ljava/net/UnknownHostException;", "Ljava/io/IOException;");
}

}  // namespace ogplay::runtime::dexvm::intrinsics
