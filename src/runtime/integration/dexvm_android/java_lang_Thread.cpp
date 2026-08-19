#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_lang_Thread(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Ljava/lang/Thread;", "Ljava/lang/Object;", {"Ljava/lang/Runnable;"});
    builder.StaticMethod("sleep", "(J)V", [context](dx::IntrinsicContext& call) {
        context->uptime_millis += call.arguments[0].AsLong();
        if (context->threads != nullptr) context->threads->Yield();
        return dx::VmValue::Void();
    });
    builder.Constructor("()V", [context](dx::IntrinsicContext& call) {
        auto& state = context->java_threads[call.receiver.Value()];
        state = DexVmAndroidContext::JavaThreadState{};
        state.name = "Thread-" + std::to_string(call.receiver.Value());
        return dx::VmValue::Void();
    });
    builder.Constructor("(Ljava/lang/Runnable;)V",
        [context](dx::IntrinsicContext& call) {
            auto& state = context->java_threads[call.receiver.Value()];
            state = DexVmAndroidContext::JavaThreadState{};
            state.runnable = call.arguments[0].ref;
            state.name = "Thread-" + std::to_string(call.receiver.Value());
            return dx::VmValue::Void();
        });
    builder.FinalMethod("start", "()V", [context](dx::IntrinsicContext& call) {
        const auto found = context->java_threads.find(call.receiver.Value());
        if (found == context->java_threads.end()) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalThreadStateException;",
                                  "thread has no runnable target"};
        }
        if (found->second.started) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalThreadStateException;",
                                  "thread started twice"};
        }
        const auto target = found->second.runnable.IsValid()
                                ? found->second.runnable
                                : call.receiver;
        ThreadRuntime(context).Start(call.receiver, target, found->second.name);
        found->second.started = true;
        return dx::VmValue::Void();
    });
    builder.FinalMethod("join", "()V", [context](dx::IntrinsicContext& call) {
        ThreadRuntime(context).Join(call.receiver);
        const auto found = context->java_threads.find(call.receiver.Value());
        if (found != context->java_threads.end()) found->second.finished = true;
        return dx::VmValue::Void();
    });
    builder.FinalMethod("isAlive", "()Z", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(
            ThreadRuntime(context).IsAlive(call.receiver) ? 1 : 0);
    });
    builder.StaticMethod("currentThread", "()Ljava/lang/Thread;",
        [context](dx::IntrinsicContext& call) {
            auto& runtime = ThreadRuntime(context);
            if (const auto current = runtime.CurrentThreadObject();
                current.IsValid()) {
                return dx::VmValue::Ref(current);
            }
            const auto main = Singleton(
                call, context, "thread.main", "Ljava/lang/Thread;");
            auto& state = context->java_threads[main.Value()];
            if (state.name.empty()) state.name = "main";
            return dx::VmValue::Ref(main);
        });
    builder.FinalMethod("interrupt", "()V", [context](dx::IntrinsicContext& call) {
        ThreadRuntime(context).Interrupt(call.receiver);
        return dx::VmValue::Void();
    });
    builder.FinalMethod("isInterrupted", "()Z", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(
            ThreadRuntime(context).IsInterrupted(call.receiver) ? 1 : 0);
    });
    builder.StaticMethod("interrupted", "()Z", [context](dx::IntrinsicContext&) {
        return dx::VmValue::Int(
            ThreadRuntime(context).ClearCurrentInterrupt() ? 1 : 0);
    });
    builder.StaticMethod("yield", "()V", [context](dx::IntrinsicContext&) {
        ThreadRuntime(context).Yield();
        return dx::VmValue::Void();
    });
    builder.FinalMethod("getId", "()J", [](dx::IntrinsicContext& call) {
        return dx::VmValue::Long(
            static_cast<std::int64_t>(call.receiver.Value()));
    });
    builder.FinalMethod("getName", "()Ljava/lang/String;",
        [context](dx::IntrinsicContext& call) {
            auto& state = context->java_threads[call.receiver.Value()];
            if (state.name.empty()) {
                state.name = "Thread-" + std::to_string(call.receiver.Value());
            }
            return dx::VmValue::Ref(call.vm.NewStringUtf8(state.name));
        });
    builder.FinalMethod("setName", "(Ljava/lang/String;)V",
        [context](dx::IntrinsicContext& call) {
            if (!call.arguments[0].ref.IsValid()) {
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                      "thread name is null"};
            }
            context->java_threads[call.receiver.Value()].name =
                call.vm.StringUtf8(call.arguments[0].ref);
            return dx::VmValue::Void();
        });
    builder.FinalMethod("setPriority", "(I)V",
        [context](dx::IntrinsicContext& call) {
            const auto priority = call.arguments[0].AsInt();
            if (priority < 1 || priority > 10) {
                throw dx::VmJavaThrow{
                    "Ljava/lang/IllegalArgumentException;",
                    "Thread priority must be between 1 and 10"};
            }
            context->java_threads[call.receiver.Value()].priority = priority;
            return dx::VmValue::Void();
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
