#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_hardware_SystemSensorManager(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/hardware/SystemSensorManager;");
    builder.MergesBootClass();
    constexpr auto flags = dx::kAccPrivate | dx::kAccStatic | dx::kAccNative;
    builder.StaticMethod("nativeClassInit", "()V", [](dx::IntrinsicContext&) { return dx::VmValue::Void(); }, flags);
    builder.StaticMethod(
        "nativeGetNextSensor", "(Landroid/hardware/Sensor;I)I",
        [context](dx::IntrinsicContext& call) {
            if (!context->sensor_directory || context->sensor_directory->Count() != 0) {
                if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.sensor_absent_facade", 0);
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "nonempty sensor directory"};
            }
            return dx::VmValue::Int(-1);  // Enumeration exhausted without publishing a device.
        },
        flags);
    return std::move(builder).Build();
}

dx::VmObjectRef SensorManagerForContext(dx::IntrinsicContext& call, const Context& context) {
    constexpr auto key = "sensor";
    if (const auto found = context->singletons.find(key); found != context->singletons.end()) return found->second;
    const auto looper = CallAndroidMethod(call.vm, call.receiver, "getMainLooper", "()Landroid/os/Looper;").ref;
    const auto instance = NewPlatformObject(call.vm, "Landroid/hardware/SystemSensorManager;",
                                            "(Landroid/content/Context;Landroid/os/Looper;)V",
                                            {dx::VmValue::Ref(call.receiver), dx::VmValue::Ref(looper)});
    return context->singletons.try_emplace(key, instance).first->second;
}

}  // namespace ogplay::runtime::android_intrinsics
