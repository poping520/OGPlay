#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_javax_net_ssl_SSLContext(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Ljavax/net/ssl/SSLContext;", "Ljava/lang/Object;");
    builder.StaticMethod("getInstance", "(Ljava/lang/String;)Ljavax/net/ssl/SSLContext;",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(
                call.vm.NewIntrinsicInstance("Ljavax/net/ssl/SSLContext;"));
        });
    builder.FinalMethod("init", "([Ljavax/net/ssl/KeyManager;[Ljavax/net/ssl/TrustManager;Ljava/security/SecureRandom;)V", GraphicsNoopHandler());
    builder.FinalMethod("getSocketFactory", "()Ljavax/net/ssl/SSLSocketFactory;",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(
                Singleton(call, context, "ssl_socket_factory",
                          "Ljavax/net/ssl/SSLSocketFactory;"));
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
