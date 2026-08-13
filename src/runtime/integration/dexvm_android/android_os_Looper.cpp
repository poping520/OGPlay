#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_Looper(const Context& context) {
    dx::IntrinsicClassBuilder builder("Landroid/os/Looper;");
    builder.Super("Ljava/lang/Object;");
    const auto noop = dx::IntrinsicHandler(
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.Static("prepare", "()V", noop);
    builder.Static("loop", "()V", noop);
    builder.Static("getMainLooper", "()Landroid/os/Looper;", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(Singleton(call, context, "main_looper", "Landroid/os/Looper;"));
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
