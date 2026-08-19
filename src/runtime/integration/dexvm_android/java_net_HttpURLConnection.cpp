#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_net_HttpURLConnection(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Ljava/net/HttpURLConnection;", "Ljava/net/URLConnection;");
    builder.FinalMethod("connect", "()V", NetUnsupportedHandler());
    builder.FinalMethod("disconnect", "()V", NetUnsupportedHandler());
    builder.FinalMethod("getInputStream", "()Ljava/io/InputStream;", NetUnsupportedHandler());
    builder.FinalMethod("setConnectTimeout", "(I)V", NetUnsupportedHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
