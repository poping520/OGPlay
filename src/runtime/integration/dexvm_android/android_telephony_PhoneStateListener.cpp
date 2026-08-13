#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_telephony_PhoneStateListener(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/telephony/PhoneStateListener;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("<init>", "()V", NeutralHandler('V'));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
