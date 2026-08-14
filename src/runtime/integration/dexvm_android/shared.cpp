// android.* intrinsic assembly: helpers shared by the handler batches
// plus the registration entry point. Batches live in the sibling
// dexvm_android_*.cpp files, one per platform area.

#include <algorithm>
#include <cstring>

#include "ogplay/runtime/vfs/vfs.h"

#include "shared.h"

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

dx::IntrinsicHandler NeutralHandler(const char shorty) {
    return [shorty](dx::IntrinsicContext&) {
        switch (shorty) {
            case 'V': return dx::VmValue::Void();
            case 'J': return dx::VmValue::Long(0);
            case 'F': return dx::VmValue::Float(0.0F);
            case 'D': return dx::VmValue::Double(0.0);
            case 'L': return dx::VmValue::Ref(dx::VmObjectRef{});
            case 'Z':
            case 'B':
            case 'S':
            case 'C':
            case 'I': return dx::VmValue::Int(0);
            default:
                throw dx::DexVmError(
                    dx::DexVmErrorReason::internal_invariant,
                    "unsupported neutral android intrinsic shorty");
        }
    };
}

dx::IntrinsicHandler PlaceholderString(std::string value) {
    return [value = std::move(value)](dx::IntrinsicContext& call) {
        return MakeString(call, value);
    };
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

std::string FilePathOf(dx::IntrinsicContext& call,
                       const dx::VmObjectRef file) {
    const auto slots = call.vm.Model().InstanceSlots(file);
    return call.vm.StringUtf8(dx::VmObjectRef(slots[0].bits));
}

DexVmAndroidContext::OutputStream& OutputOf(dx::IntrinsicContext& call,
                                            const Context& context) {
    const auto found = context->output_streams.find(call.receiver.Value());
    if (found == context->output_streams.end()) {
        throw dx::VmJavaThrow{"Ljava/io/IOException;",
                              "output stream was never opened"};
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

[[nodiscard]] std::optional<std::vector<std::byte>> VfsReadAll(
    const Context& context, const std::string& path) {
    if (context->vfs == nullptr) return std::nullopt;
    try {
        const auto info = context->vfs->Stat(path);
        const auto descriptor =
            context->vfs->Open(path, VfsOpenOptions{.read = true});
        std::vector<std::byte> bytes(info.size);
        std::size_t cursor = 0;
        while (cursor < bytes.size()) {
            const auto got = context->vfs->Read(
                descriptor, std::span(bytes).subspan(cursor));
            if (got == 0) break;
            cursor += got;
        }
        context->vfs->Close(descriptor);
        bytes.resize(cursor);
        return bytes;
    } catch (const VfsError&) {
        return std::nullopt;
    }
}

// The File family goes through the shared VFS so a Java save and a native
// fopen see one world, and so the sandbox overlay persists both (ADR-0020).
// A missing VFS is a host assembly defect, not a guest-visible gap.
[[nodiscard]] VirtualFileSystem& RequireVfs(const Context& context) {
    if (context->vfs == nullptr) {
        throw dx::DexVmError(
            dx::DexVmErrorReason::internal_invariant,
            "the android platform context has no guest filesystem");
    }
    return *context->vfs;
}

// close() is a sandbox flush point, so this is where a save reaches disk.
void VfsWriteAll(const Context& context, const std::string& path,
                 const std::span<const std::byte> bytes) {
    auto& vfs = RequireVfs(context);
    try {
        const auto descriptor = vfs.Open(
            path, VfsOpenOptions{.write = true, .create = true,
                                 .truncate = true});
        std::size_t cursor = 0;
        while (cursor < bytes.size()) {
            cursor += vfs.Write(descriptor, bytes.subspan(cursor));
        }
        vfs.Close(descriptor);
    } catch (const VfsError& error) {
        throw dx::VmJavaThrow{"Ljava/io/IOException;",
                              "cannot write " + path + ": " + error.what()};
    }
}

void FlushOutput(dx::IntrinsicContext& call, const Context& context,
                 const std::uint32_t handle) {
    const auto found = context->output_streams.find(handle);
    if (found == context->output_streams.end()) return;
    VfsWriteAll(context, found->second.path, found->second.bytes);
    found->second.closed = true;
    static_cast<void>(call);
}

dx::VmValue UnsupportedNetwork(dx::IntrinsicContext&) {
    throw dx::VmJavaThrow{
        "Ljava/lang/UnsupportedOperationException;",
        "SMS/network actions are outside the compatibility scope"};
}

[[nodiscard]] std::string PreferencesPathOf(const Context& context,
                                            const std::string& name) {
    return PreferencesGuestPath(context->package_name, name);
}

dx::VmThreadRuntime& ThreadRuntime(const Context& context) {
    if (context->threads == nullptr) {
        throw dx::DexVmError(
            dx::DexVmErrorReason::internal_invariant,
            "the android platform context has no DexVM thread runtime");
    }
    return *context->threads;
}

void DeliverMessage(dx::IntrinsicContext& call,
                    const dx::VmObjectRef handler,
                    const dx::VmObjectRef message) {
    auto& vm = call.vm;
    auto& linker = vm.Linker();
    if (!handler.IsValid()) {
        throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                              "message has no delivery target"};
    }
    const auto handler_class = vm.Model().ObjectClass(handler);
    const auto index = linker.FindVtableIndex(
        handler_class, "handleMessage", "(Landroid/os/Message;)V");
    if (!index.has_value()) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                              "handler has no handleMessage"};
    }
    const auto outcome = vm.Call(
        linker.Class(handler_class).vtable[*index],
        std::vector<dx::VmValue>{dx::VmValue::Ref(handler),
                                 dx::VmValue::Ref(message)});
    if (outcome.exception.IsValid()) {
        throw dx::VmJavaThrow{"Ljava/lang/RuntimeException;",
                              "handleMessage raised: " +
                                  outcome.exception_message};
    }
}

