#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_net_URLEncoder(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Ljava/net/URLEncoder;");
    builder.Super("Ljava/lang/Object;");
    builder.Static("encode", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", handlers.handler_android_url_encoder_encode);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
