// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from shared_handlers.cpp ----
// Factories for handlers shared by several per-class declaration units
// (and the platform core bindings in dexvm_bridge.cpp).

#include <algorithm>
#include <chrono>
#include <limits>
#include <thread>
#include <tuple>
#include <utility>

#include "shared.h"
#include "ogplay/runtime/integration/native_library_loader.h"

namespace ogplay::runtime::android_intrinsics {

namespace {

[[nodiscard]] ui::UiNodeId OwnerTextNode(const Context& context,
                                         dx::IntrinsicContext& call) {
    const auto found = context->editable_owner.find(call.receiver.Value());
    if (found == context->editable_owner.end()) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                              "Editable has no owning text widget"};
    }
    return EnsureViewUiNode(*context, dx::VmObjectRef(found->second),
                            ui::UiClass::TextView);
}

[[nodiscard]] std::u16string& OwnerBuffer(const Context& context,
                                          dx::IntrinsicContext& call) {
    return context->ui_tree.Get(OwnerTextNode(context, call))->text;
}

[[nodiscard]] std::int64_t DateMillis(dx::IntrinsicContext& call) {
    const auto slots = call.vm.Model().InstanceSlots(call.receiver);
    return static_cast<std::int64_t>(
        (static_cast<std::uint64_t>(slots[1].bits) << 32U) | slots[0].bits);
}

[[nodiscard]] PreferenceMap& PreferencesOf(dx::IntrinsicContext& call,
                                           const Context& context) {
    const auto found = context->preference_names.find(call.receiver.Value());
    if (found == context->preference_names.end()) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                              "SharedPreferences instance has no backing "
                              "store"};
    }
    return context->preferences[found->second];
}

// commit()/apply() is the flush point: the VFS close persists it.
void SavePreferences(dx::IntrinsicContext& call, const Context& context) {
    const auto found = context->preference_names.find(call.receiver.Value());
    if (found == context->preference_names.end() || context->vfs == nullptr) {
        return;
    }
    try {
        StorePreferences(*context->vfs,
                         PreferencesPathOf(context, found->second),
                         context->preferences[found->second]);
    } catch (const PreferencesXmlError& error) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                              error.what()};
    }
}

// Typed preference getter: absent keys answer the caller's default, a
// type mismatch throws the real ClassCastException.
template <typename ValueType>
[[nodiscard]] std::optional<ValueType> PreferenceValueOf(
    dx::IntrinsicContext& call, const Context& context,
    const std::string& key) {
    auto& store = PreferencesOf(call, context);
    const auto found = store.find(key);
    if (found == store.end()) return std::nullopt;
    const auto* value = std::get_if<ValueType>(&found->second);
    if (value == nullptr) {
        throw dx::VmJavaThrow{"Ljava/lang/ClassCastException;",
                              "preference has another type: " + key};
    }
    return *value;
}

}  // namespace

dx::IntrinsicHandler EditableClearHandler(const Context& context) {
    return dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        const auto node = OwnerTextNode(context, call);
        context->ui_tree.Get(node)->text.clear();
        context->ui_tree.MarkLayoutDirty(node);
        return dx::VmValue::Void();
    });
}

dx::IntrinsicHandler EditableLengthHandler(const Context& context) {
    return dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(
            static_cast<std::int32_t>(OwnerBuffer(context, call).size()));
    });
}

dx::IntrinsicHandler EditableReplaceHandler(const Context& context) {
    return dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        auto& buffer = OwnerBuffer(context, call);
        const auto start = call.arguments[0].AsInt();
        const auto end = call.arguments[1].AsInt();
        if (start < 0 || start > end ||
            static_cast<std::size_t>(end) > buffer.size()) {
            throw dx::VmJavaThrow{
                "Ljava/lang/IndexOutOfBoundsException;",
                "Editable.replace range is invalid"};
        }
        const auto value = call.arguments[2].ref;
        auto candidate = buffer;
        candidate.replace(static_cast<std::size_t>(start),
                          static_cast<std::size_t>(end - start),
                          value.IsValid()
                              ? call.vm.Model().StringValue(value)
                              : std::u16string());
        try {
            static_cast<void>(ui::MeasureFixedText(candidate, 8.0F));
        } catch (const std::runtime_error& error) {
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                  error.what()};
        }
        buffer = std::move(candidate);
        context->ui_tree.MarkLayoutDirty(OwnerTextNode(context, call));
        return Self(call);
    });
}

dx::IntrinsicHandler GraphicsNoopHandler() {
    return dx::IntrinsicHandler([](dx::IntrinsicContext&) {
        // Pure drawing state with no consuming canvas surface yet.
        return dx::VmValue::Void();
    });
}

dx::IntrinsicHandler PrefsEditHandler(const Context& context) {
    return dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        const auto name = context->preference_names.at(call.receiver.Value());
        const auto editor =
            Singleton(call, context, "prefs_editor:" + name,
                      "Landroid/content/SharedPreferencesEditorImpl;");
        context->preference_names[editor.Value()] = name;
        return dx::VmValue::Ref(editor);
    });
}

dx::IntrinsicHandler PrefsEditorCommitHandler(const Context& context) {
    return dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        SavePreferences(call, context);
        return dx::VmValue::Int(1);
    });
}

dx::IntrinsicHandler PrefsEditorPutBooleanHandler(const Context& context) {
    // Edits apply to the in-memory map immediately and commit() writes the
    // XML back; no staged-rollback behaviour is claimed.
    return dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        PreferencesOf(call,
                      context)[call.vm.StringUtf8(call.arguments[0].ref)] =
            call.arguments[1].AsInt() != 0;
        return Self(call);
    });
}

dx::IntrinsicHandler PrefsEditorPutIntHandler(const Context& context) {
    return dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        PreferencesOf(call,
                      context)[call.vm.StringUtf8(call.arguments[0].ref)] =
            call.arguments[1].AsInt();
        return Self(call);
    });
}

