#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_net_NetworkInfo_State(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/net/NetworkInfo$State;");
}

}  // namespace ogplay::runtime::android_intrinsics
