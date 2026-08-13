#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_Build_VERSION(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/os/Build$VERSION;");
}

}  // namespace ogplay::runtime::android_intrinsics
