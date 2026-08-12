// Context/Activity/Window/View/Resources handlers. setContentView(int)
// inflates the real binary XML layout into the view registry that
// findViewById answers from; presentation-only calls stay no-ops.

#include "ogplay/loader/binary_xml.h"
#include "ogplay/runtime/integration/host_image_decode.h"

#include "dexvm_android_internal.h"

namespace ogplay::runtime::android_intrinsics {

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
                element.parent < 0 ? -1 : fact_of[static_cast<std::size_t>(
                                              element.parent)];
            if (element.name == "merge") continue;  // container marker
            const auto found = kTagDescriptors.find(element.name);
            if (found == kTagDescriptors.end()) {
                GuestLog(call, core::LogLevel::warn,
                         "layout tag has no widget intrinsic: " +
                             element.name);
                fact_of[index] = parent_fact;
                continue;
            }
            const auto instance =
                call.vm.NewIntrinsicInstance(found->second);
            if (!root.IsValid()) root = instance;
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
                if (drawable != nullptr &&
                    drawable->string_value.has_value()) {
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

}  // namespace ogplay::runtime::android_intrinsics
