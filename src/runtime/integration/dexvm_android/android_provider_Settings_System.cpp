#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_provider_Settings_System(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/provider/Settings$System;");
    builder.Super("Ljava/lang/Object;");
    builder.Static("getInt", "(Landroid/content/ContentResolver;Ljava/lang/String;I)I", handlers.handler_android_settings_get_int);
    builder.Static("putInt", "(Landroid/content/ContentResolver;Ljava/lang/String;I)Z", handlers.handler_android_settings_put_int);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
