#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_javax_net_ssl_HttpsURLConnection(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Ljavax/net/ssl/HttpsURLConnection;");
    builder.Super("Ljava/net/HttpURLConnection;");
    builder.Static("setDefaultHostnameVerifier", "(Ljavax/net/ssl/HostnameVerifier;)V", handlers.handler_android_graphics_noop);
    builder.Static("setDefaultSSLSocketFactory", "(Ljavax/net/ssl/SSLSocketFactory;)V", handlers.handler_android_graphics_noop);
    builder.Virtual("setRequestMethod", "(Ljava/lang/String;)V", handlers.handler_android_net_unsupported);
    builder.Virtual("setRequestProperty", "(Ljava/lang/String;Ljava/lang/String;)V", handlers.handler_android_net_unsupported);
    builder.Virtual("getResponseCode", "()I", handlers.handler_android_net_unsupported);
    builder.Virtual("getInputStream", "()Ljava/io/InputStream;", handlers.handler_android_net_unsupported);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
