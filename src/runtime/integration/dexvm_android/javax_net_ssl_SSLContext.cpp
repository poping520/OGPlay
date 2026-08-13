#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_javax_net_ssl_SSLContext(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Ljavax/net/ssl/SSLContext;");
    builder.Super("Ljava/lang/Object;");
    builder.Static("getInstance", "(Ljava/lang/String;)Ljavax/net/ssl/SSLContext;", handlers.handler_android_ssl_context_instance);
    builder.Virtual("init", "([Ljavax/net/ssl/KeyManager;[Ljavax/net/ssl/TrustManager;Ljava/security/SecureRandom;)V", handlers.handler_android_graphics_noop);
    builder.Virtual("getSocketFactory", "()Ljavax/net/ssl/SSLSocketFactory;", handlers.handler_android_ssl_socket_factory);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
