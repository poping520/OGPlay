#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_net_Uri(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/net/Uri;");
    builder.Super("Ljava/lang/Object;");
    builder.Static("parse", "(Ljava/lang/String;)Landroid/net/Uri;",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(
                call.vm.NewIntrinsicInstance("Landroid/net/Uri;"));
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
