#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_Looper(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/os/Looper;");
    builder.Super("Ljava/lang/Object;");
    builder.Static("prepare", "()V", handlers.handler_android_looper_noop);
    builder.Static("loop", "()V", handlers.handler_android_looper_noop);
    builder.Static("getMainLooper", "()Landroid/os/Looper;", handlers.handler_android_looper_get_main_looper);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
