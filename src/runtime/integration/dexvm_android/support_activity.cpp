// Context/Activity/Window/View/Resources handlers. setContentView(int)
// inflates the real binary XML layout into the view registry that
// findViewById answers from; presentation-only calls stay no-ops.

#include <algorithm>

#include "ogplay/loader/binary_xml.h"
#include "ogplay/runtime/integration/host_image_decode.h"

#include "shared.h"

namespace ogplay::runtime::android_intrinsics {

void PopulateContextActivity(AndroidHandlers& handlers,
                             const Context& context) {
  handlers.handler_android_context_init = dx::IntrinsicHandler([](dx::IntrinsicContext &) { return dx::VmValue::Void(); });
  handlers.handler_android_activity_init = dx::IntrinsicHandler([](dx::IntrinsicContext &) { return dx::VmValue::Void(); });
    handlers.handler_android_activity_lifecycle_noop = dx::IntrinsicHandler([](dx::IntrinsicContext &) { return dx::VmValue::Void(); });
  handlers.handler_android_activity_get_window = dx::IntrinsicHandler([context](dx::IntrinsicContext &call) {
        return dx::VmValue::Ref(
            Singleton(call, context, "window", "Landroid/view/Window;"));
    });
    handlers.handler_android_activity_request_window_feature = dx::IntrinsicHandler([](dx::IntrinsicContext &) { return dx::VmValue::Int(1); });
    handlers.handler_android_activity_set_content_view = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        context->content_view = call.arguments[0].ref;
        return dx::VmValue::Void();
    });
    // Minimal layout inflation: binary XML tags become widget intrinsic
    // instances and android:id entries feed findViewById. Layout geometry
    // attributes are not applied (the widget layer holds state only).
  handlers.handler_android_activity_set_content_view_id = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        const auto layout_id =
            static_cast<std::uint32_t>(call.arguments[0].AsInt());
        const auto* entry = context->arsc.FindById(layout_id);
        if (entry == nullptr || !entry->string_value.has_value()) {
          throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
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
        context->widget_states.clear();
        context->layout_views.clear();
        dx::VmObjectRef root;
        const auto elements = loader::ParseBinaryXmlElements(bytes);
        // Element index -> layout_views index; skipped elements (merge,
        // unknown tags) map to their own parent so children re-attach to
        // the nearest instantiated ancestor.
        std::vector<std::int32_t> fact_of(elements.size(), -1);
        for (std::size_t index = 0; index < elements.size(); ++index) {
            const auto& element = elements[index];
            const auto parent_fact =
              element.parent < 0
                  ? -1
                  : fact_of[static_cast<std::size_t>(element.parent)];
          if (element.name == "merge")
            continue; // container marker
            const auto found = kTagDescriptors.find(element.name);
            if (found == kTagDescriptors.end()) {
                GuestLog(call, core::LogLevel::warn,
                     "layout tag has no widget intrinsic: " + element.name);
                fact_of[index] = parent_fact;
                continue;
            }
          const auto instance = call.vm.NewIntrinsicInstance(found->second);
          if (!root.IsValid())
            root = instance;
            if (element.id != 0) {
                context->view_registry[element.id] = instance;
            }
            DexVmAndroidContext::LayoutViewFact fact;
            fact.view = instance;
            fact.parent = parent_fact;
            fact.tag = element.name;
            fact.layout_width = element.layout_width;
            fact.layout_height = element.layout_height;
            fact.gravity = element.gravity;
            fact.layout_gravity = element.layout_gravity;
            fact.padding_top = element.padding_top;
            // wrap_content image widgets measure by their drawable; a
            // missing or undecodable drawable leaves the size unknown
            // (bounds stay underivable, a recorded gap).
            if (element.src != 0) {
                const auto* drawable = context->arsc.FindById(element.src);
            if (drawable != nullptr && drawable->string_value.has_value()) {
                    try {
                        const auto image = DecodeImageToArgb(
                            ReadApkFile(context, *drawable->string_value));
                        if (image.has_value()) {
                            fact.measured_width = image->width;
                            fact.measured_height = image->height;
                        }
                    } catch (const dx::VmJavaThrow&) {
                        // unreadable entry: size stays unknown
                    }
                }
            }
            fact_of[index] =
                static_cast<std::int32_t>(context->layout_views.size());
            context->layout_views.push_back(std::move(fact));
        }
        if (!root.IsValid()) {
          throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
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
  handlers.handler_android_activity_run_on_ui_thread = dx::IntrinsicHandler([](dx::IntrinsicContext &call) {
        const auto runnable = call.arguments[0].ref;
        if (!runnable.IsValid()) {
            throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                  "runOnUiThread action is null"};
        }
        auto& vm = call.vm;
        auto& linker = vm.Linker();
        const auto runnable_class = vm.Model().ObjectClass(runnable);
        const auto index = linker.FindVtableIndex(runnable_class, "run", "()V");
        if (!index.has_value()) {
          throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
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
  handlers.handler_android_activity_find_view_by_id = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        const auto found = context->view_registry.find(
            static_cast<std::uint32_t>(call.arguments[0].AsInt()));
        if (found == context->view_registry.end()) {
            // Absent id: null is the documented answer.
            return dx::VmValue::Ref(dx::VmObjectRef{});
        }
        return dx::VmValue::Ref(found->second);
    });
    handlers.handler_android_activity_set_volume_control_stream = dx::IntrinsicHandler([](dx::IntrinsicContext &) { return dx::VmValue::Void(); });
    handlers.handler_android_activity_on_key_false = dx::IntrinsicHandler([](dx::IntrinsicContext &) { return dx::VmValue::Int(0); });
    handlers.handler_android_activity_on_touch_false = dx::IntrinsicHandler([](dx::IntrinsicContext &) { return dx::VmValue::Int(0); });
    handlers.handler_android_activity_finish = dx::IntrinsicHandler([context](dx::IntrinsicContext&) {
        context->exit_requested = true;
        return dx::VmValue::Void();
    });
    handlers.handler_android_context_get_package_name = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        return MakeString(call, context->package_name);
    });
  handlers.handler_android_context_get_resources = dx::IntrinsicHandler([context](dx::IntrinsicContext &call) {
        return dx::VmValue::Ref(Singleton(call, context, "resources",
            "Landroid/content/res/Resources;"));
    });
  handlers.handler_android_context_get_assets = dx::IntrinsicHandler([context](dx::IntrinsicContext &call) {
        return dx::VmValue::Ref(Singleton(
            call, context, "assets", "Landroid/content/res/AssetManager;"));
    });
  handlers.handler_android_context_get_system_service = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        const auto name = call.vm.StringUtf8(call.arguments[0].ref);
        if (name == "phone") {
            return dx::VmValue::Ref(Singleton(
              call, context, "phone", "Landroid/telephony/TelephonyManager;"));
        }
        if (name == "audio") {
          return dx::VmValue::Ref(Singleton(call, context, "audio",
                                            "Landroid/media/AudioManager;"));
        }
        if (name == "wifi") {
          return dx::VmValue::Ref(Singleton(call, context, "wifi",
                                            "Landroid/net/wifi/WifiManager;"));
        }
        if (name == "sensor") {
            return dx::VmValue::Ref(Singleton(
              call, context, "sensor", "Landroid/hardware/SensorManager;"));
        }
        if (name == "connectivity") {
          return dx::VmValue::Ref(
              Singleton(call, context, "connectivity",
                "Landroid/net/ConnectivityManager;"));
        }
        if (name == "input_method") {
          return dx::VmValue::Ref(
              Singleton(call, context, "input_method",
                "Landroid/view/inputmethod/InputMethodManager;"));
        }
        if (name == "window") {
          return dx::VmValue::Ref(
              Singleton(call, context, "window_manager",
                "Landroid/view/WindowManagerImpl;"));
        }
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                              "system service is not provided: " + name};
    });
    handlers.handler_android_activity_get_window_manager = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
                      return dx::VmValue::Ref(
                          Singleton(call, context, "window_manager",
            "Landroid/view/WindowManagerImpl;"));
    });
    handlers.handler_android_windowmanager_get_default_display = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(Singleton(
            call, context, "display", "Landroid/view/Display;"));
    });
  handlers.handler_android_display_get_width = dx::IntrinsicHandler([context](
                                                     dx::IntrinsicContext &) {
    return dx::VmValue::Int(static_cast<std::int32_t>(context->surface_width));
    });
  handlers.handler_android_display_get_height = dx::IntrinsicHandler([context](
                                                      dx::IntrinsicContext &) {
    return dx::VmValue::Int(static_cast<std::int32_t>(context->surface_height));
  });
  handlers.handler_android_display_get_rotation = dx::IntrinsicHandler([](dx::IntrinsicContext &) {
    // Managed surface coordinates are landscape-natural and never
    // rotate independently from the host window.
    return dx::VmValue::Int(0);
    });
    handlers.handler_android_connectivity_get_active_network_info = dx::IntrinsicHandler([](dx::IntrinsicContext&) {
                      // Truthful offline fact: no active network (documented
                      // null).
        return dx::VmValue::Ref(dx::VmObjectRef{});
    });
    handlers.handler_android_connectivity_get_network_info = dx::IntrinsicHandler([](dx::IntrinsicContext&) {
        // No network of any type is connected on this platform.
        return dx::VmValue::Ref(dx::VmObjectRef{});
    });
    handlers.handler_android_context_get_content_resolver = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(
            Singleton(call, context, "content_resolver",
                      "Landroid/content/ContentResolver;"));
    });
  handlers.handler_android_context_send_broadcast = dx::IntrinsicHandler([](dx::IntrinsicContext &call) {
        // No other process exists; the broadcast truthfully has no
        // audience. Logged so silent drops stay visible.
        GuestLog(call, core::LogLevel::debug,
                 "sendBroadcast dropped: no receivers on this platform");
        return dx::VmValue::Void();
    });
  handlers.handler_android_context_start_service_none = dx::IntrinsicHandler([](dx::IntrinsicContext &call) {
        GuestLog(call, core::LogLevel::debug,
                 "startService answered null: no services on this platform");
        return dx::VmValue::Ref(dx::VmObjectRef{});
    });
    handlers.handler_android_context_register_receiver = dx::IntrinsicHandler([](dx::IntrinsicContext&) {
                      // Sticky broadcast lookup: nothing pending on this
                      // platform.
        return dx::VmValue::Ref(dx::VmObjectRef{});
    });
  handlers.handler_android_context_start_activity = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) -> dx::VmValue {
        // In-process activity switch: only intents with an explicit
        // component that resolves to a dex activity are supported; anything
        // else (external apps, market links, ...) stays an explicit
        // failure.
        const auto intent = call.arguments[0].ref;
        const auto component = context->intent_components.find(intent.Value());
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
  handlers.handler_android_activity_get_intent = dx::IntrinsicHandler([context](dx::IntrinsicContext &call) {
        if (context->current_intent.IsValid()) {
            return dx::VmValue::Ref(context->current_intent);
        }
        // Root activity launch: an empty intent with no extras.
        return dx::VmValue::Ref(
            call.vm.NewIntrinsicInstance("Landroid/content/Intent;"));
    });
}

