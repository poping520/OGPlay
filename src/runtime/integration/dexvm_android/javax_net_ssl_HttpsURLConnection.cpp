#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_javax_net_ssl_HttpsURLConnection(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Ljavax/net/ssl/HttpsURLConnection;", "Ljava/net/HttpURLConnection;");
    builder.StaticMethod("setDefaultHostnameVerifier", "(Ljavax/net/ssl/HostnameVerifier;)V", GraphicsNoopHandler());
    builder.StaticMethod("setDefaultSSLSocketFactory", "(Ljavax/net/ssl/SSLSocketFactory;)V", GraphicsNoopHandler());
    builder.FinalMethod("setRequestMethod", "(Ljava/lang/String;)V", NetUnsupportedHandler());
    builder.FinalMethod("setRequestProperty", "(Ljava/lang/String;Ljava/lang/String;)V", NetUnsupportedHandler());
    builder.FinalMethod("getResponseCode", "()I", NetUnsupportedHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
