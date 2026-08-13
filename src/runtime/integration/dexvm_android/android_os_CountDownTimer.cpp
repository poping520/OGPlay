#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_CountDownTimer(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/os/CountDownTimer;");
}

}  // namespace ogplay::runtime::android_intrinsics