dx::IntrinsicHandler PrefsEditorPutLongHandler(const Context& context) {
    return dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        PreferencesOf(call,
                      context)[call.vm.StringUtf8(call.arguments[0].ref)] =
            call.arguments[1].AsLong();
        return Self(call);
    });
}

dx::IntrinsicHandler PrefsEditorPutStringHandler(const Context& context) {
    return dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        PreferencesOf(call,
                      context)[call.vm.StringUtf8(call.arguments[0].ref)] =
            call.vm.StringUtf8(call.arguments[1].ref);
        return Self(call);
    });
}

dx::IntrinsicHandler PrefsGetBooleanHandler(const Context& context) {
    return dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        const auto key = call.vm.StringUtf8(call.arguments[0].ref);
        const auto value = PreferenceValueOf<bool>(call, context, key);
        return dx::VmValue::Int(
            value.value_or(call.arguments[1].AsInt() != 0) ? 1 : 0);
    });
}

dx::IntrinsicHandler PrefsGetIntHandler(const Context& context) {
    return dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        const auto key = call.vm.StringUtf8(call.arguments[0].ref);
        const auto value = PreferenceValueOf<std::int32_t>(call, context, key);
        return dx::VmValue::Int(value.value_or(call.arguments[1].AsInt()));
    });
}

dx::IntrinsicHandler PrefsGetLongHandler(const Context& context) {
    return dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        const auto key = call.vm.StringUtf8(call.arguments[0].ref);
        const auto value = PreferenceValueOf<std::int64_t>(call, context, key);
        return dx::VmValue::Long(value.value_or(call.arguments[1].AsLong()));
    });
}

dx::IntrinsicHandler PrefsGetStringHandler(const Context& context) {
    return dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        const auto key = call.vm.StringUtf8(call.arguments[0].ref);
        const auto value = PreferenceValueOf<std::string>(call, context, key);
        if (!value.has_value()) {
            return dx::VmValue::Ref(call.arguments[1].ref);
        }
        return MakeString(call, *value);
    });
}

dx::IntrinsicHandler SurfaceHolderAddCallbackHandler(const Context& context) {
    return dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        const auto callback = call.arguments[0].ref;
        if (!callback.IsValid()) {
            throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                  "SurfaceHolder callback is null"};
        }
        auto& callbacks = context->surface_callbacks[call.receiver.Value()];
        // Registering the same callback twice does not double the events.
        if (std::find(callbacks.begin(), callbacks.end(), callback) ==
            callbacks.end()) {
            callbacks.push_back(callback);
        }
        return dx::VmValue::Void();
    });
}

dx::IntrinsicHandler SurfaceHolderSetFormatHandler() {
    return dx::IntrinsicHandler([](dx::IntrinsicContext&) {
        // Pixel format is fixed by the managed RGBA8 EGL surface.
        return dx::VmValue::Void();
    });
}

dx::IntrinsicHandler SurfaceHolderSetTypeHandler() {
    return dx::IntrinsicHandler([](dx::IntrinsicContext&) {
        // Managed EGL owns the surface type; the legacy value is
        // only a device hint and has no observable effect here.
        return dx::VmValue::Void();
    });
}

dx::IntrinsicHandler TelephonyEmptyStringHandler() {
    return dx::IntrinsicHandler([](dx::IntrinsicContext& call) {
        // Absent-SIM answers are the empty string per the platform docs.
        return MakeString(call, "");
    });
}

dx::IntrinsicHandler TelephonyFalseHandler() {
    return dx::IntrinsicHandler(
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });
}

dx::IntrinsicHandler ViewInitHandler(const Context& context) {
    return dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        const auto descriptor = call.vm.Linker()
                                    .Class(call.vm.Model().ObjectClass(
                                        call.receiver))
                                    .descriptor;
        static_cast<void>(EnsureViewUiNode(
            *context, call.receiver, UiClassForDescriptor(descriptor)));
        return dx::VmValue::Void();
    });
}

dx::IntrinsicHandler ViewSetIdHandler(const Context& context) {
    return dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        const auto node = EnsureViewUiNode(
            *context, call.receiver, ui::UiClass::View);
        context->ui_tree.SetAndroidId(node, call.arguments[0].AsInt());
        return dx::VmValue::Void();
    });
}

dx::IntrinsicHandler WidgetNoopHandler() {
    return dx::IntrinsicHandler([](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
}

dx::IntrinsicHandler WindowmanagerGetDefaultDisplayHandler(
    const Context& context) {
    return dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(Singleton(
            call, context, "display", "Landroid/view/Display;"));
    });
}

dx::IntrinsicHandler PlatformDateGetTimeHandler() {
    return dx::IntrinsicHandler([](dx::IntrinsicContext& call) {
        return dx::VmValue::Long(DateMillis(call));
    });
}

dx::IntrinsicHandler PlatformDateGetYearHandler() {
    return dx::IntrinsicHandler([](dx::IntrinsicContext& call) {
        using days = std::chrono::days;
        const auto time_point =
            std::chrono::sys_days(std::chrono::January / 1 / 1970) +
            std::chrono::milliseconds(DateMillis(call));
        const std::chrono::year_month_day date(
            std::chrono::floor<days>(time_point));
        // Date.getYear is 1900-based.
        return dx::VmValue::Int(
            static_cast<std::int32_t>(static_cast<int>(date.year())) - 1900);
    });
}

dx::IntrinsicHandler PlatformDateInitHandler(const Context& context) {
    // java.util.Date over the same deterministic platform clock.
    return dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        const auto millis =
            1'400'000'000'000LL + context->uptime_millis.load();
        const auto millis_bits = static_cast<std::uint64_t>(millis);
        const auto slots = call.vm.Model().InstanceSlots(call.receiver);
        slots[0] = {static_cast<std::uint32_t>(millis_bits & 0xffffffffULL),
                    dx::SlotTag::wide_lo};
        slots[1] = {static_cast<std::uint32_t>(millis_bits >> 32U),
                    dx::SlotTag::wide_hi};
        return dx::VmValue::Void();
    });
}