dx::VmObjectRef MakeMessage(dx::IntrinsicContext& call,
                            const std::int32_t what,
                            const dx::VmObjectRef object,
                            const dx::VmObjectRef target) {
    const auto message = call.vm.NewIntrinsicInstance("Landroid/os/Message;");
    const auto slots = call.vm.Model().InstanceSlots(message);
    slots[0] = {static_cast<std::uint32_t>(what), dx::SlotTag::cat1};
    slots[3] = {object.Value(), dx::SlotTag::ref};
    slots[4] = {target.Value(), dx::SlotTag::ref};
    return message;
}

DexVmAndroidContext::VideoViewState* VideoStateOf(
    const Context& context, const std::uint64_t handle) {
    const auto found = context->video_views.find(handle);
    return found == context->video_views.end() ? nullptr : &found->second;
}

std::int64_t VideoPositionOf(
    const DexVmAndroidContext::VideoViewState& state,
    const std::int64_t uptime_ms) {
    if (!state.playing) return state.base_position_ms;
    const auto elapsed = std::max<std::int64_t>(
        uptime_ms - state.start_uptime_ms, 0);
    return std::min(state.base_position_ms + elapsed, state.duration_ms);
}

std::optional<std::string> InvokeVideoCompletionListener(
    dx::Interpreter& vm, DexVmAndroidContext& context,
    const std::uint64_t handle) {
    const auto found = context.video_completion.find(handle);
    if (found == context.video_completion.end() ||
        !found->second.IsValid()) {
        return std::nullopt;
    }
    auto& linker = vm.Linker();
    const auto listener_class = vm.Model().ObjectClass(found->second);
    const auto index = linker.FindVtableIndex(
        listener_class, "onCompletion", "(Landroid/media/MediaPlayer;)V");
    if (!index.has_value()) {
        return "completion listener has no onCompletion method";
    }
    const auto player =
        vm.NewIntrinsicInstance("Landroid/media/MediaPlayer;");
    const auto outcome = vm.Call(
        linker.Class(listener_class).vtable[*index],
        std::vector<dx::VmValue>{dx::VmValue::Ref(found->second),
                                 dx::VmValue::Ref(player)});
    if (outcome.exception.IsValid()) {
        return "onCompletion raised: " + outcome.exception_message;
    }
    return std::nullopt;
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

std::optional<std::string> PumpJavaThreads(dx::Interpreter& vm,
                                           DexVmAndroidContext& context) {
    // Cooperative Timer tasks; their bodies may queue further tasks.
    while (!context.java_thread_queue.empty()) {
        const auto thread = context.java_thread_queue.front();
        context.java_thread_queue.erase(context.java_thread_queue.begin());
        const auto error =
            android_intrinsics::RunJavaThreadNow(vm, context, thread);
        if (error.has_value()) return error;
    }
    // A real Java thread that died with an uncaught exception is fatal to
    // the process on device; surface it at the frame boundary rather than
    // at whoever happens to call join().
    if (context.threads != nullptr) return context.threads->TakeFailure();
    return std::nullopt;
}

bool SessionExitRequested(const DexVmAndroidContext& context) {
    if (context.exit_requested.load()) return true;
    const auto finishing = context.finishing_activity.load();
    if (finishing == 0U || finishing != context.activity.Value()) {
        return false;
    }
    // The last activity finishing itself ends the session; a handoff still
    // in flight does not. finish() is published after the startActivity it
    // follows, so observing the former guarantees the latter is visible.
    return !context.activity_switch_pending.load();
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
