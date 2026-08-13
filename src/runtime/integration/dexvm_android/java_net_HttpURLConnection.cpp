#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_net_HttpURLConnection(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Ljava/net/HttpURLConnection;");
    builder.Super("Ljava/net/URLConnection;");
    builder.Virtual("connect", "()V", NetUnsupportedHandler());
    builder.Virtual("disconnect", "()V", NetUnsupportedHandler());
    builder.Virtual("getInputStream", "()Ljava/io/InputStream;", NetUnsupportedHandler());
    builder.Virtual("setConnectTimeout", "(I)V", NetUnsupportedHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
