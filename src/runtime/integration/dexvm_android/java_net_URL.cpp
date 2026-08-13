#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_net_URL(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Ljava/net/URL;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("<init>", "(Ljava/lang/String;)V", NetUnsupportedHandler());
    builder.Virtual("openConnection", "()Ljava/net/URLConnection;", NetUnsupportedHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
