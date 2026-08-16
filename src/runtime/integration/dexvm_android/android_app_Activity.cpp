// Activity handlers. setContentView(int) inflates the real binary XML
// layout into the view registry that findViewById answers from;
// presentation-only calls stay no-ops.

#include "ogplay/loader/binary_xml.h"
#include "ogplay/runtime/integration/host_image_decode.h"

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_app_Activity(const Context& context) {
    dx::IntrinsicClassBuilder builder("Landroid/app/Activity;");
    builder.Super("Landroid/content/Context;");
    builder.Virtual("<init>", "()V", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    const auto lifecycle_noop = dx::IntrinsicHandler(
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.Overridable("onCreate", "(Landroid/os/Bundle;)V", lifecycle_noop);
    builder.Overridable("onStart", "()V", lifecycle_noop);
    builder.Overridable("onRestart", "()V", lifecycle_noop);
    builder.Overridable("onResume", "()V", lifecycle_noop);
    builder.Overridable("onPause", "()V", lifecycle_noop);
    builder.Overridable("onStop", "()V", lifecycle_noop);
    builder.Overridable("onDestroy", "()V", lifecycle_noop);
    builder.Overridable("onConfigurationChanged",
        "(Landroid/content/res/Configuration;)V", lifecycle_noop);
    builder.Virtual("getWindow", "()Landroid/view/Window;",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(
                Singleton(call, context, "window", "Landroid/view/Window;"));
        });
    builder.Virtual("requestWindowFeature", "(I)Z",
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(1); });
    builder.Virtual("setContentView", "(Landroid/view/View;)V",
        [context](dx::IntrinsicContext& call) {
            context->content_view = call.arguments[0].ref;
            return dx::VmValue::Void();
        });
    // Minimal layout inflation: binary XML tags become widget intrinsic
    // instances and android:id entries feed findViewById. Layout geometry
    // attributes are not applied (the widget layer holds state only).
    builder.Virtual("setContentView", "(I)V",
        [context](dx::IntrinsicContext& call) {
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
            ResetViewUiState(*context);
            dx::VmObjectRef root;
            const auto elements = loader::ParseBinaryXmlElements(bytes);
            // Element index -> layout_views index; skipped elements (merge,
            // unknown tags) map to their own parent so children re-attach to
            // the nearest instantiated ancestor.
            std::vector<std::int32_t> fact_of(elements.size(), -1);
            std::vector<ui::UiNodeId> node_of(elements.size());
            for (std::size_t index = 0; index < elements.size(); ++index) {
                const auto& element = elements[index];
                const auto parent_fact =
                    element.parent < 0
                        ? -1
                        : fact_of[static_cast<std::size_t>(element.parent)];
                const auto parent_node =
                    element.parent < 0
                        ? context->ui_tree.Root()
                        : node_of[static_cast<std::size_t>(element.parent)];
                if (element.name == "merge") {
                    node_of[index] = parent_node;
                    continue; // container marker
                }
                const auto found = kTagDescriptors.find(element.name);
                if (found == kTagDescriptors.end()) {
                    GuestLog(call, core::LogLevel::warn,
                             "layout tag has no widget intrinsic: " +
                                 element.name);
                    fact_of[index] = parent_fact;
                    node_of[index] = parent_node;
                    continue;
                }
                const auto instance =
                    call.vm.NewIntrinsicInstance(found->second);
                const auto node = context->ui_tree.CreateNode(
                    UiClassForDescriptor(found->second));
                BindViewToUiNode(*context, instance, node);
                if (element.id != 0) {
                    context->ui_tree.SetAndroidId(
                        node, static_cast<std::int32_t>(element.id));
                }
                context->ui_tree.Attach(parent_node, node);
                node_of[index] = node;
                if (!root.IsValid())
                    root = instance;
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
                            const auto image = DecodeImageToArgb(ReadApkFile(
                                context, *drawable->string_value));
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
    builder.Virtual("findViewById", "(I)Landroid/view/View;",
        [context](dx::IntrinsicContext& call) {
            const auto found = context->ui_tree.FindByAndroidId(
                call.arguments[0].AsInt());
            if (!found.has_value()) {
                // Absent id: null is the documented answer.
                return dx::VmValue::Ref(dx::VmObjectRef{});
            }
            return dx::VmValue::Ref(ViewObjectForUiNode(*context, *found));
        });
    builder.Virtual("getIntent", "()Landroid/content/Intent;",
        [context](dx::IntrinsicContext& call) {
            if (context->current_intent.IsValid()) {
                return dx::VmValue::Ref(context->current_intent);
            }
            // Root activity launch: an empty intent with no extras.
            return dx::VmValue::Ref(
                call.vm.NewIntrinsicInstance("Landroid/content/Intent;"));
        });
    // The whole VM is the UI thread in the cooperative model, so the
    // runnable executes synchronously (matches Android semantics when the
    // caller is already on the UI thread).
    builder.Virtual("runOnUiThread", "(Ljava/lang/Runnable;)V",
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
    builder.Virtual("setVolumeControlStream", "(I)V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    const auto on_key_false = dx::IntrinsicHandler(
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });
    builder.Overridable("onKeyDown", "(ILandroid/view/KeyEvent;)Z",
        on_key_false);
    builder.Overridable("onKeyUp", "(ILandroid/view/KeyEvent;)Z",
        on_key_false);
    builder.Overridable("onTouchEvent", "(Landroid/view/MotionEvent;)Z",
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });
    builder.Overridable("finish", "()V",
        [context](dx::IntrinsicContext& call) {
            context->finishing_activity = call.receiver.Value();
            return dx::VmValue::Void();
        });
    builder.Virtual("getWindowManager", "()Landroid/view/WindowManager;",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(
                Singleton(call, context, "window_manager",
                          "Landroid/view/WindowManagerImpl;"));
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
