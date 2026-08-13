#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_hardware_SensorManager(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/hardware/SensorManager;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("getDefaultSensor", "(I)Landroid/hardware/Sensor;",
        [](dx::IntrinsicContext&) {
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    builder.Virtual("registerListener", "(Landroid/hardware/SensorEventListener;Landroid/hardware/Sensor;I)Z",
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });
    builder.Virtual("unregisterListener", "(Landroid/hardware/SensorEventListener;)V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
