#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {
namespace {

ui::DimensionSpec Dimension(const std::int32_t value) {
    if (value == -1) return {ui::SizeMode::MatchParent, 0};
    if (value == -2) return {ui::SizeMode::WrapContent, 0};
    if (value >= 0) return {ui::SizeMode::Fixed, value};
    throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                          "invalid RelativeLayout dimension: " +
                              std::to_string(value)};
}

ui::LayoutParams& Params(const Context& context,
                         const dx::VmObjectRef object) {
    const auto found = context->ui_layout_params.find(object.Value());
    if (found == context->ui_layout_params.end()) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                              "LayoutParams is not initialized"};
    }
    return found->second;
}

void SyncAttached(const Context& context, const dx::VmObjectRef object) {
    const auto& params = Params(context, object);
    for (const auto& [view, assigned] : context->ui_view_layout_params) {
        if (assigned != object) continue;
        const auto node = FindViewUiNode(*context, view);
        if (!node.has_value()) continue;
        context->ui_tree.Get(*node)->layout = params;
        context->ui_tree.MarkLayoutDirty(*node);
    }
}

void SetSiblingRule(std::optional<std::int32_t>& target,
                    const std::int32_t anchor) {
    if (anchor == 0) {
        target.reset();
        return;
    }
    if (anchor < 0) {
        throw dx::VmJavaThrow{
            "Ljava/lang/IllegalArgumentException;",
            "RelativeLayout sibling rule requires a positive id"};
    }
    target = anchor;
}

void AddRule(ui::LayoutParams& params, const std::int32_t verb,
             const std::int32_t subject) {
    auto& rules = params.relative;
    switch (verb) {
        case 0: SetSiblingRule(rules.left_of, subject); break;
        case 1: SetSiblingRule(rules.right_of, subject); break;
        case 2: SetSiblingRule(rules.above, subject); break;
        case 3: SetSiblingRule(rules.below, subject); break;
        case 4:
            throw dx::VmJavaThrow{
                "Ljava/lang/UnsupportedOperationException;",
                "RelativeLayout ALIGN_BASELINE is unsupported"};
        case 5: SetSiblingRule(rules.align_left, subject); break;
        case 6: SetSiblingRule(rules.align_top, subject); break;
        case 7: SetSiblingRule(rules.align_right, subject); break;
        case 8: SetSiblingRule(rules.align_bottom, subject); break;
        case 9: rules.align_parent_left = subject != 0; break;
        case 10: rules.align_parent_top = subject != 0; break;
        case 11: rules.align_parent_right = subject != 0; break;
        case 12: rules.align_parent_bottom = subject != 0; break;
        case 13: rules.center_in_parent = subject != 0; break;
        case 14: rules.center_horizontal = subject != 0; break;
        case 15: rules.center_vertical = subject != 0; break;
        default:
            throw dx::VmJavaThrow{
                "Ljava/lang/UnsupportedOperationException;",
                "unsupported RelativeLayout rule verb"};
    }
}

}  // namespace

Decl Declare_android_widget_RelativeLayout_LayoutParams(const Context& context) {
    dx::IntrinsicClassBuilder builder("Landroid/widget/RelativeLayout$LayoutParams;");
    builder.Super("Landroid/view/ViewGroup$LayoutParams;");
    builder.Virtual("<init>", "(II)V",
        [context](dx::IntrinsicContext& call) {
            ui::LayoutParams params;
            params.width = Dimension(call.arguments[0].AsInt());
            params.height = Dimension(call.arguments[1].AsInt());
            context->ui_layout_params[call.receiver.Value()] = params;
            return dx::VmValue::Void();
        });
    builder.Virtual("addRule", "(I)V",
        [context](dx::IntrinsicContext& call) {
            AddRule(Params(context, call.receiver),
                    call.arguments[0].AsInt(), -1);
            SyncAttached(context, call.receiver);
            return dx::VmValue::Void();
        });
    builder.Virtual("addRule", "(II)V",
        [context](dx::IntrinsicContext& call) {
            AddRule(Params(context, call.receiver),
                    call.arguments[0].AsInt(), call.arguments[1].AsInt());
            SyncAttached(context, call.receiver);
            return dx::VmValue::Void();
        });
    builder.Virtual("setMargins", "(IIII)V",
        [context](dx::IntrinsicContext& call) {
            Params(context, call.receiver).margin = {
                call.arguments[0].AsInt(), call.arguments[1].AsInt(),
                call.arguments[2].AsInt(), call.arguments[3].AsInt()};
            SyncAttached(context, call.receiver);
            return dx::VmValue::Void();
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
