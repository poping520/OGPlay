// android.* intrinsic handlers bound to the running guest session.
// Behaviour-sensitive gaps stay explicit failures (03 §6): unsupported
// network/SMS actions throw with accounting instead of faking success.

#include <algorithm>
#include <chrono>
#include <cstring>

#include "ogplay/audio/java_sound_pool_mixer.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/loader/binary_xml.h"
#include "ogplay/runtime/integration/host_image_decode.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"
#include "ogplay/runtime/vfs/vfs.h"

namespace ogplay::runtime {
namespace {

namespace dx = ogplay::runtime::dexvm;
using Context = std::shared_ptr<DexVmAndroidContext>;

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
                           const char* descriptor = "Ljava/io/InputStream;") {
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

void RegisterContextActivity(dx::IntrinsicRegistry& registry,
                             const Context& context) {
    registry.Register("android.context.init", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.activity.init", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.activity.lifecycle_noop",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.activity.get_window",
                      [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(
            Singleton(call, context, "window", "Landroid/view/Window;"));
    });
    registry.Register("android.activity.request_window_feature",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(1);
    });
    registry.Register("android.activity.set_content_view",
                      [context](dx::IntrinsicContext& call) {
        context->content_view = call.arguments[0].ref;
        return dx::VmValue::Void();
    });
    // Minimal layout inflation: binary XML tags become widget intrinsic
    // instances and android:id entries feed findViewById. Layout geometry
    // attributes are not applied (the widget layer holds state only).
    registry.Register("android.activity.set_content_view_id",
                      [context](dx::IntrinsicContext& call) {
        const auto layout_id =
            static_cast<std::uint32_t>(call.arguments[0].AsInt());
        const auto* entry = context->arsc.FindById(layout_id);
        if (entry == nullptr || !entry->string_value.has_value()) {
            throw dx::VmJavaThrow{
                "Ljava/lang/IllegalArgumentException;",
                "layout resource id has no file entry: " +
                    std::to_string(layout_id)};
        }
        const auto bytes = ReadApkFile(context, *entry->string_value);
        static const std::unordered_map<std::string, const char*>
            kTagDescriptors = {
                {"View", "Landroid/view/View;"},
                {"TextView", "Landroid/widget/TextView;"},
                {"Button", "Landroid/widget/Button;"},
                {"EditText", "Landroid/widget/EditText;"},
                {"ImageView", "Landroid/widget/ImageView;"},
                {"ImageButton", "Landroid/widget/ImageButton;"},
                {"ProgressBar", "Landroid/widget/ProgressBar;"},
                {"VideoView", "Landroid/widget/VideoView;"},
                {"WebView", "Landroid/webkit/WebView;"},
                {"LinearLayout", "Landroid/widget/LinearLayout;"},
                {"FrameLayout", "Landroid/widget/FrameLayout;"},
                {"RelativeLayout", "Landroid/widget/RelativeLayout;"},
                {"TableLayout", "Landroid/widget/TableLayout;"},
                {"TableRow", "Landroid/widget/TableRow;"},
                {"ScrollView", "Landroid/widget/ScrollView;"},
                {"AbsoluteLayout", "Landroid/widget/AbsoluteLayout;"},
            };
        context->view_registry.clear();
        dx::VmObjectRef root;
        for (const auto& element :
             loader::ParseBinaryXmlElements(bytes)) {
            if (element.name == "merge") continue;  // container marker
            const auto found = kTagDescriptors.find(element.name);
            if (found == kTagDescriptors.end()) {
                GuestLog(call, core::LogLevel::warn,
                         "layout tag has no widget intrinsic: " +
                             element.name);
                continue;
            }
            const auto instance =
                call.vm.NewIntrinsicInstance(found->second);
            if (!root.IsValid()) root = instance;
            if (element.id != 0) {
                context->view_registry[element.id] = instance;
            }
        }
        if (!root.IsValid()) {
            throw dx::VmJavaThrow{
                "Ljava/lang/IllegalStateException;",
                "layout produced no inflatable widget: " +
                    *entry->string_value};
        }
        // Document order puts the layout root first; it becomes the
        // installed content view for the lifecycle harness.
        context->content_view = root;
        return dx::VmValue::Void();
    });
    // The whole VM is the UI thread in the cooperative model, so the
    // runnable executes synchronously (matches Android semantics when the
    // caller is already on the UI thread).
    registry.Register("android.activity.run_on_ui_thread",
                      [](dx::IntrinsicContext& call) {
        const auto runnable = call.arguments[0].ref;
        if (!runnable.IsValid()) {
            throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                  "runOnUiThread action is null"};
        }
        auto& vm = call.vm;
        auto& linker = vm.Linker();
        const auto runnable_class = vm.Model().ObjectClass(runnable);
        const auto index =
            linker.FindVtableIndex(runnable_class, "run", "()V");
        if (!index.has_value()) {
            throw dx::VmJavaThrow{
                "Ljava/lang/IllegalStateException;",
                "runOnUiThread target has no run() method"};
        }
        const auto outcome =
            vm.Call(linker.Class(runnable_class).vtable[*index],
                    std::vector<dx::VmValue>{dx::VmValue::Ref(runnable)});
        if (outcome.exception.IsValid()) {
            throw dx::VmJavaThrow{"Ljava/lang/RuntimeException;",
                                  "runOnUiThread raised: " +
                                      outcome.exception_message};
        }
        return dx::VmValue::Void();
    });
    registry.Register("android.activity.find_view_by_id",
                      [context](dx::IntrinsicContext& call) {
        const auto found = context->view_registry.find(
            static_cast<std::uint32_t>(call.arguments[0].AsInt()));
        if (found == context->view_registry.end()) {
            // Absent id: null is the documented answer.
            return dx::VmValue::Ref(dx::VmObjectRef{});
        }
        return dx::VmValue::Ref(found->second);
    });
    registry.Register("android.activity.set_volume_control_stream",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.activity.on_key_false",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(0);
    });
    registry.Register("android.activity.on_touch_false",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(0);
    });
    registry.Register("android.activity.finish",
                      [context](dx::IntrinsicContext&) {
        context->exit_requested = true;
        return dx::VmValue::Void();
    });
    registry.Register("android.context.get_package_name",
                      [context](dx::IntrinsicContext& call) {
        return MakeString(call, context->package_name);
    });
    registry.Register("android.context.get_resources",
                      [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(Singleton(
            call, context, "resources",
            "Landroid/content/res/Resources;"));
    });
    registry.Register("android.context.get_assets",
                      [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(Singleton(
            call, context, "assets",
            "Landroid/content/res/AssetManager;"));
    });
    registry.Register("android.context.get_system_service",
                      [context](dx::IntrinsicContext& call) {
        const auto name = call.vm.StringUtf8(call.arguments[0].ref);
        if (name == "phone") {
            return dx::VmValue::Ref(Singleton(
                call, context, "phone",
                "Landroid/telephony/TelephonyManager;"));
        }
        if (name == "audio") {
            return dx::VmValue::Ref(Singleton(
                call, context, "audio", "Landroid/media/AudioManager;"));
        }
        if (name == "wifi") {
            return dx::VmValue::Ref(Singleton(
                call, context, "wifi", "Landroid/net/wifi/WifiManager;"));
        }
        if (name == "sensor") {
            return dx::VmValue::Ref(Singleton(
                call, context, "sensor",
                "Landroid/hardware/SensorManager;"));
        }
        if (name == "connectivity") {
            return dx::VmValue::Ref(Singleton(
                call, context, "connectivity",
                "Landroid/net/ConnectivityManager;"));
        }
        if (name == "input_method") {
            return dx::VmValue::Ref(Singleton(
                call, context, "input_method",
                "Landroid/view/inputmethod/InputMethodManager;"));
        }
        if (name == "window") {
            return dx::VmValue::Ref(Singleton(
                call, context, "window_manager",
                "Landroid/view/WindowManagerImpl;"));
        }
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                              "system service is not provided: " + name};
    });
    registry.Register("android.activity.get_window_manager",
                      [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(Singleton(
            call, context, "window_manager",
            "Landroid/view/WindowManagerImpl;"));
    });
    registry.Register("android.windowmanager.get_default_display",
                      [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(Singleton(
            call, context, "display", "Landroid/view/Display;"));
    });
    registry.Register("android.display.get_width",
                      [context](dx::IntrinsicContext&) {
        return dx::VmValue::Int(
            static_cast<std::int32_t>(context->surface_width));
    });
    registry.Register("android.display.get_height",
                      [context](dx::IntrinsicContext&) {
        return dx::VmValue::Int(
            static_cast<std::int32_t>(context->surface_height));
    });
    registry.Register("android.connectivity.get_active_network_info",
                      [](dx::IntrinsicContext&) {
        // Truthful offline fact: no active network (documented null).
        return dx::VmValue::Ref(dx::VmObjectRef{});
    });
    registry.Register("android.connectivity.get_network_info",
                      [](dx::IntrinsicContext&) {
        // No network of any type is connected on this platform.
        return dx::VmValue::Ref(dx::VmObjectRef{});
    });
    registry.Register("android.context.get_content_resolver",
                      [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(
            Singleton(call, context, "content_resolver",
                      "Landroid/content/ContentResolver;"));
    });
    registry.Register("android.context.send_broadcast",
                      [](dx::IntrinsicContext& call) {
        // No other process exists; the broadcast truthfully has no
        // audience. Logged so silent drops stay visible.
        GuestLog(call, core::LogLevel::debug,
                 "sendBroadcast dropped: no receivers on this platform");
        return dx::VmValue::Void();
    });
    registry.Register("android.context.register_receiver",
                      [](dx::IntrinsicContext&) {
        // Sticky broadcast lookup: nothing pending on this platform.
        return dx::VmValue::Ref(dx::VmObjectRef{});
    });
    registry.Register("android.context.start_activity",
                      [context](dx::IntrinsicContext& call) -> dx::VmValue {
        // In-process activity switch: only intents with an explicit
        // component that resolves to a dex activity are supported; anything
        // else (external apps, market links, ...) stays an explicit
        // failure.
        const auto intent = call.arguments[0].ref;
        const auto component =
            context->intent_components.find(intent.Value());
        if (component == context->intent_components.end()) {
            throw dx::VmJavaThrow{
                "Ljava/lang/UnsupportedOperationException;",
                "startActivity without an in-package component is outside "
                "the compatibility scope"};
        }
        context->pending_activity_descriptor = component->second;
        context->current_intent = intent;
        return dx::VmValue::Void();
    });
    registry.Register("android.activity.get_intent",
                      [context](dx::IntrinsicContext& call) {
        if (context->current_intent.IsValid()) {
            return dx::VmValue::Ref(context->current_intent);
        }
        // Root activity launch: an empty intent with no extras.
        return dx::VmValue::Ref(
            call.vm.NewIntrinsicInstance("Landroid/content/Intent;"));
    });
}

