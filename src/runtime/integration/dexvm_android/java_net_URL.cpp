#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_net_URL(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Ljava/net/URL;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("<init>", "(Ljava/lang/String;)V", handlers.handler_android_net_unsupported);
    builder.Virtual("openConnection", "()Ljava/net/URLConnection;", handlers.handler_android_net_unsupported);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
