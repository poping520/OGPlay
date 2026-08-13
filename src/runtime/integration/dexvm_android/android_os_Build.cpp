#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_Build(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/os/Build;");
}

}  // namespace ogplay::runtime::android_intrinsics
