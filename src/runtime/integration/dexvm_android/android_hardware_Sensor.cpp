#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_hardware_Sensor(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/hardware/Sensor;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("getType", "()I", [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(1);  // TYPE_ACCELEROMETER
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