void PopulateViewSurface(AndroidHandlers& handlers,
                         const Context& context) {
  handlers.handler_android_window_noop = dx::IntrinsicHandler([](dx::IntrinsicContext &) { return dx::VmValue::Void(); });
  handlers.handler_android_window_noop_add = dx::IntrinsicHandler([](dx::IntrinsicContext &) { return dx::VmValue::Void(); });
    handlers.handler_android_window_noop_clear = dx::IntrinsicHandler([](dx::IntrinsicContext &) { return dx::VmValue::Void(); });
  handlers.handler_android_view_init = dx::IntrinsicHandler([](dx::IntrinsicContext &) { return dx::VmValue::Void(); });
  handlers.handler_android_view_noop_size = dx::IntrinsicHandler([](dx::IntrinsicContext &) { return dx::VmValue::Void(); });
  handlers.handler_android_view_noop_focus = dx::IntrinsicHandler([](dx::IntrinsicContext &) { return dx::VmValue::Void(); });
  handlers.handler_android_view_noop_flag = dx::IntrinsicHandler([](dx::IntrinsicContext &) { return dx::VmValue::Void(); });
    handlers.handler_android_graphics_noop = dx::IntrinsicHandler([](dx::IntrinsicContext&) {
        // Pure drawing state with no consuming canvas surface yet.
        return dx::VmValue::Void();
    });
    handlers.handler_android_paint_set_typeface = dx::IntrinsicHandler([](dx::IntrinsicContext& call) {
                      // Returns the typeface that was set, per the platform
                      // contract.
        return dx::VmValue::Ref(call.arguments[0].ref);
    });
  handlers.handler_android_typeface_default_from_style = dx::IntrinsicHandler([](dx::IntrinsicContext &call) {
        return dx::VmValue::Ref(
            call.vm.NewIntrinsicInstance("Landroid/graphics/Typeface;"));
    });
  handlers.handler_android_typeface_clinit = dx::IntrinsicHandler([](dx::IntrinsicContext &call) {
        auto& vm = call.vm;
        vm.SetIntrinsicStaticRef(
        "Landroid/graphics/Typeface;", "SERIF", "Landroid/graphics/Typeface;",
            vm.NewIntrinsicInstance("Landroid/graphics/Typeface;"));
        return dx::VmValue::Void();
    });
    handlers.handler_android_rect_width = dx::IntrinsicHandler([](dx::IntrinsicContext& call) {
        const auto slots = call.vm.Model().InstanceSlots(call.receiver);
        return dx::VmValue::Int(static_cast<std::int32_t>(slots[2].bits) -
                                static_cast<std::int32_t>(slots[0].bits));
    });
    handlers.handler_android_rect_height = dx::IntrinsicHandler([](dx::IntrinsicContext& call) {
        const auto slots = call.vm.Model().InstanceSlots(call.receiver);
        return dx::VmValue::Int(static_cast<std::int32_t>(slots[3].bits) -
                                static_cast<std::int32_t>(slots[1].bits));
    });
  handlers.handler_android_window_get_attributes = dx::IntrinsicHandler([context](dx::IntrinsicContext &call) {
        return dx::VmValue::Ref(
            Singleton(call, context, "window_attributes",
                      "Landroid/view/WindowManager$LayoutParams;"));
    });
  handlers.handler_android_view_request_focus = dx::IntrinsicHandler([](dx::IntrinsicContext &) {
        // The single fullscreen view always holds focus.
        return dx::VmValue::Int(1);
    });
    handlers.handler_android_view_get_id = dx::IntrinsicHandler([](dx::IntrinsicContext&) {
        return dx::VmValue::Int(-1);  // View.NO_ID: no id was assigned
    });
  handlers.handler_android_view_get_tree_observer = dx::IntrinsicHandler([context](dx::IntrinsicContext &call) {
        auto &observer = context->view_tree_observers[call.receiver.Value()];
        if (!observer.IsValid()) {
          observer =
              call.vm.NewIntrinsicInstance("Landroid/view/ViewTreeObserver;");
        }
        return dx::VmValue::Ref(observer);
    });
  handlers.handler_android_view_tree_add_global_listener = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        const auto listener = call.arguments[0].ref;
        if (!listener.IsValid()) {
          throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                "global layout listener is null"};
        }
        context->global_layout_listeners[call.receiver.Value()] = listener;
        return dx::VmValue::Void();
    });
    handlers.handler_android_view_tree_remove_global_listener = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        const auto found = context->global_layout_listeners.find(
            call.receiver.Value());
        if (found != context->global_layout_listeners.end() &&
            found->second == call.arguments[0].ref) {
            context->global_layout_listeners.erase(found);
        }
        return dx::VmValue::Void();
    });
  handlers.handler_android_surface_view_get_holder = dx::IntrinsicHandler([context](dx::IntrinsicContext &call) {
        auto& holder = context->surface_holders[call.receiver.Value()];
        if (!holder.IsValid()) {
          holder =
              call.vm.NewIntrinsicInstance("Landroid/view/SurfaceHolder$Impl;");
        }
        return dx::VmValue::Ref(holder);
    });
    handlers.handler_android_surface_holder_add_callback = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
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
    handlers.handler_android_surface_holder_set_type = dx::IntrinsicHandler([](dx::IntrinsicContext&) {
                      // Managed EGL owns the surface type; the legacy value is
                      // only a device hint and has no observable effect here.
        return dx::VmValue::Void();
    });
    handlers.handler_android_surface_holder_set_format = dx::IntrinsicHandler([](dx::IntrinsicContext&) {
        // Pixel format is fixed by the managed RGBA8 EGL surface.
        return dx::VmValue::Void();
    });
    handlers.handler_android_glsurfaceview_init = dx::IntrinsicHandler([](dx::IntrinsicContext &) { return dx::VmValue::Void(); });
    handlers.handler_android_glsurfaceview_set_renderer = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        context->renderer = call.arguments[0].ref;
        return dx::VmValue::Void();
    });
    handlers.handler_android_glsurfaceview_request_render = dx::IntrinsicHandler([](dx::IntrinsicContext &) { return dx::VmValue::Void(); });
    handlers.handler_android_glsurfaceview_lifecycle_noop = dx::IntrinsicHandler([](dx::IntrinsicContext&) {
        // Render pause/resume is owned by the lifecycle driver.
        return dx::VmValue::Void();
    });
}

