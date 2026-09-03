// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from android_hardware_Sensor.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_hardware_Sensor(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/hardware/Sensor;", "Ljava/lang/Object;");
    builder.FinalMethod("getType", "()I", [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(1);  // TYPE_ACCELEROMETER
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_hardware_SensorEvent.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_hardware_SensorEvent(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/hardware/SensorEvent;", "Ljava/lang/Object;");
    builder.InstanceField("values", "[F");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_hardware_SensorEventListener.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_hardware_SensorEventListener(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/hardware/SensorEventListener;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_hardware_SensorManager.cpp ----
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
