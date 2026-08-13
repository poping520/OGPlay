#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_Handler(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/os/Handler;");
}

}  // namespace ogplay::runtime::android_intrinsics
