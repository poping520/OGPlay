#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_hardware_LocalSensorManager(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/hardware/LocalSensorManager;", "Landroid/hardware/SensorManager;");
    builder.StaticMethod("nativeGetSensors", "()[Landroid/hardware/Sensor;",
        [](dx::IntrinsicContext& call) {
            auto& linker = call.vm.Linker();
            return dx::VmValue::Ref(call.vm.Model().NewObjectArray(
                linker.ResolveDescriptor("[Landroid/hardware/Sensor;"),
                linker.ResolveDescriptor("Landroid/hardware/Sensor;"), 0));
        }, dx::kAccPrivate | dx::kAccNative);
    // The process has no sensor devices or successful registrations. False
    // reports that absence; unregister cannot deliver or retain any callback.
    const auto unavailable = [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); };
    constexpr auto flags = dx::kAccProtected | dx::kAccNative;
    builder.OverrideMethod("registerListenerImpl",
        "(Landroid/hardware/SensorEventListener;Landroid/hardware/Sensor;ILandroid/os/Handler;II)Z",
        unavailable, flags);
    builder.OverrideMethod("unregisterListenerImpl",
        "(Landroid/hardware/SensorEventListener;Landroid/hardware/Sensor;)V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); }, flags);
    builder.OverrideMethod("requestTriggerSensorImpl",
        "(Landroid/hardware/TriggerEventListener;Landroid/hardware/Sensor;)Z",
        [](dx::IntrinsicContext& call) {
            if (!call.arguments[1].ref.IsValid())
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "sensor cannot be null"};
            return dx::VmValue::Int(0);
        }, flags);
    builder.OverrideMethod("cancelTriggerSensorImpl",
        "(Landroid/hardware/TriggerEventListener;Landroid/hardware/Sensor;Z)Z", unavailable, flags);
    builder.OverrideMethod("flushImpl", "(Landroid/hardware/SensorEventListener;)Z",
        [](dx::IntrinsicContext& call) {
            if (!call.arguments[0].ref.IsValid())
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "listener cannot be null"};
            return dx::VmValue::Int(0);
        }, flags);
    return std::move(builder).Build();
}

Decl Declare_android_hardware_LegacySensorManager(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/hardware/LegacySensorManager;");
    builder.StaticMethod("nativeGetDisplayRotation", "()I",
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); },
        dx::kAccPrivate | dx::kAccNative);
    return std::move(builder).Build();
}

dx::VmObjectRef SensorManagerForContext(dx::IntrinsicContext& call, const Context& context) {
    const auto found = context->singletons.find("sensor");
    if (found != context->singletons.end()) return found->second;
    auto& vm = call.vm;
    const auto type = vm.Linker().ResolveDescriptor("Landroid/hardware/LocalSensorManager;");
    const auto require = [&](const dx::VmCallOutcome& outcome) {
        if (outcome.exception.IsValid())
            throw dx::VmJavaThrow{vm.Linker().Class(outcome.exception_class).descriptor,
                                  outcome.exception_message, outcome.exception};
    };
    require(vm.EnsureClassInitialized(type));
    const auto instance = vm.NewIntrinsicInstance("Landroid/hardware/LocalSensorManager;");
    const auto roots = vm.ProtectReferences(std::array{instance});
    const auto constructor = vm.Linker().FindDirectMethod(type, "<init>", "()V");
    if (!constructor)
        throw dx::DexVmError(dx::DexVmErrorReason::unresolved_reference,
                             "LocalSensorManager constructor is missing");
    require(vm.Call(*constructor, std::array{dx::VmValue::Ref(instance)}));
    context->singletons.emplace("sensor", instance);
    return instance;
}

} // namespace ogplay::runtime::android_intrinsics
