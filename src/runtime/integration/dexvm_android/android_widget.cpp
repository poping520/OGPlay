// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from android_widget_AbsoluteLayout_LayoutParams.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_widget_AbsoluteLayout_LayoutParams {

Decl Declare_android_widget_AbsoluteLayout_LayoutParams(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/AbsoluteLayout$LayoutParams;", "Ljava/lang/Object;");
    builder.Constructor("(IIII)V", GraphicsNoopHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_widget_AbsoluteLayout_LayoutParams(const Context& context) {
    return dvm80_android_widget_AbsoluteLayout_LayoutParams::Declare_android_widget_AbsoluteLayout_LayoutParams(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_widget_AbsoluteLayout.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_widget_AbsoluteLayout {

Decl Declare_android_widget_AbsoluteLayout(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/AbsoluteLayout;", "Landroid/view/ViewGroup;");
    builder.Constructor("(Landroid/content/Context;)V", ViewInitHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_widget_AbsoluteLayout(const Context& context) {
    return dvm80_android_widget_AbsoluteLayout::Declare_android_widget_AbsoluteLayout(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_widget_Button.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_widget_Button {

Decl Declare_android_widget_Button(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/Button;", "Landroid/widget/TextView;");
    builder.Constructor("(Landroid/content/Context;)V", ViewInitHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_widget_Button(const Context& context) {
    return dvm80_android_widget_Button::Declare_android_widget_Button(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_widget_EditText.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_widget_EditText {

Decl Declare_android_widget_EditText(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/EditText;", "Landroid/widget/TextView;");
    builder.Constructor("(Landroid/content/Context;)V", ViewInitHandler(context));
    builder.FinalMethod("getText", "()Landroid/text/Editable;",
        [context](dx::IntrinsicContext& call) {
            const auto key =
                "editable:" + std::to_string(call.receiver.Value());
            const auto editable = Singleton(call, context, key,
                                            "Landroid/text/EditableImpl;");
            context->editable_owner[editable.Value()] = call.receiver.Value();
            return dx::VmValue::Ref(editable);
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_widget_EditText(const Context& context) {
    return dvm80_android_widget_EditText::Declare_android_widget_EditText(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_widget_FrameLayout_LayoutParams.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_widget_FrameLayout_LayoutParams {
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

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_widget_FrameLayout_LayoutParams(const Context& context) {
    return dvm80_android_widget_FrameLayout_LayoutParams::Declare_android_widget_FrameLayout_LayoutParams(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_widget_FrameLayout.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_widget_FrameLayout {

Decl Declare_android_widget_FrameLayout(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/FrameLayout;", "Landroid/view/ViewGroup;");
    builder.Constructor("(Landroid/content/Context;)V", ViewInitHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_widget_FrameLayout(const Context& context) {
    return dvm80_android_widget_FrameLayout::Declare_android_widget_FrameLayout(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_widget_ImageButton.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_widget_ImageButton {

Decl Declare_android_widget_ImageButton(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/ImageButton;", "Landroid/widget/ImageView;");
    builder.Constructor("(Landroid/content/Context;)V", ViewInitHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_widget_ImageButton(const Context& context) {
    return dvm80_android_widget_ImageButton::Declare_android_widget_ImageButton(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_widget_ImageView_ScaleType.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_widget_ImageView_ScaleType {

Decl Declare_android_widget_ImageView_ScaleType(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/ImageView$ScaleType;", "Ljava/lang/Object;");
    builder.StaticField("CENTER", "Landroid/widget/ImageView$ScaleType;");
    builder.StaticField("CENTER_INSIDE", "Landroid/widget/ImageView$ScaleType;");
    builder.StaticField("FIT_CENTER", "Landroid/widget/ImageView$ScaleType;");
    builder.StaticField("FIT_XY", "Landroid/widget/ImageView$ScaleType;");
    builder.StaticField("CENTER_CROP", "Landroid/widget/ImageView$ScaleType;");
    builder.ClassInitializer([context](dx::IntrinsicContext& call) {
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

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_widget_ImageView_ScaleType(const Context& context) {
    return dvm80_android_widget_ImageView_ScaleType::Declare_android_widget_ImageView_ScaleType(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_widget_ImageView.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_widget_ImageView {

Decl Declare_android_widget_ImageView(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/ImageView;", "Landroid/view/View;");
    builder.Constructor("(Landroid/content/Context;)V", ViewInitHandler(context));
    builder.FinalMethod("setImageResource", "(I)V",
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
    builder.FinalMethod("setScaleType", "(Landroid/widget/ImageView$ScaleType;)V",
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

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_widget_ImageView(const Context& context) {
    return dvm80_android_widget_ImageView::Declare_android_widget_ImageView(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_widget_LinearLayout_LayoutParams.cpp ----
#include "catalog.h"

#include <cmath>

namespace ogplay::runtime::android_intrinsics::dvm80_android_widget_LinearLayout_LayoutParams {
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

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_widget_LinearLayout_LayoutParams(const Context& context) {
    return dvm80_android_widget_LinearLayout_LayoutParams::Declare_android_widget_LinearLayout_LayoutParams(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_widget_LinearLayout.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_widget_LinearLayout {

Decl Declare_android_widget_LinearLayout(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/LinearLayout;", "Landroid/view/ViewGroup;");
    builder.Constructor("(Landroid/content/Context;)V", ViewInitHandler(context));
    builder.FinalMethod("setOrientation", "(I)V",
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
    builder.FinalMethod("getOrientation", "()I",
        [context](dx::IntrinsicContext& call) {
            const auto node = EnsureViewUiNode(
                *context, call.receiver, ui::UiClass::LinearLayout);
            return dx::VmValue::Int(
                context->ui_tree.Get(node)->orientation ==
                        ui::Orientation::Horizontal
                    ? 0
                    : 1);
        });
    builder.FinalMethod("setGravity", "(I)V",
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

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_widget_LinearLayout(const Context& context) {
    return dvm80_android_widget_LinearLayout::Declare_android_widget_LinearLayout(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_widget_ProgressBar.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_widget_ProgressBar {

Decl Declare_android_widget_ProgressBar(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/ProgressBar;", "Landroid/view/View;");
    builder.Constructor("(Landroid/content/Context;)V", ViewInitHandler(context));
    builder.Constructor("(Landroid/content/Context;Landroid/util/AttributeSet;I)V", ViewInitHandler(context));
    builder.FinalMethod("setMax", "(I)V", WidgetNoopHandler());
    builder.FinalMethod("setProgress", "(I)V", WidgetNoopHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_widget_ProgressBar(const Context& context) {
    return dvm80_android_widget_ProgressBar::Declare_android_widget_ProgressBar(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_widget_RelativeLayout_LayoutParams.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_widget_RelativeLayout_LayoutParams {
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
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/RelativeLayout$LayoutParams;", "Landroid/view/ViewGroup$LayoutParams;");
    builder.Constructor("(II)V",
        [context](dx::IntrinsicContext& call) {
            ui::LayoutParams params;
            params.width = Dimension(call.arguments[0].AsInt());
            params.height = Dimension(call.arguments[1].AsInt());
            context->ui_layout_params[call.receiver.Value()] = params;
            return dx::VmValue::Void();
        });
    builder.FinalMethod("addRule", "(I)V",
        [context](dx::IntrinsicContext& call) {
            AddRule(Params(context, call.receiver),
                    call.arguments[0].AsInt(), -1);
            SyncAttached(context, call.receiver);
            return dx::VmValue::Void();
        });
    builder.FinalMethod("addRule", "(II)V",
        [context](dx::IntrinsicContext& call) {
            AddRule(Params(context, call.receiver),
                    call.arguments[0].AsInt(), call.arguments[1].AsInt());
            SyncAttached(context, call.receiver);
            return dx::VmValue::Void();
        });
    builder.FinalMethod("setMargins", "(IIII)V",
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

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_widget_RelativeLayout_LayoutParams(const Context& context) {
    return dvm80_android_widget_RelativeLayout_LayoutParams::Declare_android_widget_RelativeLayout_LayoutParams(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_widget_RelativeLayout.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_widget_RelativeLayout {

Decl Declare_android_widget_RelativeLayout(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/RelativeLayout;", "Landroid/view/ViewGroup;");
    builder.Constructor("(Landroid/content/Context;)V", ViewInitHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_widget_RelativeLayout(const Context& context) {
    return dvm80_android_widget_RelativeLayout::Declare_android_widget_RelativeLayout(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_widget_ScrollView.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_widget_ScrollView {

Decl Declare_android_widget_ScrollView(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/ScrollView;", "Landroid/view/ViewGroup;");
    builder.Constructor("(Landroid/content/Context;)V", ViewInitHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_widget_ScrollView(const Context& context) {
    return dvm80_android_widget_ScrollView::Declare_android_widget_ScrollView(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_widget_TableLayout.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_widget_TableLayout {

Decl Declare_android_widget_TableLayout(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/TableLayout;", "Landroid/view/ViewGroup;");
    builder.Constructor("(Landroid/content/Context;)V", ViewInitHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_widget_TableLayout(const Context& context) {
    return dvm80_android_widget_TableLayout::Declare_android_widget_TableLayout(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_widget_TableRow.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_widget_TableRow {

Decl Declare_android_widget_TableRow(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/TableRow;", "Landroid/view/ViewGroup;");
    builder.Constructor("(Landroid/content/Context;)V", ViewInitHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_widget_TableRow(const Context& context) {
    return dvm80_android_widget_TableRow::Declare_android_widget_TableRow(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_widget_TextView.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_widget_TextView {
namespace {

ui::UiNodeId TextNode(dx::IntrinsicContext& call, const Context& context) {
    const auto descriptor = call.vm.Linker()
                                .Class(call.vm.Model().ObjectClass(call.receiver))
                                .descriptor;
    return EnsureViewUiNode(
        *context, call.receiver, UiClassForDescriptor(descriptor));
}

std::uint32_t AndroidColorToRgba(const std::uint32_t argb) {
    return ((argb & 0x00ffffffU) << 8U) | (argb >> 24U);
}

}  // namespace

Decl Declare_android_widget_TextView(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/TextView;", "Landroid/view/View;");
    builder.Constructor("(Landroid/content/Context;)V",
                    ViewInitHandler(context));
    builder.FinalMethod("setText", "(Ljava/lang/CharSequence;)V",
        [context](dx::IntrinsicContext& call) {
            const auto value = call.arguments[0].ref;
            auto text = value.IsValid() ? call.vm.Model().StringValue(value)
                                        : std::u16string();
            try {
                static_cast<void>(ui::MeasureFixedText(text, 8.0F));
            } catch (const std::runtime_error& error) {
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                      error.what()};
            }
            const auto node = TextNode(call, context);
            context->ui_tree.Get(node)->text = std::move(text);
            context->ui_tree.MarkLayoutDirty(node);
            return dx::VmValue::Void();
        });
    builder.FinalMethod("getText", "()Ljava/lang/CharSequence;",
        [context](dx::IntrinsicContext& call) {
            const auto node = TextNode(call, context);
            return dx::VmValue::Ref(call.vm.Model().NewString(
                context->ui_tree.Get(node)->text));
        });
    builder.FinalMethod("setTextColor", "(I)V",
        [context](dx::IntrinsicContext& call) {
            const auto node = TextNode(call, context);
            context->ui_tree.Get(node)->text_color = AndroidColorToRgba(
                static_cast<std::uint32_t>(call.arguments[0].AsInt()));
            context->ui_tree.MarkDrawDirty(node);
            return dx::VmValue::Void();
        });
    const auto set_text_size = [context](dx::IntrinsicContext& call,
                                         const std::size_t index) {
        const auto size = call.arguments[index].AsFloat();
        try {
            static_cast<void>(ui::MeasureFixedText(u"", size));
        } catch (const std::runtime_error& error) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                  error.what()};
        }
        const auto node = TextNode(call, context);
        context->ui_tree.Get(node)->text_size_px = size;
        context->ui_tree.MarkLayoutDirty(node);
        return dx::VmValue::Void();
    };
    builder.FinalMethod("setTextSize", "(F)V",
        [set_text_size](dx::IntrinsicContext& call) {
            return set_text_size(call, 0);
        });
    builder.FinalMethod("setTextSize", "(IF)V",
        [set_text_size](dx::IntrinsicContext& call) {
            const auto unit = call.arguments[0].AsInt();
            if (unit < 0 || unit > 2) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                      "unsupported TextView dimension unit"};
            }
            return set_text_size(call, 1);
        });
    const auto one_line = [context](dx::IntrinsicContext& call) {
        if (call.arguments[0].AsInt() != 1) {
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                  "multiline TextView is unsupported"};
        }
        const auto node = TextNode(call, context);
        context->ui_tree.Get(node)->max_lines = 1;
        return dx::VmValue::Void();
    };
    builder.FinalMethod("setLines", "(I)V", one_line);
    builder.FinalMethod("setMaxLines", "(I)V", one_line);
    builder.FinalMethod("setSingleLine", "()V",
        [context](dx::IntrinsicContext& call) {
            const auto node = TextNode(call, context);
            context->ui_tree.Get(node)->max_lines = 1;
            return dx::VmValue::Void();
        });
    builder.FinalMethod("setSingleLine", "(Z)V",
        [context](dx::IntrinsicContext& call) {
            if (call.arguments[0].AsInt() == 0) {
                throw dx::VmJavaThrow{
                    "Ljava/lang/UnsupportedOperationException;",
                    "multiline TextView is unsupported"};
            }
            const auto node = TextNode(call, context);
            context->ui_tree.Get(node)->max_lines = 1;
            return dx::VmValue::Void();
        });
    builder.FinalMethod("setMaxWidth", "(I)V", WidgetNoopHandler());
    builder.FinalMethod("setGravity", "(I)V",
        [context](dx::IntrinsicContext& call) {
            const auto node = TextNode(call, context);
            context->ui_tree.Get(node)->gravity =
                static_cast<std::uint32_t>(call.arguments[0].AsInt());
            context->ui_tree.MarkLayoutDirty(node);
            return dx::VmValue::Void();
        });
    builder.FinalMethod("setTypeface", "(Landroid/graphics/Typeface;)V", WidgetNoopHandler());
    builder.FinalMethod("getPaint", "()Landroid/text/TextPaint;",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(
                Singleton(call, context, "text_paint",
                          "Landroid/text/TextPaint;"));
        });
    builder.FinalMethod("addTextChangedListener", "(Landroid/text/TextWatcher;)V", WidgetNoopHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_widget_TextView(const Context& context) {
    return dvm80_android_widget_TextView::Declare_android_widget_TextView(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_widget_Toast.cpp ----
// Toast has no on-screen surface here; show() lands in the guest log.

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_widget_Toast {

Decl Declare_android_widget_Toast(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/Toast;", "Ljava/lang/Object;");
    builder.StaticMethod("makeText",
        "(Landroid/content/Context;Ljava/lang/CharSequence;I)"
        "Landroid/widget/Toast;",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(
                call.vm.NewIntrinsicInstance("Landroid/widget/Toast;"));
        });
    builder.FinalMethod("show", "()V",
        [](dx::IntrinsicContext& call) {
            GuestLog(call, core::LogLevel::info, "Toast.show()");
            return dx::VmValue::Void();
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_widget_Toast(const Context& context) {
    return dvm80_android_widget_Toast::Declare_android_widget_Toast(context);
}
}  // namespace ogplay::runtime::android_intrinsics
