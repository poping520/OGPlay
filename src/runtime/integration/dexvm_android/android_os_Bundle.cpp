#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_Bundle(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/os/Bundle;");
}

}  // namespace ogplay::runtime::android_intrinsics
