// BroadcastReceiver is inert: the session never dispatches broadcasts, so
// the base onReceive stays a recorded no-op games may override.

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_BroadcastReceiver(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/content/BroadcastReceiver;", "Ljava/lang/Object;");
    builder.Constructor("()V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.VirtualMethod("onReceive",
        "(Landroid/content/Context;Landroid/content/Intent;)V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