void RegisterViewSurface(dx::IntrinsicRegistry& registry,
                         const Context& context) {
    registry.Register("android.window.noop", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.window.noop_add", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.window.noop_clear",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.view.init", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.view.noop_size", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.view.noop_focus", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.view.noop_flag", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.graphics.noop", [](dx::IntrinsicContext&) {
        // Pure drawing state with no consuming canvas surface yet.
        return dx::VmValue::Void();
    });
    registry.Register("android.paint.set_typeface",
                      [](dx::IntrinsicContext& call) {
        // Returns the typeface that was set, per the platform contract.
        return dx::VmValue::Ref(call.arguments[0].ref);
    });
    registry.Register("android.typeface.default_from_style",
                      [](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(
            call.vm.NewIntrinsicInstance("Landroid/graphics/Typeface;"));
    });
    registry.Register("android.typeface.clinit",
                      [](dx::IntrinsicContext& call) {
        auto& vm = call.vm;
        vm.SetIntrinsicStaticRef(
            "Landroid/graphics/Typeface;", "SERIF",
            "Landroid/graphics/Typeface;",
            vm.NewIntrinsicInstance("Landroid/graphics/Typeface;"));
        return dx::VmValue::Void();
    });
    registry.Register("android.rect.width", [](dx::IntrinsicContext& call) {
        const auto slots = call.vm.Model().InstanceSlots(call.receiver);
        return dx::VmValue::Int(static_cast<std::int32_t>(slots[2].bits) -
                                static_cast<std::int32_t>(slots[0].bits));
    });
    registry.Register("android.rect.height", [](dx::IntrinsicContext& call) {
        const auto slots = call.vm.Model().InstanceSlots(call.receiver);
        return dx::VmValue::Int(static_cast<std::int32_t>(slots[3].bits) -
                                static_cast<std::int32_t>(slots[1].bits));
    });
    registry.Register("android.window.get_attributes",
                      [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(
            Singleton(call, context, "window_attributes",
                      "Landroid/view/WindowManager$LayoutParams;"));
    });
    registry.Register("android.view.request_focus",
                      [](dx::IntrinsicContext&) {
        // The single fullscreen view always holds focus.
        return dx::VmValue::Int(1);
    });
    registry.Register("android.view.get_id", [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(-1);  // View.NO_ID: no id was assigned
    });
    registry.Register("android.glsurfaceview.init",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.glsurfaceview.set_renderer",
                      [context](dx::IntrinsicContext& call) {
        context->renderer = call.arguments[0].ref;
        return dx::VmValue::Void();
    });
    registry.Register("android.glsurfaceview.request_render",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.glsurfaceview.lifecycle_noop",
                      [](dx::IntrinsicContext&) {
        // Render pause/resume is owned by the lifecycle driver.
        return dx::VmValue::Void();
    });
}

void RegisterResources(dx::IntrinsicRegistry& registry,
                       const Context& context) {
    registry.Register("android.resources.get_configuration",
                      [context](dx::IntrinsicContext& call) {
        const auto instance = Singleton(
            call, context, "configuration",
            "Landroid/content/res/Configuration;");
        // keyboard = KEYBOARD_NOKEYS (1): desktop host has no guest keypad.
        const auto slots = call.vm.Model().InstanceSlots(instance);
        slots[0] = {1U, dx::SlotTag::cat1};
        return dx::VmValue::Ref(instance);
    });
    registry.Register("android.resources.get_identifier",
                      [context](dx::IntrinsicContext& call) {
        const auto entry_name = call.vm.StringUtf8(call.arguments[0].ref);
        const auto type_name = call.vm.StringUtf8(call.arguments[1].ref);
        const auto* entry =
            context->arsc.FindByName(type_name, entry_name);
        return dx::VmValue::Int(
            entry == nullptr ? 0
                             : static_cast<std::int32_t>(entry->resource_id));
    });
    registry.Register("android.resources.open_raw_resource",
                      [context](dx::IntrinsicContext& call) {
        const auto resource_id =
            static_cast<std::uint32_t>(call.arguments[0].AsInt());
        const auto* entry = context->arsc.FindById(resource_id);
        if (entry == nullptr || !entry->string_value.has_value()) {
            throw dx::VmJavaThrow{
                "Ljava/lang/IllegalArgumentException;",
                "resource id has no file entry: " +
                    std::to_string(resource_id)};
        }
        return dx::VmValue::Ref(OpenStream(
            call, context, ReadApkFile(context, *entry->string_value)));
    });
    registry.Register("android.resources.get_string",
                      [](dx::IntrinsicContext&) -> dx::VmValue {
        throw dx::VmJavaThrow{
            "Ljava/lang/UnsupportedOperationException;",
            "string resources are not provided yet"};
    });
    registry.Register("android.assets.open",
                      [context](dx::IntrinsicContext& call) {
        const auto name = call.vm.StringUtf8(call.arguments[0].ref);
        return dx::VmValue::Ref(OpenStream(
            call, context, ReadApkFile(context, "assets/" + name)));
    });
    registry.Register("android.assets.open_mode",
                      [context](dx::IntrinsicContext& call) {
        const auto name = call.vm.StringUtf8(call.arguments[0].ref);
        return dx::VmValue::Ref(OpenStream(
            call, context, ReadApkFile(context, "assets/" + name)));
    });
}

