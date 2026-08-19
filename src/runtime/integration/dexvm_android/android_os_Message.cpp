#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_Message(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/os/Message;", "Ljava/lang/Object;");
    builder.InstanceField("what", "I");
    builder.InstanceField("arg1", "I");
    builder.InstanceField("arg2", "I");
    builder.InstanceField("obj", "Ljava/lang/Object;");
    builder.InstanceField("target", "Landroid/os/Handler;");
    builder.StaticMethod("obtain", "(Landroid/os/Handler;ILjava/lang/Object;)Landroid/os/Message;",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(MakeMessage(
                call, call.arguments[1].AsInt(), call.arguments[2].ref,
                call.arguments[0].ref));
        });
    builder.FinalMethod("sendToTarget", "()V", [](dx::IntrinsicContext& call) {
        const auto slots = call.vm.Model().InstanceSlots(call.receiver);
        DeliverMessage(call, dx::VmObjectRef(slots[4].bits), call.receiver);
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
