#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_util_Log(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/util/Log;");
    builder.Super("Ljava/lang/Object;");
    builder.Static("d", "(Ljava/lang/String;Ljava/lang/String;)I", handlers.handler_android_log_d);
    builder.Static("e", "(Ljava/lang/String;Ljava/lang/String;)I", handlers.handler_android_log_e);
    builder.Static("i", "(Ljava/lang/String;Ljava/lang/String;)I", handlers.handler_android_log_i);
    builder.Static("w", "(Ljava/lang/String;Ljava/lang/String;)I", handlers.handler_android_log_w);
    builder.Static("v", "(Ljava/lang/String;Ljava/lang/String;)I", handlers.handler_android_log_d);
    builder.Static("e", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/Throwable;)I", handlers.handler_android_log_e);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
