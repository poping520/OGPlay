#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_lang_Thread(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Ljava/lang/Thread;");
    builder.Super("Ljava/lang/Object;");
    builder.Implements("Ljava/lang/Runnable;");
    builder.Static("sleep", "(J)V", handlers.handler_android_thread_sleep);
    builder.Virtual("<init>", "()V", handlers.handler_android_thread_init);
    builder.Virtual("<init>", "(Ljava/lang/Runnable;)V", handlers.handler_android_thread_init_runnable);
    builder.Virtual("start", "()V", handlers.handler_android_thread_start);
    builder.Virtual("join", "()V", handlers.handler_android_thread_join);
    builder.Virtual("isAlive", "()Z", handlers.handler_android_thread_is_alive);
    builder.Static("currentThread", "()Ljava/lang/Thread;", handlers.handler_android_thread_current);
    builder.Virtual("interrupt", "()V", handlers.handler_android_thread_interrupt);
    builder.Virtual("isInterrupted", "()Z", handlers.handler_android_thread_is_interrupted);
    builder.Static("interrupted", "()Z", handlers.handler_android_thread_clear_interrupted);
    builder.Static("yield", "()V", handlers.handler_android_thread_yield);
    builder.Virtual("getId", "()J", handlers.handler_android_thread_get_id);
    builder.Virtual("getName", "()Ljava/lang/String;", handlers.handler_android_thread_get_name);
    builder.Virtual("setName", "(Ljava/lang/String;)V", handlers.handler_android_thread_set_name);
    builder.Virtual("setPriority", "(I)V", handlers.handler_android_thread_set_priority);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
