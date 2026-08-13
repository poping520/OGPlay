#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_hardware_SensorManager(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/hardware/SensorManager;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("getDefaultSensor", "(I)Landroid/hardware/Sensor;", handlers.handler_android_sensor_manager_get_default);
    builder.Virtual("registerListener", "(Landroid/hardware/SensorEventListener;Landroid/hardware/Sensor;I)Z", handlers.handler_android_sensor_manager_register);
    builder.Virtual("unregisterListener", "(Landroid/hardware/SensorEventListener;)V", handlers.handler_android_sensor_manager_unregister);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
