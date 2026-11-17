#include "catalog.h"

#include <bit>

namespace ogplay::runtime::android_intrinsics {
namespace {
constexpr auto kStaticNative = dx::kAccPrivate | dx::kAccStatic | dx::kAccNative;

dx::Slot& Field(dx::Interpreter& vm, dx::VmObjectRef object, const char* name, const char* descriptor) {
    const auto field = vm.Linker().FindFieldRecursive(vm.Model().ObjectClass(object), name, descriptor);
    if (!field) throw std::logic_error(std::string("sensor requires framework field ") + name);
    return vm.Model().InstanceSlots(object)[vm.Linker().Field(*field).slot];
}
}  // namespace

Decl Declare_android_hardware_SystemSensorManager_Natives(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/hardware/SystemSensorManager;");
    builder.MergesBootClass();
    builder.StaticMethod(
        "nativeClassInit", "()V", [](dx::IntrinsicContext&) { return dx::VmValue::Void(); }, kStaticNative);
    builder.StaticMethod(
        "nativeGetNextSensor", "(Landroid/hardware/Sensor;I)I",
        [context](dx::IntrinsicContext& call) {
            const auto sensors = context->sensor_directory->Sensors();
            const auto index = static_cast<std::size_t>(call.arguments[1].AsInt());
            if (index >= sensors.size()) return dx::VmValue::Int(-1);
            const auto object = dx::IntrinsicCall(call).NonNullRef(0, "sensor");
            const auto& sensor = sensors[index];
            auto& vm = call.vm;
            const auto name = vm.NewStringUtf8(sensor.name);
            const auto name_root = vm.ProtectReferences(std::array{name});
            const auto vendor = vm.NewStringUtf8(sensor.vendor);
            Field(vm, object, "mName", "Ljava/lang/String;") = {name.Value(), dx::SlotTag::ref};
            Field(vm, object, "mVendor", "Ljava/lang/String;") = {vendor.Value(), dx::SlotTag::ref};
            Field(vm, object, "mVersion", "I") = {static_cast<std::uint32_t>(sensor.version), dx::SlotTag::cat1};
            Field(vm, object, "mHandle", "I") = {static_cast<std::uint32_t>(sensor.handle), dx::SlotTag::cat1};
            Field(vm, object, "mType", "I") = {static_cast<std::uint32_t>(sensor.type), dx::SlotTag::cat1};
            Field(vm, object, "mMaxRange", "F") = {std::bit_cast<std::uint32_t>(sensor.max_range), dx::SlotTag::cat1};
            Field(vm, object, "mResolution", "F") = {std::bit_cast<std::uint32_t>(sensor.resolution),
                                                     dx::SlotTag::cat1};
            Field(vm, object, "mPower", "F") = {std::bit_cast<std::uint32_t>(sensor.power), dx::SlotTag::cat1};
            Field(vm, object, "mMinDelay", "I") = {static_cast<std::uint32_t>(sensor.min_delay_us), dx::SlotTag::cat1};
            Field(vm, object, "mFifoReservedEventCount", "I") = {static_cast<std::uint32_t>(sensor.fifo_reserved_count),
                                                                 dx::SlotTag::cat1};
            Field(vm, object, "mFifoMaxEventCount", "I") = {static_cast<std::uint32_t>(sensor.fifo_max_count),
                                                            dx::SlotTag::cat1};
            return dx::VmValue::Int(index + 1 < sensors.size() ? static_cast<std::int32_t>(index + 1) : 0);
        },
        kStaticNative);
    return std::move(builder).Build();
}

Decl Declare_android_hardware_BaseEventQueue_Natives(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/hardware/SystemSensorManager$BaseEventQueue;");
    builder.MergesBootClass();
    builder.DirectMethod(
        "nativeInitBaseEventQueue",
        "(Landroid/hardware/SystemSensorManager$BaseEventQueue;Landroid/os/MessageQueue;[F)I",
        [context](dx::IntrinsicContext& call) {
            const auto message_queue = dx::IntrinsicCall(call).NonNullRef(1, "messageQueue");
            const auto pointer = Field(call.vm, message_queue, "mPtr", "I").bits;
            std::uint64_t thread{};
            {
                std::scoped_lock lock(context->scheduler_mutex);
                const auto queue = context->message_queues.find(pointer);
                if (queue != context->message_queues.end() && !queue->second->destroyed)
                    thread = queue->second->context_token;
            }
            if (!thread || !context->sensor_queues || !context->native_thread_for_context)
                throw dx::VmJavaThrow{"Ljava/lang/RuntimeException;", "MessageQueue is not initialized"};
            const auto readable = std::make_shared<std::atomic<bool>>();
            const auto id = context->sensor_queues->OpenSensorQueue(context->native_thread_for_context(thread),
                                                                    [readable] { readable->store(true); });
            try {
                context->java_sensor_queues.emplace(id, DexVmAndroidContext::JavaSensorQueue{call.arguments[0].ref,
                                                                                             call.arguments[2].ref,
                                                                                             thread, readable});
            } catch (...) {
                context->sensor_queues->CloseSensorQueue(id);
                throw;
            }
            return dx::VmValue::Int(static_cast<std::int32_t>(id));
        },
        dx::kAccPrivate | dx::kAccNative);
    builder.StaticMethod(
        "nativeEnableSensor", "(IIIII)I",
        [context](dx::IntrinsicContext& call) {
            // FIFO depth is zero. ROM registerListenerImpl retries without batching.
            const auto success = context->sensor_queues && !call.arguments[3].AsInt() &&
                                 context->sensor_queues->EnableSensor(call.arguments[0].cat1, call.arguments[1].AsInt(),
                                                                      call.arguments[2].AsInt());
            return dx::VmValue::Int(success ? 0 : -22);
        },
        kStaticNative);
    builder.StaticMethod(
        "nativeDisableSensor", "(II)I",
        [context](dx::IntrinsicContext& call) {
            const auto success = context->sensor_queues && context->sensor_queues->DisableSensor(
                                                               call.arguments[0].cat1, call.arguments[1].AsInt());
            return dx::VmValue::Int(success ? 0 : -22);
        },
        kStaticNative);
    builder.StaticMethod(
        "nativeDestroySensorEventQueue", "(I)V",
        [context](dx::IntrinsicContext& call) {
            const auto id = call.arguments[0].cat1;
            if (context->sensor_queues) context->sensor_queues->CloseSensorQueue(id);
            context->java_sensor_queues.erase(id);
            return dx::VmValue::Void();
        },
        kStaticNative);
    builder.StaticMethod(
        "nativeFlushSensor", "(I)I",
        [context](dx::IntrinsicContext& call) {
            const auto success = context->sensor_queues && context->sensor_queues->FlushSensors(call.arguments[0].cat1);
            return dx::VmValue::Int(success ? 0 : -22);
        },
        kStaticNative);
    return std::move(builder).Build();
}

void PumpJavaSensorEvents(dx::Interpreter& vm, const Context& context) {
    if (!context->sensor_queues) return;
    std::vector<std::uint32_t> ready;
    for (const auto& [id, queue] : context->java_sensor_queues)
        if (queue.thread == vm.CurrentContextToken() && queue.readable->exchange(false)) ready.push_back(id);
    for (const auto id : ready) {
        const auto found = context->java_sensor_queues.find(id);
        if (found == context->java_sensor_queues.end()) continue;
        const auto queue = found->second;
        const auto roots = vm.ProtectReferences(std::array{queue.receiver, queue.scratch});
        std::array<AndroidSensorSample, 256> events;
        const auto count = context->sensor_queues->ReadSensors(id, events);
        for (const auto& event : std::span(events).first(count)) {
            if (!context->java_sensor_queues.contains(id)) break;
            if (event.type == 0) {
                static_cast<void>(CallAndroidMethod(vm, queue.receiver, "dispatchFlushCompleteEvent", "(I)V",
                                                    {dx::VmValue::Int(std::bit_cast<std::int32_t>(event.values[1]))}));
                continue;
            }
            for (std::size_t i = 0; i < event.values.size(); ++i)
                vm.Model().SetPrimitiveElement(queue.scratch, static_cast<JniSize>(i),
                                               std::bit_cast<std::uint32_t>(event.values[i]));
            const auto accuracy = std::bit_cast<std::uint32_t>(event.values[3]) & 0xff;
            static_cast<void>(CallAndroidMethod(vm, queue.receiver, "dispatchSensorEvent", "(I[FIJ)V",
                                                {dx::VmValue::Int(event.sensor), dx::VmValue::Ref(queue.scratch),
                                                 dx::VmValue::Int(static_cast<std::int32_t>(accuracy)),
                                                 dx::VmValue::Long(event.timestamp)}));
        }
    }
}
}  // namespace ogplay::runtime::android_intrinsics