dx::IntrinsicHandler PlatformSystemCurrentTimeMillisHandler(
    const Context& context) {
    return dx::IntrinsicHandler([context](dx::IntrinsicContext&) {
        // Deterministic epoch base plus lifecycle-published uptime.
        return dx::VmValue::Long(1'400'000'000'000LL +
                                 context->uptime_millis.load());
    });
}

dx::IntrinsicHandler PlatformSystemExitHandler(const Context& context) {
    return dx::IntrinsicHandler([context](dx::IntrinsicContext&) {
        context->exit_requested = true;
        return dx::VmValue::Void();
    });
}

namespace {

[[nodiscard]] std::string SystemLoadArgument(dx::IntrinsicContext& call,
                                             const char* name) {
    const auto reference = call.arguments[0].ref;
    if (!reference.IsValid()) {
        throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                              std::string(name) + " == null"};
    }
    return call.vm.StringUtf8(reference);
}

template <typename Load>
dx::IntrinsicHandler PlatformSystemLoadHandlerImpl(
    const Context& context, const char* argument_name, Load load) {
    return dx::IntrinsicHandler(
        [context, argument_name, load = std::move(load)](
            dx::IntrinsicContext& call) {
            const auto argument = SystemLoadArgument(call, argument_name);
            if (context->native_libraries == nullptr ||
                context->application_class_loader_token == 0U) {
                throw dx::VmJavaThrow{
                    "Ljava/lang/UnsatisfiedLinkError;",
                    "APK native library loader is unavailable"};
            }
            try {
                load(*context->native_libraries, argument,
                     context->application_class_loader_token);
            } catch (const NativeLibraryLoadError& error) {
                throw dx::VmJavaThrow{"Ljava/lang/UnsatisfiedLinkError;",
                                      error.what()};
            }
            return dx::VmValue::Void();
        });
}

}  // namespace

dx::IntrinsicHandler PlatformSystemLoadHandler(const Context& context) {
    return PlatformSystemLoadHandlerImpl(
        context, "pathName",
        [](NativeLibraryLoader& libraries, const std::string_view path,
           const JavaClassLoaderToken class_loader) {
            static_cast<void>(libraries.LoadPath(path, class_loader));
        });
}

dx::IntrinsicHandler PlatformSystemLoadLibraryHandler(
    const Context& context) {
    return PlatformSystemLoadHandlerImpl(
        context, "libraryName",
        [](NativeLibraryLoader& libraries, const std::string_view name,
           const JavaClassLoaderToken class_loader) {
            static_cast<void>(libraries.LoadLibrary(name, class_loader));
        });
}

dx::IntrinsicHandler PlatformSystemNanoTimeHandler(const Context& context) {
    return dx::IntrinsicHandler([context](dx::IntrinsicContext&) {
        return dx::VmValue::Long(context->uptime_millis.load() *
                                 1'000'000LL);
    });
}

}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from shared.cpp ----
// android.* intrinsic assembly: helpers shared by the handler batches
// plus the registration entry point. Batches live in the sibling
// dexvm_android_*.cpp files, one per platform area.

#include <algorithm>
#include <cstring>

#include "ogplay/runtime/vfs/vfs.h"

#include "shared.h"
#include "ogplay/runtime/dexvm/io_runtime.h"

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

std::string FilePathOf(dx::IntrinsicContext& call,
                       const dx::VmObjectRef file) {
    const auto slots = call.vm.Model().InstanceSlots(file);
    return call.vm.StringUtf8(dx::VmObjectRef(slots[0].bits));
}

