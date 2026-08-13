// Factories for handlers shared by several per-class declaration units
// (and the platform core bindings in dexvm_bridge.cpp). Bodies moved
// verbatim from the former AndroidHandlers population batches.

#include <algorithm>
#include <chrono>

#include "shared.h"

namespace ogplay::runtime::android_intrinsics {

namespace {

// FileReader/FileInputStream constructors share one VFS-backed open path.
dx::VmValue OpenInput(const Context& context, dx::IntrinsicContext& call,
                      const std::string& path) {
    auto bytes = VfsReadAll(context, path);
    if (!bytes.has_value()) {
        throw dx::VmJavaThrow{"Ljava/io/FileNotFoundException;",
                              "file not found: " + path};
    }
    context->streams[call.receiver.Value()] =
        DexVmAndroidContext::Stream{std::move(*bytes), 0, false};
    return dx::VmValue::Void();
}

// Editable text lives in the interpreter's builder buffer of the owning
// text widget.
[[nodiscard]] std::u16string& OwnerBuffer(const Context& context,
                                          dx::IntrinsicContext& call) {
    const auto found = context->editable_owner.find(call.receiver.Value());
    if (found == context->editable_owner.end()) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                              "Editable has no owning text widget"};
    }
    return call.vm.BuilderBuffer(dx::VmObjectRef(found->second));
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

dx::IntrinsicHandler ByteOutputWriteRangeHandler(const Context& context) {
    return dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        const auto found =
            context->output_streams.find(call.receiver.Value());
        if (found == context->output_streams.end() ||
            found->second.closed) {
            throw dx::VmJavaThrow{"Ljava/io/IOException;",
                                  "output stream is closed"};
        }
        auto& model = call.vm.Model();
        const auto array = call.arguments[0].ref;
        const auto offset = call.arguments[1].AsInt();
        const auto length = call.arguments[2].AsInt();
        if (offset < 0 || length < 0 ||
            static_cast<std::int64_t>(offset) + length >
                model.ArrayLength(array)) {
            throw dx::VmJavaThrow{
                "Ljava/lang/IndexOutOfBoundsException;",
                "write range exceeds the source array"};
        }
        const auto bytes = model.ReadByteRegion(array, offset, length);
        found->second.bytes.insert(found->second.bytes.end(), bytes.begin(),
                                   bytes.end());
        return dx::VmValue::Void();
    });
}

dx::IntrinsicHandler EditableClearHandler(const Context& context) {
    return dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        OwnerBuffer(context, call).clear();
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
        buffer.replace(static_cast<std::size_t>(start),
                       static_cast<std::size_t>(end - start),
                       value.IsValid()
                           ? call.vm.Model().StringValue(value)
                           : std::u16string());
        return Self(call);
    });
}

dx::IntrinsicHandler FileOutputCloseHandler(const Context& context) {
    return dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        FlushOutput(call, context, call.receiver.Value());
        return dx::VmValue::Void();
    });
}

dx::IntrinsicHandler FileOutputFlushHandler(const Context& context) {
    return dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        // Bytes become visible to readers at flush (and again at close).
        const auto found =
            context->output_streams.find(call.receiver.Value());
        if (found != context->output_streams.end() &&
            !found->second.closed) {
            VfsWriteAll(context, found->second.path, found->second.bytes);
        }
        return dx::VmValue::Void();
    });
}

dx::IntrinsicHandler FileOutputWriteBytesHandler(const Context& context) {
    return dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        const auto found =
            context->output_streams.find(call.receiver.Value());
        if (found == context->output_streams.end() ||
            found->second.closed) {
            throw dx::VmJavaThrow{"Ljava/io/IOException;",
                                  "output stream is closed"};
        }
        auto& model = call.vm.Model();
        const auto array = call.arguments[0].ref;
        const auto bytes =
            model.ReadByteRegion(array, 0, model.ArrayLength(array));
        found->second.bytes.insert(found->second.bytes.end(), bytes.begin(),
                                   bytes.end());
        return dx::VmValue::Void();
    });
}

