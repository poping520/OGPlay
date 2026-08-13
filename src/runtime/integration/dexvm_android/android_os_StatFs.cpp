#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_StatFs(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/os/StatFs;");
}

}  // namespace ogplay::runtime::android_intrinsics