dx::VmObjectRef OpenStream(dx::IntrinsicContext& call, const Context& context,
                           std::vector<std::byte> bytes,
                           const char* descriptor) {
    const auto instance = call.vm.NewIntrinsicInstance(descriptor);
    call.vm.IO().SetInput(instance, {std::move(bytes), 0, false});
    static_cast<void>(context);
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

namespace {

[[nodiscard]] std::int64_t SaturatingDeadline(const std::int64_t now,
                                               const std::int64_t delay) {
    if (delay <= 0) return now;
    return delay > std::numeric_limits<std::int64_t>::max() - now
               ? std::numeric_limits<std::int64_t>::max()
               : now + delay;
}

[[nodiscard]] dx::VmObjectRef CurrentJavaThread(dx::Interpreter& vm) {
    auto& linker = vm.Linker();
    const auto owner = linker.FindClass("Ljava/lang/Thread;");
    if (!owner.has_value()) return dx::VmObjectRef{};
    const auto method = linker.FindDirectMethod(
        *owner, "currentThread", "()Ljava/lang/Thread;");
    if (!method.has_value()) return dx::VmObjectRef{};
    const auto outcome = vm.Call(*method, {});
    if (outcome.exception.IsValid()) {
        vm.SetPendingException(outcome.exception);
        return dx::VmObjectRef{};
    }
    return outcome.value.ref;
}

[[nodiscard]] dx::VmCallOutcome CallVirtual(
    dx::Interpreter& vm, const dx::VmObjectRef receiver,
    const char* name, const char* descriptor,
    std::vector<dx::VmValue> arguments = {}) {
    if (!receiver.IsValid()) {
        throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                              std::string(name) + " receiver is null"};
    }
    auto& linker = vm.Linker();
    const auto owner = vm.Model().ObjectClass(receiver);
    const auto index = linker.FindVtableIndex(owner, name, descriptor);
    if (!index.has_value()) {
        throw dx::VmJavaThrow{"Ljava/lang/AbstractMethodError;",
                              linker.Class(owner).descriptor + "." + name +
                                  descriptor};
    }
    arguments.insert(arguments.begin(), dx::VmValue::Ref(receiver));
    return vm.Call(linker.Class(owner).vtable[*index], arguments);
}

[[nodiscard]] dx::VmCallOutcome NormalOutcome() {
    return {dx::VmValue::Void(), dx::VmObjectRef{}, dx::DexClassId{}, {}, {}};
}

[[nodiscard]] std::string RenderScheduledFailure(
    dx::Interpreter& vm, const dx::VmCallOutcome& outcome,
    const char* callback) {
    std::string rendered = std::string(callback) + " raised: " +
                           vm.Linker().Class(outcome.exception_class).descriptor +
                           ": " + outcome.exception_message;
    for (const auto& entry : outcome.exception_stack) {
        rendered += "\n  at " + entry.class_descriptor + "." +
                    entry.method_name + " (pc " +
                    std::to_string(entry.pc) + ")";
    }
    return rendered;
}

void EnqueueLocked(DexVmAndroidContext& context,
                   DexVmAndroidContext::ScheduledWork work) {
    work.sequence = context.next_scheduler_sequence++;
    context.scheduled_work.push_back(work);
}

[[nodiscard]] std::optional<DexVmAndroidContext::ScheduledWork> TakeDue(
    DexVmAndroidContext& context, const dx::VmObjectRef looper,
    const std::int64_t now) {
    std::scoped_lock lock(context.scheduler_mutex);
    auto selected = context.scheduled_work.end();
    for (auto it = context.scheduled_work.begin();
         it != context.scheduled_work.end(); ++it) {
        if (it->looper != looper || it->deadline_millis > now) continue;
        if (selected == context.scheduled_work.end() ||
            std::tie(it->deadline_millis, it->sequence) <
                std::tie(selected->deadline_millis, selected->sequence)) {
            selected = it;
        }
    }
    if (selected == context.scheduled_work.end()) return std::nullopt;
    auto work = *selected;
    context.scheduled_work.erase(selected);
    return work;
}

[[nodiscard]] bool LooperStopped(const DexVmAndroidContext& context,
                                 const dx::VmObjectRef looper) {
    const auto found = context.loopers.find(looper.Value());
    return context.scheduler_shutdown || found == context.loopers.end() ||
           found->second.quitting;
}

[[nodiscard]] dx::VmCallOutcome DispatchScheduled(
    dx::Interpreter& vm, DexVmAndroidContext& context,
    const DexVmAndroidContext::ScheduledWork& work) {
    using Kind = DexVmAndroidContext::ScheduledWorkKind;
    if (work.kind == Kind::handler_message) {
        return CallVirtual(vm, work.target, "dispatchMessage",
                           "(Landroid/os/Message;)V",
                           {dx::VmValue::Ref(work.payload)});
    }
    if (work.kind == Kind::handler_runnable) {
        return CallVirtual(vm, work.payload, "run", "()V");
    }
    if (work.kind == Kind::timer_task) {
        const auto outcome = CallVirtual(vm, work.owner, "run", "()V");
        if (outcome.exception.IsValid()) return outcome;
        std::scoped_lock lock(context.scheduler_mutex);
        const auto found = context.timer_tasks.find(work.owner.Value());
        if (found == context.timer_tasks.end() || found->second.cancelled ||
            found->second.generation != work.generation ||
            found->second.period_millis <= 0 ||
            context.cancelled_timers[found->second.timer.Value()]) {
            if (found != context.timer_tasks.end()) {
                found->second.scheduled = false;
            }
            return outcome;
        }
        const auto now = context.uptime_millis.load();
        auto& state = found->second;
        state.scheduled_time = state.fixed_rate
                                   ? SaturatingDeadline(
                                         state.scheduled_time,
                                         state.period_millis)
                                   : SaturatingDeadline(now,
                                                        state.period_millis);
        EnqueueLocked(context,
                      {state.scheduled_time, 0, Kind::timer_task,
                       work.looper, work.owner, dx::VmObjectRef{},
                       dx::VmObjectRef{}, dx::VmObjectRef{}, 0,
                       state.generation});
        context.scheduler_changed.notify_all();
        return outcome;
    }
    if (work.kind == Kind::countdown) {
        std::int64_t remaining{};
        std::int64_t interval{};
        {
            std::scoped_lock lock(context.scheduler_mutex);
            const auto found = context.countdown_timers.find(work.owner.Value());
            if (found == context.countdown_timers.end() ||
                found->second.cancelled ||
                found->second.generation != work.generation) {
                return NormalOutcome();
            }
            remaining = found->second.stop_time_millis -
                        context.uptime_millis.load();
            interval = found->second.interval_millis;
        }
        if (remaining <= 0) {
            return CallVirtual(vm, work.owner, "onFinish", "()V");
        }
        if (remaining < interval) {
            std::scoped_lock lock(context.scheduler_mutex);
            EnqueueLocked(context,
                          {SaturatingDeadline(context.uptime_millis.load(),
                                              remaining),
                           0, Kind::countdown, work.looper, work.owner,
                           dx::VmObjectRef{}, dx::VmObjectRef{},
                           dx::VmObjectRef{}, 0, work.generation});
            context.scheduler_changed.notify_all();
            return NormalOutcome();
        }
        const auto outcome = CallVirtual(
            vm, work.owner, "onTick", "(J)V",
            {dx::VmValue::Long(remaining)});
        if (outcome.exception.IsValid()) return outcome;
        std::scoped_lock lock(context.scheduler_mutex);
        const auto found = context.countdown_timers.find(work.owner.Value());
        if (found != context.countdown_timers.end() &&
            !found->second.cancelled &&
            found->second.generation == work.generation) {
            EnqueueLocked(context,
                          {SaturatingDeadline(context.uptime_millis.load(),
                                              interval),
                           0, Kind::countdown, work.looper, work.owner,
                           dx::VmObjectRef{}, dx::VmObjectRef{},
                           dx::VmObjectRef{}, 0, work.generation});
            context.scheduler_changed.notify_all();
        }
        return outcome;
    }
    if (work.kind == Kind::async_progress) {
        return CallVirtual(vm, work.owner, "onProgressUpdate",
                           "([Ljava/lang/Object;)V",
                           {dx::VmValue::Ref(work.payload)});
    }
    if (work.kind == Kind::async_post) {
        bool cancelled{};
        {
            std::scoped_lock lock(context.scheduler_mutex);
            const auto found = context.async_tasks.find(work.owner.Value());
            if (found == context.async_tasks.end()) return NormalOutcome();
            found->second.status = DexVmAndroidContext::AsyncStatus::finished;
            cancelled = found->second.cancelled;
        }
        if (cancelled) {
            return CallVirtual(vm, work.owner, "onCancelled",
                               "(Ljava/lang/Object;)V",
                               {dx::VmValue::Ref(work.payload)});
        }
        return CallVirtual(vm, work.owner, "onPostExecute",
                           "(Ljava/lang/Object;)V",
                           {dx::VmValue::Ref(work.payload)});
    }
    return NormalOutcome();
}

[[nodiscard]] std::optional<std::string> PumpLooperDue(
    dx::Interpreter& vm, DexVmAndroidContext& context,
    const dx::VmObjectRef looper) {
    constexpr std::size_t kMaxCallbacksPerPump = 10'000;
    for (std::size_t count = 0; count < kMaxCallbacksPerPump; ++count) {
        const auto work = TakeDue(context, looper,
                                  context.uptime_millis.load());
        if (!work.has_value()) return std::nullopt;
        const auto outcome = DispatchScheduled(vm, context, *work);
        if (outcome.exception.IsValid()) {
            return RenderScheduledFailure(vm, outcome,
                                          "scheduled callback");
        }
    }
    return "Android scheduler exceeded the per-pump callback budget";
}

}  // namespace

