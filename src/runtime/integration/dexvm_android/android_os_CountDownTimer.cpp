#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_CountDownTimer(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/os/CountDownTimer;", "Ljava/lang/Object;");
    builder.Constructor("()V", NeutralHandler('V'));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
