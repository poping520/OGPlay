#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_LinearLayout(const Context& context) {
    dx::IntrinsicClassBuilder builder("Landroid/widget/LinearLayout;");
    builder.Super("Landroid/view/ViewGroup;");
    builder.Virtual("<init>", "(Landroid/content/Context;)V", ViewInitHandler(context));
    builder.Virtual("setOrientation", "(I)V",
        [context](dx::IntrinsicContext& call) {
            const auto value = call.arguments[0].AsInt();
            if (value != 0 && value != 1) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                      "LinearLayout orientation must be horizontal or vertical"};
            }
            const auto node = EnsureViewUiNode(
                *context, call.receiver, ui::UiClass::LinearLayout);
            context->ui_tree.Get(node)->orientation =
                value == 0 ? ui::Orientation::Horizontal
                           : ui::Orientation::Vertical;
            context->ui_tree.MarkLayoutDirty(node);
            return dx::VmValue::Void();
        });
    builder.Virtual("getOrientation", "()I",
        [context](dx::IntrinsicContext& call) {
            const auto node = EnsureViewUiNode(
                *context, call.receiver, ui::UiClass::LinearLayout);
            return dx::VmValue::Int(
                context->ui_tree.Get(node)->orientation ==
                        ui::Orientation::Horizontal
                    ? 0
                    : 1);
        });
    builder.Virtual("setGravity", "(I)V",
        [context](dx::IntrinsicContext& call) {
            const auto node = EnsureViewUiNode(
                *context, call.receiver, ui::UiClass::LinearLayout);
            context->ui_tree.Get(node)->gravity =
                static_cast<std::uint32_t>(call.arguments[0].AsInt());
            context->ui_tree.MarkLayoutDirty(node);
            return dx::VmValue::Void();
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
