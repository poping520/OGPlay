#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {
namespace {

ui::DimensionSpec Dimension(const std::int32_t value) {
    if (value == -1) return {ui::SizeMode::MatchParent, 0};
    if (value == -2) return {ui::SizeMode::WrapContent, 0};
    if (value >= 0) return {ui::SizeMode::Fixed, value};
    throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                          "invalid LayoutParams dimension: " +
                              std::to_string(value)};
}

}  // namespace

Decl Declare_android_view_ViewGroup_LayoutParams(const Context& context) {
    dx::IntrinsicClassBuilder builder("Landroid/view/ViewGroup$LayoutParams;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("<init>", "(II)V",
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
