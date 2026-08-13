#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_hardware_SensorEventListener(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/hardware/SensorEventListener;");
    builder.MarkInterface();
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
