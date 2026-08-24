// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from shared_handlers.cpp ----
// Factories for handlers shared by several per-class declaration units
// (and the platform core bindings in dexvm_bridge.cpp).

#include <algorithm>
#include <chrono>
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
