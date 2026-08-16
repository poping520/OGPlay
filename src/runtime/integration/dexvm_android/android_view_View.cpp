#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {
namespace {

ui::UiNodeId ViewNode(dx::IntrinsicContext& call, const Context& context) {
    const auto descriptor = call.vm.Linker()
                                .Class(call.vm.Model().ObjectClass(call.receiver))
                                .descriptor;
    return EnsureViewUiNode(
        *context, call.receiver, UiClassForDescriptor(descriptor));
}

void EnsureLayout(const Context& context) {
    if (context->ui_tree.Get(context->ui_tree.Root())->layout_dirty) {
        ui::LayoutUiTree(
            context->ui_tree,
            {static_cast<std::int32_t>(context->surface_width),
             static_cast<std::int32_t>(context->surface_height)});
    }
}

std::uint32_t AndroidColorToRgba(const std::uint32_t argb) {
    return ((argb & 0x00ffffffU) << 8U) | (argb >> 24U);
}

}  // namespace

Decl Declare_android_view_View(const Context& context) {
    dx::IntrinsicClassBuilder builder("Landroid/view/View;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("<init>", "(Landroid/content/Context;)V",
                    ViewInitHandler(context));
    builder.Overridable("onSizeChanged", "(IIII)V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.Overridable("onWindowFocusChanged", "(Z)V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    const auto noop_flag = dx::IntrinsicHandler(
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.Virtual("setFocusable", "(Z)V", noop_flag);
    builder.Virtual("setFocusableInTouchMode", "(Z)V", noop_flag);
    builder.Virtual("requestFocus", "()Z",
        [](dx::IntrinsicContext&) {
            // The single fullscreen view always holds focus.
            return dx::VmValue::Int(1);
        });
    const auto invalidate = dx::IntrinsicHandler(
        [context](dx::IntrinsicContext& call) {
            const auto node = EnsureViewUiNode(
                *context, call.receiver, ui::UiClass::View);
            context->ui_tree.MarkDrawDirty(node);
            return dx::VmValue::Void();
        });
    builder.Virtual("invalidate", "()V", invalidate);
    builder.Virtual("postInvalidate", "()V", invalidate);
    builder.Virtual("getId", "()I",
        [context](dx::IntrinsicContext& call) {
            const auto node = EnsureViewUiNode(
                *context, call.receiver, ui::UiClass::View);
            return dx::VmValue::Int(
                context->ui_tree.Get(node)->android_id);
        });
    builder.Virtual("setId", "(I)V", ViewSetIdHandler(context));
    builder.Virtual("setLayoutParams", "(Landroid/view/ViewGroup$LayoutParams;)V",
        [context](dx::IntrinsicContext& call) {
            const auto node = ViewNode(call, context);
            const auto params = call.arguments[0].ref;
            if (!params.IsValid()) {
                context->ui_view_layout_params.erase(call.receiver.Value());
                context->ui_tree.Get(node)->layout = {};
            } else {
                const auto found =
                    context->ui_layout_params.find(params.Value());
                if (found == context->ui_layout_params.end()) {
                    throw dx::VmJavaThrow{
                        "Ljava/lang/IllegalArgumentException;",
                        "LayoutParams is not initialized"};
                }
                context->ui_view_layout_params[call.receiver.Value()] = params;
                context->ui_tree.Get(node)->layout = found->second;
            }
            context->ui_tree.MarkLayoutDirty(node);
            return dx::VmValue::Void();
        });
    builder.Virtual("getLayoutParams", "()Landroid/view/ViewGroup$LayoutParams;",
        [context](dx::IntrinsicContext& call) {
            const auto found =
                context->ui_view_layout_params.find(call.receiver.Value());
            return dx::VmValue::Ref(
                found == context->ui_view_layout_params.end()
                    ? dx::VmObjectRef{}
                    : found->second);
        });
    builder.Virtual("setPadding", "(IIII)V",
        [context](dx::IntrinsicContext& call) {
            ui::Insets padding{call.arguments[0].AsInt(),
                               call.arguments[1].AsInt(),
                               call.arguments[2].AsInt(),
                               call.arguments[3].AsInt()};
            if (padding.left < 0 || padding.top < 0 || padding.right < 0 ||
                padding.bottom < 0) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                      "View padding cannot be negative"};
            }
            const auto node = ViewNode(call, context);
            context->ui_tree.Get(node)->padding = padding;
            context->ui_tree.MarkLayoutDirty(node);
            return dx::VmValue::Void();
        });
    const auto geometry = [context](const auto member) {
        return dx::IntrinsicHandler(
            [context, member](dx::IntrinsicContext& call) {
                const auto node = ViewNode(call, context);
                EnsureLayout(context);
                return dx::VmValue::Int(member(*context->ui_tree.Get(node)));
            });
    };
    builder.Virtual("getLeft", "()I", geometry([](const ui::UiNode& node) {
        return node.frame.left;
    }));
    builder.Virtual("getTop", "()I", geometry([](const ui::UiNode& node) {
        return node.frame.top;
    }));
    builder.Virtual("getRight", "()I", geometry([](const ui::UiNode& node) {
        return node.frame.right;
    }));
    builder.Virtual("getBottom", "()I", geometry([](const ui::UiNode& node) {
        return node.frame.bottom;
    }));
    builder.Virtual("getWidth", "()I", geometry([](const ui::UiNode& node) {
        return node.frame.right - node.frame.left;
    }));
    builder.Virtual("getHeight", "()I", geometry([](const ui::UiNode& node) {
        return node.frame.bottom - node.frame.top;
    }));
    builder.Virtual("setVisibility", "(I)V", [context](dx::IntrinsicContext& call) {
        const auto value = call.arguments[0].AsInt();
        if (value != kVisible && value != kInvisible && value != kGone) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                "setVisibility value is not one of VISIBLE/INVISIBLE/GONE: " +
                    std::to_string(value)};
        }
        const auto node = EnsureViewUiNode(
            *context, call.receiver, ui::UiClass::View);
        const auto visibility = value == kVisible
                                    ? ui::Visibility::Visible
                                    : value == kInvisible
                                          ? ui::Visibility::Invisible
                                          : ui::Visibility::Gone;
        context->ui_tree.SetVisibility(node, visibility);
        return dx::VmValue::Void();
    });
    builder.Virtual("getVisibility", "()I", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(VisibilityOf(*context, call.receiver.Value()));
    });
    builder.Virtual("setBackgroundColor", "(I)V",
        [context](dx::IntrinsicContext& call) {
            const auto node = ViewNode(call, context);
            context->ui_tree.Get(node)->background_color =
                AndroidColorToRgba(static_cast<std::uint32_t>(
                    call.arguments[0].AsInt()));
            context->ui_tree.MarkDrawDirty(node);
            return dx::VmValue::Void();
        });
    builder.Virtual("setBackgroundResource", "(I)V", WidgetNoopHandler());
    builder.Virtual("setBackgroundDrawable", "(Landroid/graphics/drawable/Drawable;)V", WidgetNoopHandler());
    builder.Virtual("setOnClickListener", "(Landroid/view/View$OnClickListener;)V",
        [context](dx::IntrinsicContext& call) {
            const auto node = EnsureViewUiNode(
                *context, call.receiver, ui::UiClass::View);
            if (call.arguments[0].ref.IsValid()) {
                context->ui_click_listeners[node] = call.arguments[0].ref;
            } else {
                context->ui_click_listeners.erase(node);
            }
            if (!context->ui_tree.Get(node)->parent.has_value() &&
                call.arguments[0].ref.IsValid()) {
                GuestLog(call, core::LogLevel::warn,
                    "setOnClickListener: the view is detached; "
                    "clicks fall through to Activity.onTouchEvent (recorded gap)");
            }
            return dx::VmValue::Void();
        });
    builder.Virtual("setOnTouchListener", "(Landroid/view/View$OnTouchListener;)V",
        [context](dx::IntrinsicContext& call) {
            const auto node = EnsureViewUiNode(
                *context, call.receiver, ui::UiClass::View);
            if (call.arguments[0].ref.IsValid()) {
                context->ui_touch_listeners[node] = call.arguments[0].ref;
            } else {
                context->ui_touch_listeners.erase(node);
            }
            return dx::VmValue::Void();
        });
    builder.Virtual("clearFocus", "()V", WidgetNoopHandler());
    builder.Virtual("getWindowToken", "()Landroid/os/IBinder;",
        [](dx::IntrinsicContext&) {
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    builder.Virtual("getViewTreeObserver", "()Landroid/view/ViewTreeObserver;",
        [context](dx::IntrinsicContext& call) {
            auto& observer =
                context->view_tree_observers[call.receiver.Value()];
            if (!observer.IsValid()) {
                observer = call.vm.NewIntrinsicInstance(
                    "Landroid/view/ViewTreeObserver;");
            }
            return dx::VmValue::Ref(observer);
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
