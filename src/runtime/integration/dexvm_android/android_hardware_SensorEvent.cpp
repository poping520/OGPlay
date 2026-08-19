#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_hardware_SensorEvent(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/hardware/SensorEvent;", "Ljava/lang/Object;");
    builder.InstanceField("values", "[F");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
