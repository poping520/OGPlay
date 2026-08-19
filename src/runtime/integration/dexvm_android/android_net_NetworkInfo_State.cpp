#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_net_NetworkInfo_State(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/net/NetworkInfo$State;", "Ljava/lang/Object;");
    builder.StaticField("CONNECTED", "Landroid/net/NetworkInfo$State;");
    builder.ClassInitializer([](dx::IntrinsicContext& call) {
        call.vm.SetIntrinsicStaticRef(
            "Landroid/net/NetworkInfo$State;", "CONNECTED",
            "Landroid/net/NetworkInfo$State;",
            call.vm.NewIntrinsicInstance(
                "Landroid/net/NetworkInfo$State;"));
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
