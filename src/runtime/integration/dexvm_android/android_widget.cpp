#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_Button(const Context&) {
    return std::move(dx::IntrinsicClassBuilder::Class("Landroid/widget/Button;", "Landroid/widget/TextView;")
                         .MergesBootClass())
        .Build();
}

Decl Declare_android_widget_FrameLayout(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/FrameLayout;", "Landroid/view/ViewGroup;");
    builder.MergesBootClass();

    // AOSP 4.4 ROM line 211.

    return std::move(builder).Build();
}

Decl Declare_android_widget_ImageButton(const Context&) {
    return std::move(dx::IntrinsicClassBuilder::Class("Landroid/widget/ImageButton;", "Landroid/widget/ImageView;")
                         .MergesBootClass())
        .Build();
}

Decl Declare_android_widget_ImageView_ScaleType(const Context&) {
    return std::move(dx::IntrinsicClassBuilder::Class("Landroid/widget/ImageView$ScaleType;", "Ljava/lang/Enum;")
                         .MergesBootClass())
        .Build();
}

Decl Declare_android_widget_LinearLayout(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/LinearLayout;", "Landroid/view/ViewGroup;");
    builder.MergesBootClass();

    // AOSP 4.4 ROM line 1802.

    return std::move(builder).Build();
}

Decl Declare_android_widget_ProgressBar(const Context&) {
    return std::move(dx::IntrinsicClassBuilder::Class("Landroid/widget/ProgressBar;", "Landroid/view/View;")
                         .MergesBootClass())
        .Build();
}

Decl Declare_android_widget_RelativeLayout(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/RelativeLayout;", "Landroid/view/ViewGroup;");
    builder.MergesBootClass();

    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