void PopulateResources(AndroidHandlers& handlers,
                       const Context& context) {
    handlers.handler_android_resources_get_configuration = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
                      const auto instance =
                          Singleton(call, context, "configuration",
            "Landroid/content/res/Configuration;");
                      // keyboard = KEYBOARD_NOKEYS (1): desktop host has no
                      // guest keypad.
                      const auto slots =
                          call.vm.Model().InstanceSlots(instance);
        slots[0] = {1U, dx::SlotTag::cat1};
        return dx::VmValue::Ref(instance);
    });
  handlers.handler_android_resources_get_identifier = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        const auto entry_name = call.vm.StringUtf8(call.arguments[0].ref);
        const auto type_name = call.vm.StringUtf8(call.arguments[1].ref);
        const auto *entry = context->arsc.FindByName(type_name, entry_name);
        return dx::VmValue::Int(
            entry == nullptr ? 0
                             : static_cast<std::int32_t>(entry->resource_id));
    });
  handlers.handler_android_resources_open_raw_resource = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        const auto resource_id =
            static_cast<std::uint32_t>(call.arguments[0].AsInt());
        const auto* entry = context->arsc.FindById(resource_id);
        if (entry == nullptr || !entry->string_value.has_value()) {
          throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                "resource id has no file entry: " +
                    std::to_string(resource_id)};
        }
        return dx::VmValue::Ref(OpenStream(
            call, context, ReadApkFile(context, *entry->string_value)));
    });
    handlers.handler_android_resources_get_string = dx::IntrinsicHandler([](dx::IntrinsicContext&) -> dx::VmValue {
        throw dx::VmJavaThrow{
            "Ljava/lang/UnsupportedOperationException;",
            "string resources are not provided yet"};
    });
  handlers.handler_android_assets_open = dx::IntrinsicHandler([context](dx::IntrinsicContext &call) {
        const auto name = call.vm.StringUtf8(call.arguments[0].ref);
        return dx::VmValue::Ref(
            OpenStream(call, context, ReadApkFile(context, "assets/" + name)));
    });
  handlers.handler_android_assets_open_mode = dx::IntrinsicHandler([context](dx::IntrinsicContext &call) {
        const auto name = call.vm.StringUtf8(call.arguments[0].ref);
        return dx::VmValue::Ref(
            OpenStream(call, context, ReadApkFile(context, "assets/" + name)));
    });
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime {

std::optional<std::string> DispatchSurfaceHolderCallbacks(
    dexvm::Interpreter& vm, DexVmAndroidContext& context,
    const SurfaceHolderPhase phase) {
    namespace dx = dexvm;
    const auto* name = "surfaceCreated";
    std::string descriptor = "(Landroid/view/SurfaceHolder;)V";
    std::vector<dx::VmValue> extra;
    if (phase == SurfaceHolderPhase::changed) {
        name = "surfaceChanged";
        descriptor = "(Landroid/view/SurfaceHolder;III)V";
        // PixelFormat.RGBA_8888: the managed surface really is RGBA8.
        extra = {dx::VmValue::Int(1),
                 dx::VmValue::Int(
                     static_cast<std::int32_t>(context.surface_width)),
                 dx::VmValue::Int(
                     static_cast<std::int32_t>(context.surface_height))};
    } else if (phase == SurfaceHolderPhase::destroyed) {
        name = "surfaceDestroyed";
    }

    auto& linker = vm.Linker();
    std::size_t delivered = 0;
    for (const auto& [holder_handle, callbacks] : context.surface_callbacks) {
        for (const auto callback : callbacks) {
            const auto callback_class = vm.Model().ObjectClass(callback);
            const auto index =
                linker.FindVtableIndex(callback_class, name, descriptor);
            if (!index.has_value()) {
                // A registered callback that cannot receive the event is a
                // real defect in the guest's own class, not something to
                // quietly skip.
                return std::string("SurfaceHolder.Callback has no ") + name +
                       ": " + linker.Class(callback_class).descriptor;
            }
            std::vector<dx::VmValue> arguments{
                dx::VmValue::Ref(callback),
                dx::VmValue::Ref(dx::VmObjectRef(holder_handle))};
            arguments.insert(arguments.end(), extra.begin(), extra.end());
            const auto outcome = vm.Call(
                linker.Class(callback_class).vtable[*index], arguments);
            ++delivered;
            if (!outcome.exception.IsValid()) continue;
            std::string rendered = std::string(name) + " raised " +
                                   linker.Class(outcome.exception_class)
                                       .descriptor +
                                   ": " + outcome.exception_message;
            for (const auto& entry : outcome.exception_stack) {
                rendered += "\n  at " + entry.class_descriptor + "." +
                            entry.method_name + " (pc " +
                            std::to_string(entry.pc) + ")";
            }
            return rendered;
        }
    }
    if (auto* logger = vm.Log(); logger != nullptr && delivered > 0) {
        logger->Write(core::LogLevel::info, "session.dex_lifecycle",
                      std::string("managed surface ") + name +
                          " delivered to " + std::to_string(delivered) +
                          " holder callback(s)");
    }
    return std::nullopt;
}

}  // namespace ogplay::runtime
