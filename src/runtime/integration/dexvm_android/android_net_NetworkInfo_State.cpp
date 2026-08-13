#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_net_NetworkInfo_State(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/net/NetworkInfo$State;");
    builder.Super("Ljava/lang/Object;");
    builder.Field("CONNECTED", "Landroid/net/NetworkInfo$State;", true);
    builder.Clinit(handlers.handler_android_network_state_clinit);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
