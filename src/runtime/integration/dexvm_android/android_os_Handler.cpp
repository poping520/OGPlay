#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_Handler(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/os/Handler;", "Ljava/lang/Object;");
    const auto noop = dx::IntrinsicHandler(
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.Constructor("()V", noop);
    builder.Constructor("(Landroid/os/Looper;)V", noop);
    builder.FinalMethod("obtainMessage", "()Landroid/os/Message;",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(
                call.vm.NewIntrinsicInstance("Landroid/os/Message;"));
        });
    builder.FinalMethod("obtainMessage", "(I)Landroid/os/Message;",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(MakeMessage(
                call, call.arguments[0].AsInt(), dx::VmObjectRef{},
                call.receiver));
        });
    builder.FinalMethod("obtainMessage", "(ILjava/lang/Object;)Landroid/os/Message;",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(MakeMessage(
                call, call.arguments[0].AsInt(), call.arguments[1].ref,
                call.receiver));
        });
    builder.FinalMethod("sendMessage", "(Landroid/os/Message;)Z",
        [](dx::IntrinsicContext& call) {
            DeliverMessage(call, call.receiver, call.arguments[0].ref);
            return dx::VmValue::Int(1);
        });
    builder.FinalMethod("dispatchMessage", "(Landroid/os/Message;)V",
        [](dx::IntrinsicContext& call) {
            DeliverMessage(call, call.receiver, call.arguments[0].ref);
            return dx::VmValue::Void();
        });
    builder.FinalMethod("post", "(Ljava/lang/Runnable;)Z",
        [](dx::IntrinsicContext& call) {
            auto& vm = call.vm;
            auto& linker = vm.Linker();
            const auto runnable = call.arguments[0].ref;
            if (!runnable.IsValid()) {
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                      "posted Runnable is null"};
            }
            const auto runnable_class = vm.Model().ObjectClass(runnable);
            const auto index =
                linker.FindVtableIndex(runnable_class, "run", "()V");
            if (!index.has_value()) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                      "posted object has no run()"};
            }
            const auto outcome = vm.Call(
                linker.Class(runnable_class).vtable[*index],
                std::vector<dx::VmValue>{dx::VmValue::Ref(runnable)});
            if (outcome.exception.IsValid()) {
                throw dx::VmJavaThrow{
                    "Ljava/lang/RuntimeException;",
                    "posted run() raised: " + outcome.exception_message};
            }
            return dx::VmValue::Int(1);
        });
    builder.VirtualMethod("handleMessage", "(Landroid/os/Message;)V", noop);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
