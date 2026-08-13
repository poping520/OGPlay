#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_hardware_SensorEvent(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/hardware/SensorEvent;");
}

}  // namespace ogplay::runtime::android_intrinsics
