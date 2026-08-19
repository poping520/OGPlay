#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_Looper(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/os/Looper;", "Ljava/lang/Object;");
    const auto noop = dx::IntrinsicHandler(
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.StaticMethod("prepare", "()V", noop);
    builder.StaticMethod("loop", "()V", noop);
    builder.StaticMethod("getMainLooper", "()Landroid/os/Looper;", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(Singleton(call, context, "main_looper", "Landroid/os/Looper;"));
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
