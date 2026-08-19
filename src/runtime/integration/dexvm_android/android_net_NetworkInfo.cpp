#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_net_NetworkInfo(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/net/NetworkInfo;", "Ljava/lang/Object;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
