#include "catalog.h"

#include <optional>

namespace ogplay::runtime::android_intrinsics {
namespace {

ui::UiNodeId NodeFor(dx::IntrinsicContext& call, const Context& context,
                     const dx::VmObjectRef view) {
    if (!view.IsValid()) {
        throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                              "ViewGroup child is null"};
    }
    const auto descriptor = call.vm.Linker()
                                .Class(call.vm.Model().ObjectClass(view))
                                .descriptor;
    return EnsureViewUiNode(*context, view, UiClassForDescriptor(descriptor));
}

void ApplyParams(const Context& context, const dx::VmObjectRef view,
                 const ui::UiNodeId node,
                 const std::optional<dx::VmObjectRef> params) {
    if (params.has_value()) {
        if (!params->IsValid()) {
            throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                  "LayoutParams is null"};
        }
        const auto found = context->ui_layout_params.find(params->Value());
        if (found == context->ui_layout_params.end()) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                  "LayoutParams is not initialized"};
        }
        context->ui_tree.Get(node)->layout = found->second;
        context->ui_view_layout_params[view.Value()] = *params;
        context->ui_tree.MarkLayoutDirty(node);
        return;
    }
    const auto assigned = context->ui_view_layout_params.find(view.Value());
    if (assigned == context->ui_view_layout_params.end()) return;
    const auto found = context->ui_layout_params.find(assigned->second.Value());
    if (found != context->ui_layout_params.end()) {
        context->ui_tree.Get(node)->layout = found->second;
    }
}

dx::IntrinsicHandler AddHandler(const Context& context, const bool has_index,
                                const bool has_params) {
    return [context, has_index, has_params](dx::IntrinsicContext& call) {
        const auto child = call.arguments[0].ref;
        const auto parent_node = NodeFor(call, context, call.receiver);
        const auto child_node = NodeFor(call, context, child);
        std::optional<std::size_t> index;
        std::size_t argument = 1;
        if (has_index) {
            const auto requested = call.arguments[argument++].AsInt();
            if (requested >= 0) index = static_cast<std::size_t>(requested);
        }
        std::optional<dx::VmObjectRef> params;
        if (has_params) params = call.arguments[argument].ref;
        ApplyParams(context, child, child_node, params);
        try {
            context->ui_tree.Attach(parent_node, child_node, index);
        } catch (const std::runtime_error& error) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                  error.what()};
        }
        return dx::VmValue::Void();
    };
}

}  // namespace

Decl Declare_android_view_ViewGroup(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/ViewGroup;", "Landroid/view/View;");
    builder.FinalMethod("addView", "(Landroid/view/View;)V",
                    AddHandler(context, false, false));
    builder.FinalMethod(
        "addView",
        "(Landroid/view/View;ILandroid/view/ViewGroup$LayoutParams;)V",
        AddHandler(context, true, true));
    builder.FinalMethod(
        "addView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
        AddHandler(context, false, true));
    builder.FinalMethod("removeView", "(Landroid/view/View;)V",
        [context](dx::IntrinsicContext& call) {
            const auto child = call.arguments[0].ref;
            if (!child.IsValid()) return dx::VmValue::Void();
            const auto parent = FindViewUiNode(*context, call.receiver.Value());
            const auto node = FindViewUiNode(*context, child.Value());
            if (parent.has_value() && node.has_value() &&
                context->ui_tree.Get(*node)->parent == parent) {
                context->ui_tree.Detach(*node);
            }
            return dx::VmValue::Void();
        });
    builder.FinalMethod("removeViews", "(II)V",
        [context](dx::IntrinsicContext& call) {
            const auto parent = NodeFor(call, context, call.receiver);
            const auto start = call.arguments[0].AsInt();
            const auto count = call.arguments[1].AsInt();
            const auto children = context->ui_tree.Get(parent)->children;
            if (start < 0 || count < 0 ||
                static_cast<std::size_t>(start) > children.size() ||
                static_cast<std::size_t>(count) >
                    children.size() - static_cast<std::size_t>(start)) {
                throw dx::VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;",
                                      "removeViews range is outside children"};
            }
            for (std::int32_t offset = count; offset > 0; --offset) {
                context->ui_tree.Detach(children[static_cast<std::size_t>(
                    start + offset - 1)]);
            }
            return dx::VmValue::Void();
        });
    builder.FinalMethod(
        "updateViewLayout",
        "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
        [context](dx::IntrinsicContext& call) {
            const auto child = call.arguments[0].ref;
            const auto parent = NodeFor(call, context, call.receiver);
            const auto node = NodeFor(call, context, child);
            if (context->ui_tree.Get(node)->parent != parent) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                      "view is not a child of this ViewGroup"};
            }
            ApplyParams(context, child, node, call.arguments[1].ref);
            return dx::VmValue::Void();
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
