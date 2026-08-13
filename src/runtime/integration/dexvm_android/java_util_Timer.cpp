#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_util_Timer(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Ljava/util/Timer;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("<init>", "()V", handlers.handler_android_timer_init);
    builder.Virtual("schedule", "(Ljava/util/TimerTask;J)V", handlers.handler_android_timer_schedule);
    builder.Virtual("schedule", "(Ljava/util/TimerTask;JJ)V", handlers.handler_android_timer_schedule_repeating);
    builder.Virtual("cancel", "()V", handlers.handler_android_timer_cancel);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
