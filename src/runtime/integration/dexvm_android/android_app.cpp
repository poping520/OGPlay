// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from android_app_Activity.cpp ----
// Activity handlers. setContentView(int) inflates the real binary XML
// layout through the registry inflater into the UiTree;
// presentation-only calls stay no-ops.

#include "ogplay/loader/binary_xml.h"

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_app_Activity {

Decl Declare_android_app_Application(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/app/Application;", "Landroid/content/Context;");
    // Bounded equivalence for API 19 Application->ContextWrapper->Context:
    // the wrapper has no independent service/resource behavior in OGPlay.
    builder.Constructor("()V", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("attachBaseContext", "(Landroid/content/Context;)V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.VirtualMethod("onCreate", "()V", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    builder.FinalMethod("getBaseContext", "()Landroid/content/Context;",
        [context](dx::IntrinsicContext&) {
            return dx::VmValue::Ref(context->application_base_context);
        });
    return std::move(builder).Build();
}

Decl Declare_android_app_Activity(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/app/Activity;", "Landroid/content/Context;");
    builder.Constructor("()V", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    const auto lifecycle_noop = dx::IntrinsicHandler(
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.VirtualMethod("onCreate", "(Landroid/os/Bundle;)V", lifecycle_noop);
    builder.VirtualMethod("onStart", "()V", lifecycle_noop);
    builder.VirtualMethod("onRestart", "()V", lifecycle_noop);
    builder.VirtualMethod("onResume", "()V", lifecycle_noop);
    builder.VirtualMethod("onPause", "()V", lifecycle_noop);
    builder.VirtualMethod("onStop", "()V", lifecycle_noop);
    builder.VirtualMethod("onDestroy", "()V", lifecycle_noop);
    builder.VirtualMethod("onConfigurationChanged",
        "(Landroid/content/res/Configuration;)V", lifecycle_noop);
    builder.FinalMethod("getWindow", "()Landroid/view/Window;",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(
                Singleton(call, context, "window", "Landroid/view/Window;"));
        });
    builder.FinalMethod("getApplication", "()Landroid/app/Application;",
        [context](dx::IntrinsicContext&) {
            return dx::VmValue::Ref(context->application);
        });
    builder.FinalMethod("requestWindowFeature", "(I)Z",
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(1); });
    builder.FinalMethod("setContentView", "(Landroid/view/View;)V",
        [context](dx::IntrinsicContext& call) {
            const auto view = call.arguments[0].ref;
            if (!view.IsValid()) {
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                      "content view is null"};
            }
            const auto descriptor = call.vm.Linker()
                                        .Class(call.vm.Model().ObjectClass(view))
                                        .descriptor;
            const auto node = EnsureViewUiNode(
                *context, view, UiClassForDescriptor(descriptor));
            const auto parent = context->ui_tree.Get(node)->parent;
            if (parent.has_value() && *parent != context->ui_tree.Root()) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                      "content view already has a parent"};
            }
            const auto old_content =
                context->ui_tree.Get(context->ui_tree.Root())->children;
            for (const auto old_root : old_content) {
                if (old_root == node) continue;
                std::vector<ui::UiNodeId> pending{old_root};
                while (!pending.empty()) {
                    const auto current = pending.back();
                    pending.pop_back();
                    const auto* state = context->ui_tree.Get(current);
                    pending.insert(pending.end(), state->children.begin(),
                                   state->children.end());
                    const auto object = ViewObjectForUiNode(*context, current);
                    if (object.IsValid()) {
                        context->object_to_ui_node.erase(object.Value());
                        context->ui_view_layout_params.erase(object.Value());
                    }
                    context->ui_node_to_object.erase(current);
                    context->ui_click_listeners.erase(current);
                    context->ui_touch_listeners.erase(current);
                }
                context->ui_tree.DestroySubtree(old_root);
            }
            if (!parent.has_value()) {
                context->ui_tree.Attach(context->ui_tree.Root(), node);
            }
            ui::LayoutUiTree(context->ui_tree,
                             {static_cast<std::int32_t>(context->surface_width),
                              static_cast<std::int32_t>(context->surface_height)});
            context->content_view = view;
            return dx::VmValue::Void();
        });
    builder.FinalMethod("setContentView", "(I)V",
        [context](dx::IntrinsicContext& call) {
            const auto layout_id =
                static_cast<std::uint32_t>(call.arguments[0].AsInt());
            try {
                context->content_view =
                    InflateUiLayoutResource(call.vm, *context, layout_id);
                ui::LayoutUiTree(context->ui_tree,
                                 {static_cast<std::int32_t>(context->surface_width),
                                  static_cast<std::int32_t>(context->surface_height)});
            } catch (const std::runtime_error& error) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                      std::string("layout inflation failed: ") +
                                          error.what()};
            }
            return dx::VmValue::Void();
        });
    builder.FinalMethod("findViewById", "(I)Landroid/view/View;",
        [context](dx::IntrinsicContext& call) {
            const auto found = context->ui_tree.FindByAndroidId(
                call.arguments[0].AsInt());
            if (!found.has_value()) {
                // Absent id: null is the documented answer.
                return dx::VmValue::Ref(dx::VmObjectRef{});
            }
            return dx::VmValue::Ref(ViewObjectForUiNode(*context, *found));
        });
    builder.FinalMethod("getIntent", "()Landroid/content/Intent;",
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
    builder.FinalMethod("runOnUiThread", "(Ljava/lang/Runnable;)V",
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
    builder.FinalMethod("setVolumeControlStream", "(I)V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.FinalMethod("setRequestedOrientation", "(I)V",
        [context](dx::IntrinsicContext& call) {
            context->requested_orientations[call.receiver.Value()] =
                call.arguments[0].AsInt();
            return dx::VmValue::Void();
        });
    builder.FinalMethod("getRequestedOrientation", "()I",
        [context](dx::IntrinsicContext& call) {
            const auto found = context->requested_orientations.find(
                call.receiver.Value());
            return dx::VmValue::Int(
                found == context->requested_orientations.end()
                    ? -1
                    : found->second);
        });
    const auto on_key_false = dx::IntrinsicHandler(
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });
    builder.VirtualMethod("onKeyDown", "(ILandroid/view/KeyEvent;)Z",
        on_key_false);
    builder.VirtualMethod("onKeyUp", "(ILandroid/view/KeyEvent;)Z",
        on_key_false);
    builder.VirtualMethod("onTouchEvent", "(Landroid/view/MotionEvent;)Z",
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });
    builder.VirtualMethod("finish", "()V",
        [context](dx::IntrinsicContext& call) {
            context->finishing_activity = call.receiver.Value();
            return dx::VmValue::Void();
        });
    // AOSP Activity.isTaskRoot: whether this activity is the first one of
    // its task. OGPlay runs one task per process; the manifest launcher
    // opened it, in-process startActivity handoffs did not. The retired
    // shell of a handoff keeps answering true for its own handle, as its
    // token would on the platform.
    builder.FinalMethod("isTaskRoot", "()Z",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(
                call.receiver.Value() == context->task_root_activity ? 1
                                                                     : 0);
        });
    builder.FinalMethod("getWindowManager", "()Landroid/view/WindowManager;",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(
                Singleton(call, context, "window_manager",
                          "Landroid/view/WindowManagerImpl;"));
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_app_Application(const Context& context) {
    return dvm80_android_app_Activity::Declare_android_app_Application(context);
}
Decl Declare_android_app_Activity(const Context& context) {
    return dvm80_android_app_Activity::Declare_android_app_Activity(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_app_AlertDialog_Builder.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_app_AlertDialog_Builder {

Decl Declare_android_app_AlertDialog_Builder(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/app/AlertDialog$Builder;", "Ljava/lang/Object;");
    builder.Constructor("(Landroid/content/Context;)V", WidgetNoopHandler());
    const auto self = dx::IntrinsicHandler([](dx::IntrinsicContext& call) {
        return Self(call);
    });
    builder.FinalMethod("setTitle", "(Ljava/lang/CharSequence;)Landroid/app/AlertDialog$Builder;", self);
    builder.FinalMethod("setItems", "([Ljava/lang/CharSequence;Landroid/content/DialogInterface$OnClickListener;)Landroid/app/AlertDialog$Builder;", self);
    builder.FinalMethod("create", "()Landroid/app/AlertDialog;",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(
                call.vm.NewIntrinsicInstance("Landroid/app/AlertDialog;"));
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_app_AlertDialog_Builder(const Context& context) {
    return dvm80_android_app_AlertDialog_Builder::Declare_android_app_AlertDialog_Builder(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_app_AlertDialog.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_app_AlertDialog {

Decl Declare_android_app_AlertDialog(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/app/AlertDialog;", "Ljava/lang/Object;");
    builder.FinalMethod("show", "()V", WidgetNoopHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_app_AlertDialog(const Context& context) {
    return dvm80_android_app_AlertDialog::Declare_android_app_AlertDialog(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_app_Dialog.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_app_Dialog {

Decl Declare_android_app_Dialog(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/app/Dialog;", "Ljava/lang/Object;");
    builder.Constructor("()V", NeutralHandler('V'));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_app_Dialog(const Context& context) {
    return dvm80_android_app_Dialog::Declare_android_app_Dialog(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_app_IntentService.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_app_IntentService {

Decl Declare_android_app_IntentService(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/app/IntentService;", "Ljava/lang/Object;");
    builder.Constructor("()V", NeutralHandler('V'));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_app_IntentService(const Context& context) {
    return dvm80_android_app_IntentService::Declare_android_app_IntentService(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_app_PendingIntent.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_app_PendingIntent {

Decl Declare_android_app_PendingIntent(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/app/PendingIntent;", "Ljava/lang/Object;");
    builder.StaticMethod("getBroadcast",
        "(Landroid/content/Context;ILandroid/content/Intent;I)"
        "Landroid/app/PendingIntent;",
        [](dx::IntrinsicContext&) {
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_app_PendingIntent(const Context& context) {
    return dvm80_android_app_PendingIntent::Declare_android_app_PendingIntent(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_app_ProgressDialog.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_app_ProgressDialog {

Decl Declare_android_app_ProgressDialog(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/app/ProgressDialog;", "Ljava/lang/Object;");
    builder.Constructor("(Landroid/content/Context;)V", WidgetNoopHandler());
    builder.FinalMethod("setMessage", "(Ljava/lang/CharSequence;)V", WidgetNoopHandler());
    builder.FinalMethod("setProgressStyle", "(I)V", WidgetNoopHandler());
    builder.FinalMethod("show", "()V", WidgetNoopHandler());
    builder.FinalMethod("dismiss", "()V", WidgetNoopHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_app_ProgressDialog(const Context& context) {
    return dvm80_android_app_ProgressDialog::Declare_android_app_ProgressDialog(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from support_activity.cpp ----
// Managed surface lifecycle callback dispatch. Intrinsic handlers live in
// their per-class declaration files.

#include "shared.h"

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

std::optional<std::string> RetireSurfaceHolderGeneration(
    dexvm::Interpreter& vm, DexVmAndroidContext& context) {
    const auto error = DispatchSurfaceHolderCallbacks(
        vm, context, SurfaceHolderPhase::destroyed);
    if (error.has_value()) return error;
    context.surface_callbacks.clear();
    context.surface_holders.clear();
    return std::nullopt;
}

}  // namespace ogplay::runtime
