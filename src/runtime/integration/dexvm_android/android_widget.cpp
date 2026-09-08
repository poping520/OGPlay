// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from android_widget_AbsoluteLayout_LayoutParams.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_AbsoluteLayout_LayoutParams(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/AbsoluteLayout$LayoutParams;", "Ljava/lang/Object;");
    builder.Constructor("(IIII)V", GraphicsNoopHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_widget_AbsoluteLayout.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_AbsoluteLayout(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/AbsoluteLayout;", "Landroid/view/ViewGroup;");
    builder.Constructor("(Landroid/content/Context;)V", ViewInitHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_widget_Button.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_Button(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/Button;", "Landroid/widget/TextView;");
    builder.Constructor("(Landroid/content/Context;)V", ViewInitHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_widget_EditText.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

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


// ---- migrated from android_widget_FrameLayout.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_FrameLayout(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/FrameLayout;", "Landroid/view/ViewGroup;");
    builder.Constructor("(Landroid/content/Context;)V", ViewInitHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_widget_ImageButton.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_ImageButton(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/ImageButton;", "Landroid/widget/ImageView;");
    builder.Constructor("(Landroid/content/Context;)V", ViewInitHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_widget_ImageView_ScaleType.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_ImageView_ScaleType(const Context& context) {
    constexpr std::array scale_types{
        ui::ImageScaleType::Center,
        ui::ImageScaleType::CenterInside,
        ui::ImageScaleType::FitCenter,
        ui::ImageScaleType::FitXy,
        ui::ImageScaleType::CenterCrop};
    dx::IntrinsicEnumBuilder builder(
        "Landroid/widget/ImageView$ScaleType;",
        {"CENTER", "CENTER_INSIDE", "FIT_CENTER", "FIT_XY",
         "CENTER_CROP"});
    builder.WithConstantInitializer(
        [context, scale_types](dx::IntrinsicContext&, dx::VmObjectRef value,
                               std::string_view, std::size_t ordinal) {
            context->ui_image_scale_types[value.Value()] =
                scale_types[ordinal];
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_widget_ImageView.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

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


// ---- migrated from android_widget_LinearLayout.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

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


// ---- migrated from android_widget_ProgressBar.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

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


// ---- migrated from android_widget_RelativeLayout.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_RelativeLayout(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/RelativeLayout;", "Landroid/view/ViewGroup;");
    builder.Constructor("(Landroid/content/Context;)V", [context](dx::IntrinsicContext& call) {
        const auto result = ViewInitHandler(context)(call);
        const auto node = EnsureViewUiNode(*context, call.receiver, ui::UiClass::RelativeLayout);
        context->ui_tree.Get(node)->gravity = 0x00800033U; // START | TOP
        return result;
    });
    builder.VirtualMethod("getGravity", "()I", [context](dx::IntrinsicContext& call) {
        const auto node = EnsureViewUiNode(*context, call.receiver, ui::UiClass::RelativeLayout);
        return dx::VmValue::Int(static_cast<std::int32_t>(context->ui_tree.Get(node)->gravity));
    });
    builder.VirtualMethod("setGravity", "(I)V", [context](dx::IntrinsicContext& call) {
        auto gravity = static_cast<std::uint32_t>(call.arguments[0].AsInt());
        if ((gravity & ~0x00800077U) != 0) {
            if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.layout_params", 0);
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "unsupported RelativeLayout gravity"};
        }
        if ((gravity & 0x00800007U) == 0) gravity |= 0x00800003U;
        if ((gravity & 0x70U) == 0) gravity |= 0x30U;
        const auto node = EnsureViewUiNode(*context, call.receiver, ui::UiClass::RelativeLayout);
        if (context->ui_tree.Get(node)->gravity != gravity) {
            context->ui_tree.Get(node)->gravity = gravity;
            static_cast<void>(CallAndroidMethod(call.vm, call.receiver, "requestLayout", "()V"));
        }
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_widget_ScrollView.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_ScrollView(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/ScrollView;", "Landroid/view/ViewGroup;");
    builder.Constructor("(Landroid/content/Context;)V", ViewInitHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_widget_TableLayout.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_TableLayout(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/TableLayout;", "Landroid/view/ViewGroup;");
    builder.Constructor("(Landroid/content/Context;)V", ViewInitHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_widget_TableRow.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_TableRow(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/TableRow;", "Landroid/view/ViewGroup;");
    builder.Constructor("(Landroid/content/Context;)V", ViewInitHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_widget_TextView.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {
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


// ---- migrated from android_widget_Toast.cpp ----
// Toast has no on-screen surface here; show() lands in the guest log.

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

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
