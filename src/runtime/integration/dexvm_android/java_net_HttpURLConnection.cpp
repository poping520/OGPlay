#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_net_HttpURLConnection(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Ljava/net/HttpURLConnection;");
    builder.Super("Ljava/net/URLConnection;");
    builder.Virtual("connect", "()V", handlers.handler_android_net_unsupported);
    builder.Virtual("disconnect", "()V", handlers.handler_android_net_unsupported);
    builder.Virtual("getInputStream", "()Ljava/io/InputStream;", handlers.handler_android_net_unsupported);
    builder.Virtual("setConnectTimeout", "(I)V", handlers.handler_android_net_unsupported);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