dx::IntrinsicHandler FileStreamInitFileHandler(const Context& context) {
    return dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        const auto file = call.arguments[0].ref;
        const auto slots = call.vm.Model().InstanceSlots(file);
        return OpenInput(
            context, call,
            call.vm.StringUtf8(dx::VmObjectRef(slots[0].bits)));
    });
}

dx::IntrinsicHandler FileStreamInitPathHandler(const Context& context) {
    return dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        return OpenInput(context, call,
                         call.vm.StringUtf8(call.arguments[0].ref));
    });
}

dx::IntrinsicHandler GraphicsNoopHandler() {
    return dx::IntrinsicHandler([](dx::IntrinsicContext&) {
        // Pure drawing state with no consuming canvas surface yet.
        return dx::VmValue::Void();
    });
}

dx::IntrinsicHandler NetUnsupportedHandler() {
    return dx::IntrinsicHandler(UnsupportedNetwork);
}

dx::IntrinsicHandler OutputAdoptHandler(const Context& context) {
    // Output wrapper constructors move the wrapped record to the wrapper
    // handle (single-owner, mirroring android.reader.adopt_stream).
    return dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        const auto target = call.arguments[0].ref;
        const auto found = context->output_streams.find(target.Value());
        if (found == context->output_streams.end() || found->second.closed) {
            throw dx::VmJavaThrow{"Ljava/io/IOException;",
                                  "wrapped output stream is closed or was "
                                  "never opened"};
        }
        context->output_streams[call.receiver.Value()] =
            std::move(found->second);
        context->output_streams.erase(target.Value());
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

dx::IntrinsicHandler ReaderAdoptStreamHandler(const Context& context) {
    // Wrapper constructors adopt the wrapped stream's record: the wrapper
    // handle takes ownership and the wrapped object becomes closed.
    return dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        const auto target = call.arguments[0].ref;
        const auto found = context->streams.find(target.Value());
        if (found == context->streams.end() || found->second.closed) {
            throw dx::VmJavaThrow{"Ljava/io/IOException;",
                                  "wrapped stream is closed or was never "
                                  "opened"};
        }
        context->streams[call.receiver.Value()] =
            std::move(found->second);
        context->streams.erase(target.Value());
        return dx::VmValue::Void();
    });
}

dx::IntrinsicHandler SaxParseUnsupportedHandler() {
    return dx::IntrinsicHandler([](dx::IntrinsicContext&) -> dx::VmValue {
        throw dx::VmJavaThrow{
            "Ljava/lang/UnsupportedOperationException;",
            "SAX parsing is not implemented"};
    });
}

dx::IntrinsicHandler SaxSetContentHandlerHandler(const Context& context) {
    return dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        const auto handler = call.arguments[0].ref;
        if (!handler.IsValid()) {
            throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                  "SAX content handler is null"};
        }
        context->sax_content_handlers[call.receiver.Value()] = handler;
        return dx::VmValue::Void();
    });
}

dx::IntrinsicHandler StreamCloseHandler(const Context& context) {
    return dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        const auto found = context->streams.find(call.receiver.Value());
        if (found != context->streams.end()) found->second.closed = true;
        return dx::VmValue::Void();
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

dx::IntrinsicHandler ViewInitHandler() {
    return dx::IntrinsicHandler(
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
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

dx::IntrinsicHandler PlatformSystemLoadLibraryHandler() {
    return dx::IntrinsicHandler([](dx::IntrinsicContext& call) {
        // Libraries are preloaded and initialized by the session
        // (04 §2 step 4); the name is recorded for diagnostics.
        GuestLog(call, core::LogLevel::info,
                 "System.loadLibrary(" +
                     call.vm.StringUtf8(call.arguments[0].ref) + ")");
        return dx::VmValue::Void();
    });
}

dx::IntrinsicHandler PlatformSystemNanoTimeHandler(const Context& context) {
    return dx::IntrinsicHandler([context](dx::IntrinsicContext&) {
        return dx::VmValue::Long(context->uptime_millis.load() *
                                 1'000'000LL);
    });
}

}  // namespace ogplay::runtime::android_intrinsics