dx::VmObjectRef EnsureMainLooper(dx::Interpreter& vm,
                                 const Context& context) {
    dx::VmObjectRef main{};
    {
        std::scoped_lock lock(context->scheduler_mutex);
        main = context->main_looper;
    }
    if (!main.IsValid()) {
        const auto looper = vm.NewIntrinsicInstance("Landroid/os/Looper;");
        {
            std::scoped_lock lock(context->scheduler_mutex);
            if (!context->main_looper.IsValid()) {
                context->main_looper = looper;
                context->loopers[looper.Value()] = {
                    1U, dx::VmObjectRef{}, true, false};
                context->thread_loopers[1U] = looper;
            }
            main = context->main_looper;
        }
    }
    if (vm.CurrentContextToken() == 1U) {
        const auto thread = CurrentJavaThread(vm);
        std::scoped_lock lock(context->scheduler_mutex);
        const auto found = context->loopers.find(main.Value());
        if (found != context->loopers.end() &&
            !found->second.thread.IsValid()) {
            found->second.thread = thread;
        }
    }
    return main;
}

dx::VmObjectRef EnsureMainLooper(dx::IntrinsicContext& call,
                                 const Context& context) {
    return EnsureMainLooper(call.vm, context);
}

dx::VmObjectRef CurrentLooper(const Context& context,
                              const std::uint64_t context_token) {
    std::scoped_lock lock(context->scheduler_mutex);
    const auto found = context->thread_loopers.find(context_token);
    return found == context->thread_loopers.end() ? dx::VmObjectRef{}
                                                  : found->second;
}

dx::VmObjectRef PrepareLooper(dx::IntrinsicContext& call,
                              const Context& context, const bool main) {
    const auto token = call.vm.CurrentContextToken();
    if (main && token != 1U) {
        throw dx::VmJavaThrow{"Ljava/lang/RuntimeException;",
                              "the main Looper belongs to the root thread"};
    }
    {
        std::scoped_lock lock(context->scheduler_mutex);
        if (context->thread_loopers.contains(token)) {
            throw dx::VmJavaThrow{"Ljava/lang/RuntimeException;",
                                  "only one Looper may be created per thread"};
        }
    }
    if (main) return EnsureMainLooper(call, context);
    const auto looper = call.vm.NewIntrinsicInstance("Landroid/os/Looper;");
    const auto thread = CurrentJavaThread(call.vm);
    std::scoped_lock lock(context->scheduler_mutex);
    context->loopers[looper.Value()] = {token, thread, false, false};
    context->thread_loopers[token] = looper;
    context->scheduler_changed.notify_all();
    return looper;
}

bool EnqueueHandlerWork(const Context& context, const dx::VmObjectRef looper,
                        const dx::VmObjectRef handler,
                        const dx::VmObjectRef payload,
                        const dx::VmObjectRef token, const std::int32_t what,
                        const bool runnable,
                        const std::int64_t deadline_millis) {
    std::scoped_lock lock(context->scheduler_mutex);
    if (!looper.IsValid() || LooperStopped(*context, looper)) return false;
    EnqueueLocked(*context,
                  {std::max(deadline_millis, context->uptime_millis.load()),
                   0,
                   runnable
                       ? DexVmAndroidContext::ScheduledWorkKind::handler_runnable
                       : DexVmAndroidContext::ScheduledWorkKind::handler_message,
                   looper, handler, handler, payload, token, what, 0});
    context->scheduler_changed.notify_all();
    return true;
}

