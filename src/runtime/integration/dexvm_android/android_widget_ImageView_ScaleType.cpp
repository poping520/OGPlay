#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_ImageView_ScaleType(const Context& context) {
    dx::IntrinsicClassBuilder builder("Landroid/widget/ImageView$ScaleType;");
    builder.Super("Ljava/lang/Object;");
    builder.Field("CENTER", "Landroid/widget/ImageView$ScaleType;", true);
    builder.Field("CENTER_INSIDE", "Landroid/widget/ImageView$ScaleType;", true);
    builder.Field("FIT_CENTER", "Landroid/widget/ImageView$ScaleType;", true);
    builder.Field("FIT_XY", "Landroid/widget/ImageView$ScaleType;", true);
    builder.Field("CENTER_CROP", "Landroid/widget/ImageView$ScaleType;", true);
    builder.Clinit([context](dx::IntrinsicContext& call) {
        const auto publish = [&call, &context](
                                 const std::string_view name,
                                 const ui::ImageScaleType type) {
            const auto object = call.vm.NewIntrinsicInstance(
                "Landroid/widget/ImageView$ScaleType;");
            context->ui_image_scale_types[object.Value()] = type;
            call.vm.SetIntrinsicStaticRef(
                "Landroid/widget/ImageView$ScaleType;", name,
                "Landroid/widget/ImageView$ScaleType;", object);
        };
        publish("CENTER", ui::ImageScaleType::Center);
        publish("CENTER_INSIDE", ui::ImageScaleType::CenterInside);
        publish("FIT_CENTER", ui::ImageScaleType::FitCenter);
        publish("FIT_XY", ui::ImageScaleType::FitXy);
        publish("CENTER_CROP", ui::ImageScaleType::CenterCrop);
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
