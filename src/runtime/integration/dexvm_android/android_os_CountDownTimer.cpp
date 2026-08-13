#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_CountDownTimer(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/os/CountDownTimer;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("<init>", "()V", NeutralHandler('V'));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
