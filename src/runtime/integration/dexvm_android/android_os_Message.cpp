#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_Message(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/os/Message;");
    builder.Super("Ljava/lang/Object;");
    builder.Field("what", "I", false);
    builder.Field("arg1", "I", false);
    builder.Field("arg2", "I", false);
    builder.Field("obj", "Ljava/lang/Object;", false);
    builder.Field("target", "Landroid/os/Handler;", false);
    builder.Static("obtain", "(Landroid/os/Handler;ILjava/lang/Object;)Landroid/os/Message;", handlers.handler_android_message_obtain_static);
    builder.Virtual("sendToTarget", "()V", handlers.handler_android_message_send_to_target);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