void RemoveHandlerWork(const Context& context,
                       const dx::VmObjectRef handler,
                       const std::optional<std::int32_t> what,
                       const dx::VmObjectRef payload, const bool runnable,
                       const dx::VmObjectRef token, const bool match_token) {
    std::scoped_lock lock(context->scheduler_mutex);
    std::erase_if(context->scheduled_work, [&](const auto& work) {
        if (work.target != handler) return false;
        if (runnable !=
            (work.kind == DexVmAndroidContext::ScheduledWorkKind::handler_runnable)) {
            return false;
        }
        if (what.has_value() && work.what != *what) return false;
        if (payload.IsValid() && work.payload != payload) return false;
        return !match_token || work.token == token;
    });
}

bool HasHandlerWork(const Context& context, const dx::VmObjectRef handler,
                    const std::int32_t what, const dx::VmObjectRef token,
                    const bool match_token) {
    std::scoped_lock lock(context->scheduler_mutex);
    return std::ranges::any_of(context->scheduled_work, [&](const auto& work) {
        return work.target == handler &&
               work.kind ==
                   DexVmAndroidContext::ScheduledWorkKind::handler_message &&
               work.what == what && (!match_token || work.token == token);
    });
}

bool QuitLooper(const Context& context, const dx::VmObjectRef looper) {
    std::scoped_lock lock(context->scheduler_mutex);
    const auto found = context->loopers.find(looper.Value());
    if (found == context->loopers.end()) return false;
    if (found->second.main) {
        throw dx::VmJavaThrow{"Ljava/lang/RuntimeException;",
                              "the main Looper cannot quit"};
    }
    found->second.quitting = true;
    std::erase_if(context->scheduled_work,
                  [looper](const auto& work) { return work.looper == looper; });
    context->scheduler_changed.notify_all();
    return true;
}

void LoopLooper(dx::IntrinsicContext& call, const Context& context,
                const dx::VmObjectRef looper) {
    if (!looper.IsValid()) {
        throw dx::VmJavaThrow{"Ljava/lang/RuntimeException;",
                              "no Looper for this thread"};
    }
    {
        std::scoped_lock lock(context->scheduler_mutex);
        const auto found = context->loopers.find(looper.Value());
        if (found == context->loopers.end() ||
            found->second.context_token != call.vm.CurrentContextToken()) {
            throw dx::VmJavaThrow{"Ljava/lang/RuntimeException;",
                                  "Looper.loop called from the wrong thread"};
        }
        if (found->second.main) {
            throw dx::VmJavaThrow{
                "Ljava/lang/UnsupportedOperationException;",
                "the main Looper is driven by the lifecycle safe point"};
        }
    }
    for (;;) {
        if (const auto error = PumpLooperDue(call.vm, *context, looper);
            error.has_value()) {
            throw dx::VmJavaThrow{"Ljava/lang/RuntimeException;", *error};
        }
        {
            std::scoped_lock lock(context->scheduler_mutex);
            if (LooperStopped(*context, looper) ||
                ThreadRuntime(context).ShuttingDown()) {
                return;
            }
        }
        auto& execution_lock = call.vm.ExecutionLock();
        const auto depth = execution_lock.ReleaseForBlocking();
        {
            std::unique_lock lock(context->scheduler_mutex);
            context->scheduler_changed.wait_for(
                lock, std::chrono::milliseconds(2));
        }
        execution_lock.ReacquireAfterBlocking(depth);
    }
}

dx::VmObjectRef WaitForHandlerThreadLooper(dx::IntrinsicContext& call,
                                           const Context& context,
                                           const dx::VmObjectRef thread) {
    for (;;) {
        {
            std::scoped_lock lock(context->scheduler_mutex);
            const auto found = context->handler_threads.find(thread.Value());
            if (found != context->handler_threads.end() &&
                found->second.IsValid()) {
                return found->second;
            }
            if (context->scheduler_shutdown) return dx::VmObjectRef{};
        }
        if (!ThreadRuntime(context).IsAlive(thread)) return dx::VmObjectRef{};
        auto& execution_lock = call.vm.ExecutionLock();
        const auto depth = execution_lock.ReleaseForBlocking();
        {
            std::unique_lock lock(context->scheduler_mutex);
            context->scheduler_changed.wait_for(
                lock, std::chrono::milliseconds(2));
        }
        execution_lock.ReacquireAfterBlocking(depth);
    }
}

void PublishHandlerThreadLooper(const Context& context,
                                const dx::VmObjectRef thread,
                                const dx::VmObjectRef looper) {
    std::scoped_lock lock(context->scheduler_mutex);
    context->handler_threads[thread.Value()] = looper;
    context->scheduler_changed.notify_all();
}

void ScheduleCountDown(const Context& context, const dx::VmObjectRef timer) {
    const auto main = context->main_looper;
    std::scoped_lock lock(context->scheduler_mutex);
    auto& state = context->countdown_timers[timer.Value()];
    state.cancelled = false;
    ++state.generation;
    state.stop_time_millis = SaturatingDeadline(
        context->uptime_millis.load(), state.duration_millis);
    EnqueueLocked(*context,
                  {context->uptime_millis.load(), 0,
                   DexVmAndroidContext::ScheduledWorkKind::countdown,
                   main, timer, dx::VmObjectRef{}, dx::VmObjectRef{},
                   dx::VmObjectRef{}, 0, state.generation});
    context->scheduler_changed.notify_all();
}