void RegisterStreams(dx::IntrinsicRegistry& registry,
                     const Context& context) {
    registry.Register("android.stream.read_one",
                      [context](dx::IntrinsicContext& call) {
        auto& stream = StreamOf(call, context);
        if (stream.cursor >= stream.bytes.size()) {
            return dx::VmValue::Int(-1);
        }
        return dx::VmValue::Int(static_cast<std::int32_t>(
            static_cast<std::uint8_t>(stream.bytes[stream.cursor++])));
    });
    // Wrapper constructors adopt the wrapped stream's record: the wrapper
    // handle takes ownership and the wrapped object becomes closed.
    registry.Register("android.reader.adopt_stream",
                      [context](dx::IntrinsicContext& call) {
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
    registry.Register("android.reader.read_line",
                      [context](dx::IntrinsicContext& call) {
        auto& stream = StreamOf(call, context);
        if (stream.cursor >= stream.bytes.size()) {
            return dx::VmValue::Ref(dx::VmObjectRef{});  // EOF is null
        }
        // Line terminators: \n, \r\n or \r; decoded as UTF-8 (the
        // platform default; explicit charsets are not tracked yet).
        std::string line;
        while (stream.cursor < stream.bytes.size()) {
            const auto byte =
                static_cast<char>(stream.bytes[stream.cursor++]);
            if (byte == '\n') break;
            if (byte == '\r') {
                if (stream.cursor < stream.bytes.size() &&
                    static_cast<char>(stream.bytes[stream.cursor]) ==
                        '\n') {
                    ++stream.cursor;
                }
                break;
            }
            line.push_back(byte);
        }
        return dx::VmValue::Ref(call.vm.NewStringUtf8(line));
    });
    registry.Register("android.reader.ready",
                      [context](dx::IntrinsicContext& call) {
        auto& stream = StreamOf(call, context);
        return dx::VmValue::Int(
            stream.cursor < stream.bytes.size() ? 1 : 0);
    });
    registry.Register("android.charset.for_name",
                      [](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(
            call.vm.NewIntrinsicInstance("Ljava/nio/charset/Charset;"));
    });
    registry.Register("android.byte_stream.init_input",
                      [context](dx::IntrinsicContext& call) {
        auto& model = call.vm.Model();
        const auto array = call.arguments[0].ref;
        context->streams[call.receiver.Value()] =
            DexVmAndroidContext::Stream{
                model.ReadByteRegion(array, 0, model.ArrayLength(array)),
                0, false};
        return dx::VmValue::Void();
    });
    registry.Register("android.data_input.read_fully",
                      [context](dx::IntrinsicContext& call) {
        auto& stream = StreamOf(call, context);
        auto& model = call.vm.Model();
        const auto array = call.arguments[0].ref;
        const auto wanted =
            static_cast<std::size_t>(model.ArrayLength(array));
        if (stream.bytes.size() - stream.cursor < wanted) {
            throw dx::VmJavaThrow{"Ljava/io/IOException;",
                                  "readFully hit end of stream"};
        }
        model.WriteByteRegion(
            array, 0,
            std::span(stream.bytes).subspan(stream.cursor, wanted));
        stream.cursor += wanted;
        return dx::VmValue::Void();
    });
    registry.Register("android.data_input.skip_bytes",
                      [context](dx::IntrinsicContext& call) {
        auto& stream = StreamOf(call, context);
        const auto wanted = call.arguments[0].AsInt();
        const auto amount = std::min<std::size_t>(
            wanted > 0 ? static_cast<std::size_t>(wanted) : 0,
            stream.bytes.size() - stream.cursor);
        stream.cursor += amount;
        return dx::VmValue::Int(static_cast<std::int32_t>(amount));
    });
    registry.Register("android.byte_output.init",
                      [context](dx::IntrinsicContext& call) {
        // No path: bytes stay in memory and never publish to a file.
        context->output_streams[call.receiver.Value()] =
            DexVmAndroidContext::OutputStream{{}, {}, false};
        return dx::VmValue::Void();
    });
    registry.Register("android.byte_output.write_range",
                      [context](dx::IntrinsicContext& call) {
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
    const auto byte_output_of = [context](dx::IntrinsicContext& call)
        -> DexVmAndroidContext::OutputStream& {
        const auto found =
            context->output_streams.find(call.receiver.Value());
        if (found == context->output_streams.end()) {
            throw dx::VmJavaThrow{"Ljava/io/IOException;",
                                  "output stream was never opened"};
        }
        return found->second;
    };
    registry.Register("android.byte_output.to_byte_array",
                      [byte_output_of](dx::IntrinsicContext& call) {
        auto& output = byte_output_of(call);
        auto& vm = call.vm;
        const auto array_class = vm.Linker().ResolveDescriptor("[B");
        const auto array = vm.Model().NewPrimitiveArray(
            array_class, JniPrimitiveKind::byte,
            static_cast<JniSize>(output.bytes.size()));
        if (!output.bytes.empty()) {
            vm.Model().WriteByteRegion(array, 0, output.bytes);
        }
        return dx::VmValue::Ref(array);
    });
    registry.Register("android.byte_output.size",
                      [byte_output_of](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(static_cast<std::int32_t>(
            byte_output_of(call).bytes.size()));
    });
    registry.Register("android.byte_output.to_string",
                      [byte_output_of](dx::IntrinsicContext& call) {
        auto& output = byte_output_of(call);
        return dx::VmValue::Ref(call.vm.NewStringUtf8(std::string(
            reinterpret_cast<const char*>(output.bytes.data()),
            output.bytes.size())));
    });
    registry.Register("android.file_writer.append_char",
                      [byte_output_of](dx::IntrinsicContext& call) {
        auto& output = byte_output_of(call);
        // BMP code unit encoded as UTF-8 (ASCII fast path; otherwise a
        // string round-trip through the interpreter's UTF-8 rendering).
        const auto unit = static_cast<char16_t>(
            call.arguments[0].cat1 & 0xffffU);
        std::string encoded;
        if (unit < 0x80U) {
            encoded.push_back(static_cast<char>(unit));
        } else {
            encoded = call.vm.StringUtf8(
                call.vm.Model().NewString(std::u16string(1, unit)));
        }
        for (const auto character : encoded) {
            output.bytes.push_back(static_cast<std::byte>(character));
        }
        return dx::VmValue::Ref(call.receiver);
    });
    registry.Register("android.file_writer.append_sequence",
                      [byte_output_of](dx::IntrinsicContext& call) {
        auto& output = byte_output_of(call);
        const auto value = call.arguments[0].ref;
        const auto text = value.IsValid()
                              ? call.vm.StringUtf8(value)
                              : std::string("null");
        for (const auto character : text) {
            output.bytes.push_back(static_cast<std::byte>(character));
        }
        return dx::VmValue::Ref(call.receiver);
    });
    registry.Register("android.stream.read_range",
                      [context](dx::IntrinsicContext& call) {
        auto& stream = StreamOf(call, context);
        const auto array = call.arguments[0].ref;
        const auto offset = call.arguments[1].AsInt();
        const auto length = call.arguments[2].AsInt();
        if (offset < 0 || length < 0) {
            throw dx::VmJavaThrow{
                "Ljava/lang/IndexOutOfBoundsException;",
                "negative stream read range"};
        }
        const auto remaining = stream.bytes.size() - stream.cursor;
        if (remaining == 0) return dx::VmValue::Int(-1);
        const auto amount = std::min<std::size_t>(
            static_cast<std::size_t>(length), remaining);
        call.vm.Model().WriteByteRegion(
            array, offset,
            std::span(stream.bytes).subspan(stream.cursor, amount));
        stream.cursor += amount;
        return dx::VmValue::Int(static_cast<std::int32_t>(amount));
    });
    registry.Register("android.stream.read_full",
                      [context](dx::IntrinsicContext& call) {
        auto& stream = StreamOf(call, context);
        const auto array = call.arguments[0].ref;
        const auto capacity = call.vm.Model().ArrayLength(array);
        const auto remaining = stream.bytes.size() - stream.cursor;
        if (remaining == 0) return dx::VmValue::Int(-1);
        const auto amount = std::min<std::size_t>(
            static_cast<std::size_t>(capacity), remaining);
        call.vm.Model().WriteByteRegion(
            array, 0,
            std::span(stream.bytes).subspan(stream.cursor, amount));
        stream.cursor += amount;
        return dx::VmValue::Int(static_cast<std::int32_t>(amount));
    });
    registry.Register("android.stream.available",
                      [context](dx::IntrinsicContext& call) {
        auto& stream = StreamOf(call, context);
        return dx::VmValue::Int(static_cast<std::int32_t>(
            stream.bytes.size() - stream.cursor));
    });
    registry.Register("android.stream.close",
                      [context](dx::IntrinsicContext& call) {
        const auto found = context->streams.find(call.receiver.Value());
        if (found != context->streams.end()) found->second.closed = true;
        return dx::VmValue::Void();
    });
    registry.Register("android.stream.skip",
                      [context](dx::IntrinsicContext& call) {
        auto& stream = StreamOf(call, context);
        const auto requested = call.arguments[0].AsLong();
        const auto remaining = static_cast<std::int64_t>(
            stream.bytes.size() - stream.cursor);
        const auto amount =
            std::max<std::int64_t>(0, std::min(requested, remaining));
        stream.cursor += static_cast<std::size_t>(amount);
        return dx::VmValue::Long(amount);
    });
}

// Reads a whole file from the shared guest VFS; nullopt when the VFS is
// absent or the path does not resolve.
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

[[nodiscard]] std::optional<std::uint64_t> VfsSizeOf(
    const Context& context, const std::string& path) {
    if (context->vfs == nullptr) return std::nullopt;
    try {
        return context->vfs->Stat(path).size;
    } catch (const VfsError&) {
        return std::nullopt;
    }
}

[[nodiscard]] std::string FilePathOf(dx::IntrinsicContext& call,
                                     const dx::VmObjectRef file) {
    const auto slots = call.vm.Model().InstanceSlots(file);
    return call.vm.StringUtf8(dx::VmObjectRef(slots[0].bits));
}

void RegisterFiles(dx::IntrinsicRegistry& registry, const Context& context) {
    registry.Register("android.file.init",
                      [](dx::IntrinsicContext& call) {
        const auto slots = call.vm.Model().InstanceSlots(call.receiver);
        slots[0] = {call.arguments[0].ref.Value(), dx::SlotTag::ref};
        return dx::VmValue::Void();
    });
    registry.Register("android.file.init_parent_child",
                      [](dx::IntrinsicContext& call) {
        auto joined = call.vm.StringUtf8(call.arguments[0].ref);
        if (!joined.empty() && joined.back() != '/') joined += '/';
        joined += call.vm.StringUtf8(call.arguments[1].ref);
        const auto slots = call.vm.Model().InstanceSlots(call.receiver);
        slots[0] = {call.vm.NewStringUtf8(joined).Value(),
                    dx::SlotTag::ref};
        return dx::VmValue::Void();
    });
    registry.Register("android.file.exists",
                      [context](dx::IntrinsicContext& call) {
        const auto path = FilePathOf(call, call.receiver);
        const bool exists = context->memory_files.contains(path) ||
                            VfsSizeOf(context, path).has_value();
        return dx::VmValue::Int(exists ? 1 : 0);
    });
    registry.Register("android.file.length",
                      [context](dx::IntrinsicContext& call) {
        const auto path = FilePathOf(call, call.receiver);
        const auto overlay = context->memory_files.find(path);
        if (overlay != context->memory_files.end()) {
            return dx::VmValue::Long(
                static_cast<std::int64_t>(overlay->second.size()));
        }
        const auto size = VfsSizeOf(context, path);
        // 0 is the documented value for nonexistent paths.
        return dx::VmValue::Long(
            size.has_value() ? static_cast<std::int64_t>(*size) : 0);
    });
    registry.Register("android.file.get_path",
                      [](dx::IntrinsicContext& call) {
        return MakeString(call, FilePathOf(call, call.receiver));
    });
    registry.Register("android.file.mkdirs", [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(1);
    });
    registry.Register("android.file.create_new",
                      [context](dx::IntrinsicContext& call) {
        const auto path = FilePathOf(call, call.receiver);
        if (context->memory_files.contains(path) ||
            VfsSizeOf(context, path).has_value()) {
            return dx::VmValue::Int(0);
        }
        context->memory_files[path] = {};
        return dx::VmValue::Int(1);
    });
    registry.Register("android.environment.get_external_storage_dir",
                      [context](dx::IntrinsicContext& call) {
        const auto file = call.vm.NewIntrinsicInstance("Ljava/io/File;");
        const auto slots = call.vm.Model().InstanceSlots(file);
        slots[0] = {
            call.vm.NewStringUtf8(context->external_storage_root).Value(),
            dx::SlotTag::ref};
        return dx::VmValue::Ref(file);
    });
    registry.Register("android.environment.get_external_storage_state",
                      [context](dx::IntrinsicContext& call) {
        // The external mount is required by the profile and read at
        // startup, so MEDIA_MOUNTED is the truthful state.
        return MakeString(call, "mounted");
    });
    registry.Register("android.statfs.init", [](dx::IntrinsicContext&) {
        // Only the external volume is queryable on this platform; the
        // constructor path argument selects nothing further.
        return dx::VmValue::Void();
    });
    registry.Register("android.statfs.get_block_size",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(4096);
    });
    registry.Register("android.statfs.get_available_blocks",
                      [context](dx::IntrinsicContext&) {
        const auto blocks = context->external_free_bytes / 4096U;
        return dx::VmValue::Int(static_cast<std::int32_t>(
            std::min<std::uint64_t>(blocks, INT32_MAX)));
    });
    registry.Register("android.file_writer.init_file_append",
                      [context](dx::IntrinsicContext& call) {
        const auto path = FilePathOf(call, call.arguments[0].ref);
        DexVmAndroidContext::OutputStream output{path, {}, false};
        if (call.arguments[1].AsInt() != 0) {
            const auto overlay = context->memory_files.find(path);
            if (overlay != context->memory_files.end()) {
                output.bytes = overlay->second;
            } else if (const auto existing = VfsReadAll(context, path)) {
                output.bytes = *existing;
            }
        }
        context->output_streams[call.receiver.Value()] = std::move(output);
        return dx::VmValue::Void();
    });
    registry.Register("android.file.delete",
                      [context](dx::IntrinsicContext& call) {
        const auto path = FilePathOf(call, call.receiver);
        if (context->memory_files.erase(path) > 0) {
            return dx::VmValue::Int(1);
        }
        // Mounted (read-only) entries cannot be deleted: report failure.
        return dx::VmValue::Int(0);
    });
    const auto open_input = [context](dx::IntrinsicContext& call,
                                      const std::string& path) {
        const auto found = context->memory_files.find(path);
        if (found != context->memory_files.end()) {
            context->streams[call.receiver.Value()] =
                DexVmAndroidContext::Stream{found->second, 0, false};
            return dx::VmValue::Void();
        }
        auto bytes = VfsReadAll(context, path);
        if (!bytes.has_value()) {
            throw dx::VmJavaThrow{"Ljava/io/FileNotFoundException;",
                                  "file not found: " + path};
        }
        context->streams[call.receiver.Value()] =
            DexVmAndroidContext::Stream{std::move(*bytes), 0, false};
        return dx::VmValue::Void();
    };
    registry.Register("android.file_stream.init_file",
                      [context, open_input](dx::IntrinsicContext& call) {
        const auto file = call.arguments[0].ref;
        const auto slots = call.vm.Model().InstanceSlots(file);
        return open_input(
            call, call.vm.StringUtf8(dx::VmObjectRef(slots[0].bits)));
    });
    registry.Register("android.file_stream.init_path",
                      [context, open_input](dx::IntrinsicContext& call) {
        return open_input(call,
                          call.vm.StringUtf8(call.arguments[0].ref));
    });
    const auto open_output = [context](dx::IntrinsicContext& call,
                                       const std::string& path) {
        context->output_streams[call.receiver.Value()] =
            DexVmAndroidContext::OutputStream{path, {}, false};
        return dx::VmValue::Void();
    };
    registry.Register("android.file_output.init_path",
                      [open_output](dx::IntrinsicContext& call) {
        return open_output(call,
                           call.vm.StringUtf8(call.arguments[0].ref));
    });
    registry.Register("android.file_output.init_file",
                      [open_output](dx::IntrinsicContext& call) {
        const auto slots =
            call.vm.Model().InstanceSlots(call.arguments[0].ref);
        return open_output(
            call, call.vm.StringUtf8(dx::VmObjectRef(slots[0].bits)));
    });
    const auto flush_output = [context](dx::IntrinsicContext& call,
                                        const std::uint32_t handle) {
        const auto found = context->output_streams.find(handle);
        if (found == context->output_streams.end()) return;
        context->memory_files[found->second.path] = found->second.bytes;
        found->second.closed = true;
        static_cast<void>(call);
    };
    registry.Register("android.file_output.write_bytes",
                      [context](dx::IntrinsicContext& call) {
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
    registry.Register("android.file_output.flush",
                      [context](dx::IntrinsicContext& call) {
        // Bytes become visible to readers at flush (and again at close).
        const auto found =
            context->output_streams.find(call.receiver.Value());
        if (found != context->output_streams.end() &&
            !found->second.closed) {
            context->memory_files[found->second.path] =
                found->second.bytes;
        }
        return dx::VmValue::Void();
    });
    registry.Register("android.file_output.close",
                      [context, flush_output](dx::IntrinsicContext& call) {
        flush_output(call, call.receiver.Value());
        return dx::VmValue::Void();
    });
    registry.Register("android.data_output.init",
                      [context](dx::IntrinsicContext& call) {
        // Chain: reuse the wrapped stream's output slot.
        const auto target = call.arguments[0].ref;
        const auto found = context->output_streams.find(target.Value());
        if (found == context->output_streams.end()) {
            throw dx::VmJavaThrow{"Ljava/io/IOException;",
                                  "DataOutputStream target is not open"};
        }
        context->output_streams[call.receiver.Value()] =
            DexVmAndroidContext::OutputStream{found->second.path, {}, false};
        return dx::VmValue::Void();
    });
    registry.Register("android.data_output.write_utf",
                      [context](dx::IntrinsicContext& call) {
        auto found = context->output_streams.find(call.receiver.Value());
        if (found == context->output_streams.end() || found->second.closed) {
            throw dx::VmJavaThrow{"Ljava/io/IOException;",
                                  "DataOutputStream is closed"};
        }
        const auto text = call.vm.StringUtf8(call.arguments[0].ref);
        auto& bytes = found->second.bytes;
        bytes.push_back(static_cast<std::byte>((text.size() >> 8U) & 0xffU));
        bytes.push_back(static_cast<std::byte>(text.size() & 0xffU));
        for (const auto character : text) {
            bytes.push_back(static_cast<std::byte>(character));
        }
        return dx::VmValue::Void();
    });
    registry.Register("android.data_output.close",
                      [context, flush_output](dx::IntrinsicContext& call) {
        flush_output(call, call.receiver.Value());
        return dx::VmValue::Void();
    });
}

void RegisterDeviceServices(dx::IntrinsicRegistry& registry,
                            const Context& context) {
    registry.Register("android.log.d", [](dx::IntrinsicContext& call) {
        GuestLog(call, core::LogLevel::debug,
                 call.vm.StringUtf8(call.arguments[0].ref) + ": " +
                     call.vm.StringUtf8(call.arguments[1].ref));
        return dx::VmValue::Int(0);
    });
    registry.Register("android.log.i", [](dx::IntrinsicContext& call) {
        GuestLog(call, core::LogLevel::info,
                 call.vm.StringUtf8(call.arguments[0].ref) + ": " +
                     call.vm.StringUtf8(call.arguments[1].ref));
        return dx::VmValue::Int(0);
    });
    registry.Register("android.log.w", [](dx::IntrinsicContext& call) {
        GuestLog(call, core::LogLevel::warn,
                 call.vm.StringUtf8(call.arguments[0].ref) + ": " +
                     call.vm.StringUtf8(call.arguments[1].ref));
        return dx::VmValue::Int(0);
    });
    registry.Register("android.log.e", [](dx::IntrinsicContext& call) {
        GuestLog(call, core::LogLevel::error,
                 call.vm.StringUtf8(call.arguments[0].ref) + ": " +
                     call.vm.StringUtf8(call.arguments[1].ref));
        return dx::VmValue::Int(0);
    });
    registry.Register("android.audio_manager.get_ringer_mode",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(2);  // RINGER_MODE_NORMAL
    });
    registry.Register("android.audio_manager.get_stream_max_volume",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(15);
    });
    registry.Register("android.audio_manager.set_stream_volume",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.wifi.is_enabled", [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(0);
    });
    registry.Register("android.wifi.get_state", [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(1);  // WIFI_STATE_DISABLED
    });
    registry.Register("android.wifi.set_enabled",
                      [](dx::IntrinsicContext&) {
        // The platform has no radio to enable; the call truthfully fails.
        return dx::VmValue::Int(0);
    });
    registry.Register("android.wifi.get_connection_info",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Ref(dx::VmObjectRef{});
    });
    registry.Register("android.wifi.create_lock",
                      [](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(call.vm.NewIntrinsicInstance(
            "Landroid/net/wifi/WifiManager$WifiLock;"));
    });
    registry.Register("android.telephony.empty_string",
                      [](dx::IntrinsicContext& call) {
        // Absent-SIM answers are the empty string per the platform docs.
        return MakeString(call, "");
    });
    registry.Register("android.telephony.false",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(0);
    });
    registry.Register("android.sensor.get_type", [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(1);  // TYPE_ACCELEROMETER
    });
    registry.Register("android.sensor_manager.get_default",
                      [](dx::IntrinsicContext&) {
        // No host sensors: games observe the documented "no sensor" result.
        return dx::VmValue::Ref(dx::VmObjectRef{});
    });
    registry.Register("android.sensor_manager.register",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(0);
    });
    registry.Register("android.sensor_manager.unregister",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.telephony.get_device_id",
                      [context](dx::IntrinsicContext& call) {
        return MakeString(call, context->device_id);
    });
    registry.Register("android.telephony.get_software_version",
                      [context](dx::IntrinsicContext& call) {
        return MakeString(call, context->device_software_version);
    });
    registry.Register("android.telephony.get_line1_number",
                      [context](dx::IntrinsicContext& call) {
        return MakeString(call, context->line_number);
    });
    registry.Register("android.telephony.get_network_operator",
                      [context](dx::IntrinsicContext& call) {
        return MakeString(call, context->network_operator);
    });
    registry.Register("android.locale.get_default",
                      [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(Singleton(call, context, "locale",
                                          "Ljava/util/Locale;"));
    });
    registry.Register("android.locale.get_iso3_language",
                      [context](dx::IntrinsicContext& call) {
        return MakeString(call, context->iso3_language);
    });
    registry.Register("android.locale.get_iso3_country",
                      [context](dx::IntrinsicContext& call) {
        return MakeString(call, context->iso3_country);
    });
    registry.Register("android.locale.get_country",
                      [context](dx::IntrinsicContext& call) {
        return MakeString(call, context->iso_country);
    });
    registry.Register("android.thread.sleep",
                      [context](dx::IntrinsicContext& call) {
        // Unified deterministic time: sleeping advances published uptime.
        context->uptime_millis += call.arguments[0].AsLong();
        return dx::VmValue::Void();
    });
    registry.Register("android.looper.noop", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.handler.init", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.handler.handle_message_noop",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.handler.obtain_message",
                      [](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(
            call.vm.NewIntrinsicInstance("Landroid/os/Message;"));
    });
    registry.Register("android.handler.send_message",
                      [](dx::IntrinsicContext& call) {
        // Single VM host thread: deliver synchronously through the
        // receiver's handleMessage override.
        auto& vm = call.vm;
        auto& linker = vm.Linker();
        const auto handler_class = vm.Model().ObjectClass(call.receiver);
        const auto index = linker.FindVtableIndex(
            handler_class, "handleMessage", "(Landroid/os/Message;)V");
        if (!index.has_value()) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                  "handler has no handleMessage"};
        }
        const auto outcome = vm.Call(
            linker.Class(handler_class).vtable[*index],
            std::vector<dx::VmValue>{dx::VmValue::Ref(call.receiver),
                                     call.arguments[0]});
        if (outcome.exception.IsValid()) {
            throw dx::VmJavaThrow{"Ljava/lang/RuntimeException;",
                                  "handleMessage raised: " +
                                      outcome.exception_message};
        }
        return dx::VmValue::Int(1);
    });
    registry.Register("android.thread.init_runnable",
                      [context](dx::IntrinsicContext& call) {
        context->java_threads[call.receiver.Value()] =
            DexVmAndroidContext::JavaThreadState{call.arguments[0].ref,
                                                 false, false};
        return dx::VmValue::Void();
    });
    registry.Register("android.thread.start",
                      [context](dx::IntrinsicContext& call) {
        const auto found =
            context->java_threads.find(call.receiver.Value());
        if (found == context->java_threads.end()) {
            throw dx::VmJavaThrow{
                "Ljava/lang/IllegalThreadStateException;",
                "thread has no runnable target"};
        }
        if (found->second.started) {
            throw dx::VmJavaThrow{
                "Ljava/lang/IllegalThreadStateException;",
                "thread started twice"};
        }
        found->second.started = true;
        context->java_thread_queue.push_back(call.receiver);
        return dx::VmValue::Void();
    });
    registry.Register("android.thread.join",
                      [context](dx::IntrinsicContext& call) {
        // Cooperative model: join runs the target to completion now.
        const auto error =
            RunJavaThreadNow(call.vm, *context, call.receiver);
        if (error.has_value()) {
            throw dx::VmJavaThrow{"Ljava/lang/RuntimeException;", *error};
        }
        return dx::VmValue::Void();
    });
    registry.Register("android.timer.init", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.timer.schedule",
                      [context](dx::IntrinsicContext& call) {
        // One-shot task on the cooperative queue; the delay collapses to
        // the next lifecycle frame boundary (deterministic clock).
        const auto task = call.arguments[0].ref;
        if (!task.IsValid()) {
            throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                  "scheduled TimerTask is null"};
        }
        context->java_threads[task.Value()] =
            DexVmAndroidContext::JavaThreadState{task, true, false};
        context->java_thread_queue.push_back(task);
        return dx::VmValue::Void();
    });
    registry.Register("android.timer.schedule_repeating",
                      [](dx::IntrinsicContext&) -> dx::VmValue {
        // Unbounded repetition cannot terminate under the cooperative
        // model; recorded gap, explicit failure.
        throw dx::VmJavaThrow{
            "Ljava/lang/UnsupportedOperationException;",
            "repeating Timer.schedule is not provided"};
    });
    registry.Register("android.timer.cancel",
                      [context](dx::IntrinsicContext&) {
        // Cancels everything still pending (per-timer task tracking is
        // not kept; a single installer timer is the observed use).
        context->java_thread_queue.clear();
        return dx::VmValue::Void();
    });
    registry.Register("android.thread.is_alive",
                      [context](dx::IntrinsicContext& call) {
        const auto found =
            context->java_threads.find(call.receiver.Value());
        const bool alive = found != context->java_threads.end() &&
                           found->second.started &&
                           !found->second.finished;
        return dx::VmValue::Int(alive ? 1 : 0);
    });
}

