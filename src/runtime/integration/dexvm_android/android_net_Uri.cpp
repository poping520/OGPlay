#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_net_Uri(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/net/Uri;");
    builder.Super("Ljava/lang/Object;");
    builder.Static("parse", "(Ljava/lang/String;)Landroid/net/Uri;", handlers.handler_android_uri_parse);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
