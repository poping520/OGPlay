#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_Looper(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/os/Looper;");
}

}  // namespace ogplay::runtime::android_intrinsics
