#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_javax_net_ssl_SSLContext(const Context& context) {
    dx::IntrinsicClassBuilder builder("Ljavax/net/ssl/SSLContext;");
    builder.Super("Ljava/lang/Object;");
    builder.Static("getInstance", "(Ljava/lang/String;)Ljavax/net/ssl/SSLContext;",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(
                call.vm.NewIntrinsicInstance("Ljavax/net/ssl/SSLContext;"));
        });
    builder.Virtual("init", "([Ljavax/net/ssl/KeyManager;[Ljavax/net/ssl/TrustManager;Ljava/security/SecureRandom;)V", GraphicsNoopHandler());
    builder.Virtual("getSocketFactory", "()Ljavax/net/ssl/SSLSocketFactory;",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(
                Singleton(call, context, "ssl_socket_factory",
                          "Ljavax/net/ssl/SSLSocketFactory;"));
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
