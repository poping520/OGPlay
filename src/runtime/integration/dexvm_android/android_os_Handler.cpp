#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_Handler(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/os/Handler;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("<init>", "()V", handlers.handler_android_handler_init);
    builder.Virtual("<init>", "(Landroid/os/Looper;)V", handlers.handler_android_handler_init);
    builder.Virtual("obtainMessage", "()Landroid/os/Message;", handlers.handler_android_handler_obtain_message);
    builder.Virtual("obtainMessage", "(I)Landroid/os/Message;", handlers.handler_android_handler_obtain_message_what);
    builder.Virtual("obtainMessage", "(ILjava/lang/Object;)Landroid/os/Message;", handlers.handler_android_handler_obtain_message_what_obj);
    builder.Virtual("sendMessage", "(Landroid/os/Message;)Z", handlers.handler_android_handler_send_message);
    builder.Virtual("dispatchMessage", "(Landroid/os/Message;)V", handlers.handler_android_handler_dispatch_message);
    builder.Virtual("post", "(Ljava/lang/Runnable;)Z", handlers.handler_android_handler_post);
    builder.Overridable("handleMessage", "(Landroid/os/Message;)V", handlers.handler_android_handler_handle_message_noop);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
