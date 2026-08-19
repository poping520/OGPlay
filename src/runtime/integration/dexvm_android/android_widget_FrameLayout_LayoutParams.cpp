#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {
namespace {

ui::DimensionSpec Dimension(const std::int32_t value) {
    if (value == -1) return {ui::SizeMode::MatchParent, 0};
    if (value == -2) return {ui::SizeMode::WrapContent, 0};
    if (value >= 0) return {ui::SizeMode::Fixed, value};
    throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                          "invalid FrameLayout dimension: " +
                              std::to_string(value)};
}

}  // namespace

Decl Declare_android_widget_FrameLayout_LayoutParams(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/FrameLayout$LayoutParams;", "Landroid/view/ViewGroup$LayoutParams;");
    builder.Constructor("(II)V",
        [context](dx::IntrinsicContext& call) {
            ui::LayoutParams params;
            params.width = Dimension(call.arguments[0].AsInt());
            params.height = Dimension(call.arguments[1].AsInt());
            context->ui_layout_params[call.receiver.Value()] = params;
            return dx::VmValue::Void();
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
