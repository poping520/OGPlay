#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_app_PendingIntent(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/app/PendingIntent;");
    builder.Super("Ljava/lang/Object;");
    builder.Static("getBroadcast", "(Landroid/content/Context;ILandroid/content/Intent;I)Landroid/app/PendingIntent;", handlers.handler_android_pending_intent_get_broadcast);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