void RegisterAudioVideo(dx::IntrinsicRegistry& registry,
                        const Context& context) {
    registry.Register("android.sound_pool.init", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.sound_pool.load",
                      [context](dx::IntrinsicContext& call) {
        const auto resource = call.arguments[1].AsInt();
        auto& mixer = context->session->SoundPoolMixer();
        if (!mixer.Load(resource)) {
            GuestLog(call, core::LogLevel::warn,
                     "SoundPool.load failed for resource " +
                         std::to_string(resource));
            return dx::VmValue::Int(0);
        }
        return dx::VmValue::Int(resource);  // sound id == resource id
    });
    registry.Register("android.sound_pool.play",
                      [context](dx::IntrinsicContext& call) {
        const auto sound = call.arguments[0].AsInt();
        const auto volume = call.arguments[1].AsFloat();
        const auto loop = call.arguments[3].AsInt();
        auto& mixer = context->session->SoundPoolMixer();
        const auto stream = context->next_sound_stream++;
        if (!mixer.Play(audio::JavaSoundPoolKind::pool, sound, stream,
                        volume, loop != 0)) {
            return dx::VmValue::Int(0);
        }
        context->sound_streams[stream] = sound;
        return dx::VmValue::Int(stream);
    });
    const auto stream_call =
        [context](dx::IntrinsicContext& call,
                  const std::function<void(audio::JavaSoundPoolMixer&,
                                           std::int32_t, std::int32_t)>&
                      action) {
            const auto stream = call.arguments[0].AsInt();
            const auto found = context->sound_streams.find(stream);
            if (found != context->sound_streams.end()) {
                action(context->session->SoundPoolMixer(), found->second,
                       stream);
            }
            return dx::VmValue::Void();
        };
    registry.Register("android.sound_pool.pause",
                      [stream_call](dx::IntrinsicContext& call) {
        return stream_call(call, [](auto& mixer, const auto resource,
                                    const auto stream) {
            mixer.Pause(audio::JavaSoundPoolKind::pool, resource, stream);
        });
    });
    registry.Register("android.sound_pool.resume",
                      [stream_call](dx::IntrinsicContext& call) {
        return stream_call(call, [](auto& mixer, const auto resource,
                                    const auto stream) {
            mixer.Resume(audio::JavaSoundPoolKind::pool, resource, stream);
        });
    });
    registry.Register("android.sound_pool.stop",
                      [stream_call](dx::IntrinsicContext& call) {
        return stream_call(call, [](auto& mixer, const auto resource,
                                    const auto stream) {
            mixer.Stop(audio::JavaSoundPoolKind::pool, resource, stream);
        });
    });
    registry.Register("android.sound_pool.set_volume",
                      [context](dx::IntrinsicContext& call) {
        const auto stream = call.arguments[0].AsInt();
        const auto found = context->sound_streams.find(stream);
        if (found != context->sound_streams.end()) {
            context->session->SoundPoolMixer().SetVolume(
                audio::JavaSoundPoolKind::pool, found->second, stream,
                call.arguments[1].AsFloat());
        }
        return dx::VmValue::Void();
    });
    registry.Register("android.sound_pool.set_rate",
                      [context](dx::IntrinsicContext& call) {
        const auto stream = call.arguments[0].AsInt();
        const auto found = context->sound_streams.find(stream);
        if (found != context->sound_streams.end()) {
            context->session->SoundPoolMixer().SetPitch(
                audio::JavaSoundPoolKind::pool, found->second, stream,
                call.arguments[1].AsFloat());
        }
        return dx::VmValue::Void();
    });
    registry.Register("android.sound_pool.unload",
                      [context](dx::IntrinsicContext& call) {
        context->session->SoundPoolMixer().Unload(
            call.arguments[0].AsInt());
        return dx::VmValue::Int(1);
    });
    registry.Register("android.sound_pool.release",
                      [context](dx::IntrinsicContext&) {
        context->session->SoundPoolMixer().StopAllSounds();
        return dx::VmValue::Void();
    });

    registry.Register("android.media_player.init",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.media_player.set_data_source",
                      [context](dx::IntrinsicContext& call) {
        // Path-backed playback is not wired to the mixer yet: record the
        // gap loudly; start() on this instance will have no audio.
        GuestLog(call, core::LogLevel::warn,
                 "MediaPlayer.setDataSource is not wired to the mixer: " +
                     call.vm.StringUtf8(call.arguments[0].ref));
        return dx::VmValue::Void();
    });
    registry.Register("android.media_player.is_looping",
                      [context](dx::IntrinsicContext& call) {
        const auto found =
            context->media_looping.find(call.receiver.Value());
        return dx::VmValue::Int(
            found != context->media_looping.end() && found->second ? 1
                                                                   : 0);
    });
    registry.Register("android.media_player.create",
                      [context](dx::IntrinsicContext& call) {
        const auto resource = call.arguments[1].AsInt();
        const auto instance = call.vm.NewIntrinsicInstance(
            "Landroid/media/MediaPlayer;");
        if (!context->session->SoundPoolMixer().Load(resource)) {
            GuestLog(call, core::LogLevel::warn,
                     "MediaPlayer.create failed for resource " +
                         std::to_string(resource));
            return dx::VmValue::Ref(dx::VmObjectRef{});
        }
        context->media_resources[instance.Value()] = resource;
        context->media_playing[instance.Value()] = false;
        return dx::VmValue::Ref(instance);
    });
    const auto media_resource = [context](dx::IntrinsicContext& call)
        -> std::optional<std::int32_t> {
        const auto found =
            context->media_resources.find(call.receiver.Value());
        if (found == context->media_resources.end()) return std::nullopt;
        return found->second;
    };
    registry.Register("android.media_player.start",
                      [context, media_resource](dx::IntrinsicContext& call) {
        const auto resource = media_resource(call);
        if (resource.has_value()) {
            static_cast<void>(context->session->SoundPoolMixer().Play(
                audio::JavaSoundPoolKind::big, *resource, 0, 1.0F, true));
            context->media_playing[call.receiver.Value()] = true;
        }
        return dx::VmValue::Void();
    });
    registry.Register("android.media_player.pause",
                      [context, media_resource](dx::IntrinsicContext& call) {
        const auto resource = media_resource(call);
        if (resource.has_value()) {
            context->session->SoundPoolMixer().Pause(
                audio::JavaSoundPoolKind::big, *resource, 0);
            context->media_playing[call.receiver.Value()] = false;
        }
        return dx::VmValue::Void();
    });
    registry.Register("android.media_player.stop",
                      [context, media_resource](dx::IntrinsicContext& call) {
        const auto resource = media_resource(call);
        if (resource.has_value()) {
            context->session->SoundPoolMixer().Stop(
                audio::JavaSoundPoolKind::big, *resource, 0);
            context->media_playing[call.receiver.Value()] = false;
        }
        return dx::VmValue::Void();
    });
    registry.Register("android.media_player.release",
                      [context, media_resource](dx::IntrinsicContext& call) {
        const auto resource = media_resource(call);
        if (resource.has_value()) {
            context->session->SoundPoolMixer().Stop(
                audio::JavaSoundPoolKind::big, *resource, 0);
            context->session->SoundPoolMixer().Unload(*resource);
        }
        context->media_resources.erase(call.receiver.Value());
        context->media_playing.erase(call.receiver.Value());
        return dx::VmValue::Void();
    });
    registry.Register("android.media_player.is_playing",
                      [context](dx::IntrinsicContext& call) {
        const auto found =
            context->media_playing.find(call.receiver.Value());
        return dx::VmValue::Int(
            found != context->media_playing.end() && found->second ? 1 : 0);
    });
    registry.Register("android.media_player.prepare",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.media_player.seek_to",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.media_player.set_looping",
                      [context](dx::IntrinsicContext& call) {
        context->media_looping[call.receiver.Value()] =
            call.arguments[0].AsInt() != 0;
        return dx::VmValue::Void();
    });
    registry.Register("android.media_player.set_volume",
                      [context, media_resource](dx::IntrinsicContext& call) {
        const auto resource = media_resource(call);
        if (resource.has_value()) {
            context->session->SoundPoolMixer().SetVolume(
                audio::JavaSoundPoolKind::big, *resource, 0,
                call.arguments[0].AsFloat());
        }
        return dx::VmValue::Void();
    });
    registry.Register("android.media_player.set_completion_listener",
                      [](dx::IntrinsicContext&) {
        // Completion callbacks require the media clock; recorded gap.
        return dx::VmValue::Void();
    });
}

