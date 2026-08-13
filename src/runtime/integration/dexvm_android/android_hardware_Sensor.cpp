#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_hardware_Sensor(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/hardware/Sensor;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("getType", "()I", handlers.handler_android_sensor_get_type);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
