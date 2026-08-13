#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_IBinder(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/os/IBinder;");
}

}  // namespace ogplay::runtime::android_intrinsics
