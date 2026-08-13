#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_net_ConnectivityManager(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/net/ConnectivityManager;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("getActiveNetworkInfo", "()Landroid/net/NetworkInfo;",
        [](dx::IntrinsicContext&) {
            // Truthful offline fact: no active network (documented null).
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    builder.Virtual("getNetworkInfo", "(I)Landroid/net/NetworkInfo;",
        [](dx::IntrinsicContext&) {
            // No network of any type is connected on this platform.
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
