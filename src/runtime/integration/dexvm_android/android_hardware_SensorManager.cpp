#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_hardware_SensorManager(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/hardware/SensorManager;", "Ljava/lang/Object;");
    builder.FinalMethod("getDefaultSensor", "(I)Landroid/hardware/Sensor;",
        [](dx::IntrinsicContext&) {
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    builder.FinalMethod("registerListener", "(Landroid/hardware/SensorEventListener;Landroid/hardware/Sensor;I)Z",
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });
    builder.FinalMethod("unregisterListener", "(Landroid/hardware/SensorEventListener;)V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
