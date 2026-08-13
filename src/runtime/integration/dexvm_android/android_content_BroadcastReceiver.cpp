// BroadcastReceiver is inert: the session never dispatches broadcasts, so
// the base onReceive stays a recorded no-op games may override.

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_BroadcastReceiver(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/content/BroadcastReceiver;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("<init>", "()V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.Overridable("onReceive",
        "(Landroid/content/Context;Landroid/content/Intent;)V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
