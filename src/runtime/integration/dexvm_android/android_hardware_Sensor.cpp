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