[[nodiscard]] std::unordered_map<std::string,
                                 DexVmAndroidContext::PreferenceValue>&
PreferencesOf(dx::IntrinsicContext& call, const Context& context) {
    const auto found = context->preference_names.find(call.receiver.Value());
    if (found == context->preference_names.end()) {
        throw dx::VmJavaThrow{
            "Ljava/lang/IllegalStateException;",
            "SharedPreferences instance has no backing store"};
    }
    return context->preferences[found->second];
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

void RegisterSharedPreferences(dx::IntrinsicRegistry& registry,
                               const Context& context) {
    registry.Register("android.context.get_shared_preferences",
                      [context](dx::IntrinsicContext& call) {
        const auto name = call.vm.StringUtf8(call.arguments[0].ref);
        const auto instance =
            Singleton(call, context, "prefs:" + name,
                      "Landroid/content/SharedPreferencesImpl;");
        context->preference_names[instance.Value()] = name;
        return dx::VmValue::Ref(instance);
    });
    registry.Register("android.prefs.edit",
                      [context](dx::IntrinsicContext& call) {
        const auto name =
            context->preference_names.at(call.receiver.Value());
        const auto editor =
            Singleton(call, context, "prefs_editor:" + name,
                      "Landroid/content/SharedPreferencesEditorImpl;");
        context->preference_names[editor.Value()] = name;
        return dx::VmValue::Ref(editor);
    });
    registry.Register("android.prefs.get_boolean",
                      [context](dx::IntrinsicContext& call) {
        const auto key = call.vm.StringUtf8(call.arguments[0].ref);
        const auto value = PreferenceValueOf<bool>(call, context, key);
        return dx::VmValue::Int(value.value_or(
            call.arguments[1].AsInt() != 0) ? 1 : 0);
    });
    registry.Register("android.prefs.get_int",
                      [context](dx::IntrinsicContext& call) {
        const auto key = call.vm.StringUtf8(call.arguments[0].ref);
        const auto value =
            PreferenceValueOf<std::int32_t>(call, context, key);
        return dx::VmValue::Int(
            value.value_or(call.arguments[1].AsInt()));
    });
    registry.Register("android.prefs.get_long",
                      [context](dx::IntrinsicContext& call) {
        const auto key = call.vm.StringUtf8(call.arguments[0].ref);
        const auto value =
            PreferenceValueOf<std::int64_t>(call, context, key);
        return dx::VmValue::Long(
            value.value_or(call.arguments[1].AsLong()));
    });
    registry.Register("android.prefs.get_string",
                      [context](dx::IntrinsicContext& call) {
        const auto key = call.vm.StringUtf8(call.arguments[0].ref);
        const auto value =
            PreferenceValueOf<std::string>(call, context, key);
        if (!value.has_value()) {
            return dx::VmValue::Ref(call.arguments[1].ref);
        }
        return MakeString(call, *value);
    });
    // v1 write semantics: edits apply immediately and commit() truthfully
    // reports the store accepted them (session lifetime, like
    // memory_files); no staged-rollback behaviour is claimed.
    registry.Register("android.prefs_editor.put_boolean",
                      [context](dx::IntrinsicContext& call) {
        PreferencesOf(call, context)
            [call.vm.StringUtf8(call.arguments[0].ref)] =
                call.arguments[1].AsInt() != 0;
        return Self(call);
    });
    registry.Register("android.prefs_editor.put_int",
                      [context](dx::IntrinsicContext& call) {
        PreferencesOf(call, context)
            [call.vm.StringUtf8(call.arguments[0].ref)] =
                call.arguments[1].AsInt();
        return Self(call);
    });
    registry.Register("android.prefs_editor.put_long",
                      [context](dx::IntrinsicContext& call) {
        PreferencesOf(call, context)
            [call.vm.StringUtf8(call.arguments[0].ref)] =
                call.arguments[1].AsLong();
        return Self(call);
    });
    registry.Register("android.prefs_editor.put_string",
                      [context](dx::IntrinsicContext& call) {
        PreferencesOf(call, context)
            [call.vm.StringUtf8(call.arguments[0].ref)] =
                call.vm.StringUtf8(call.arguments[1].ref);
        return Self(call);
    });
    registry.Register("android.prefs_editor.commit",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(1);
    });
}

