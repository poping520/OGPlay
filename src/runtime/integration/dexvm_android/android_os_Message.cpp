#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_Message(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/os/Message;");
    builder.Super("Ljava/lang/Object;");
    builder.Field("what", "I", false);
    builder.Field("arg1", "I", false);
    builder.Field("arg2", "I", false);
    builder.Field("obj", "Ljava/lang/Object;", false);
    builder.Field("target", "Landroid/os/Handler;", false);
    builder.Static("obtain", "(Landroid/os/Handler;ILjava/lang/Object;)Landroid/os/Message;",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(MakeMessage(
                call, call.arguments[1].AsInt(), call.arguments[2].ref,
                call.arguments[0].ref));
        });
    builder.Virtual("sendToTarget", "()V", [](dx::IntrinsicContext& call) {
        const auto slots = call.vm.Model().InstanceSlots(call.receiver);
        DeliverMessage(call, dx::VmObjectRef(slots[4].bits), call.receiver);
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
