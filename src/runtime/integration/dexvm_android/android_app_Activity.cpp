// Activity handlers. setContentView(int) inflates the real binary XML
// layout through the registry inflater into the UiTree;
// presentation-only calls stay no-ops.

#include "ogplay/loader/binary_xml.h"

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
            const auto view = call.arguments[0].ref;
            if (!view.IsValid()) {
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                      "content view is null"};
            }
            const auto descriptor = call.vm.Linker()
                                        .Class(call.vm.Model().ObjectClass(view))
                                        .descriptor;
            ResetViewUiState(*context);
            const auto node = EnsureViewUiNode(
                *context, view, UiClassForDescriptor(descriptor));
            context->ui_tree.Attach(context->ui_tree.Root(), node);
            context->content_view = view;
            return dx::VmValue::Void();
        });
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
            const auto elements = loader::ParseBinaryXmlElements(bytes);
            try {
                context->content_view =
                    InflateUiElements(call.vm, *context, elements);
            } catch (const std::runtime_error& error) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                      std::string("layout inflation failed: ") +
                                          error.what()};
            }
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
