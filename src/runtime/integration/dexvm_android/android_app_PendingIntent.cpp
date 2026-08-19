#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_app_PendingIntent(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/app/PendingIntent;", "Ljava/lang/Object;");
    builder.StaticMethod("getBroadcast",
        "(Landroid/content/Context;ILandroid/content/Intent;I)"
        "Landroid/app/PendingIntent;",
        [](dx::IntrinsicContext&) {
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
