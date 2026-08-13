#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_hardware_SensorEvent(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/hardware/SensorEvent;");
    builder.Super("Ljava/lang/Object;");
    builder.Field("values", "[F", false);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
