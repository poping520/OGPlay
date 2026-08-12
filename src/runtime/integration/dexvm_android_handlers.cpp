// android.* intrinsic assembly: helpers shared by the handler batches
// plus the registration entry point. Batches live in the sibling
// dexvm_android_*.cpp files, one per platform area.

#include <cstring>

#include "dexvm_android_internal.h"

namespace ogplay::runtime {

namespace dx = dexvm;

namespace android_intrinsics {

[[nodiscard]] dx::VmValue Self(dx::IntrinsicContext& call) {
    return dx::VmValue::Ref(call.receiver);
}

[[nodiscard]] dx::VmObjectRef Singleton(dx::IntrinsicContext& call,
                                        const Context& context,
                                        const std::string& key,
                                        const char* descriptor) {
    const auto found = context->singletons.find(key);
    if (found != context->singletons.end()) return found->second;
    const auto instance = call.vm.NewIntrinsicInstance(descriptor);
    context->singletons.emplace(key, instance);
    return instance;
}

[[nodiscard]] dx::VmValue MakeString(dx::IntrinsicContext& call,
                                     const std::string& value) {
    return dx::VmValue::Ref(call.vm.NewStringUtf8(value));
}

void GuestLog(dx::IntrinsicContext& call, const core::LogLevel level,
              const std::string& line) {
    auto* logger = call.vm.Log();
    if (logger == nullptr) return;
    logger->Write(level, "runtime.dexvm.guest", line);
}

[[nodiscard]] DexVmAndroidContext::Stream& StreamOf(
    dx::IntrinsicContext& call, const Context& context) {
    const auto found = context->streams.find(call.receiver.Value());
    if (found == context->streams.end() || found->second.closed) {
        throw dx::VmJavaThrow{"Ljava/io/IOException;",
                              "stream is closed or was never opened"};
    }
    return found->second;
}

dx::VmObjectRef OpenStream(dx::IntrinsicContext& call, const Context& context,
                           std::vector<std::byte> bytes,
                           const char* descriptor) {
    const auto instance = call.vm.NewIntrinsicInstance(descriptor);
    context->streams[instance.Value()] =
        DexVmAndroidContext::Stream{std::move(bytes), 0, false};
    return instance;
}

[[nodiscard]] std::vector<std::byte> ReadApkFile(const Context& context,
                                                 const std::string& path) {
    try {
        return loader::ReadApkEntry(context->apk_bytes, context->archive,
                                    path);
    } catch (const std::exception& error) {
        // Missing/damaged entries surface as the Java IOException the
        // interpreted glue actually catches, not a host-side abort.
        throw dx::VmJavaThrow{"Ljava/io/IOException;",
                              "APK entry is unavailable: " + path + " (" +
                                  error.what() + ")"};
    }
}

// Interprets the target's run() to completion on the calling host thread.
// Returns a rendered message when the body raised an uncaught exception.
[[nodiscard]] std::optional<std::string> RunJavaThreadNow(
    dx::Interpreter& vm, DexVmAndroidContext& context,
    const dx::VmObjectRef thread) {
    const auto found = context.java_threads.find(thread.Value());
    if (found == context.java_threads.end() || found->second.finished ||
        !found->second.started) {
        return std::nullopt;  // join on new/dead thread returns immediately
    }
    found->second.finished = true;
    const auto runnable = found->second.runnable.IsValid()
                              ? found->second.runnable
                              : thread;
    auto& linker = vm.Linker();
    const auto runnable_class = vm.Model().ObjectClass(runnable);
    const auto index = linker.FindVtableIndex(runnable_class, "run", "()V");
    if (!index.has_value()) {
        return "thread target has no run() method: " +
               linker.Class(runnable_class).descriptor;
    }
    const auto outcome =
        vm.Call(linker.Class(runnable_class).vtable[*index],
                std::vector<dx::VmValue>{dx::VmValue::Ref(runnable)});
    if (outcome.exception.IsValid()) {
        std::string rendered = "uncaught exception on Java thread: " +
                               outcome.exception_message;
        for (const auto& entry : outcome.exception_stack) {
            rendered += "\n  at " + entry.class_descriptor + "." +
                        entry.method_name + " (pc " +
                        std::to_string(entry.pc) + ")";
        }
        return rendered;
    }
    return std::nullopt;
}

}  // namespace android_intrinsics

void RegisterAndroidBuiltins(dx::IntrinsicRegistry& registry,
                             const std::shared_ptr<DexVmAndroidContext>
                                 context) {
    using namespace android_intrinsics;  // NOLINT: batch entry points
    RegisterContextActivity(registry, context);
    RegisterViewSurface(registry, context);
    RegisterResources(registry, context);
    RegisterStreams(registry, context);
    RegisterFiles(registry, context);
    RegisterDeviceServices(registry, context);
    RegisterAudioVideo(registry, context);
    RegisterSharedPreferences(registry, context);
    RegisterGraphicsBitmaps(registry, context);
    RegisterWidgets(registry, context);
    RegisterVideoViews(registry, context);
    RegisterMisc(registry, context);
}

std::optional<std::string> PumpJavaThreads(dx::Interpreter& vm,
                                           DexVmAndroidContext& context) {
    // run() bodies may start further threads; drain until stable.
    while (!context.java_thread_queue.empty()) {
        const auto thread = context.java_thread_queue.front();
        context.java_thread_queue.erase(context.java_thread_queue.begin());
        const auto error =
            android_intrinsics::RunJavaThreadNow(vm, context, thread);
        if (error.has_value()) return error;
    }
    return std::nullopt;
}

dx::VmObjectRef MakeMotionEvent(dx::Interpreter& vm,
                                const std::int32_t action, const float x,
                                const float y, const std::int32_t pointer) {
    const auto instance =
        vm.NewIntrinsicInstance("Landroid/view/MotionEvent;");
    const auto slots = vm.Model().InstanceSlots(instance);
    std::uint32_t x_bits{};
    std::uint32_t y_bits{};
    std::memcpy(&x_bits, &x, sizeof(x_bits));
    std::memcpy(&y_bits, &y, sizeof(y_bits));
    slots[0] = {static_cast<std::uint32_t>(action), dx::SlotTag::cat1};
    slots[1] = {x_bits, dx::SlotTag::cat1};
    slots[2] = {y_bits, dx::SlotTag::cat1};
    slots[3] = {static_cast<std::uint32_t>(pointer), dx::SlotTag::cat1};
    return instance;
}

}  // namespace ogplay::runtime