void StartAsyncTask(dx::IntrinsicContext& call, const Context& context,
                    const dx::VmObjectRef task,
                    const dx::VmObjectRef params) {
    static_cast<void>(EnsureMainLooper(call, context));
    {
        std::scoped_lock lock(context->scheduler_mutex);
        auto& state = context->async_tasks[task.Value()];
        if (state.status != DexVmAndroidContext::AsyncStatus::pending) {
            throw dx::VmJavaThrow{
                "Ljava/lang/IllegalStateException;",
                "AsyncTask may only be executed once"};
        }
        state.status = DexVmAndroidContext::AsyncStatus::running;
        state.params = params;
    }
    const auto pre = CallVirtual(call.vm, task, "onPreExecute", "()V");
    if (pre.exception.IsValid()) {
        call.vm.SetPendingException(pre.exception);
        return;
    }

    const auto worker =
        call.vm.NewIntrinsicInstance("Landroid/os/AsyncTask$Worker;");
    {
        std::scoped_lock lock(context->scheduler_mutex);
        auto& state = context->async_tasks[task.Value()];
        state.worker = worker;
        context->async_workers[worker.Value()] = {task, params};
    }
    const auto thread = call.vm.NewIntrinsicInstance("Ljava/lang/Thread;");
    {
        std::scoped_lock lock(context->scheduler_mutex);
        context->async_tasks[task.Value()].thread = thread;
    }
    auto& linker = call.vm.Linker();
    const auto thread_class = linker.FindClass("Ljava/lang/Thread;");
    const auto constructor = thread_class.has_value()
                                 ? linker.FindDirectMethod(
                                       *thread_class, "<init>",
                                       "(Ljava/lang/Runnable;Ljava/lang/String;)V")
                                 : std::nullopt;
    if (!constructor.has_value()) {
        throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant,
                             "java.lang.Thread constructor is unavailable");
    }
    const auto name = call.vm.NewStringUtf8(
        "AsyncTask-" + std::to_string(task.Value()));
    auto outcome = call.vm.Call(
        *constructor,
        std::vector<dx::VmValue>{dx::VmValue::Ref(thread),
                                 dx::VmValue::Ref(worker),
                                 dx::VmValue::Ref(name)});
    if (outcome.exception.IsValid()) {
        call.vm.SetPendingException(outcome.exception);
        return;
    }
    outcome = CallVirtual(call.vm, thread, "start", "()V");
    if (outcome.exception.IsValid()) {
        call.vm.SetPendingException(outcome.exception);
    }
}

void ScheduleAsyncProgress(const Context& context,
                           const dx::VmObjectRef task,
                           const dx::VmObjectRef values) {
    std::scoped_lock lock(context->scheduler_mutex);
    const auto found = context->async_tasks.find(task.Value());
    if (found == context->async_tasks.end() || found->second.cancelled ||
        found->second.status != DexVmAndroidContext::AsyncStatus::running) {
        return;
    }
    EnqueueLocked(*context,
                  {context->uptime_millis.load(), 0,
                   DexVmAndroidContext::ScheduledWorkKind::async_progress,
                   context->main_looper, task, dx::VmObjectRef{}, values,
                   dx::VmObjectRef{}, 0, 0});
    context->scheduler_changed.notify_all();
}

void RunAsyncWorker(dx::IntrinsicContext& call, const Context& context,
                    const dx::VmObjectRef worker) {
    dx::VmObjectRef task{};
    dx::VmObjectRef params{};
    {
        std::scoped_lock lock(context->scheduler_mutex);
        const auto found = context->async_workers.find(worker.Value());
        if (found == context->async_workers.end()) return;
        task = found->second.task;
        params = found->second.params;
    }
    const auto outcome = CallVirtual(
        call.vm, task, "doInBackground", "([Ljava/lang/Object;)Ljava/lang/Object;",
        {dx::VmValue::Ref(params)});
    if (outcome.exception.IsValid()) {
        call.vm.SetPendingException(outcome.exception);
        return;
    }
    {
        std::scoped_lock lock(context->scheduler_mutex);
        const auto found = context->async_tasks.find(task.Value());
        if (found == context->async_tasks.end()) return;
        found->second.result = outcome.value.ref;
        EnqueueLocked(*context,
                      {context->uptime_millis.load(), 0,
                       DexVmAndroidContext::ScheduledWorkKind::async_post,
                       context->main_looper, task, dx::VmObjectRef{},
                       outcome.value.ref, dx::VmObjectRef{}, 0, 0});
        context->scheduler_changed.notify_all();
    }
}

void CancelCountDown(const Context& context, const dx::VmObjectRef timer) {
    std::scoped_lock lock(context->scheduler_mutex);
    auto& state = context->countdown_timers[timer.Value()];
    state.cancelled = true;
    ++state.generation;
    std::erase_if(context->scheduled_work, [timer](const auto& work) {
        return work.kind == DexVmAndroidContext::ScheduledWorkKind::countdown &&
               work.owner == timer;
    });
}

void ScheduleTimerTask(dx::Interpreter& vm, const Context& context,
                       const dx::VmObjectRef timer,
                       const dx::VmObjectRef task,
                       const std::int64_t delay_millis,
                       const std::int64_t period_millis,
                       const bool fixed_rate) {
    if (delay_millis < 0 || period_millis < -1) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                              "negative Timer delay or period"};
    }
    if (period_millis == 0) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                              "Timer period must be positive"};
    }
    const auto looper = EnsureMainLooper(vm, context);
    std::scoped_lock lock(context->scheduler_mutex);
    if (context->cancelled_timers[timer.Value()]) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                              "Timer is cancelled"};
    }
    auto& state = context->timer_tasks[task.Value()];
    if (state.cancelled) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                              "TimerTask is cancelled"};
    }
    if (state.scheduled) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                              "TimerTask is already scheduled"};
    }
    state.timer = timer;
    state.period_millis = std::max<std::int64_t>(period_millis, 0);
    state.fixed_rate = fixed_rate;
    state.scheduled = true;
    state.cancelled = false;
    ++state.generation;
    state.scheduled_time = SaturatingDeadline(
        context->uptime_millis.load(), delay_millis);
    EnqueueLocked(*context,
                  {state.scheduled_time, 0,
                   DexVmAndroidContext::ScheduledWorkKind::timer_task,
                   looper, task, dx::VmObjectRef{}, dx::VmObjectRef{},
                   dx::VmObjectRef{}, 0, state.generation});
    context->scheduler_changed.notify_all();
}

