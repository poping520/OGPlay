#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_util_Timer(const Context& context) {
    dx::IntrinsicClassBuilder builder("Ljava/util/Timer;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("<init>", "()V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.Virtual("schedule", "(Ljava/util/TimerTask;J)V",
        [context](dx::IntrinsicContext& call) {
            // One-shot tasks run at the next lifecycle frame boundary.
            const auto task = call.arguments[0].ref;
            if (!task.IsValid()) {
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                      "scheduled TimerTask is null"};
            }
            auto& state = context->java_threads[task.Value()];
            state = DexVmAndroidContext::JavaThreadState{};
            state.runnable = task;
            state.started = true;
            state.name = "TimerTask-" + std::to_string(task.Value());
            context->java_thread_queue.push_back(task);
            return dx::VmValue::Void();
        });
    builder.Virtual("schedule", "(Ljava/util/TimerTask;JJ)V",
        [](dx::IntrinsicContext&) -> dx::VmValue {
            throw dx::VmJavaThrow{
                "Ljava/lang/UnsupportedOperationException;",
                "repeating Timer.schedule is not provided"};
        });
    builder.Virtual("cancel", "()V", [context](dx::IntrinsicContext&) {
        context->java_thread_queue.clear();
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
