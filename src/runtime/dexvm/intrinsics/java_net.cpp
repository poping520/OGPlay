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
    builder.FinalMethod("createSocket", "()Ljava/net/Socket;",
        [](IntrinsicContext& call) {
            const auto socket = call.vm.NewIntrinsicInstance(
                "Ljavax/net/ssl/SSLSocket;");
            call.vm.Network().CreateSocket(socket, true);
            return VmValue::Ref(socket);
        });
    builder.FinalMethod("createSocket",
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
    builder.FinalMethod("read", "([BII)I", [](IntrinsicContext& call) {
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
    builder.FinalMethod("read", "()I", [](IntrinsicContext& call) {
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
    builder.FinalMethod("write", "([BII)V", [](IntrinsicContext& call) {
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
    builder.FinalMethod("write", "(I)V", [](IntrinsicContext& call) {
        const auto byte = static_cast<std::byte>(call.arguments[0].AsInt());
        try {
            call.vm.Network().WriteStream(call.receiver,
                                          std::span(&byte, 1));
            return VmValue::Void();
        } catch (const NetworkRuntimeError& error) { ThrowNetwork(error); }
    });
    builder.FinalMethod("flush", "()V", NoopVoid());
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
