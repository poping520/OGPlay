#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_util_Timer(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Ljava/util/Timer;", "Ljava/lang/Object;");
    builder.Constructor("()V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.FinalMethod("schedule", "(Ljava/util/TimerTask;J)V",
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
    builder.FinalMethod("schedule", "(Ljava/util/TimerTask;JJ)V",
        [](dx::IntrinsicContext&) -> dx::VmValue {
            throw dx::VmJavaThrow{
                "Ljava/lang/UnsupportedOperationException;",
                "repeating Timer.schedule is not provided"};
        });
    builder.FinalMethod("cancel", "()V", [context](dx::IntrinsicContext&) {
        context->java_thread_queue.clear();
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
