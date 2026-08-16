#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_ImageView(const Context& context) {
    dx::IntrinsicClassBuilder builder("Landroid/widget/ImageView;");
    builder.Super("Landroid/view/View;");
    builder.Virtual("<init>", "(Landroid/content/Context;)V", ViewInitHandler(context));
    builder.Virtual("setImageResource", "(I)V",
        [context](dx::IntrinsicContext& call) {
            const auto descriptor = call.vm.Linker()
                                        .Class(call.vm.Model().ObjectClass(
                                            call.receiver))
                                        .descriptor;
            const auto node = EnsureViewUiNode(
                *context, call.receiver, UiClassForDescriptor(descriptor));
            const auto resource_id =
                static_cast<std::uint32_t>(call.arguments[0].AsInt());
            if (resource_id == 0U) {
                context->ui_tree.Get(node)->image_resource_id = 0;
                context->ui_tree.Get(node)->intrinsic = {};
            } else {
                std::shared_ptr<const ui::UiBitmap> bitmap;
                try {
                    bitmap = ResolveUiDrawable(*context, resource_id);
                } catch (const std::runtime_error& error) {
                    throw dx::VmJavaThrow{"Landroid/content/res/Resources$NotFoundException;",
                                          error.what()};
                }
                context->ui_tree.Get(node)->image_resource_id = resource_id;
                context->ui_tree.Get(node)->intrinsic = {bitmap->width,
                                                         bitmap->height};
            }
            context->ui_tree.MarkLayoutDirty(node);
            return dx::VmValue::Void();
        });
    builder.Virtual("setScaleType", "(Landroid/widget/ImageView$ScaleType;)V",
        [context](dx::IntrinsicContext& call) {
            const auto value = call.arguments[0].ref;
            const auto found = context->ui_image_scale_types.find(value.Value());
            if (!value.IsValid() || found == context->ui_image_scale_types.end()) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                      "unknown ImageView ScaleType"};
            }
            const auto descriptor = call.vm.Linker()
                                        .Class(call.vm.Model().ObjectClass(
                                            call.receiver))
                                        .descriptor;
            const auto node = EnsureViewUiNode(
                *context, call.receiver, UiClassForDescriptor(descriptor));
            context->ui_tree.Get(node)->image_scale_type = found->second;
            context->ui_tree.MarkDrawDirty(node);
            return dx::VmValue::Void();
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
