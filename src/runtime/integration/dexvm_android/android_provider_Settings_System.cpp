#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_provider_Settings_System(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/provider/Settings$System;");
}

}  // namespace ogplay::runtime::android_intrinsics
