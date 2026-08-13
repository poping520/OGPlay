#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_nio_charset_Charset(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Ljava/nio/charset/Charset;");
    builder.Super("Ljava/lang/Object;");
    builder.Static("forName", "(Ljava/lang/String;)Ljava/nio/charset/Charset;", handlers.handler_android_charset_for_name);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
