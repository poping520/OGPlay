#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_net_Uri(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/net/Uri;", "Ljava/lang/Object;");
    builder.StaticMethod("parse", "(Ljava/lang/String;)Landroid/net/Uri;",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(
                call.vm.NewIntrinsicInstance("Landroid/net/Uri;"));
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