void CancelTimer(const Context& context, const dx::VmObjectRef timer) {
    std::scoped_lock lock(context->scheduler_mutex);
    context->cancelled_timers[timer.Value()] = true;
    for (auto& [_, state] : context->timer_tasks) {
        if (state.timer == timer) {
            state.cancelled = true;
            state.scheduled = false;
            ++state.generation;
        }
    }
    std::erase_if(context->scheduled_work, [context, timer](const auto& work) {
        if (work.kind != DexVmAndroidContext::ScheduledWorkKind::timer_task) {
            return false;
        }
        const auto found = context->timer_tasks.find(work.owner.Value());
        return found != context->timer_tasks.end() &&
               found->second.timer == timer;
    });
}

bool CancelTimerTask(const Context& context, const dx::VmObjectRef task) {
    std::scoped_lock lock(context->scheduler_mutex);
    const auto found = context->timer_tasks.find(task.Value());
    if (found == context->timer_tasks.end()) {
        auto& state = context->timer_tasks[task.Value()];
        state.cancelled = true;
        ++state.generation;
        return false;
    }
    const auto was_scheduled = found->second.scheduled &&
                               !found->second.cancelled;
    found->second.cancelled = true;
    found->second.scheduled = false;
    ++found->second.generation;
    std::erase_if(context->scheduled_work, [task](const auto& work) {
        return work.kind == DexVmAndroidContext::ScheduledWorkKind::timer_task &&
               work.owner == task;
    });
    return was_scheduled;
}

std::int64_t TimerTaskScheduledExecutionTime(
    const Context& context, const dx::VmObjectRef task) {
    std::scoped_lock lock(context->scheduler_mutex);
    const auto found = context->timer_tasks.find(task.Value());
    return found == context->timer_tasks.end() ? 0
                                               : found->second.scheduled_time;
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

}  // namespace android_intrinsics

void AdvanceAndroidClock(DexVmAndroidContext& context,
                         const std::int64_t delta_millis) {
    if (delta_millis > 0) context.uptime_millis.fetch_add(delta_millis);
    context.scheduler_changed.notify_all();
}

void ShutdownAndroidScheduler(DexVmAndroidContext& context) {
    std::scoped_lock lock(context.scheduler_mutex);
    context.scheduler_shutdown = true;
    for (auto& [_, looper] : context.loopers) looper.quitting = true;
    context.scheduled_work.clear();
    context.scheduler_changed.notify_all();
}

std::optional<std::string> PumpJavaThreads(dx::Interpreter& vm,
                                           DexVmAndroidContext& context) {
    const auto main = android_intrinsics::EnsureMainLooper(
        vm, std::shared_ptr<DexVmAndroidContext>(&context,
                                                [](DexVmAndroidContext*) {}));
    if (const auto error =
            android_intrinsics::PumpLooperDue(vm, context, main);
        error.has_value()) {
        return error;
    }
    // A real Java thread that died with an uncaught exception is fatal to
    // the process on device; surface it at the frame boundary rather than
    // at whoever happens to call join().
    if (context.threads != nullptr) return context.threads->TakeFailure();
    return std::nullopt;
}

void RegisterAndroidSchedulerStateTable(
    dx::Interpreter& vm,
    const std::shared_ptr<DexVmAndroidContext>& context) {
    if (context == nullptr) return;
    vm.RegisterIntrinsicStateTable({
        "android.scheduler",
        [context](const dx::VmObjectRef owner,
                  const dx::VmRootVisitor& visit) {
            std::scoped_lock lock(context->scheduler_mutex);
            const auto visit_if = [&visit](const dx::VmObjectRef ref) {
                if (ref.IsValid()) visit(ref);
            };
            if (const auto found = context->loopers.find(owner.Value());
                found != context->loopers.end()) {
                visit_if(found->second.thread);
            }
            for (const auto& [looper_handle, state] : context->loopers) {
                if (state.thread == owner) {
                    visit_if(dx::VmObjectRef(looper_handle));
                }
            }
            if (const auto found = context->handler_loopers.find(owner.Value());
                found != context->handler_loopers.end()) {
                visit_if(found->second);
            }
            if (const auto found = context->handler_callbacks.find(owner.Value());
                found != context->handler_callbacks.end()) {
                visit_if(found->second);
            }
            if (const auto found = context->handler_threads.find(owner.Value());
                found != context->handler_threads.end()) {
                visit_if(found->second);
            }
            if (const auto found = context->timer_tasks.find(owner.Value());
                found != context->timer_tasks.end()) {
                visit_if(found->second.timer);
            }
            if (const auto found = context->async_tasks.find(owner.Value());
                found != context->async_tasks.end()) {
                visit_if(found->second.params);
                visit_if(found->second.result);
                visit_if(found->second.worker);
                visit_if(found->second.thread);
            }
            if (const auto found = context->async_workers.find(owner.Value());
                found != context->async_workers.end()) {
                visit_if(found->second.task);
                visit_if(found->second.params);
            }
        },
        [context](const dx::VmObjectRef owner) {
            std::scoped_lock lock(context->scheduler_mutex);
            context->loopers.erase(owner.Value());
            context->handler_loopers.erase(owner.Value());
            context->handler_callbacks.erase(owner.Value());
            context->handler_threads.erase(owner.Value());
            context->timer_tasks.erase(owner.Value());
            context->cancelled_timers.erase(owner.Value());
            context->countdown_timers.erase(owner.Value());
            context->async_tasks.erase(owner.Value());
            context->async_workers.erase(owner.Value());
            std::erase_if(context->thread_loopers,
                          [owner](const auto& entry) {
                              return entry.second == owner;
                          });
            std::erase_if(context->scheduled_work,
                          [owner](const auto& work) {
                              return work.looper == owner ||
                                     work.owner == owner ||
                                     work.target == owner ||
                                     work.payload == owner ||
                                     work.token == owner;
                          });
        },
        {}});
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
