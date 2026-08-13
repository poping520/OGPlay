#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_Environment(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/os/Environment;");
}

}  // namespace ogplay::runtime::android_intrinsics
