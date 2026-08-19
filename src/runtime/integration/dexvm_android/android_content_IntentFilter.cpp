// IntentFilter is a no-op container: games only construct it to register
// receivers the session never dispatches to.

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_IntentFilter(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/content/IntentFilter;", "Ljava/lang/Object;");
    builder.Constructor("(Ljava/lang/String;)V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.Constructor("()V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.FinalMethod("addAction", "(Ljava/lang/String;)V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
