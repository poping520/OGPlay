#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_BroadcastReceiver(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/content/BroadcastReceiver;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("<init>", "()V", handlers.handler_android_receiver_init);
    builder.Overridable("onReceive", "(Landroid/content/Context;Landroid/content/Intent;)V", handlers.handler_android_receiver_on_receive_noop);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
