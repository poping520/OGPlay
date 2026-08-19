#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_telephony_PhoneStateListener(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/telephony/PhoneStateListener;", "Ljava/lang/Object;");
    builder.Constructor("()V", NeutralHandler('V'));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
