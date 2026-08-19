#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_hardware_SensorEventListener(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/hardware/SensorEventListener;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
