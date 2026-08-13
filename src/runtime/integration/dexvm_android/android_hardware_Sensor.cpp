#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_hardware_Sensor(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/hardware/Sensor;");
}

}  // namespace ogplay::runtime::android_intrinsics
