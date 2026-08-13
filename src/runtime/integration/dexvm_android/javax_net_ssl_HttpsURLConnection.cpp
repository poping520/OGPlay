#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_javax_net_ssl_HttpsURLConnection(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Ljavax/net/ssl/HttpsURLConnection;");
    builder.Super("Ljava/net/HttpURLConnection;");
    builder.Static("setDefaultHostnameVerifier", "(Ljavax/net/ssl/HostnameVerifier;)V", GraphicsNoopHandler());
    builder.Static("setDefaultSSLSocketFactory", "(Ljavax/net/ssl/SSLSocketFactory;)V", GraphicsNoopHandler());
    builder.Virtual("setRequestMethod", "(Ljava/lang/String;)V", NetUnsupportedHandler());
    builder.Virtual("setRequestProperty", "(Ljava/lang/String;Ljava/lang/String;)V", NetUnsupportedHandler());
    builder.Virtual("getResponseCode", "()I", NetUnsupportedHandler());
    builder.Virtual("getInputStream", "()Ljava/io/InputStream;", NetUnsupportedHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
