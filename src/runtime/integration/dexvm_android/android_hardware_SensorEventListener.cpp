#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_hardware_SensorEventListener(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/hardware/SensorEventListener;");
}

}  // namespace ogplay::runtime::android_intrinsics