[[nodiscard]] DexVmAndroidContext::BitmapState& BitmapOf(
    dx::IntrinsicContext& call, const Context& context) {
    const auto found = context->bitmaps.find(call.receiver.Value());
    if (found == context->bitmaps.end() || found->second.recycled) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                              "bitmap is recycled or was never created"};
    }
    return found->second;
}

// Builds a Bitmap intrinsic instance from pixels copied out of a guest int
// array (offset/stride window per the createBitmap contract).
[[nodiscard]] dx::VmValue MakeBitmapFromArray(
    dx::IntrinsicContext& call, const Context& context,
    const dx::VmObjectRef array, const std::int32_t offset,
    const std::int32_t stride, const std::int32_t width,
    const std::int32_t height) {
    auto& model = call.vm.Model();
    if (!array.IsValid()) {
        throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                              "createBitmap colors array is null"};
    }
    if (width <= 0 || height <= 0 || stride < width) {
        throw dx::VmJavaThrow{
            "Ljava/lang/IllegalArgumentException;",
            "createBitmap dimensions are invalid: " + std::to_string(width) +
                "x" + std::to_string(height) + " stride " +
                std::to_string(stride)};
    }
    const auto length = static_cast<std::int64_t>(model.ArrayLength(array));
    const auto last = static_cast<std::int64_t>(offset) +
                      static_cast<std::int64_t>(stride) * (height - 1) +
                      width;
    if (offset < 0 || last > length) {
        throw dx::VmJavaThrow{
            "Ljava/lang/ArrayIndexOutOfBoundsException;",
            "createBitmap window exceeds the colors array"};
    }
    DexVmAndroidContext::BitmapState state;
    state.width = width;
    state.height = height;
    state.argb.resize(static_cast<std::size_t>(width) *
                      static_cast<std::size_t>(height));
    for (std::int32_t row = 0; row < height; ++row) {
        for (std::int32_t column = 0; column < width; ++column) {
            state.argb[static_cast<std::size_t>(row) *
                           static_cast<std::size_t>(width) +
                       static_cast<std::size_t>(column)] =
                static_cast<std::uint32_t>(model.GetPrimitiveElement(
                    array, offset + row * stride + column));
        }
    }
    const auto instance =
        call.vm.NewIntrinsicInstance("Landroid/graphics/Bitmap;");
    context->bitmaps[instance.Value()] = std::move(state);
    return dx::VmValue::Ref(instance);
}

void RegisterGraphicsBitmaps(dx::IntrinsicRegistry& registry,
                             const Context& context) {
    registry.Register("android.bitmap_config.clinit",
                      [](dx::IntrinsicContext& call) {
        auto& vm = call.vm;
        for (const char* name : {"ARGB_4444", "ARGB_8888"}) {
            vm.SetIntrinsicStaticRef(
                "Landroid/graphics/Bitmap$Config;", name,
                "Landroid/graphics/Bitmap$Config;",
                vm.NewIntrinsicInstance("Landroid/graphics/Bitmap$Config;"));
        }
        return dx::VmValue::Void();
    });
    registry.Register("android.region_op.clinit",
                      [](dx::IntrinsicContext& call) {
        auto& vm = call.vm;
        vm.SetIntrinsicStaticRef(
            "Landroid/graphics/Region$Op;", "REPLACE",
            "Landroid/graphics/Region$Op;",
            vm.NewIntrinsicInstance("Landroid/graphics/Region$Op;"));
        return dx::VmValue::Void();
    });
    registry.Register("android.bitmap_factory.decode_byte_array",
                      [context](dx::IntrinsicContext& call) {
        auto& model = call.vm.Model();
        const auto array = call.arguments[0].ref;
        const auto offset = call.arguments[1].AsInt();
        const auto length = call.arguments[2].AsInt();
        if (!array.IsValid()) {
            throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                  "decodeByteArray data is null"};
        }
        const auto array_length =
            static_cast<std::int64_t>(model.ArrayLength(array));
        if (offset < 0 || length < 0 ||
            static_cast<std::int64_t>(offset) + length > array_length) {
            throw dx::VmJavaThrow{
                "Ljava/lang/ArrayIndexOutOfBoundsException;",
                "decodeByteArray range exceeds the data array"};
        }
        const auto bytes = model.ReadByteRegion(array, offset, length);
        const auto decoded = DecodeImageToArgb(bytes);
        if (!decoded.has_value()) {
            // Documented decode-failure result is null, not a throw.
            GuestLog(call, core::LogLevel::warn,
                     "BitmapFactory.decodeByteArray: undecodable image (" +
                         std::to_string(length) + " bytes)");
            return dx::VmValue::Ref(dx::VmObjectRef{});
        }
        DexVmAndroidContext::BitmapState state;
        state.width = decoded->width;
        state.height = decoded->height;
        state.argb = std::move(decoded->argb);
        const auto instance =
            call.vm.NewIntrinsicInstance("Landroid/graphics/Bitmap;");
        context->bitmaps[instance.Value()] = std::move(state);
        return dx::VmValue::Ref(instance);
    });
    registry.Register("android.bitmap.create",
                      [context](dx::IntrinsicContext& call) {
        const auto array = call.arguments[0].ref;
        const auto width = call.arguments[1].AsInt();
        const auto height = call.arguments[2].AsInt();
        return MakeBitmapFromArray(call, context, array, 0, width, width,
                                   height);
    });
    registry.Register("android.bitmap.create_offset",
                      [context](dx::IntrinsicContext& call) {
        const auto array = call.arguments[0].ref;
        const auto offset = call.arguments[1].AsInt();
        const auto stride = call.arguments[2].AsInt();
        const auto width = call.arguments[3].AsInt();
        const auto height = call.arguments[4].AsInt();
        return MakeBitmapFromArray(call, context, array, offset, stride,
                                   width, height);
    });
    registry.Register("android.bitmap.get_width",
                      [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(BitmapOf(call, context).width);
    });
    registry.Register("android.bitmap.get_height",
                      [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(BitmapOf(call, context).height);
    });
    registry.Register("android.bitmap.get_pixels",
                      [context](dx::IntrinsicContext& call) {
        auto& model = call.vm.Model();
        const auto& state = BitmapOf(call, context);
        const auto array = call.arguments[0].ref;
        const auto offset = call.arguments[1].AsInt();
        const auto stride = call.arguments[2].AsInt();
        const auto x = call.arguments[3].AsInt();
        const auto y = call.arguments[4].AsInt();
        const auto width = call.arguments[5].AsInt();
        const auto height = call.arguments[6].AsInt();
        if (!array.IsValid()) {
            throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                  "getPixels target array is null"};
        }
        if (x < 0 || y < 0 || width < 0 || height < 0 ||
            x + width > state.width || y + height > state.height) {
            throw dx::VmJavaThrow{
                "Ljava/lang/IllegalArgumentException;",
                "getPixels region exceeds the bitmap"};
        }
        if (width == 0 || height == 0) return dx::VmValue::Void();
        const auto length =
            static_cast<std::int64_t>(model.ArrayLength(array));
        const auto last = static_cast<std::int64_t>(offset) +
                          static_cast<std::int64_t>(stride) * (height - 1) +
                          width;
        if (offset < 0 || stride < width || last > length) {
            throw dx::VmJavaThrow{
                "Ljava/lang/ArrayIndexOutOfBoundsException;",
                "getPixels window exceeds the target array"};
        }
        for (std::int32_t row = 0; row < height; ++row) {
            for (std::int32_t column = 0; column < width; ++column) {
                const auto pixel =
                    state.argb[static_cast<std::size_t>(y + row) *
                                   static_cast<std::size_t>(state.width) +
                               static_cast<std::size_t>(x + column)];
                model.SetPrimitiveElement(array,
                                          offset + row * stride + column,
                                          pixel);
            }
        }
        return dx::VmValue::Void();
    });
    registry.Register("android.bitmap.recycle",
                      [context](dx::IntrinsicContext& call) {
        const auto found = context->bitmaps.find(call.receiver.Value());
        if (found != context->bitmaps.end()) {
            found->second.recycled = true;
            found->second.argb.clear();
            found->second.argb.shrink_to_fit();
        }
        return dx::VmValue::Void();
    });
    registry.Register("android.canvas.save", [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(1);
    });
    registry.Register("android.canvas.clip_rect",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(1);
    });
    registry.Register("android.canvas.get_clip_bounds",
                      [context](dx::IntrinsicContext& call) {
        const auto rect =
            call.vm.NewIntrinsicInstance("Landroid/graphics/Rect;");
        const auto slots = call.vm.Model().InstanceSlots(rect);
        slots[0] = {0, dx::SlotTag::cat1};
        slots[1] = {0, dx::SlotTag::cat1};
        slots[2] = {context->surface_width, dx::SlotTag::cat1};
        slots[3] = {context->surface_height, dx::SlotTag::cat1};
        return dx::VmValue::Ref(rect);
    });
}

