#include "catalog.h"

#include <cmath>

namespace ogplay::runtime::android_intrinsics {
namespace {

ui::DimensionSpec Dimension(const std::int32_t value) {
    if (value == -1) return {ui::SizeMode::MatchParent, 0};
    if (value == -2) return {ui::SizeMode::WrapContent, 0};
    if (value >= 0) return {ui::SizeMode::Fixed, value};
    throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                          "invalid LinearLayout dimension: " +
                              std::to_string(value)};
}

dx::IntrinsicHandler Init(const Context& context, const bool has_weight) {
    return [context, has_weight](dx::IntrinsicContext& call) {
        ui::LayoutParams params;
        params.width = Dimension(call.arguments[0].AsInt());
        params.height = Dimension(call.arguments[1].AsInt());
        if (has_weight) {
            params.weight = call.arguments[2].AsFloat();
            if (!std::isfinite(params.weight) || params.weight < 0.0F) {
                throw dx::VmJavaThrow{
                    "Ljava/lang/IllegalArgumentException;",
                    "LinearLayout weight must be finite and non-negative"};
            }
        }
        context->ui_layout_params[call.receiver.Value()] = params;
        return dx::VmValue::Void();
    };
}

}  // namespace

Decl Declare_android_widget_LinearLayout_LayoutParams(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/LinearLayout$LayoutParams;", "Landroid/view/ViewGroup$LayoutParams;");
    builder.Constructor("(II)V", Init(context, false));
    builder.Constructor("(IIF)V", Init(context, true));
    builder.FinalMethod("setMargins", "(IIII)V",
        [context](dx::IntrinsicContext& call) {
            const auto found =
                context->ui_layout_params.find(call.receiver.Value());
            if (found == context->ui_layout_params.end()) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                      "LayoutParams is not initialized"};
            }
            found->second.margin = {
                call.arguments[0].AsInt(), call.arguments[1].AsInt(),
                call.arguments[2].AsInt(), call.arguments[3].AsInt()};
            for (const auto& [view, params] :
                 context->ui_view_layout_params) {
                if (params != call.receiver) continue;
                const auto node = FindViewUiNode(*context, view);
                if (!node.has_value()) continue;
                context->ui_tree.Get(*node)->layout = found->second;
                context->ui_tree.MarkLayoutDirty(*node);
            }
            return dx::VmValue::Void();
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
