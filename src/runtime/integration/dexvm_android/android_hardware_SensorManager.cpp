#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_hardware_SensorManager(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/hardware/SensorManager;");
}

}  // namespace ogplay::runtime::android_intrinsics