void RegisterWidgets(dx::IntrinsicRegistry& registry,
                     const Context& context) {
    registry.Register("android.widget.noop", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.widget.zero", [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(0);
    });
    registry.Register("android.widget.null", [](dx::IntrinsicContext&) {
        return dx::VmValue::Ref(dx::VmObjectRef{});
    });
    registry.Register("android.widget.self", [](dx::IntrinsicContext& call) {
        return Self(call);
    });
    // TextView text is real state in the interpreter's builder buffer so
    // interpreted logic round-trips what it stored.
    registry.Register("android.textview.set_text",
                      [](dx::IntrinsicContext& call) {
        auto& buffer = call.vm.BuilderBuffer(call.receiver);
        const auto value = call.arguments[0].ref;
        buffer = value.IsValid()
                     ? call.vm.Model().StringValue(value)
                     : std::u16string();
        return dx::VmValue::Void();
    });
    registry.Register("android.textview.get_text",
                      [](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(
            call.vm.Model().NewString(call.vm.BuilderBuffer(call.receiver)));
    });
    registry.Register("android.textview.get_paint",
                      [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(
            Singleton(call, context, "text_paint",
                      "Landroid/text/TextPaint;"));
    });
    registry.Register("android.edittext.get_editable",
                      [context](dx::IntrinsicContext& call) {
        const auto key =
            "editable:" + std::to_string(call.receiver.Value());
        const auto editable = Singleton(call, context, key,
                                        "Landroid/text/EditableImpl;");
        context->editable_owner[editable.Value()] = call.receiver.Value();
        return dx::VmValue::Ref(editable);
    });
    const auto owner_buffer =
        [context](dx::IntrinsicContext& call) -> std::u16string& {
        const auto found =
            context->editable_owner.find(call.receiver.Value());
        if (found == context->editable_owner.end()) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                  "Editable has no owning text widget"};
        }
        return call.vm.BuilderBuffer(dx::VmObjectRef(found->second));
    };
    registry.Register("android.editable.clear",
                      [owner_buffer](dx::IntrinsicContext& call) {
        owner_buffer(call).clear();
        return dx::VmValue::Void();
    });
    registry.Register("android.editable.length",
                      [owner_buffer](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(
            static_cast<std::int32_t>(owner_buffer(call).size()));
    });
    registry.Register("android.editable.replace",
                      [owner_buffer](dx::IntrinsicContext& call) {
        auto& buffer = owner_buffer(call);
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
    registry.Register("android.paint.get_text_bounds",
                      [](dx::IntrinsicContext& call) {
        // No font engine exists; the v1 metric is a deterministic
        // monospace estimate (8x16 px per glyph) so layout math stays
        // finite and consistent.
        const auto start = call.arguments[1].AsInt();
        const auto end = call.arguments[2].AsInt();
        const auto rect = call.arguments[3].ref;
        if (!rect.IsValid()) {
            throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                  "getTextBounds bounds rect is null"};
        }
        const auto count = end > start ? end - start : 0;
        const auto slots = call.vm.Model().InstanceSlots(rect);
        slots[0] = {0, dx::SlotTag::cat1};
        slots[1] = {static_cast<std::uint32_t>(-16), dx::SlotTag::cat1};
        slots[2] = {static_cast<std::uint32_t>(count * 8),
                    dx::SlotTag::cat1};
        slots[3] = {0, dx::SlotTag::cat1};
        return dx::VmValue::Void();
    });
    registry.Register("android.dialog.create",
                      [](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(
            call.vm.NewIntrinsicInstance("Landroid/app/AlertDialog;"));
    });
    registry.Register("android.videoview.unsupported",
                      [](dx::IntrinsicContext& call) {
        // Recorded gap: video playback is not provided; position and
        // duration answer zero so skip paths trigger immediately.
        GuestLog(call, core::LogLevel::warn,
                 "VideoView playback is not provided on this platform");
        return dx::VmValue::Void();
    });
    registry.Register("android.videoview.set_completion",
                      [context](dx::IntrinsicContext& call) {
        context->video_completion[call.receiver.Value()] =
            call.arguments[0].ref;
        return dx::VmValue::Void();
    });
    // Playback is not provided, so start() reports completion right away
    // through the registered listener; splash-video activities then advance
    // exactly as they would after a real playback finished.
    registry.Register("android.videoview.start",
                      [context](dx::IntrinsicContext& call) {
        GuestLog(call, core::LogLevel::warn,
                 "VideoView playback is not provided; reporting immediate "
                 "completion");
        const auto found =
            context->video_completion.find(call.receiver.Value());
        if (found == context->video_completion.end() ||
            !found->second.IsValid()) {
            return dx::VmValue::Void();
        }
        auto& vm = call.vm;
        auto& linker = vm.Linker();
        const auto listener_class = vm.Model().ObjectClass(found->second);
        const auto index = linker.FindVtableIndex(
            listener_class, "onCompletion",
            "(Landroid/media/MediaPlayer;)V");
        if (!index.has_value()) {
            throw dx::VmJavaThrow{
                "Ljava/lang/IllegalStateException;",
                "completion listener has no onCompletion method"};
        }
        const auto player =
            vm.NewIntrinsicInstance("Landroid/media/MediaPlayer;");
        const auto outcome = vm.Call(
            linker.Class(listener_class).vtable[*index],
            std::vector<dx::VmValue>{dx::VmValue::Ref(found->second),
                                     dx::VmValue::Ref(player)});
        if (outcome.exception.IsValid()) {
            throw dx::VmJavaThrow{"Ljava/lang/RuntimeException;",
                                  "onCompletion raised: " +
                                      outcome.exception_message};
        }
        return dx::VmValue::Void();
    });
    registry.Register("android.webview.load_url",
                      [](dx::IntrinsicContext& call) {
        GuestLog(call, core::LogLevel::warn,
                 "WebView.loadUrl dropped (web content is a non-goal): " +
                     call.vm.StringUtf8(call.arguments[0].ref));
        return dx::VmValue::Void();
    });
    registry.Register("android.webview.get_settings",
                      [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(
            Singleton(call, context, "web_settings",
                      "Landroid/webkit/WebSettings;"));
    });
    // System settings table shares the session-lifetime preference store.
    registry.Register("android.settings.get_int",
                      [context](dx::IntrinsicContext& call) {
        const auto key = call.vm.StringUtf8(call.arguments[1].ref);
        auto& store = context->preferences["__android.settings.system"];
        const auto found = store.find(key);
        if (found != store.end()) {
            if (const auto* value = std::get_if<std::int32_t>(
                    &found->second)) {
                return dx::VmValue::Int(*value);
            }
        }
        return dx::VmValue::Int(call.arguments[2].AsInt());
    });
    registry.Register("android.settings.put_int",
                      [context](dx::IntrinsicContext& call) {
        const auto key = call.vm.StringUtf8(call.arguments[1].ref);
        context->preferences["__android.settings.system"][key] =
            call.arguments[2].AsInt();
        return dx::VmValue::Int(1);
    });
    registry.Register("android.ssl.context_instance",
                      [](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(
            call.vm.NewIntrinsicInstance("Ljavax/net/ssl/SSLContext;"));
    });
    registry.Register("android.ssl.socket_factory",
                      [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(
            Singleton(call, context, "ssl_socket_factory",
                      "Ljavax/net/ssl/SSLSocketFactory;"));
    });
    registry.Register("android.scale_type.clinit",
                      [](dx::IntrinsicContext& call) {
        call.vm.SetIntrinsicStaticRef(
            "Landroid/widget/ImageView$ScaleType;", "CENTER",
            "Landroid/widget/ImageView$ScaleType;",
            call.vm.NewIntrinsicInstance(
                "Landroid/widget/ImageView$ScaleType;"));
        return dx::VmValue::Void();
    });
    registry.Register("android.network_state.clinit",
                      [](dx::IntrinsicContext& call) {
        call.vm.SetIntrinsicStaticRef(
            "Landroid/net/NetworkInfo$State;", "CONNECTED",
            "Landroid/net/NetworkInfo$State;",
            call.vm.NewIntrinsicInstance(
                "Landroid/net/NetworkInfo$State;"));
        return dx::VmValue::Void();
    });
}

void RegisterMisc(dx::IntrinsicRegistry& registry, const Context& context) {
    registry.Register("android.bundle.init", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.bundle.get", [](dx::IntrinsicContext&) {
        return dx::VmValue::Ref(dx::VmObjectRef{});
    });
    // Bundles carry no entries on this platform (no saved instance state,
    // no cross-process extras), so typed getters answer the documented
    // absent-key defaults.
    registry.Register("android.bundle.get_int", [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(0);
    });
    registry.Register("android.bundle.get_string",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Ref(dx::VmObjectRef{});
    });
    registry.Register("android.receiver.init", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.receiver.on_receive_noop",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.intent_filter.init",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.intent_filter.init_empty",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.intent_filter.add_action",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.intent.init", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.intent.init_component",
                      [context](dx::IntrinsicContext& call) {
        const auto class_object = call.arguments[1].ref;
        const auto target =
            call.vm.Model().ClassOfClassObject(class_object);
        context->intent_components[call.receiver.Value()] =
            call.vm.Linker().Class(target).descriptor;
        return dx::VmValue::Void();
    });
    registry.Register("android.intent.set_class_name",
                      [context](dx::IntrinsicContext& call) {
        auto dotted = call.vm.StringUtf8(call.arguments[1].ref);
        std::string descriptor = "L";
        for (const auto unit : dotted) {
            descriptor.push_back(unit == '.' ? '/' : unit);
        }
        descriptor.push_back(';');
        context->intent_components[call.receiver.Value()] =
            std::move(descriptor);
        return Self(call);
    });
    registry.Register("android.intent.put_extra_int",
                      [context](dx::IntrinsicContext& call) {
        context->intent_int_extras[call.receiver.Value()]
            [call.vm.StringUtf8(call.arguments[0].ref)] =
                call.arguments[1].AsInt();
        return Self(call);
    });
    registry.Register("android.intent.put_extra_string",
                      [context](dx::IntrinsicContext& call) {
        context->intent_string_extras[call.receiver.Value()]
            [call.vm.StringUtf8(call.arguments[0].ref)] =
                call.vm.StringUtf8(call.arguments[1].ref);
        return Self(call);
    });
    registry.Register("android.intent.get_string_extra",
                      [context](dx::IntrinsicContext& call) {
        const auto extras =
            context->intent_string_extras.find(call.receiver.Value());
        if (extras != context->intent_string_extras.end()) {
            const auto found = extras->second.find(
                call.vm.StringUtf8(call.arguments[0].ref));
            if (found != extras->second.end()) {
                return MakeString(call, found->second);
            }
        }
        return dx::VmValue::Ref(dx::VmObjectRef{});
    });
    registry.Register("android.intent.get_int_extra",
                      [context](dx::IntrinsicContext& call) {
        const auto extras =
            context->intent_int_extras.find(call.receiver.Value());
        if (extras != context->intent_int_extras.end()) {
            const auto found = extras->second.find(
                call.vm.StringUtf8(call.arguments[0].ref));
            if (found != extras->second.end()) {
                return dx::VmValue::Int(found->second);
            }
        }
        return dx::VmValue::Int(call.arguments[1].AsInt());
    });
    registry.Register("android.intent.get_action",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Ref(dx::VmObjectRef{});
    });
    registry.Register("android.intent.get_extras",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Ref(dx::VmObjectRef{});
    });
    registry.Register("android.intent.set_flags",
                      [](dx::IntrinsicContext& call) {
        return Self(call);
    });
    registry.Register("android.intent.set_data_and_type",
                      [](dx::IntrinsicContext& call) {
        return Self(call);
    });
    registry.Register("android.pending_intent.get_broadcast",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Ref(dx::VmObjectRef{});
    });
    registry.Register("android.uri.parse", [](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(
            call.vm.NewIntrinsicInstance("Landroid/net/Uri;"));
    });
    registry.Register("android.toast.make_text",
                      [](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(
            call.vm.NewIntrinsicInstance("Landroid/widget/Toast;"));
    });
    registry.Register("android.toast.show", [](dx::IntrinsicContext& call) {
        GuestLog(call, core::LogLevel::info, "Toast.show()");
        return dx::VmValue::Void();
    });
    registry.Register("android.sms.get_default",
                      [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(Singleton(
            call, context, "sms", "Landroid/telephony/SmsManager;"));
    });
    for (const auto* blocked :
         {"android.sms.send_text", "android.sms.create_from_pdu",
          "android.sms.get_message_body",
          "android.sms.get_originating_address",
          "android.net.unsupported"}) {
        registry.Register(blocked, [](dx::IntrinsicContext&) -> dx::VmValue {
            throw dx::VmJavaThrow{
                "Ljava/lang/UnsupportedOperationException;",
                "SMS/network actions are outside the compatibility scope"};
        });
    }

    // Motion events read their slots directly.
    registry.Register("android.motion_event.get_action",
                      [](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(static_cast<std::int32_t>(
            call.vm.Model().InstanceSlots(call.receiver)[0].bits));
    });
    const auto slot_float = [](dx::IntrinsicContext& call,
                               const std::size_t slot) {
        dx::VmValue value;
        value.kind = dx::VmValue::Kind::cat1;
        value.cat1 = call.vm.Model().InstanceSlots(call.receiver)[slot].bits;
        return value;
    };
    registry.Register("android.motion_event.get_x",
                      [slot_float](dx::IntrinsicContext& call) {
        return slot_float(call, 1);
    });
    registry.Register("android.motion_event.get_y",
                      [slot_float](dx::IntrinsicContext& call) {
        return slot_float(call, 2);
    });
    registry.Register("android.motion_event.get_x_indexed",
                      [slot_float](dx::IntrinsicContext& call) {
        return slot_float(call, 1);
    });
    registry.Register("android.motion_event.get_y_indexed",
                      [slot_float](dx::IntrinsicContext& call) {
        return slot_float(call, 2);
    });
    registry.Register("android.motion_event.get_pointer_count",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(1);
    });
    registry.Register("android.motion_event.get_pointer_id",
                      [](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(static_cast<std::int32_t>(
            call.vm.Model().InstanceSlots(call.receiver)[3].bits));
    });

    // Platform System handlers (declared by the core catalog).
    registry.Register("platform.system.current_time_millis",
                      [context](dx::IntrinsicContext&) {
        // Deterministic epoch base plus lifecycle-published uptime.
        return dx::VmValue::Long(1'400'000'000'000LL +
                                 context->uptime_millis.load());
    });
    // java.util.Date over the same deterministic platform clock.
    registry.Register("platform.date.init",
                      [context](dx::IntrinsicContext& call) {
        const auto millis =
            1'400'000'000'000LL + context->uptime_millis.load();
        const auto slots = call.vm.Model().InstanceSlots(call.receiver);
        slots[0] = {static_cast<std::uint32_t>(millis & 0xffffffffULL),
                    dx::SlotTag::wide_lo};
        slots[1] = {static_cast<std::uint32_t>(
                        static_cast<std::uint64_t>(millis) >> 32U),
                    dx::SlotTag::wide_hi};
        return dx::VmValue::Void();
    });
    const auto date_millis = [](dx::IntrinsicContext& call) {
        const auto slots = call.vm.Model().InstanceSlots(call.receiver);
        return static_cast<std::int64_t>(
            (static_cast<std::uint64_t>(slots[1].bits) << 32U) |
            slots[0].bits);
    };
    registry.Register("platform.date.get_time",
                      [date_millis](dx::IntrinsicContext& call) {
        return dx::VmValue::Long(date_millis(call));
    });
    registry.Register("platform.date.get_year",
                      [date_millis](dx::IntrinsicContext& call) {
        using days = std::chrono::days;
        const auto time_point =
            std::chrono::sys_days(std::chrono::January / 1 / 1970) +
            std::chrono::milliseconds(date_millis(call));
        const std::chrono::year_month_day date(
            std::chrono::floor<days>(time_point));
        // Date.getYear is 1900-based.
        return dx::VmValue::Int(static_cast<std::int32_t>(
                                    static_cast<int>(date.year())) -
                                1900);
    });
    registry.Register("platform.system.nano_time",
                      [context](dx::IntrinsicContext&) {
        return dx::VmValue::Long(context->uptime_millis.load() * 1'000'000LL);
    });
    registry.Register("platform.system.load_library",
                      [](dx::IntrinsicContext& call) {
        // Libraries are preloaded and initialized by the session
        // (04 §2 step 4); the name is recorded for diagnostics.
        GuestLog(call, core::LogLevel::info,
                 "System.loadLibrary(" +
                     call.vm.StringUtf8(call.arguments[0].ref) + ")");
        return dx::VmValue::Void();
    });
    registry.Register("platform.system.exit",
                      [context](dx::IntrinsicContext&) {
        context->exit_requested = true;
        return dx::VmValue::Void();
    });
}

}  // namespace

void RegisterAndroidBuiltins(dx::IntrinsicRegistry& registry,
                             const std::shared_ptr<DexVmAndroidContext>
                                 context) {
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
    RegisterMisc(registry, context);
}

std::optional<std::string> PumpJavaThreads(dx::Interpreter& vm,
                                           DexVmAndroidContext& context) {
    // run() bodies may start further threads; drain until stable.
    while (!context.java_thread_queue.empty()) {
        const auto thread = context.java_thread_queue.front();
        context.java_thread_queue.erase(context.java_thread_queue.begin());
        const auto error = RunJavaThreadNow(vm, context, thread);
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
