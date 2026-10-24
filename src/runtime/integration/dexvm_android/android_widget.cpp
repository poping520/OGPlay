// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from android_widget_AbsoluteLayout_LayoutParams.cpp ----
#include "catalog.h"
#include <array>
#include <map>
#include <cmath>
#include <set>
#include <algorithm>
#include <stdexcept>

// Defined by the TextView section below: constructor handler that also
// resolves the framework default style carried by defStyleAttr.
namespace ogplay::runtime::android_intrinsics {
[[nodiscard]] dx::IntrinsicHandler ViewDefaultStyleInitHandler(const Context& context);

namespace {
Decl ExperimentalImageView(const Context& context);
}
}

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_AbsoluteLayout_LayoutParams(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/AbsoluteLayout$LayoutParams;",
                                                    "Landroid/view/ViewGroup$LayoutParams;");
    const auto x = builder.BoundInstanceField("x", "I");
    const auto y = builder.BoundInstanceField("y", "I");
    builder.Constructor("(IIII)V", [x, y](dx::IntrinsicContext& c) {
        const auto target = c.vm.Linker().FindDirectMethod(
            c.vm.Linker().ResolveDescriptor("Landroid/view/ViewGroup$LayoutParams;"), "<init>", "(II)V");
        const auto result =
            c.vm.Call(*target, std::array{dx::VmValue::Ref(c.receiver), c.arguments[0], c.arguments[1]});
        if (result.exception.IsValid())
            throw dx::VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                  result.exception};
        dx::IntrinsicCall(c).SetInt(x, c.arguments[2].AsInt());
        dx::IntrinsicCall(c).SetInt(y, c.arguments[3].AsInt());
        return dx::VmValue::Void();
    });
    // AOSP 4.4 ROM line 213.
    builder.Constructor(
        "(Landroid/view/ViewGroup$LayoutParams;)V",
        [](dx::IntrinsicContext& c) {
            const auto target =
                c.vm.Linker().FindDirectMethod(c.vm.Linker().ResolveDescriptor("Landroid/view/ViewGroup$LayoutParams;"),
                                               "<init>", "(Landroid/view/ViewGroup$LayoutParams;)V");
            const auto result =
                c.vm.Call(*target, std::vector<dx::VmValue>{dx::VmValue::Ref(c.receiver), c.arguments[0]});
            if (result.exception.IsValid())
                throw dx::VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                      result.exception};
            return result.value;
        },
        dx::kAccPublic);

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
    if (context->ui_traversal_owner == AndroidUiTraversalOwner::JavaTraversal) {
        // Retain these containers' own ROM failure boundary instead of inheriting
        // the experimental View/FrameLayout algorithms through a missing overlay.
        builder.UnimplementedOverride("onMeasure", "(II)V", dx::kAccProtected);
        builder.UnimplementedOverride("onLayout", "(ZIIII)V", dx::kAccProtected);
    }
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
    builder.Constructor("(Landroid/content/Context;Landroid/util/AttributeSet;I)V",
                        ViewDefaultStyleInitHandler(context));
    // AOSP 4.4 ROM line 103.
    builder.Constructor(
        "(Landroid/content/Context;Landroid/util/AttributeSet;)V",
        [](dx::IntrinsicContext& c) {
            const auto target =
                c.vm.Linker().FindDirectMethod(c.vm.Linker().ResolveDescriptor("Landroid/widget/Button;"), "<init>",
                                               "(Landroid/content/Context;Landroid/util/AttributeSet;I)V");
            const auto result =
                c.vm.Call(*target, std::vector<dx::VmValue>{dx::VmValue::Ref(c.receiver), c.arguments[0],
                                                            c.arguments[1], dx::VmValue::Int(0x1010048)});
            if (result.exception.IsValid())
                throw dx::VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                      result.exception};
            return result.value;
        },
        dx::kAccPublic);

    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_widget_EditText.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_EditText(const Context& context) {
    if (context->ui_traversal_owner == AndroidUiTraversalOwner::JavaTraversal)
        return dx::IntrinsicClassBuilder::Class("Landroid/widget/EditText;", "Landroid/widget/TextView;").Build();
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/EditText;", "Landroid/widget/TextView;");
    builder.Constructor("(Landroid/content/Context;)V", ViewInitHandler(context));
    builder.VirtualMethod("getText", "()Landroid/text/Editable;", [context](dx::IntrinsicContext& call) {
        const auto key = "editable:" + std::to_string(call.receiver.Value());
        const auto editable = Singleton(call, context, key, "Landroid/text/EditableImpl;");
        context->editable_owner[editable.Value()] = call.receiver.Value();
        return dx::VmValue::Ref(editable);
    });
    // AOSP 4.4 ROM line 56.
    builder.Constructor(
        "(Landroid/content/Context;Landroid/util/AttributeSet;)V",
        [](dx::IntrinsicContext& c) {
            const auto target =
                c.vm.Linker().FindDirectMethod(c.vm.Linker().ResolveDescriptor("Landroid/widget/EditText;"), "<init>",
                                               "(Landroid/content/Context;Landroid/util/AttributeSet;I)V");
            const auto result =
                c.vm.Call(*target, std::vector<dx::VmValue>{dx::VmValue::Ref(c.receiver), c.arguments[0],
                                                            c.arguments[1], dx::VmValue::Int(0x101006e)});
            if (result.exception.IsValid())
                throw dx::VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                      result.exception};
            return result.value;
        },
        dx::kAccPublic);

    // AOSP 4.4 ROM line 60.
    builder.Constructor(
        "(Landroid/content/Context;Landroid/util/AttributeSet;I)V",
        [](dx::IntrinsicContext& c) {
            const auto target =
                c.vm.Linker().FindDirectMethod(c.vm.Linker().ResolveDescriptor("Landroid/widget/TextView;"), "<init>",
                                               "(Landroid/content/Context;Landroid/util/AttributeSet;I)V");
            const auto result =
                c.vm.Call(*target, std::vector<dx::VmValue>{dx::VmValue::Ref(c.receiver), c.arguments[0],
                                                            c.arguments[1], c.arguments[2]});
            if (result.exception.IsValid())
                throw dx::VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                      result.exception};
            return result.value;
        },
        dx::kAccPublic);

    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_widget_FrameLayout.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_FrameLayout(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/FrameLayout;", "Landroid/view/ViewGroup;");
    builder.OverrideMethod("generateDefaultLayoutParams", "()Landroid/view/ViewGroup$LayoutParams;",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(NewAndroidLayoutParams(call.vm,
                "Landroid/widget/FrameLayout$LayoutParams;", -1, -1));
        }, dx::kAccProtected);
    builder.Constructor("(Landroid/content/Context;)V", ViewInitHandler(context));
    // AOSP 4.4 ROM line 211.
    builder.VirtualMethod(
        "generateDefaultLayoutParams", "()Landroid/widget/FrameLayout$LayoutParams;",
        [](dx::IntrinsicContext& c) {
            const auto width = -1;
            const auto object = c.vm.NewIntrinsicInstance("Landroid/widget/FrameLayout$LayoutParams;");
            const auto root = c.vm.ProtectReferences(std::array{object});
            const auto target = c.vm.Linker().FindDirectMethod(
                c.vm.Linker().ResolveDescriptor("Landroid/widget/FrameLayout$LayoutParams;"), "<init>", "(II)V");
            const auto result =
                c.vm.Call(*target, std::vector<dx::VmValue>{dx::VmValue::Ref(object), dx::VmValue::Int(width),
                                                            dx::VmValue::Int(-1)});
            if (result.exception.IsValid())
                throw dx::VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                      result.exception};
            return dx::VmValue::Ref(object);
        },
        dx::kAccProtected);

    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_widget_ImageButton.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_ImageButton(const Context& context) {
    if (context->ui_traversal_owner == AndroidUiTraversalOwner::JavaTraversal)
        return dx::IntrinsicClassBuilder::Class("Landroid/widget/ImageButton;", "Landroid/widget/ImageView;").Build();
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
    if (context->ui_traversal_owner == AndroidUiTraversalOwner::JavaTraversal)
        return dx::IntrinsicClassBuilder::Class("Landroid/widget/ImageView$ScaleType;", "Ljava/lang/Enum;").Build();
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
    if (context->ui_traversal_owner == AndroidUiTraversalOwner::JavaTraversal) return ExperimentalImageView(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/ImageView;", "Landroid/view/View;");
    builder.Constructor("(Landroid/content/Context;)V", ViewInitHandler(context));
    builder.VirtualMethod("setImageBitmap", "(Landroid/graphics/Bitmap;)V",
        [context](dx::IntrinsicContext& call) {
            auto& vm = call.vm;
            auto& linker = vm.Linker();
            const auto checked = [&linker](const dx::VmCallOutcome& outcome) {
                if (outcome.exception.IsValid()) {
                    throw dx::VmJavaThrow{linker.Class(outcome.exception_class).descriptor,
                                          outcome.exception_message, outcome.exception};
                }
                return outcome.value;
            };
            const auto invoke = [&](const dx::VmObjectRef receiver,
                                    const char* name, const char* descriptor,
                                    std::vector<dx::VmValue> arguments) {
                if (!receiver.IsValid()) {
                    throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "ImageView Context is null"};
                }
                const auto type = vm.Model().ObjectClass(receiver);
                const auto index = linker.FindVtableIndex(type, name, descriptor);
                if (!index) throw std::logic_error("ImageView delegate method is unavailable");
                arguments.insert(arguments.begin(), dx::VmValue::Ref(receiver));
                return checked(vm.Call(linker.Class(type).vtable[*index], arguments));
            };
            const auto type = linker.ResolveDescriptor("Landroid/graphics/drawable/BitmapDrawable;");
            checked(vm.EnsureClassInitialized(type));
            const auto drawable = vm.NewIntrinsicInstance("Landroid/graphics/drawable/BitmapDrawable;");
            const auto drawable_root = vm.ProtectReferences(std::array{drawable});
            const auto owner = dx::IntrinsicCall(call).GetRef(*context->view_context_field);
            const auto resources = invoke(owner, "getResources",
                "()Landroid/content/res/Resources;", {}).ref;
            const auto resource_root = vm.ProtectReferences(std::array{resources});
            const auto constructor = linker.FindDirectMethod(type, "<init>",
                "(Landroid/content/res/Resources;Landroid/graphics/Bitmap;)V");
            if (!constructor) throw std::logic_error("BitmapDrawable constructor is unavailable");
            checked(vm.Call(*constructor, std::array{dx::VmValue::Ref(drawable),
                dx::VmValue::Ref(resources), call.arguments[0]}));
            // API19 creates a fresh wrapper even for the same or a null Bitmap.
            invoke(call.receiver, "setImageDrawable",
                "(Landroid/graphics/drawable/Drawable;)V", {dx::VmValue::Ref(drawable)});
            return dx::VmValue::Void();
        });
    builder.VirtualMethod("setImageDrawable", "(Landroid/graphics/drawable/Drawable;)V",
        [context](dx::IntrinsicContext& call) {
            BindImageDrawable(call.vm, context, call.receiver, call.arguments[0].ref);
            return dx::VmValue::Void();
        });
    builder.VirtualMethod("getDrawable", "()Landroid/graphics/drawable/Drawable;",
        [context](dx::IntrinsicContext& call) {
            const auto found = context->ui_view_images.find(call.receiver.Value());
            return dx::VmValue::Ref(found == context->ui_view_images.end() ? dx::VmObjectRef{} : found->second);
        });
    builder.VirtualMethod("setImageResource", "(I)V", [context](dx::IntrinsicContext& call) {
        const auto node = EnsureViewUiNode(*context, call.receiver, UiClassForObject(call.vm, call.receiver));
        const auto resource_id = static_cast<std::uint32_t>(call.arguments[0].AsInt());
        if (resource_id == 0U) {
            BindImageDrawable(call.vm, context, call.receiver, dx::VmObjectRef{});
            context->ui_tree.Get(node)->image_resource_id = 0;
            context->ui_tree.Get(node)->intrinsic = {};
        } else {
            std::shared_ptr<const ui::UiBitmap> bitmap;
            try {
                bitmap = ResolveUiDrawable(*context, resource_id);
            } catch (const std::runtime_error& error) {
                throw dx::VmJavaThrow{"Landroid/content/res/Resources$NotFoundException;", error.what()};
            }
            if (context->ui_view_images.contains(call.receiver.Value()))
                BindImageDrawable(call.vm, context, call.receiver, dx::VmObjectRef{});
            context->ui_tree.Get(node)->image_resource_id = resource_id;
            context->ui_tree.Get(node)->intrinsic = {bitmap->width, bitmap->height};
        }
        context->ui_tree.MarkLayoutDirty(node);
        return dx::VmValue::Void();
    });
    builder.VirtualMethod(
        "setScaleType", "(Landroid/widget/ImageView$ScaleType;)V", [context](dx::IntrinsicContext& call) {
            const auto value = call.arguments[0].ref;
            const auto found = context->ui_image_scale_types.find(value.Value());
            if (!value.IsValid() || found == context->ui_image_scale_types.end()) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                      "unknown ImageView ScaleType"};
            }
            const auto node = EnsureViewUiNode(
                *context, call.receiver,
                UiClassForObject(call.vm, call.receiver));
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
    builder.OverrideMethod("generateDefaultLayoutParams", "()Landroid/view/ViewGroup$LayoutParams;",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(NewAndroidLayoutParams(call.vm,
                "Landroid/widget/LinearLayout$LayoutParams;",
                context->ui_tree.Get(EnsureViewUiNode(*context, call.receiver, ui::UiClass::LinearLayout))->orientation == ui::Orientation::Horizontal ? -2 : -1, -2));
        }, dx::kAccProtected);
    builder.Constructor("(Landroid/content/Context;)V", ViewInitHandler(context));
    builder.Constructor("(Landroid/content/Context;Landroid/util/AttributeSet;)V",
                        ViewNullAttributeSetInitHandler(context));
    builder.VirtualMethod("setOrientation", "(I)V", [context](dx::IntrinsicContext& call) {
        const auto value = call.arguments[0].AsInt();
        if (value != 0 && value != 1) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                  "LinearLayout orientation must be horizontal or vertical"};
        }
        const auto node = EnsureViewUiNode(*context, call.receiver, ui::UiClass::LinearLayout);
        context->ui_tree.Get(node)->orientation = value == 0 ? ui::Orientation::Horizontal : ui::Orientation::Vertical;
        context->ui_tree.MarkLayoutDirty(node);
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("getOrientation", "()I", [context](dx::IntrinsicContext& call) {
        const auto node = EnsureViewUiNode(*context, call.receiver, ui::UiClass::LinearLayout);
        return dx::VmValue::Int(context->ui_tree.Get(node)->orientation == ui::Orientation::Horizontal ? 0 : 1);
    });
    builder.VirtualMethod("setGravity", "(I)V", [context](dx::IntrinsicContext& call) {
        const auto node = EnsureViewUiNode(*context, call.receiver, ui::UiClass::LinearLayout);
        context->ui_tree.Get(node)->gravity = static_cast<std::uint32_t>(call.arguments[0].AsInt());
        context->ui_tree.MarkLayoutDirty(node);
        return dx::VmValue::Void();
    });
    // AOSP 4.4 ROM line 1802.
    builder.VirtualMethod(
        "generateDefaultLayoutParams", "()Landroid/widget/LinearLayout$LayoutParams;",
        [](dx::IntrinsicContext& c) {
            const auto orientation = CallAndroidMethod(c.vm, c.receiver, "getOrientation", "()I").AsInt();
            if (orientation != 0 && orientation != 1) return dx::VmValue::Ref(dx::VmObjectRef{});
            const auto width = orientation == 0 ? -2 : -1;
            const auto object = c.vm.NewIntrinsicInstance("Landroid/widget/LinearLayout$LayoutParams;");
            const auto root = c.vm.ProtectReferences(std::array{object});
            const auto target = c.vm.Linker().FindDirectMethod(
                c.vm.Linker().ResolveDescriptor("Landroid/widget/LinearLayout$LayoutParams;"), "<init>", "(II)V");
            const auto result =
                c.vm.Call(*target, std::vector<dx::VmValue>{dx::VmValue::Ref(object), dx::VmValue::Int(width),
                                                            dx::VmValue::Int(-2)});
            if (result.exception.IsValid())
                throw dx::VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                      result.exception};
            return dx::VmValue::Ref(object);
        },
        dx::kAccProtected);

    if (context->ui_traversal_owner == AndroidUiTraversalOwner::JavaTraversal) {
        // Retain these containers' own ROM failure boundary instead of inheriting
        // the experimental View/FrameLayout algorithms through a missing overlay.
        builder.UnimplementedOverride("onMeasure", "(II)V", dx::kAccProtected);
        builder.UnimplementedOverride("onLayout", "(ZIIII)V", dx::kAccProtected);
    }
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
    builder.VirtualMethod("setMax", "(I)V", WidgetNoopHandler());
    builder.VirtualMethod("setProgress", "(I)V", WidgetNoopHandler());
    // AOSP 4.4 ROM line 242.
    builder.Constructor(
        "(Landroid/content/Context;Landroid/util/AttributeSet;)V",
        [](dx::IntrinsicContext& c) {
            const auto target =
                c.vm.Linker().FindDirectMethod(c.vm.Linker().ResolveDescriptor("Landroid/widget/ProgressBar;"),
                                               "<init>", "(Landroid/content/Context;Landroid/util/AttributeSet;I)V");
            const auto result =
                c.vm.Call(*target, std::vector<dx::VmValue>{dx::VmValue::Ref(c.receiver), c.arguments[0],
                                                            c.arguments[1], dx::VmValue::Int(0x1010077)});
            if (result.exception.IsValid())
                throw dx::VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                      result.exception};
            return result.value;
        },
        dx::kAccPublic);

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
    builder.OverrideMethod(
        "generateDefaultLayoutParams",
        "()Landroid/view/ViewGroup$LayoutParams;",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(NewAndroidLayoutParams(
                call.vm, "Landroid/widget/RelativeLayout$LayoutParams;", -2,
                -2));
        },
        dx::kAccProtected);
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
    if (context->ui_traversal_owner == AndroidUiTraversalOwner::JavaTraversal) {
        // Retain these containers' own ROM failure boundary instead of inheriting
        // the experimental View/FrameLayout algorithms through a missing overlay.
        builder.UnimplementedOverride("onMeasure", "(II)V", dx::kAccProtected);
        builder.UnimplementedOverride("onLayout", "(ZIIII)V", dx::kAccProtected);
    }
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_widget_ScrollView.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_ScrollView(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/ScrollView;", "Landroid/widget/FrameLayout;");
    builder.Constructor("(Landroid/content/Context;)V", [](dx::IntrinsicContext& call) {
        const auto target =
            call.vm.Linker().FindDirectMethod(call.vm.Linker().ResolveDescriptor("Landroid/widget/FrameLayout;"),
                                              "<init>", "(Landroid/content/Context;)V");
        const auto result = call.vm.Call(*target, std::array{dx::VmValue::Ref(call.receiver), call.arguments[0]});
        if (result.exception.IsValid())
            throw dx::VmJavaThrow{call.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                  result.exception};
        return result.value;
    });
    if (context->ui_traversal_owner == AndroidUiTraversalOwner::JavaTraversal) {
        // Retain these containers' own ROM failure boundary instead of inheriting
        // the experimental View/FrameLayout algorithms through a missing overlay.
        builder.UnimplementedOverride("onMeasure", "(II)V", dx::kAccProtected);
        builder.UnimplementedOverride("onLayout", "(ZIIII)V", dx::kAccProtected);
    }
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_widget_TableLayout.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_TableLayout(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/TableLayout;", "Landroid/widget/LinearLayout;");
    builder.Constructor("(Landroid/content/Context;)V", [](dx::IntrinsicContext& c) {
        const auto target = c.vm.Linker().FindDirectMethod(
            c.vm.Linker().ResolveDescriptor("Landroid/widget/LinearLayout;"), "<init>", "(Landroid/content/Context;)V");
        std::vector<dx::VmValue> arguments{dx::VmValue::Ref(c.receiver)};
        const std::vector<dx::VmValue> parameters{c.arguments[0]};
        arguments.insert(arguments.end(), parameters.begin(), parameters.end());
        const auto result = c.vm.Call(*target, arguments);
        if (result.exception.IsValid())
            throw dx::VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                  result.exception};
        return result.value;
    });
    if (context->ui_traversal_owner == AndroidUiTraversalOwner::JavaTraversal) {
        // Retain these containers' own ROM failure boundary instead of inheriting
        // the experimental View/FrameLayout algorithms through a missing overlay.
        builder.UnimplementedOverride("onMeasure", "(II)V", dx::kAccProtected);
        builder.UnimplementedOverride("onLayout", "(ZIIII)V", dx::kAccProtected);
    }
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_widget_TableRow.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_TableRow(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/TableRow;", "Landroid/widget/LinearLayout;");
    builder.Constructor("(Landroid/content/Context;)V", [](dx::IntrinsicContext& c) {
        const auto target = c.vm.Linker().FindDirectMethod(
            c.vm.Linker().ResolveDescriptor("Landroid/widget/LinearLayout;"), "<init>", "(Landroid/content/Context;)V");
        std::vector<dx::VmValue> arguments{dx::VmValue::Ref(c.receiver)};
        const std::vector<dx::VmValue> parameters{c.arguments[0]};
        arguments.insert(arguments.end(), parameters.begin(), parameters.end());
        const auto result = c.vm.Call(*target, arguments);
        if (result.exception.IsValid())
            throw dx::VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                  result.exception};
        return result.value;
    });
    if (context->ui_traversal_owner == AndroidUiTraversalOwner::JavaTraversal) {
        // Retain these containers' own ROM failure boundary instead of inheriting
        // the experimental View/FrameLayout algorithms through a missing overlay.
        builder.UnimplementedOverride("onMeasure", "(II)V", dx::kAccProtected);
        builder.UnimplementedOverride("onLayout", "(ZIIII)V", dx::kAccProtected);
    }
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_widget_TextView.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {
namespace {

using TextAttributes = std::unordered_map<std::uint32_t, loader::ArscBagValue>;

[[noreturn]] void UnsupportedTextStyle(dx::Interpreter& vm, const char* message) {
    if (auto* ledger = vm.Ledger()) ledger->RecordUnimplemented("dexvm.text_appearance", 0);
    throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", message};
}

dx::VmValue TextStatic(dx::Interpreter& vm, const char* owner, const char* name,
                        const char* signature, std::vector<dx::VmValue> arguments) {
    const auto type = vm.Linker().ResolveDescriptor(owner);
    const auto method = vm.Linker().FindDirectMethod(type, name, signature);
    if (!method) throw dx::DexVmError(dx::DexVmErrorReason::unresolved_reference, name);
    const auto outcome = vm.Call(*method, arguments);
    if (outcome.exception.IsValid())
        throw dx::VmJavaThrow{vm.Linker().Class(outcome.exception_class).descriptor,
                              outcome.exception_message, outcome.exception};
    return outcome.value;
}

bool IsFrameworkAttribute(dx::Interpreter& vm, std::uint32_t id) {
    const auto type = vm.Linker().ResolveDescriptor("Landroid/R$attr;");
    const auto initialized = vm.EnsureClassInitialized(type);
    if (initialized.exception.IsValid())
        throw dx::VmJavaThrow{vm.Linker().Class(initialized.exception_class).descriptor,
                              initialized.exception_message, initialized.exception};
    const auto& klass = vm.Linker().Class(type);
    for (const auto field : klass.own_static_fields) {
        const auto& info = vm.Linker().Field(field);
        if (info.descriptor == "I" && klass.static_storage[info.slot] == id) return true;
    }
    return false;
}

void MergeTextStyle(dx::Interpreter& vm, DexVmAndroidContext& context, std::uint32_t id,
                    TextAttributes& values, std::unordered_set<std::uint32_t>& seen) {
    if (id == 0) return;
    if (seen.size() >= 16 || !seen.insert(id).second)
        UnsupportedTextStyle(vm, "text style parent/reference cycle or depth exceeds 16");
    // AOSP core/res/res/values/themes.xml + colors.xml. This is the TextAppearance
    // projection of these themes, not a full framework resource table.
    const bool legacy = id >= 0x01030005U && id <= 0x01030011U;
    const bool holo = id == 0x0103006bU || id == 0x01030128U;
    if (legacy || holo) {
        constexpr std::array<std::uint32_t, 6> legacy_appearances{
            0x01030042U, 0x01030044U, 0x01030046U, 0x01030043U, 0x01030045U, 0x01030047U};
        constexpr std::array<std::uint32_t, 6> holo_appearances{
            0x010300fdU, 0x010300ffU, 0x01030101U, 0x010300feU, 0x01030100U, 0x01030102U};
        for (std::uint32_t i = 0; i < 6; ++i) {
            const auto attr = 0x01010040U + i;
            values[attr] = {attr, 1, (holo ? holo_appearances : legacy_appearances)[i], {}};
        }
        const bool light = id >= 0x0103000cU && id <= 0x0103000eU;
        const auto color = [&](std::uint32_t attr, std::uint32_t argb) {
            values[attr] = {attr, 0x1c, argb, {}};
        };
        color(0x01010099, holo ? 0x6633b5e5U : 0x9983cc39U); // highlight
        color(0x0101009a, 0xff808080U); // hint
        color(0x0101009b, holo ? 0xff33b5e5U : light ? 0xff0000eeU : 0xff5c5cffU); // link
        if (legacy) {
            values[0x01010048U] = {0x01010048U, 1, 0x01030014U, {}}; // buttonStyle
            values[0x01010049U] = {0x01010049U, 1, 0x01030016U, {}}; // buttonStyleSmall
            values[0x01010045U] = {0x01010045U, 1, 0x01030047U, {}}; // textAppearanceSmallInverse
        }
        return;
    }
    // Passing an attr id to obtainStyledAttributes(resid, attrs) does not
    // resolve that attr to a style: its metadata bag has no appearance values.
    if ((id >> 24U) == 1U) {
        if (IsFrameworkAttribute(vm, id) || id == 0x01030040U || id == 0x01030048U) return;
        UnsupportedTextStyle(vm, "framework text style is outside the registered resource projection");
    }
    const auto* entry = context.arsc.FindById(id);
    if (!entry) throw dx::VmJavaThrow{"Landroid/content/res/Resources$NotFoundException;", "text style resource is missing"};
    if (entry->value_type == 1) {
        MergeTextStyle(vm, context, entry->value_data, values, seen);
        return;
    }
    if (entry->type_name == "attr") return;
    if (entry->type_name != "style" || !entry->is_complex)
        UnsupportedTextStyle(vm, "resource is not an attribute or style bag");
    MergeTextStyle(vm, context, entry->parent, values, seen);
    for (const auto& value : entry->bag) values[value.name] = value;
}

TextAttributes ResolveTextTheme(dx::Interpreter& vm, DexVmAndroidContext& context,
                                dx::VmObjectRef owner) {
    if (!owner.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "text appearance Context is null"};
    auto theme = context.application_theme;
    std::unordered_set<std::uint32_t> wrappers;
    for (auto current = owner; current.IsValid();) {
        if (wrappers.size() >= 16 || !wrappers.insert(current.Value()).second)
            UnsupportedTextStyle(vm, "ContextWrapper cycle or depth exceeds 16");
        const auto type = vm.Model().ObjectClass(current);
        if (vm.Linker().FindVtableIndex(type, "getThemeResId", "()I")) {
            theme = static_cast<std::uint32_t>(CallAndroidMethod(vm, current, "getThemeResId", "()I").AsInt());
            break;
        }
        const auto base = vm.Linker().FindFieldRecursive(type, "mBase", "Landroid/content/Context;");
        if (!base) break;
        current = dx::VmObjectRef(vm.Model().InstanceSlots(current)[vm.Linker().Field(*base).slot].bits);
        if (!current.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "ContextWrapper base is null"};
    }
    if (theme == 0) theme = context.target_sdk_version < 11 ? 0x01030005U :
                            context.target_sdk_version < 14 ? 0x0103006bU : 0x01030128U;
    TextAttributes theme_values;
    std::unordered_set<std::uint32_t> seen;
    MergeTextStyle(vm, context, theme, theme_values, seen);
    return theme_values;
}

TextAttributes ResolveTextAppearance(dx::Interpreter& vm, const Context& context,
                                      dx::VmObjectRef owner, std::uint32_t id) {
    const auto theme_values = ResolveTextTheme(vm, *context, owner);
    auto values = theme_values;
    std::unordered_set<std::uint32_t> seen;
    MergeTextStyle(vm, *context, id, values, seen);
    // Attribute references resolve against the theme, not against the overlay.
    for (const auto attr : {0x01010095U, 0x01010096U, 0x01010097U, 0x01010098U,
                           0x01010099U, 0x0101009aU, 0x0101009bU, 0x01010161U,
                           0x01010162U, 0x01010163U, 0x01010164U, 0x0101038cU, 0x010103acU}) {
        const auto found = values.find(attr);
        if (found == values.end()) continue;
        auto& value = found->second;
        std::unordered_set<std::uint64_t> references;
        while (value.value_type == 1 || value.value_type == 2) {
            if (value.value_data == 0) { value = {}; break; }
            const auto key = (static_cast<std::uint64_t>(value.value_type) << 32U) | value.value_data;
            if (references.size() >= 16 || !references.insert(key).second)
                UnsupportedTextStyle(vm, "text attribute reference cycle or depth exceeds 16");
            if (value.value_type == 2) {
                const auto target = theme_values.find(value.value_data);
                if (target == theme_values.end())
                    UnsupportedTextStyle(vm, "theme attribute is unavailable");
                value = target->second;
            } else {
                const auto* target = context->arsc.FindById(value.value_data);
                if (!target || target->is_complex)
                    UnsupportedTextStyle(vm, "text style reference is unavailable or complex");
                value = {attr, target->value_type, target->value_data, target->string_value};
            }
        }
    }
    return values;
}

ui::UiNodeId TextNode(dx::IntrinsicContext& call, const Context& context) {
    return EnsureViewUiNode(
        *context, call.receiver, UiClassForObject(call.vm, call.receiver));
}

std::uint32_t AndroidColorToRgba(const std::uint32_t argb) {
    return ((argb & 0x00ffffffU) << 8U) | (argb >> 24U);
}

// API 19 public.xml: android.R.attr.buttonStyle/buttonStyleSmall.
constexpr std::uint32_t kButtonStyleAttr = 0x01010048U;
constexpr std::uint32_t kButtonStyleSmallAttr = 0x01010049U;

}  // namespace

std::uint32_t ResolveTextAppearanceAttribute(dx::Interpreter& vm,
    DexVmAndroidContext& context, const dx::VmObjectRef owner, std::uint32_t attribute) {
    const auto values = ResolveTextTheme(vm, context, owner);
    std::unordered_set<std::uint32_t> seen;
    for (;;) {
        if (seen.size() >= 16 || !seen.insert(attribute).second)
            UnsupportedTextStyle(vm, "textAppearance theme attribute cycle or depth exceeds 16");
        const auto found = values.find(attribute);
        if (found == values.end()) UnsupportedTextStyle(vm, "textAppearance theme attribute is unavailable");
        const auto& value = found->second;
        if (value.value_type == 1 && value.value_data != 0) return value.value_data;
        if (value.value_type != 2) UnsupportedTextStyle(vm, "textAppearance theme value is not a style reference");
        attribute = value.value_data;
    }
}

dx::IntrinsicHandler ViewDefaultStyleInitHandler(const Context& context) {
    return dx::IntrinsicHandler(
        [context](dx::IntrinsicContext& call) {
            const auto owner = call.arguments[0].ref;
            if (!owner.IsValid())
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "View Context is null"};
            AssignViewContext(call.vm, *context, call.receiver, owner);
            const auto attrs = call.arguments[1].ref;
            const auto def_style_attr = static_cast<std::uint32_t>(
                call.arguments[2].AsInt());
            if (attrs.IsValid()) {
                if (auto* ledger = call.vm.Ledger()) {
                    ledger->RecordUnimplemented(
                        "dexvm.view_xml_attributes", 0);
                }
                throw dx::VmJavaThrow{
                    "Ljava/lang/UnsupportedOperationException;",
                    "constructing a View from an AttributeSet is unsupported"};
            }
            if (def_style_attr == 0U) return ViewInitHandler(context)(call);
            InitializeAndroidViewJavaLayout(call.vm, *context, call.receiver);
            if (def_style_attr != kButtonStyleAttr &&
                def_style_attr != kButtonStyleSmallAttr) {
                if (auto* ledger = call.vm.Ledger()) {
                    ledger->RecordUnimplemented(
                        "dexvm.view_default_style", 0);
                }
                throw dx::VmJavaThrow{
                    "Ljava/lang/UnsupportedOperationException;",
                    "defStyleAttr is outside the registered default-style projection"};
            }
            // Resolve against the actual constructor Context, including theme
            // inheritance and aliases. Never silently replace an app override
            // or a Holo/DeviceDefault widget with the legacy projection.
            const auto theme = ResolveTextTheme(call.vm, *context, owner);
            const auto resolve = [&](std::uint32_t attr) {
                const auto found = theme.find(attr);
                if (found == theme.end())
                    UnsupportedTextStyle(call.vm, "button theme attribute is outside the registered projection");
                auto value = found->second;
                std::unordered_set<std::uint64_t> seen;
                while (value.value_type == 1 || value.value_type == 2) {
                    const auto key = (static_cast<std::uint64_t>(value.value_type) << 32U) | value.value_data;
                    if (seen.size() >= 16 || !seen.insert(key).second)
                        UnsupportedTextStyle(call.vm, "button theme reference cycle or depth exceeds 16");
                    if (value.value_type == 2) {
                        const auto target = theme.find(value.value_data);
                        if (target == theme.end())
                            UnsupportedTextStyle(call.vm, "button theme attribute is unavailable");
                        value = target->second;
                    } else {
                        const auto* target = context->arsc.FindById(value.value_data);
                        if (!target || target->is_complex) break;
                        value = {attr, target->value_type, target->value_data, target->string_value};
                    }
                }
                return value;
            };
            const auto style = resolve(def_style_attr);
            const auto appearance = resolve(0x01010045U);
            if (style.value_type != 1 ||
                (style.value_data != 0x01030014U && style.value_data != 0x01030016U) ||
                appearance.value_type != 1 || appearance.value_data != 0x01030047U) {
                UnsupportedTextStyle(call.vm, "button default style or text appearance is outside the registered projection");
            }
            // Default-style projection of pinned AOSP 4.4.4
            // core/res/res/values/{themes,styles}.xml: theme maps
            // buttonStyle(Small) to Widget.Button(.Small), which only adds a
            // 9-patch background over Widget.Button's TextAppearance.Small
            // chain (textSize 14sp) and textColor primary_text_light.
            const auto resources = CallAndroidMethod(call.vm, owner, "getResources",
                "()Landroid/content/res/Resources;").ref;
            const auto resource_root =
                call.vm.ProtectReferences(std::array{resources});
            const auto metrics = CallAndroidMethod(
                call.vm, resources, "getDisplayMetrics",
                "()Landroid/util/DisplayMetrics;").ref;
            const auto metrics_root =
                call.vm.ProtectReferences(std::array{metrics});
            const auto size = TextStatic(
                call.vm, "Landroid/util/TypedValue;",
                "complexToDimensionPixelSize",
                "(ILandroid/util/DisplayMetrics;)I",
                {dx::VmValue::Int(static_cast<std::int32_t>(
                     (14U << 8U) | 0x02U)),
                 dx::VmValue::Ref(metrics)}).AsInt();
            try {
                static_cast<void>(ui::MeasureFixedText(
                    u"", static_cast<float>(size)));
            } catch (const std::runtime_error& error) {
                throw dx::VmJavaThrow{
                    "Ljava/lang/IllegalArgumentException;", error.what()};
            }
            const auto node = TextNode(call, context);
            auto* state = context->ui_tree.Get(node);
            state->text_size_px = static_cast<float>(size);
            state->text_color = AndroidColorToRgba(0xff000000U);
            static_cast<void>(
                CallAndroidMethod(call.vm, call.receiver, "setGravity", "(I)V", {dx::VmValue::Int(0x11)}));
            context->ui_tree.SetClickable(node, true);
            context->ui_tree.MarkLayoutDirty(node);
            // The 9-patch button background has no renderer here; the gap
            // stays queryable instead of being silently swallowed.
            if (auto* ledger = call.vm.Ledger()) {
                ledger->RecordUnimplemented(
                    "dexvm.view_default_style.background", 0);
            }
            return dx::VmValue::Void();
        });
}

namespace {
// Read the actual ROM R.styleable, including its index constants. No parallel
// index/default table is introduced by the adapter.
struct TextStyleable final {
    dx::VmObjectRef array;
    std::map<int, std::string> names;
};

TextStyleable RomTextAttributes(dx::Interpreter& vm, std::string_view group) {
    const auto owner = vm.Linker().ResolveDescriptor("Lcom/android/internal/R$styleable;");
    const auto initialized = vm.EnsureClassInitialized(owner);
    if (initialized.exception.IsValid())
        throw dx::VmJavaThrow{vm.Linker().Class(initialized.exception_class).descriptor, initialized.exception_message,
                              initialized.exception};
    TextStyleable result;
    const auto& cls = vm.Linker().Class(owner);
    const auto prefix = std::string(group) + "_";
    for (const auto id : cls.own_static_fields) {
        const auto& f = vm.Linker().Field(id);
        if (f.name == group && f.descriptor == "[I") result.array = dx::VmObjectRef{cls.static_storage[f.slot]};
        if (f.name.starts_with(prefix) && f.descriptor == "I")
            result.names.emplace(static_cast<int>(cls.static_storage[f.slot]), f.name.substr(prefix.size()));
    }
    if (!result.array.IsValid())
        throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant, "ROM styleable missing");
    return result;
}

[[noreturn]] void ImageFailure(dx::Interpreter& vm, const std::string& message) {
    if (auto* ledger = vm.Ledger()) ledger->RecordUnimplemented("dexvm.image_leaf", 0);
    throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", message};
}

Decl ExperimentalImageView(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/ImageView;", "Landroid/view/View;");
    const auto drawable =
        builder.BoundInstanceField("mDrawable", "Landroid/graphics/drawable/Drawable;", dx::kAccPrivate);
    const auto scale =
        builder.BoundInstanceField("mScaleType", "Landroid/widget/ImageView$ScaleType;", dx::kAccPrivate);
    const auto alpha = builder.BoundInstanceField("mAlpha", "I", dx::kAccPrivate);
    const auto matrix = builder.BoundInstanceField("mMatrix", "Landroid/graphics/Matrix;", dx::kAccPrivate);
    const auto width = builder.BoundInstanceField("mDrawableWidth", "I", dx::kAccPrivate);
    const auto height = builder.BoundInstanceField("mDrawableHeight", "I", dx::kAccPrivate);
    const auto compat = builder.BoundInstanceField("mAdjustViewBoundsCompat", "Z", dx::kAccPrivate);
    const auto maxw = builder.BoundInstanceField("mMaxWidth", "I", dx::kAccPrivate);
    const auto maxh = builder.BoundInstanceField("mMaxHeight", "I", dx::kAccPrivate);
    const auto baseline = builder.BoundInstanceField("mBaseline", "I", dx::kAccPrivate);
    const auto src_rect = builder.BoundInstanceField("mTempSrc", "Landroid/graphics/RectF;", dx::kAccPrivate);
    const auto dst_rect = builder.BoundInstanceField("mTempDst", "Landroid/graphics/RectF;", dx::kAccPrivate);
    const auto changed = [](dx::IntrinsicContext& c) {
        static_cast<void>(CallAndroidMethod(c.vm, c.receiver, "requestLayout", "()V"));
        static_cast<void>(CallAndroidMethod(c.vm, c.receiver, "invalidate", "()V"));
    };
    const auto init = [=](dx::IntrinsicContext& c) {
        auto& vm = c.vm;
        const bool attributes = c.arguments.size() != 1;
        const auto view = vm.Linker().ResolveDescriptor("Landroid/view/View;");
        const auto ctor = vm.Linker().FindDirectMethod(
            view, "<init>",
            attributes ? "(Landroid/content/Context;Landroid/util/AttributeSet;I)V" : "(Landroid/content/Context;)V");
        std::vector<dx::VmValue> args{dx::VmValue::Ref(c.receiver)};
        args.insert(args.end(), c.arguments.begin(), c.arguments.end());
        const auto result = vm.Call(*ctor, args);
        if (result.exception.IsValid())
            throw dx::VmJavaThrow{vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                  result.exception};
        dx::IntrinsicCall fields(c);
        fields.SetInt(alpha, 255);
        fields.SetInt(maxw, INT32_MAX);
        fields.SetInt(maxh, INT32_MAX);
        fields.SetInt(baseline, -1);
        fields.SetInt(width, 0);
        fields.SetInt(height, 0);
        const auto allocate = [&](const char* name) {
            const auto type = vm.Linker().ResolveDescriptor(name);
            const auto initialized = vm.EnsureClassInitialized(type);
            if (initialized.exception.IsValid())
                throw dx::VmJavaThrow{vm.Linker().Class(initialized.exception_class).descriptor,
                                      initialized.exception_message, initialized.exception};
            const auto value = vm.NewIntrinsicInstance(name);
            const auto root = vm.ProtectReferences(std::array{value});
            const auto out =
                vm.Call(*vm.Linker().FindDirectMethod(type, "<init>", "()V"), std::array{dx::VmValue::Ref(value)});
            if (out.exception.IsValid())
                throw dx::VmJavaThrow{vm.Linker().Class(out.exception_class).descriptor, out.exception_message,
                                      out.exception};
            return value;
        };
        fields.SetRef(matrix, allocate("Landroid/graphics/Matrix;"));
        fields.SetRef(src_rect, allocate("Landroid/graphics/RectF;"));
        fields.SetRef(dst_rect, allocate("Landroid/graphics/RectF;"));
        const auto enum_type = vm.Linker().ResolveDescriptor("Landroid/widget/ImageView$ScaleType;");
        const auto sf = vm.Linker().FindFieldRecursive(enum_type, "FIT_CENTER", "Landroid/widget/ImageView$ScaleType;");
        fields.SetRef(scale, dx::VmObjectRef{vm.Linker().Class(enum_type).static_storage[vm.Linker().Field(*sf).slot]});
        const auto info =
            CallAndroidMethod(vm, c.arguments[0].ref, "getApplicationInfo", "()Landroid/content/pm/ApplicationInfo;")
                .ref;
        const auto target = vm.Linker().FindFieldRecursive(vm.Model().ObjectClass(info), "targetSdkVersion", "I");
        fields.SetInt(compat,
                      static_cast<int>(vm.Model().InstanceSlots(info)[vm.Linker().Field(*target).slot].bits) <= 17);
        if (attributes) {
            const auto attrs = c.arguments[1].ref;
            const auto group = RomTextAttributes(vm, "ImageView");
            if (attrs.IsValid()) {
                std::set<std::uint32_t> known;
                for (auto name : {"View", "ImageView", "ViewGroup_Layout", "ViewGroup_MarginLayout",
                                  "FrameLayout_Layout", "LinearLayout_Layout", "RelativeLayout_Layout"}) {
                    const auto array = RomTextAttributes(vm, name).array;
                    for (JniSize i = 0; i < vm.Model().ArrayLength(array); ++i)
                        known.insert(static_cast<std::uint32_t>(vm.Model().GetPrimitiveElement(array, i)));
                }
                const auto count = CallAndroidMethod(vm, attrs, "getAttributeCount", "()I").AsInt();
                for (int i = 0; i < count; ++i) {
                    const auto id =
                        CallAndroidMethod(vm, attrs, "getAttributeNameResource", "(I)I", {dx::VmValue::Int(i)}).cat1;
                    if (id == 0 && vm.StringUtf8(CallAndroidMethod(vm, attrs, "getAttributeName",
                                                                   "(I)Ljava/lang/String;", {dx::VmValue::Int(i)})
                                                     .ref) == "style")
                        continue;
                    if (!known.contains(id)) ImageFailure(vm, "unknown ImageView XML attribute");
                }
            }
            const auto theme =
                CallAndroidMethod(vm, c.arguments[0].ref, "getTheme", "()Landroid/content/res/Resources$Theme;").ref;
            const auto theme_root = vm.ProtectReferences(std::array{theme});
            const auto array =
                CallAndroidMethod(vm, theme, "obtainStyledAttributes",
                                  "(Landroid/util/AttributeSet;[III)Landroid/content/res/TypedArray;",
                                  {c.arguments[1], dx::VmValue::Ref(group.array), c.arguments[2], dx::VmValue::Int(0)})
                    .ref;
            const auto array_root = vm.ProtectReferences(std::array{array});
            std::map<std::string, int> indices;
            for (const auto& [index, name] : group.names)
                indices.emplace(name, index);
            const auto get = [&](const char* name, const char* method, int fallback) {
                return CallAndroidMethod(vm, array, method, "(II)I",
                                         {dx::VmValue::Int(indices.at(name)), dx::VmValue::Int(fallback)})
                    .AsInt();
            };
            const auto boolean = [&](const char* name) {
                return CallAndroidMethod(vm, array, "getBoolean", "(IZ)Z",
                                         {dx::VmValue::Int(indices.at(name)), dx::VmValue::Int(0)})
                    .AsInt();
            };
            if (boolean("baselineAlignBottom") || get("baseline", "getDimensionPixelSize", -1) != -1 ||
                boolean("adjustViewBounds") || get("maxWidth", "getDimensionPixelSize", INT32_MAX) != INT32_MAX ||
                get("maxHeight", "getDimensionPixelSize", INT32_MAX) != INT32_MAX || get("tint", "getInt", 0) != 0 ||
                boolean("cropToPadding"))
                ImageFailure(vm, "unsupported ImageView bounds/baseline/tint/crop request");
            const auto st = get("scaleType", "getInt", -1);
            if (st >= 0) {
                if (st >= 8) ImageFailure(vm, "invalid ImageView scaleType");
                const auto values =
                    vm.Linker().FindFieldRecursive(vm.Linker().ResolveDescriptor("Landroid/widget/ImageView;"),
                                                   "sScaleTypeArray", "[Landroid/widget/ImageView$ScaleType;");
                const auto value_array =
                    dx::VmObjectRef{vm.Linker()
                                        .Class(vm.Linker().ResolveDescriptor("Landroid/widget/ImageView;"))
                                        .static_storage[vm.Linker().Field(*values).slot]};
                static_cast<void>(CallAndroidMethod(vm, c.receiver, "setScaleType",
                                                    "(Landroid/widget/ImageView$ScaleType;)V",
                                                    {dx::VmValue::Ref(vm.Model().GetObjectElement(value_array, st))}));
            }
            const auto image = CallAndroidMethod(vm, array, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;",
                                                 {dx::VmValue::Int(indices.at("src"))})
                                   .ref;
            const auto image_root = vm.ProtectReferences(std::array{image});
            if (image.IsValid())
                static_cast<void>(CallAndroidMethod(vm, c.receiver, "setImageDrawable",
                                                    "(Landroid/graphics/drawable/Drawable;)V",
                                                    {dx::VmValue::Ref(image)}));
            const auto value = get("drawableAlpha", "getInt", 255);
            if (value != 255)
                static_cast<void>(
                    CallAndroidMethod(vm, c.receiver, "setImageAlpha", "(I)V", {dx::VmValue::Int(value)}));
            static_cast<void>(CallAndroidMethod(vm, array, "recycle", "()V"));
        }
        ProjectAndroidImage(vm, *context, c.receiver);
        return dx::VmValue::Void();
    };
    builder.Constructor("(Landroid/content/Context;)V", init);
    builder.Constructor("(Landroid/content/Context;Landroid/util/AttributeSet;I)V", init);
    builder.VirtualMethod("setImageDrawable", "(Landroid/graphics/drawable/Drawable;)V", [=](dx::IntrinsicContext& c) {
        const auto value = c.arguments[0].ref;
        if (value.IsValid()) {
            const auto& type = c.vm.Linker().Class(c.vm.Model().ObjectClass(value)).descriptor;
            const auto found = context->ui_drawables.find(value.Value());
            if (found == context->ui_drawables.end() || (type != "Landroid/graphics/drawable/BitmapDrawable;" &&
                                                         type != "Landroid/graphics/drawable/ColorDrawable;"))
                ImageFailure(c.vm, "unreviewed Drawable");
            const auto source = found->second.bitmap_drawable
                                    ? CallAndroidMethod(c.vm, value, "getBitmap", "()Landroid/graphics/Bitmap;").ref
                                    : dx::VmObjectRef{};
            if (source.IsValid()) {
                const auto b = context->bitmaps.find(source.Value());
                if (b == context->bitmaps.end() || b->second.recycled)
                    throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "recycled image bitmap"};
            }
        }
        dx::IntrinsicCall f(c);
        f.SetRef(drawable, value);
        f.SetInt(width, value.IsValid() ? CallAndroidMethod(c.vm, value, "getIntrinsicWidth", "()I").AsInt() : -1);
        f.SetInt(height, value.IsValid() ? CallAndroidMethod(c.vm, value, "getIntrinsicHeight", "()I").AsInt() : -1);
        ProjectAndroidImage(c.vm, *context, c.receiver);
        changed(c);
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("setImageResource", "(I)V", [](dx::IntrinsicContext& c) {
        dx::VmObjectRef image;
        if (c.arguments[0].AsInt() != 0) {
            const auto resources =
                CallAndroidMethod(c.vm, c.receiver, "getResources", "()Landroid/content/res/Resources;").ref;
            image = CallAndroidMethod(c.vm, resources, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;",
                                      {c.arguments[0]})
                        .ref;
        }
        const auto root = c.vm.ProtectReferences(std::array{image});
        static_cast<void>(CallAndroidMethod(c.vm, c.receiver, "setImageDrawable",
                                            "(Landroid/graphics/drawable/Drawable;)V", {dx::VmValue::Ref(image)}));
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("setScaleType", "(Landroid/widget/ImageView$ScaleType;)V", [=](dx::IntrinsicContext& c) {
        const auto value = c.arguments[0].ref;
        if (!value.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "scaleType"};
        const auto ordinal = CallAndroidMethod(c.vm, value, "ordinal", "()I").AsInt();
        if (ordinal != 1 && ordinal != 3 && ordinal != 5 && ordinal != 6 && ordinal != 7)
            ImageFailure(c.vm, "unsupported ImageView scaleType");
        dx::IntrinsicCall(c).SetRef(scale, value);
        ProjectAndroidImage(c.vm, *context, c.receiver);
        changed(c);
        return dx::VmValue::Void();
    });
    const auto set_alpha = [=](dx::IntrinsicContext& c) {
        dx::IntrinsicCall(c).SetInt(alpha, c.arguments[0].AsInt() & 255);
        ProjectAndroidImage(c.vm, *context, c.receiver);
        static_cast<void>(CallAndroidMethod(c.vm, c.receiver, "invalidate", "()V"));
        return dx::VmValue::Void();
    };
    builder.VirtualMethod("setAlpha", "(I)V", set_alpha);
    builder.VirtualMethod("setImageAlpha", "(I)V", set_alpha);
    builder.VirtualMethod("onMeasure", "(II)V", [=](dx::IntrinsicContext& c) {
        if (c.vm.CurrentContextToken() != 1U || context->ui_java_owner_thread != std::this_thread::get_id())
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Image measurement requires owner"};
        ProjectAndroidImage(c.vm, *context, c.receiver);
        const auto& node = *context->ui_tree.Get(*FindViewUiNode(*context, c.receiver.Value()));
        dx::IntrinsicCall fields(c);
        const bool present = fields.GetRef(drawable).IsValid();
        fields.SetInt(width, present ? node.intrinsic.width : -1);
        fields.SetInt(height, present ? node.intrinsic.height : -1);
        const ui::ImageLeafInput input{
            present ? ui::Size{std::max(1, node.intrinsic.width), std::max(1, node.intrinsic.height)} : ui::Size{},
            ReadAndroidViewPadding(c.vm, *context, c.receiver),
            {CallAndroidMethod(c.vm, c.receiver, "getSuggestedMinimumWidth", "()I").AsInt(),
             CallAndroidMethod(c.vm, c.receiver, "getSuggestedMinimumHeight", "()I").AsInt()}};
        const auto spec = [&](int bits) {
            const auto mode = static_cast<std::uint32_t>(bits) >> 30U;
            if (mode == 3) ImageFailure(c.vm, "invalid MeasureSpec");
            return ui::MeasureSpec{mode == 1   ? ui::MeasureMode::Exactly
                                   : mode == 2 ? ui::MeasureMode::AtMost
                                               : ui::MeasureMode::Unspecified,
                                   bits & 0x3fffffff};
        };
        const auto result = ui::MeasureImageLeaf(input, spec(c.arguments[0].AsInt()), spec(c.arguments[1].AsInt()));
        static_cast<void>(
            CallAndroidMethod(c.vm, c.receiver, "setMeasuredDimension", "(II)V",
                              {dx::VmValue::Int(static_cast<int>(static_cast<std::uint32_t>(result.constrained.width) |
                                                                 result.width_state)),
                               dx::VmValue::Int(static_cast<int>(static_cast<std::uint32_t>(result.constrained.height) |
                                                                 result.height_state))}));
        return dx::VmValue::Void();
    });
    // No Drawable layout/state/draw stack is claimed. Execute the genuine View
    // protocol for geometry and visibility; the leaf renderer consumes backing.
    for (const auto& [name, sig] :
         std::array<std::pair<const char*, const char*>, 5>{{{"setFrame", "(IIII)Z"},
                                                             {"onRtlPropertiesChanged", "(I)V"},
                                                             {"drawableStateChanged", "()V"},
                                                             {"setSelected", "(Z)V"},
                                                             {"setVisibility", "(I)V"}}}) {
        builder.VirtualMethod(name, sig, [name, sig](dx::IntrinsicContext& c) {
            if (std::string_view(name) == "onRtlPropertiesChanged" && c.arguments[0].AsInt() != 0)
                ImageFailure(c.vm, "RTL image unsupported");
            const auto type = c.vm.Linker().ResolveDescriptor("Landroid/view/View;");
            const auto index = c.vm.Linker().FindVtableIndex(type, name, sig);
            std::vector<dx::VmValue> args{dx::VmValue::Ref(c.receiver)};
            args.insert(args.end(), c.arguments.begin(), c.arguments.end());
            const auto out = c.vm.Call(c.vm.Linker().Class(type).vtable.at(*index), args);
            if (out.exception.IsValid())
                throw dx::VmJavaThrow{c.vm.Linker().Class(out.exception_class).descriptor, out.exception_message,
                                      out.exception};
            return out.value;
        });
    }
    return std::move(builder).Build();
}

[[noreturn]] void TextInputFailure(dx::Interpreter& vm, std::string message) {
    if (auto* ledger = vm.Ledger()) ledger->RecordUnimplemented("dexvm.text_constructor", 0);
    throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", std::move(message)};
}

Decl ExperimentalTextView(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/TextView;", "Landroid/view/View;");
    const auto chain = [](int kind) {
        return dx::IntrinsicHandler([kind](dx::IntrinsicContext& c) {
            const auto method =
                c.vm.Linker().FindDirectMethod(c.vm.Linker().ResolveDescriptor("Landroid/widget/TextView;"), "<init>",
                                               kind == 1 ? "(Landroid/content/Context;Landroid/util/AttributeSet;)V"
                                                         : "(Landroid/content/Context;Landroid/util/AttributeSet;I)V");
            std::vector<dx::VmValue> args{dx::VmValue::Ref(c.receiver), c.arguments[0],
                                          kind == 1 ? dx::VmValue::Ref(dx::VmObjectRef{}) : c.arguments[1]};
            // AOSP TextView.java:618 and pinned ROM constructor constant.
            if (kind == 2) args.push_back(dx::VmValue::Int(0x01010084));
            const auto result = c.vm.Call(*method, args);
            if (result.exception.IsValid())
                throw dx::VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                      result.exception};
            return result.value;
        });
    };
    builder.Constructor("(Landroid/content/Context;)V", chain(1));
    builder.Constructor("(Landroid/content/Context;Landroid/util/AttributeSet;)V", chain(2));
    builder.Constructor("(Landroid/content/Context;Landroid/util/AttributeSet;I)V", [context](dx::IntrinsicContext& c) {
        auto& vm = c.vm;
        const auto owner = c.arguments[0].ref, attrs = c.arguments[1].ref;
        const auto style = c.arguments[2];
        const auto super = vm.Linker().FindDirectMethod(vm.Linker().ResolveDescriptor("Landroid/view/View;"), "<init>",
                                                        "(Landroid/content/Context;Landroid/util/AttributeSet;I)V");
        const auto initialized =
            vm.Call(*super, std::array{dx::VmValue::Ref(c.receiver), c.arguments[0], c.arguments[1], style});
        if (initialized.exception.IsValid())
            throw dx::VmJavaThrow{vm.Linker().Class(initialized.exception_class).descriptor,
                                  initialized.exception_message, initialized.exception};
        const auto theme = CallAndroidMethod(vm, owner, "getTheme", "()Landroid/content/res/Resources$Theme;").ref;
        const auto theme_root = vm.ProtectReferences(std::array{theme});
        const auto tva = RomTextAttributes(vm, "TextViewAppearance"), ta = RomTextAttributes(vm, "TextAppearance"),
                   tv = RomTextAttributes(vm, "TextView"), view = RomTextAttributes(vm, "View");
        if (attrs.IsValid()) {
            std::set<std::uint32_t> known;
            for (const auto group : {"View", "TextView", "ViewGroup_Layout", "ViewGroup_MarginLayout",
                                     "FrameLayout_Layout", "LinearLayout_Layout", "RelativeLayout_Layout"}) {
                const auto array = RomTextAttributes(vm, group).array;
                for (JniSize i = 0; i < vm.Model().ArrayLength(array); ++i)
                    known.insert(static_cast<std::uint32_t>(vm.Model().GetPrimitiveElement(array, i)));
            }
            const auto count = CallAndroidMethod(vm, attrs, "getAttributeCount", "()I").AsInt();
            for (int i = 0; i < count; ++i) {
                const auto id = static_cast<std::uint32_t>(
                    CallAndroidMethod(vm, attrs, "getAttributeNameResource", "(I)I", {dx::VmValue::Int(i)}).AsInt());
                // The unqualified XML style marker is handled by the ROM parser.
                if (id == 0 && vm.StringUtf8(CallAndroidMethod(vm, attrs, "getAttributeName", "(I)Ljava/lang/String;",
                                                               {dx::VmValue::Int(i)})
                                                 .ref) == "style")
                    continue;
                if (!known.contains(id)) TextInputFailure(vm, "unknown TextView XML attribute");
            }
        }
        const auto obtain = [&](dx::VmObjectRef array) {
            return CallAndroidMethod(vm, theme, "obtainStyledAttributes",
                                     "(Landroid/util/AttributeSet;[III)Landroid/content/res/TypedArray;",
                                     {dx::VmValue::Ref(attrs), dx::VmValue::Ref(array), style, dx::VmValue::Int(0)})
                .ref;
        };
        const auto appearance_ref = obtain(tva.array);
        const auto appearance_root = vm.ProtectReferences(std::array{appearance_ref});
        const auto ap =
            CallAndroidMethod(vm, appearance_ref, "getResourceId", "(II)I", {dx::VmValue::Int(0), dx::VmValue::Int(-1)})
                .AsInt();
        static_cast<void>(CallAndroidMethod(vm, appearance_ref, "recycle", "()V"));
        // Audited AOSP constructor locals/default field assignments: 15px,
        // black, START|TOP resolved below, unlimited lines; not UiNode defaults.
        int size = 15, family_index = -1, style_index = -1, max_lines = 0x7fffffff, gravity = 0x33;
        std::uint32_t color = 0xff000000;
        std::u16string text;
        std::string family;
        bool has_family = false, single = false;
        int ellipsize = -1;
        std::array<ui::CompoundDrawable, 4> compounds{};
        std::array<dx::VmObjectRef, 4> compound_refs{dx::VmObjectRef{}, dx::VmObjectRef{}, dx::VmObjectRef{},
                                                     dx::VmObjectRef{}};
        const auto retained = vm.Model().NewObjectArray(vm.Linker().ResolveDescriptor("[Ljava/lang/Object;"),
                                                        vm.Linker().ResolveDescriptor("Ljava/lang/Object;"), 5);
        const auto retained_root = vm.ProtectReferences(std::array{retained});
        const auto consume = [&](dx::VmObjectRef values, const TextStyleable& group) {
            const auto count = CallAndroidMethod(vm, values, "getIndexCount", "()I").AsInt();
            for (int i = 0; i < count; ++i) {
                const auto index = CallAndroidMethod(vm, values, "getIndex", "(I)I", {dx::VmValue::Int(i)}).AsInt();
                const auto found = group.names.find(index);
                if (found == group.names.end()) TextInputFailure(vm, "unknown ROM TextView attribute index");
                const auto& name = found->second;
                const auto integer = [&](int fallback) {
                    return CallAndroidMethod(vm, values, "getInt", "(II)I",
                                             {dx::VmValue::Int(index), dx::VmValue::Int(fallback)})
                        .AsInt();
                };
                const auto boolean = [&](bool fallback) {
                    return CallAndroidMethod(vm, values, "getBoolean", "(IZ)Z",
                                             {dx::VmValue::Int(index), dx::VmValue::Int(fallback)})
                               .AsInt() != 0;
                };
                if (name == "textAppearance") continue;  // Already consumed through TextViewAppearance.
                if (name == "textSize")
                    size = CallAndroidMethod(vm, values, "getDimensionPixelSize", "(II)I",
                                             {dx::VmValue::Int(index), dx::VmValue::Int(size)})
                               .AsInt();
                else if (name == "textColor") {
                    const auto list =
                        CallAndroidMethod(vm, values, "getColorStateList", "(I)Landroid/content/res/ColorStateList;",
                                          {dx::VmValue::Int(index)})
                            .ref;
                    const auto root = vm.ProtectReferences(std::array{list});
                    if (!list.IsValid() || CallAndroidMethod(vm, list, "isStateful", "()Z").AsInt())
                        TextInputFailure(vm, "non-scalar textColor");
                    color = static_cast<std::uint32_t>(CallAndroidMethod(vm, list, "getDefaultColor", "()I").AsInt());
                } else if (name == "fontFamily") {
                    const auto ref =
                        CallAndroidMethod(vm, values, "getString", "(I)Ljava/lang/String;", {dx::VmValue::Int(index)})
                            .ref;
                    has_family = ref.IsValid();
                    family = has_family ? vm.StringUtf8(ref) : "";
                } else if (name == "typeface")
                    family_index = integer(family_index);
                else if (name == "textStyle")
                    style_index = integer(style_index);
                else if (name == "text") {
                    const auto ref = CallAndroidMethod(vm, values, "getText", "(I)Ljava/lang/CharSequence;",
                                                       {dx::VmValue::Int(index)})
                                         .ref;
                    if (ref.IsValid() &&
                        vm.Linker().Class(vm.Model().ObjectClass(ref)).descriptor != "Ljava/lang/String;")
                        TextInputFailure(vm, "plain String required");
                    text = ref.IsValid() ? vm.Model().StringValue(ref) : u"";
                } else if (name == "gravity") {
                    gravity = integer(-1);
                    if ((gravity & ~0x77) ||
                        ((gravity & 7) != 0 && (gravity & 7) != 1 && (gravity & 7) != 3 && (gravity & 7) != 5) ||
                        ((gravity & 0x70) != 0 && (gravity & 0x70) != 0x10 && (gravity & 0x70) != 0x30 &&
                         (gravity & 0x70) != 0x50))
                        TextInputFailure(vm, "unsupported text gravity");
                    if (!(gravity & 7)) gravity |= 3;
                    if (!(gravity & 0x70)) gravity |= 0x30;
                } else if (name == "maxLines") {
                    max_lines = integer(-1);
                    if (max_lines < 1) TextInputFailure(vm, "maxLines must be positive");
                } else if (name == "singleLine")
                    single = boolean(single);
                else if (name == "ellipsize") {
                    ellipsize = integer(-1);
                    if (ellipsize != 0) TextInputFailure(vm, "ellipsize is unsupported");
                } else if (name == "drawableLeft" || name == "drawableTop" || name == "drawableRight" ||
                           name == "drawableBottom") {
                    const std::size_t slot = name == "drawableLeft"    ? 0
                                             : name == "drawableTop"   ? 1
                                             : name == "drawableRight" ? 2
                                                                       : 3;
                    const auto ref =
                        CallAndroidMethod(vm, values, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;",
                                          {dx::VmValue::Int(index)})
                            .ref;
                    if (ref.IsValid()) {
                        vm.Model().SetObjectElement(retained, static_cast<JniSize>(slot), ref);
                        const auto backing = context->ui_drawables.find(ref.Value());
                        if (backing == context->ui_drawables.end() || !backing->second.bitmap_drawable ||
                            !backing->second.leaf_pixels)
                            TextInputFailure(vm, "compound requires resource BitmapDrawable");
                        const auto width = CallAndroidMethod(vm, ref, "getIntrinsicWidth", "()I").AsInt();
                        const auto height = CallAndroidMethod(vm, ref, "getIntrinsicHeight", "()I").AsInt();
                        compounds[slot] = {backing->second.resource_id, width, height};
                        compound_refs[slot] = ref;
                    }
                } else if (name == "enabled") {
                    // The View super constructor consumed this same value.
                } else if (name == "includeFontPadding" || name == "cursorVisible" || name == "linksClickable") {
                    if (!boolean(true)) TextInputFailure(vm, "unsupported non-default " + name);
                } else if (name == "textAllCaps" || name == "editable" || name == "password" ||
                           name == "textIsSelectable" || name == "autoText" || name == "phoneNumber" ||
                           name == "selectAllOnFocus" || name == "scrollHorizontally") {
                    if (boolean(false)) TextInputFailure(vm, "unsupported " + name);
                } else if (name == "autoLink" || name == "bufferType" || name == "shadowColor") {
                    if (integer(0) != 0) TextInputFailure(vm, "unsupported " + name);
                } else {
                    TextInputFailure(vm, "unsupported TextView attribute: " + name);
                }
            }
        };
        if (ap != -1) {
            const auto values =
                CallAndroidMethod(vm, theme, "obtainStyledAttributes", "(I[I)Landroid/content/res/TypedArray;",
                                  {dx::VmValue::Int(ap), dx::VmValue::Ref(ta.array)})
                    .ref;
            const auto root = vm.ProtectReferences(std::array{values});
            consume(values, ta);
            static_cast<void>(CallAndroidMethod(vm, values, "recycle", "()V"));
        }
        const auto values = obtain(tv.array);
        const auto values_root = vm.ProtectReferences(std::array{values});
        consume(values, tv);
        static_cast<void>(CallAndroidMethod(vm, values, "recycle", "()V"));
        if (single) {
            if (ellipsize < 0) TextInputFailure(vm, "singleLine implicitly requests END ellipsize");
            if (text.find(u'\r') != text.npos) TextInputFailure(vm, "singleLine CR transformation unsupported");
            max_lines = 1;
        }
        if (family_index < -1 || family_index > 3 || style_index < -1 || style_index > 3)
            TextInputFailure(vm, "unsupported typeface/style");
        dx::VmObjectRef face;
        if (has_family || family_index > 0 || style_index > 0) {
            const auto name = has_family          ? family
                              : family_index == 1 ? "sans-serif"
                              : family_index == 2 ? "serif"
                              : family_index == 3 ? "monospace"
                                                  : "";
            if (has_family && family != "sans-serif" && family != "serif" && family != "monospace")
                TextInputFailure(vm, "unsupported fontFamily");
            const auto name_ref = vm.NewStringUtf8(name);
            const auto name_root = vm.ProtectReferences(std::array{name_ref});
            face = TextStatic(vm, "Landroid/graphics/Typeface;", "create",
                              "(Ljava/lang/String;I)Landroid/graphics/Typeface;",
                              {dx::VmValue::Ref(name_ref), dx::VmValue::Int(style_index < 0 ? 0 : style_index)})
                       .ref;
            vm.Model().SetObjectElement(retained, 4, face);
        }
        const auto font_style = face.IsValid() ? CallAndroidMethod(vm, face, "getStyle", "()I").AsInt() : 0;
        if (size < 1 || size > 128 || text.size() > 1024)
            TextInputFailure(vm, "text size/length outside bounded service");
        for (const auto unit : text)
            if (!(unit == u'\n' || (unit >= 0x20 && unit <= 0x7e) || (unit >= 0x4e00 && unit <= 0x9fff)))
                TextInputFailure(vm, "unsupported text glyph/shaping");
        try {
            static_cast<void>(ui::MeasureFixedText(text, static_cast<float>(size),
                                                   static_cast<std::uint32_t>(font_style),
                                                   context->ui_tree.FallbackFont().get()));
        } catch (const std::runtime_error& error) {
            TextInputFailure(vm, error.what());
        }
        // Default START is resolved against the real View direction, not blindly
        // treated as LEFT on an RTL View.
        if (CallAndroidMethod(vm, c.receiver, "getLayoutDirection", "()I").AsInt() != 0)
            TextInputFailure(vm, "RTL text direction unsupported");
        const auto focus_values = obtain(view.array);
        const auto focus_root = vm.ProtectReferences(std::array{focus_values});
        std::array<bool, 3> focus{};
        const auto count = CallAndroidMethod(vm, focus_values, "getIndexCount", "()I").AsInt();
        for (int i = 0; i < count; ++i) {
            const auto index = CallAndroidMethod(vm, focus_values, "getIndex", "(I)I", {dx::VmValue::Int(i)}).AsInt();
            const auto& name = view.names.at(index);
            for (std::size_t j = 0; j < 3; ++j)
                if (name == std::array{"focusable", "clickable", "longClickable"}[j])
                    focus[j] = CallAndroidMethod(vm, focus_values, "getBoolean", "(IZ)Z",
                                                 {dx::VmValue::Int(index), dx::VmValue::Int(0)})
                                   .AsInt() != 0;
        }
        static_cast<void>(CallAndroidMethod(vm, focus_values, "recycle", "()V"));
        for (std::size_t j = 0; j < 3; ++j)
            static_cast<void>(CallAndroidMethod(vm, c.receiver,
                                                std::array{"setFocusable", "setClickable", "setLongClickable"}[j],
                                                "(Z)V", {dx::VmValue::Int(focus[j])}));
        const auto node = TextNode(c, context);
        auto* state = context->ui_tree.Get(node);
        state->text = std::move(text);
        state->text_size_px = static_cast<float>(size);
        state->text_color = AndroidColorToRgba(color);
        state->text_style = static_cast<std::uint32_t>(font_style);
        state->gravity = static_cast<std::uint32_t>(gravity);
        state->max_lines = max_lines;
        state->compound_drawables = compounds;
        auto& refs = context->view_adapter_references[c.receiver.Value()];
        refs.text_single_line = single;
        refs.text_typeface = face;
        refs.text_compounds = compound_refs;
        context->ui_tree.MarkLayoutDirty(node);
        return dx::VmValue::Void();
    });
    const auto changed = [context](dx::IntrinsicContext& c) {
        auto& refs = context->view_adapter_references.at(c.receiver.Value());
        ++refs.text_revision;
        refs.text_measure.reset();
        refs.text_measure_cache.clear();
        static_cast<void>(CallAndroidMethod(c.vm, c.receiver, "requestLayout", "()V"));
        static_cast<void>(CallAndroidMethod(c.vm, c.receiver, "invalidate", "()V"));
        return dx::VmValue::Void();
    };
    const auto validate = [](dx::IntrinsicContext& c, const ui::UiNode& node, const std::u16string& text, float size,
                             std::uint32_t style) {
        if (text.size() > 1024 || !std::isfinite(size) || size < 1 || size > 128 || style > 3)
            TextInputFailure(c.vm, "text outside bounded service");
        for (auto ch : text)
            if (!(ch == u'\n' || (ch >= 32 && ch <= 126) || (ch >= 0x4e00 && ch <= 0x9fff)))
                TextInputFailure(c.vm, "unsupported text glyph/shaping");
        try {
            static_cast<void>(ui::MeasureFixedText(text, size, style, node.fallback_font.get()));
        } catch (const std::runtime_error& e) {
            TextInputFailure(c.vm, e.what());
        }
    };
    builder.FinalMethod(
        "setText", "(Ljava/lang/CharSequence;)V", [context, changed, validate](dx::IntrinsicContext& c) {
            const auto ref = c.arguments[0].ref;
            if (ref.IsValid() && c.vm.Linker().Class(c.vm.Model().ObjectClass(ref)).descriptor != "Ljava/lang/String;")
                TextInputFailure(c.vm, "plain String required");
            auto text = ref.IsValid() ? c.vm.Model().StringValue(ref) : std::u16string{};
            auto& n = *context->ui_tree.Get(TextNode(c, context));
            validate(c, n, text, n.text_size_px, n.text_style);
            n.text = std::move(text);
            return changed(c);
        });
    builder.VirtualMethod("getText", "()Ljava/lang/CharSequence;", [context](dx::IntrinsicContext& c) {
        return dx::VmValue::Ref(c.vm.Model().NewString(context->ui_tree.Get(TextNode(c, context))->text));
    });
    builder.VirtualMethod("getTextSize", "()F", [context](dx::IntrinsicContext& c) {
        return dx::VmValue::Float(context->ui_tree.Get(TextNode(c, context))->text_size_px);
    });
    builder.VirtualMethod("setTextSize", "(F)V", [](dx::IntrinsicContext& c) {
        return CallAndroidMethod(c.vm, c.receiver, "setTextSize", "(IF)V", {dx::VmValue::Int(2), c.arguments[0]});
    });
    builder.VirtualMethod("setTextSize", "(IF)V", [context, changed, validate](dx::IntrinsicContext& c) {
        const auto unit = c.arguments[0].AsInt();
        if (unit < 0 || unit > 2) TextInputFailure(c.vm, "unsupported dimension unit");
        const auto size = c.arguments[1].AsFloat() * (unit == 0   ? 1.F
                                                      : unit == 1 ? context->ui_density
                                                                  : context->ui_scaled_density);
        auto& n = *context->ui_tree.Get(TextNode(c, context));
        validate(c, n, n.text, size, n.text_style);
        n.text_size_px = size;
        return changed(c);
    });
    builder.VirtualMethod(
        "setTypeface", "(Landroid/graphics/Typeface;)V", [context, changed, validate](dx::IntrinsicContext& c) {
            const auto ref = c.arguments[0].ref;
            const auto style = ref.IsValid() ? CallAndroidMethod(c.vm, ref, "getStyle", "()I").AsInt() : 0;
            auto& n = *context->ui_tree.Get(TextNode(c, context));
            validate(c, n, n.text, n.text_size_px, static_cast<std::uint32_t>(style));
            context->view_adapter_references.at(c.receiver.Value()).text_typeface = ref;
            n.text_style = static_cast<std::uint32_t>(style);
            return changed(c);
        });
    builder.VirtualMethod("setTypeface", "(Landroid/graphics/Typeface;I)V", [](dx::IntrinsicContext& c) {
        const auto style = c.arguments[1].AsInt();
        if (style < 0 || style > 3) TextInputFailure(c.vm, "unsupported Typeface style");
        const auto ref =
            TextStatic(c.vm, "Landroid/graphics/Typeface;", "create",
                       "(Landroid/graphics/Typeface;I)Landroid/graphics/Typeface;", {c.arguments[0], c.arguments[1]})
                .ref;
        const auto root = c.vm.ProtectReferences(std::array{ref});
        return CallAndroidMethod(c.vm, c.receiver, "setTypeface", "(Landroid/graphics/Typeface;)V",
                                 {dx::VmValue::Ref(ref)});
    });
    builder.VirtualMethod("getTypeface", "()Landroid/graphics/Typeface;", [context](dx::IntrinsicContext& c) {
        return dx::VmValue::Ref(context->view_adapter_references.at(c.receiver.Value()).text_typeface);
    });
    builder.VirtualMethod("setTextColor", "(I)V", [context, changed](dx::IntrinsicContext& c) {
        context->ui_tree.Get(TextNode(c, context))->text_color =
            AndroidColorToRgba(static_cast<std::uint32_t>(c.arguments[0].AsInt()));
        return changed(c);
    });
    builder.VirtualMethod("setTextColor", "(Landroid/content/res/ColorStateList;)V", [](dx::IntrinsicContext& c) {
        const auto ref = c.arguments[0].ref;
        if (!ref.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "text colors"};
        if (CallAndroidMethod(c.vm, ref, "isStateful", "()Z").AsInt()) TextInputFailure(c.vm, "stateful text color");
        return CallAndroidMethod(c.vm, c.receiver, "setTextColor", "(I)V",
                                 {CallAndroidMethod(c.vm, ref, "getDefaultColor", "()I")});
    });
    builder.FinalMethod("getCurrentTextColor", "()I", [context](dx::IntrinsicContext& c) {
        const auto rgba = context->ui_tree.Get(TextNode(c, context))->text_color;
        return dx::VmValue::Int(static_cast<std::int32_t>((rgba >> 8U) | (rgba << 24U)));
    });
    builder.FinalMethod("getTextColors", "()Landroid/content/res/ColorStateList;", [](dx::IntrinsicContext& c) {
        return TextStatic(c.vm, "Landroid/content/res/ColorStateList;", "valueOf",
                          "(I)Landroid/content/res/ColorStateList;",
                          {CallAndroidMethod(c.vm, c.receiver, "getCurrentTextColor", "()I")});
    });
    builder.VirtualMethod("setGravity", "(I)V", [context, changed](dx::IntrinsicContext& c) {
        auto gravity = c.arguments[0].AsInt();
        if ((gravity & ~0x77) ||
            ((gravity & 7) != 0 && (gravity & 7) != 1 && (gravity & 7) != 3 && (gravity & 7) != 5) ||
            ((gravity & 0x70) != 0 && (gravity & 0x70) != 0x10 && (gravity & 0x70) != 0x30 && (gravity & 0x70) != 0x50))
            TextInputFailure(c.vm, "unsupported gravity");
        if (!(gravity & 7)) gravity |= 3;
        if (!(gravity & 0x70)) gravity |= 0x30;
        context->ui_tree.Get(TextNode(c, context))->gravity = static_cast<std::uint32_t>(gravity);
        return changed(c);
    });
    builder.VirtualMethod("getGravity", "()I", [context](dx::IntrinsicContext& c) {
        return dx::VmValue::Int(static_cast<std::int32_t>(context->ui_tree.Get(TextNode(c, context))->gravity));
    });
    builder.VirtualMethod("setMaxLines", "(I)V", [context, changed](dx::IntrinsicContext& c) {
        if (c.arguments[0].AsInt() < 1) TextInputFailure(c.vm, "non-positive maxLines");
        context->ui_tree.Get(TextNode(c, context))->max_lines = c.arguments[0].AsInt();
        return changed(c);
    });
    builder.VirtualMethod("getMaxLines", "()I", [context](dx::IntrinsicContext& c) {
        return dx::VmValue::Int(context->ui_tree.Get(TextNode(c, context))->max_lines);
    });
    builder.VirtualMethod("setSingleLine", "()V", [](dx::IntrinsicContext& c) {
        return CallAndroidMethod(c.vm, c.receiver, "setSingleLine", "(Z)V", {dx::VmValue::Int(1)});
    });
    builder.VirtualMethod("setSingleLine", "(Z)V", [context, changed](dx::IntrinsicContext& c) {
        const auto single = c.arguments[0].AsInt() != 0;
        auto& n = *context->ui_tree.Get(TextNode(c, context));
        context->view_adapter_references.at(c.receiver.Value()).text_single_line = single;
        n.max_lines = single ? 1 : 0x7fffffff;
        return changed(c);
    });
    builder.VirtualMethod("setCompoundDrawablesWithIntrinsicBounds", "(IIII)V", [](dx::IntrinsicContext& c) {
        const auto resources =
            CallAndroidMethod(c.vm, c.receiver, "getResources", "()Landroid/content/res/Resources;").ref;
        std::array<dx::VmObjectRef, 4> refs{dx::VmObjectRef{}, dx::VmObjectRef{}, dx::VmObjectRef{}, dx::VmObjectRef{}};
        const auto retained = c.vm.Model().NewObjectArray(c.vm.Linker().ResolveDescriptor("[Ljava/lang/Object;"),
                                                          c.vm.Linker().ResolveDescriptor("Ljava/lang/Object;"), 4);
        const auto roots = c.vm.ProtectReferences(std::array{retained});
        std::vector<dx::VmValue> args;
        for (std::size_t i = 0; i < 4; ++i) {
            if (c.arguments[i].AsInt())
                refs[i] = CallAndroidMethod(c.vm, resources, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;",
                                            {c.arguments[i]})
                              .ref;
            c.vm.Model().SetObjectElement(retained, static_cast<JniSize>(i), refs[i]);
            args.push_back(dx::VmValue::Ref(refs[i]));
        }
        return CallAndroidMethod(
            c.vm, c.receiver, "setCompoundDrawablesWithIntrinsicBounds",
            "(Landroid/graphics/drawable/Drawable;Landroid/graphics/drawable/Drawable;Landroid/graphics/drawable/Drawable;Landroid/graphics/drawable/Drawable;)V",
            args);
    });
    builder.VirtualMethod(
        "setCompoundDrawablesWithIntrinsicBounds",
        "(Landroid/graphics/drawable/Drawable;Landroid/graphics/drawable/Drawable;Landroid/graphics/drawable/Drawable;Landroid/graphics/drawable/Drawable;)V",
        [context, changed](dx::IntrinsicContext& c) {
            std::array<ui::CompoundDrawable, 4> values{};
            std::array<dx::VmObjectRef, 4> refs{dx::VmObjectRef{}, dx::VmObjectRef{}, dx::VmObjectRef{},
                                                dx::VmObjectRef{}};
            for (std::size_t i = 0; i < 4; ++i) {
                const auto ref = c.arguments[i].ref;
                refs[i] = ref;
                if (!ref.IsValid()) continue;
                const auto found = context->ui_drawables.find(ref.Value());
                if (found == context->ui_drawables.end() || !found->second.bitmap_drawable ||
                    !found->second.leaf_pixels)
                    TextInputFailure(c.vm, "compound requires APK bitmap");
                values[i] = {found->second.resource_id,
                             CallAndroidMethod(c.vm, ref, "getIntrinsicWidth", "()I").AsInt(),
                             CallAndroidMethod(c.vm, ref, "getIntrinsicHeight", "()I").AsInt()};
            }
            context->ui_tree.Get(TextNode(c, context))->compound_drawables = values;
            context->view_adapter_references.at(c.receiver.Value()).text_compounds = refs;
            return changed(c);
        });
    for (std::size_t i = 0; i < 4; ++i) {
        builder.VirtualMethod(std::array{"getCompoundPaddingLeft", "getCompoundPaddingTop", "getCompoundPaddingRight",
                                         "getCompoundPaddingBottom"}[i],
                              "()I", [context, i](dx::IntrinsicContext& c) {
                                  const auto pad = ReadAndroidViewPadding(c.vm, *context, c.receiver);
                                  const auto& compound =
                                      context->ui_tree.Get(TextNode(c, context))->compound_drawables[i];
                                  return dx::VmValue::Int(std::array{pad.left, pad.top, pad.right, pad.bottom}[i] +
                                                          (i % 2 ? compound.height : compound.width));
                              });
    }
    builder.VirtualMethod("onMeasure", "(II)V", [context](dx::IntrinsicContext& c) {
        if (c.vm.CurrentContextToken() != 1U || context->ui_java_owner_thread != std::this_thread::get_id())
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Text measure requires owner"};
        const auto revision = context->view_adapter_references.at(c.receiver.Value()).text_revision;
        const auto& n = *context->ui_tree.Get(TextNode(c, context));
        ui::TextLeafInput input;
        input.text = n.text;
        if (context->view_adapter_references.at(c.receiver.Value()).text_single_line)
            std::replace(input.text.begin(), input.text.end(), u'\n', u' ');
        input.size_px = n.text_size_px;
        input.style = n.text_style;
        input.fallback_font = n.fallback_font;
        input.max_lines = n.max_lines;
        input.java_text_view = true;
        input.single_line = context->view_adapter_references.at(c.receiver.Value()).text_single_line;
        for (std::size_t i = 0; i < 4; ++i)
            input.compound[i] = {n.compound_drawables[i].width, n.compound_drawables[i].height};
        std::array<int, 4> pads{};
        for (std::size_t i = 0; i < 4; ++i)
            pads[i] = CallAndroidMethod(c.vm, c.receiver,
                                        std::array{"getCompoundPaddingLeft", "getCompoundPaddingTop",
                                                   "getCompoundPaddingRight", "getCompoundPaddingBottom"}[i],
                                        "()I")
                          .AsInt() -
                      (i % 2 ? input.compound[i].height : input.compound[i].width);
        input.padding = {pads[0], pads[1], pads[2], pads[3]};
        input.minimum = {CallAndroidMethod(c.vm, c.receiver, "getSuggestedMinimumWidth", "()I").AsInt(),
                         CallAndroidMethod(c.vm, c.receiver, "getSuggestedMinimumHeight", "()I").AsInt()};
        const auto spec = [&](int bits) {
            const auto mode = static_cast<std::uint32_t>(bits) >> 30U;
            if (mode == 3) TextInputFailure(c.vm, "invalid MeasureSpec mode");
            return ui::MeasureSpec{mode == 1   ? ui::MeasureMode::Exactly
                                   : mode == 2 ? ui::MeasureMode::AtMost
                                               : ui::MeasureMode::Unspecified,
                                   bits & 0x3fffffff};
        };
        auto result = ui::MeasureTextLeaf(input, spec(c.arguments[0].AsInt()), spec(c.arguments[1].AsInt()));
        static_cast<void>(CallAndroidMethod(
            c.vm, c.receiver, "setMeasuredDimension", "(II)V",
            {dx::VmValue::Int(result.constrained.width), dx::VmValue::Int(result.constrained.height)}));
        auto& refs = context->view_adapter_references.at(c.receiver.Value());
        if (revision != refs.text_revision)
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Text changed during measurement"};
        refs.text_measure_cache[result.constrained.width] = result;
        refs.text_measure = std::move(result);
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("getBaseline", "()I", [context](dx::IntrinsicContext& c) {
        const auto width = CallAndroidMethod(c.vm, c.receiver, "getMeasuredWidth", "()I").AsInt();
        const auto& cache = context->view_adapter_references.at(c.receiver.Value()).text_measure_cache;
        const auto found = cache.find(width);
        if (found == cache.end()) return dx::VmValue::Int(-1);
        const auto result = found->second;
        const auto gravity = context->ui_tree.Get(TextNode(c, context))->gravity;
        auto baseline = *result.baseline;
        const auto height = CallAndroidMethod(c.vm, c.receiver, "getMeasuredHeight", "()I").AsInt();
        const auto top = CallAndroidMethod(c.vm, c.receiver, "getCompoundPaddingTop", "()I").AsInt();
        const auto bottom = CallAndroidMethod(c.vm, c.receiver, "getCompoundPaddingBottom", "()I").AsInt();
        const auto extra = std::max(0, height - top - bottom - result.line_plan->metrics.height);
        if ((gravity & 0x70) == 0x10) baseline += extra / 2;
        if ((gravity & 0x70) == 0x50) baseline += extra;
        return dx::VmValue::Int(baseline);
    });
    // No Editor/Layout/Drawables object graph is installed. These callbacks
    // execute the genuine View protocol; all text work belongs to the leaf owner.
    for (const auto& [name, sig] :
         std::array<std::pair<const char*, const char*>, 8>{{{"onLayout", "(ZIIII)V"},
                                                             {"onRtlPropertiesChanged", "(I)V"},
                                                             {"drawableStateChanged", "()V"},
                                                             {"setPadding", "(IIII)V"},
                                                             {"setPaddingRelative", "(IIII)V"},
                                                             {"setFrame", "(IIII)Z"},
                                                             {"resetResolvedDrawables", "()V"},
                                                             {"onResolveDrawables", "(I)V"}}}) {
        builder.VirtualMethod(name, sig, [name, sig](dx::IntrinsicContext& c) {
            if ((std::string_view(name) == "onRtlPropertiesChanged" ||
                 std::string_view(name) == "onResolveDrawables") &&
                c.arguments[0].AsInt() != 0)
                TextInputFailure(c.vm, "RTL text unsupported");
            const auto type = c.vm.Linker().ResolveDescriptor("Landroid/view/View;");
            const auto method = c.vm.Linker().FindVtableIndex(type, name, sig);
            std::vector<dx::VmValue> args(c.arguments.begin(), c.arguments.end());
            args.insert(args.begin(), dx::VmValue::Ref(c.receiver));
            const auto result = c.vm.Call(c.vm.Linker().Class(type).vtable.at(*method), args);
            if (result.exception.IsValid())
                throw dx::VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                      result.exception};
            return result.value;
        });
    }
    return std::move(builder).Build();
}
}  // namespace

Decl Declare_android_widget_TextView(const Context& context) {
    if (context->ui_traversal_owner == AndroidUiTraversalOwner::JavaTraversal) return ExperimentalTextView(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/TextView;", "Landroid/view/View;",
                                                    {"Landroid/view/ViewTreeObserver$OnPreDrawListener;"});
    builder.Constructor("(Landroid/content/Context;)V",
                    ViewInitHandler(context));
    builder.Constructor("(Landroid/content/Context;Landroid/util/AttributeSet;I)V",
                    ViewDefaultStyleInitHandler(context));
    builder.VirtualMethod("setCompoundDrawablesWithIntrinsicBounds", "(IIII)V",
        [context](dx::IntrinsicContext& call) {
            // AOSP resolves all four resources before calling the Drawable
            // overload. A failed lookup must not publish a partial update.
            std::array<ui::CompoundDrawable, 4> drawables{};
            for (std::size_t index = 0; index < 4U; ++index) {
                const auto resource_id = static_cast<std::uint32_t>(
                    call.arguments[index].AsInt());
                if (resource_id == 0U) {
                    continue;
                }
                std::shared_ptr<const ui::UiBitmap> bitmap;
                try {
                    bitmap = ResolveUiDrawable(*context, resource_id);
                } catch (const std::runtime_error& error) {
                    throw dx::VmJavaThrow{
                        "Landroid/content/res/Resources$NotFoundException;",
                        error.what()};
                }
                drawables[index] = {resource_id, bitmap->width, bitmap->height};
            }
            const auto node = TextNode(call, context);
            context->ui_tree.Get(node)->compound_drawables = drawables;
            context->ui_tree.MarkLayoutDirty(node);
            return dx::VmValue::Void();
        });
    builder.VirtualMethod(
        "setText", "(Ljava/lang/CharSequence;Landroid/widget/TextView$BufferType;)V",
        [context](dx::IntrinsicContext& call) {
            const auto buffer = call.arguments[1].ref;
            if (buffer.IsValid() && CallAndroidMethod(call.vm, buffer, "ordinal", "()I").AsInt() != 0) {
                if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("runtime.ui_text_widgets", 0);
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "TextView BufferType span/editable projection is unavailable"};
            }
            const auto value = call.arguments[0].ref;
            auto text = value.IsValid() ? call.vm.Model().StringValue(value)
                                        : std::u16string();
            try {
                static_cast<void>(ui::MeasureFixedText(text, 8.0F, 0, context->ui_tree.FallbackFont().get()));
            } catch (const std::runtime_error& error) {
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                      error.what()};
            }
            const auto node = TextNode(call, context);
            static_cast<void>(ApplyTextEdit(
                call.vm, *context, call.receiver, 0,
                static_cast<std::int32_t>(
                    context->ui_tree.Get(node)->text.size()), text));
            return dx::VmValue::Void();
        });
    builder.FinalMethod("setText", "(Ljava/lang/CharSequence;)V", [](dx::IntrinsicContext& call) {
        const auto buffer = call.vm.Linker().ResolveDescriptor("Landroid/widget/TextView$BufferType;");
        const auto initialized = call.vm.EnsureClassInitialized(buffer);
        if (initialized.exception.IsValid()) {
            call.vm.SetPendingException(initialized.exception);
            return dx::VmValue::Void();
        }
        const auto normal =
            call.vm.Linker().FindFieldRecursive(buffer, "NORMAL", "Landroid/widget/TextView$BufferType;");
        const auto value = call.vm.Linker().Class(buffer).static_storage[call.vm.Linker().Field(*normal).slot];
        return CallAndroidMethod(call.vm, call.receiver, "setText",
                                 "(Ljava/lang/CharSequence;Landroid/widget/TextView$BufferType;)V",
                                 {call.arguments[0], dx::VmValue::Ref(dx::VmObjectRef(value))});
    });
    builder.VirtualMethod("getText", "()Ljava/lang/CharSequence;", [context](dx::IntrinsicContext& call) {
        const auto node = TextNode(call, context);
        return dx::VmValue::Ref(call.vm.Model().NewString(context->ui_tree.Get(node)->text));
    });
    const auto text_colors = builder.BoundInstanceField("mTextColor", "Landroid/content/res/ColorStateList;", dx::kAccPrivate);
    const auto hint_colors = builder.BoundInstanceField("mHintTextColor", "Landroid/content/res/ColorStateList;", dx::kAccPrivate);
    const auto link_colors = builder.BoundInstanceField("mLinkTextColor", "Landroid/content/res/ColorStateList;", dx::kAccPrivate);
    const auto highlight = builder.BoundInstanceField("mHighlightColor", "I", dx::kAccPrivate);
    const auto solid = [](dx::Interpreter& vm, std::uint32_t argb) {
        return TextStatic(vm, "Landroid/content/res/ColorStateList;", "valueOf",
            "(I)Landroid/content/res/ColorStateList;", {dx::VmValue::Int(static_cast<std::int32_t>(argb))}).ref;
    };
    const auto color_methods = [&](const char* setter, const char* getter,
                                    dx::IntrinsicFieldHandle field, bool text) {
        const auto scalar = [setter, solid](dx::IntrinsicContext& call) {
            const auto colors = solid(call.vm, static_cast<std::uint32_t>(call.arguments[0].AsInt()));
            const auto root = call.vm.ProtectReferences(std::array{colors});
            return CallAndroidMethod(call.vm, call.receiver, setter,
                "(Landroid/content/res/ColorStateList;)V", {dx::VmValue::Ref(colors)});
        };
        const auto list = [context, field, text](dx::IntrinsicContext& call) {
            const auto colors = call.arguments[0].ref;
            if (text && !colors.IsValid())
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "text colors are null"};
            std::uint32_t color{};
            if (colors.IsValid()) {
                if (CallAndroidMethod(call.vm, colors, "isStateful", "()Z").AsInt())
                    UnsupportedTextStyle(call.vm, "stateful TextView color rendering is unsupported");
                color = static_cast<std::uint32_t>(CallAndroidMethod(call.vm, colors, "getDefaultColor", "()I").AsInt());
            }
            dx::IntrinsicCall(call).SetRef(field, colors);
            if (text) {
                const auto node = TextNode(call, context);
                context->ui_tree.Get(node)->text_color = AndroidColorToRgba(color);
                context->ui_tree.MarkDrawDirty(node);
            }
            return dx::VmValue::Void();
        };
        if (text) {
            builder.VirtualMethod(setter, "(I)V", scalar);
            builder.VirtualMethod(setter, "(Landroid/content/res/ColorStateList;)V", list);
        } else {
            builder.FinalMethod(setter, "(I)V", scalar);
            builder.FinalMethod(setter, "(Landroid/content/res/ColorStateList;)V", list);
        }
        builder.FinalMethod(getter, "()Landroid/content/res/ColorStateList;",
            [context, field, text, solid](dx::IntrinsicContext& call) {
                auto colors = dx::IntrinsicCall(call).GetRef(field);
                if (text && !colors.IsValid()) {
                    const auto rgba = context->ui_tree.Get(TextNode(call, context))->text_color;
                    colors = solid(call.vm, (rgba >> 8U) | (rgba << 24U));
                    dx::IntrinsicCall(call).SetRef(field, colors);
                }
                return dx::VmValue::Ref(colors);
            });
    };
    color_methods("setTextColor", "getTextColors", text_colors, true);
    color_methods("setHintTextColor", "getHintTextColors", hint_colors, false);
    color_methods("setLinkTextColor", "getLinkTextColors", link_colors, false);
    builder.FinalMethod("getCurrentTextColor", "()I", [context](dx::IntrinsicContext& call) {
        const auto rgba = context->ui_tree.Get(TextNode(call, context))->text_color;
        return dx::VmValue::Int(static_cast<std::int32_t>((rgba >> 8U) | (rgba << 24U)));
    });
    builder.FinalMethod("getCurrentHintTextColor", "()I", [hint_colors](dx::IntrinsicContext& call) {
        const auto colors = dx::IntrinsicCall(call).GetRef(hint_colors);
        return colors.IsValid() ? CallAndroidMethod(call.vm, colors, "getDefaultColor", "()I")
            : CallAndroidMethod(call.vm, call.receiver, "getCurrentTextColor", "()I");
    });
    builder.VirtualMethod("setHighlightColor", "(I)V", [highlight](dx::IntrinsicContext& call) {
        dx::IntrinsicCall(call).SetInt(highlight, call.arguments[0].AsInt());
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("getHighlightColor", "()I", [highlight](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(dx::IntrinsicCall(call).GetInt(highlight));
    });
    builder.VirtualMethod("getTextSize", "()F", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Float(context->ui_tree.Get(TextNode(call, context))->text_size_px);
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
    builder.VirtualMethod("setTextSize", "(F)V", [](dx::IntrinsicContext& call) {
        return CallAndroidMethod(call.vm, call.receiver, "setTextSize", "(IF)V",
                                 {dx::VmValue::Int(2), call.arguments[0]});
    });
    builder.VirtualMethod("setTextSize", "(IF)V", [set_text_size](dx::IntrinsicContext& call) {
        const auto unit = call.arguments[0].AsInt();
        if (unit < 0 || unit > 2) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "unsupported TextView dimension unit"};
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
    builder.VirtualMethod("setLines", "(I)V", one_line);
    builder.VirtualMethod("setMaxLines", "(I)V", one_line);
    builder.VirtualMethod("setSingleLine", "()V", [](dx::IntrinsicContext& call) {
        return CallAndroidMethod(call.vm, call.receiver, "setSingleLine", "(Z)V", {dx::VmValue::Int(1)});
    });
    builder.VirtualMethod("setSingleLine", "(Z)V", [](dx::IntrinsicContext& call) {
        if (call.arguments[0].AsInt() == 0) {
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "multiline TextView is unsupported"};
        }
        return CallAndroidMethod(call.vm, call.receiver, "setLines", "(I)V", {dx::VmValue::Int(1)});
    });
    builder.VirtualMethod("setMaxWidth", "(I)V", WidgetNoopHandler());
    builder.VirtualMethod("setGravity", "(I)V", [context](dx::IntrinsicContext& call) {
        const auto node = TextNode(call, context);
        context->ui_tree.Get(node)->gravity = static_cast<std::uint32_t>(call.arguments[0].AsInt());
        context->ui_tree.MarkLayoutDirty(node);
        return dx::VmValue::Void();
    });
    const auto typeface = builder.BoundInstanceField("mTypeface", "Landroid/graphics/Typeface;", dx::kAccPrivate);
    builder.VirtualMethod("getTypeface", "()Landroid/graphics/Typeface;", [typeface](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(dx::IntrinsicCall(call).GetRef(typeface));
    });
    builder.VirtualMethod("setTypeface", "(Landroid/graphics/Typeface;)V", [context, typeface](dx::IntrinsicContext& call) {
        const auto face = call.arguments[0].ref;
        const auto style = face.IsValid()
            ? CallAndroidMethod(call.vm, face, "getStyle", "()I").AsInt() : 0;
        if (style < 0 || style > 3)
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "invalid typeface style"};
        dx::IntrinsicCall(call).SetRef(typeface, face);
        const auto node = TextNode(call, context);
        context->ui_tree.Get(node)->text_style = static_cast<std::uint32_t>(style);
        context->ui_tree.MarkLayoutDirty(node);
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("setTypeface", "(Landroid/graphics/Typeface;I)V", [](dx::IntrinsicContext& call) {
        auto face = call.arguments[0].ref;
        const auto style = call.arguments[1].AsInt();
        if (style > 0) {
            const auto owner = call.vm.Linker().ResolveDescriptor("Landroid/graphics/Typeface;");
            const auto method = call.vm.Linker().FindDirectMethod(owner,
                face.IsValid() ? "create" : "defaultFromStyle",
                face.IsValid() ? "(Landroid/graphics/Typeface;I)Landroid/graphics/Typeface;" : "(I)Landroid/graphics/Typeface;");
            if (!method) throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant, "Typeface factory missing");
            const auto result = face.IsValid()
                ? call.vm.Call(*method, std::array{dx::VmValue::Ref(face), dx::VmValue::Int(style)})
                : call.vm.Call(*method, std::array{dx::VmValue::Int(style)});
            if (result.exception.IsValid()) throw dx::VmJavaThrow{call.vm.Linker().Class(result.exception_class).descriptor, result.exception_message, result.exception};
            face = result.value.ref;
        }
        const auto root = call.vm.ProtectReferences(std::array{face});
        return CallAndroidMethod(call.vm, call.receiver, "setTypeface", "(Landroid/graphics/Typeface;)V", {dx::VmValue::Ref(face)});
    });
    builder.VirtualMethod("setTextAppearance", "(Landroid/content/Context;I)V", [context](dx::IntrinsicContext& call) {
        const auto values = ResolveTextAppearance(call.vm, context, call.arguments[0].ref,
            static_cast<std::uint32_t>(call.arguments[1].AsInt()));
        const auto value = [&](std::uint32_t attr) -> const loader::ArscBagValue* {
            const auto found = values.find(attr);
            return found == values.end() || found->second.value_type == 0 ? nullptr : &found->second;
        };
        const auto integer = [&](std::uint32_t attr, int fallback) {
            const auto* item = value(attr);
            if (!item) return fallback;
            if (item->value_type < 0x10 || item->value_type > 0x1f)
                UnsupportedTextStyle(call.vm, "text style value is not an integer/color");
            return static_cast<std::int32_t>(item->value_data);
        };
        const std::array<std::optional<std::int32_t>, 4> colors{
            value(0x01010098) ? std::optional{integer(0x01010098, 0)} : std::nullopt,
            value(0x01010099) ? std::optional{integer(0x01010099, 0)} : std::nullopt,
            value(0x0101009a) ? std::optional{integer(0x0101009a, 0)} : std::nullopt,
            value(0x0101009b) ? std::optional{integer(0x0101009b, 0)} : std::nullopt};
        if (integer(0x01010161, 0) != 0 || integer(0x0101038c, 0) != 0)
            UnsupportedTextStyle(call.vm, "text shadow/all-caps transformation is unsupported");
        const auto style = integer(0x01010097, -1);
        const auto family_index = integer(0x01010096, -1);
        if (style < -1 || style > 3 || family_index < -1 || family_index > 3)
            UnsupportedTextStyle(call.vm, "text typeface/style value is unsupported");
        const auto* family = value(0x010103ac);
        if (family && (family->value_type != 3 || !family->string_value))
            UnsupportedTextStyle(call.vm, "fontFamily is not a string");
        std::int32_t size{};
        if (const auto* item = value(0x01010095)) {
            if (item->value_type != 5 || (item->value_data & 0xfU) > 5)
                UnsupportedTextStyle(call.vm, "textSize has an invalid dimension type/unit");
            const auto resources = call.vm.NewIntrinsicInstance("Landroid/content/res/Resources;");
            const auto resource_root = call.vm.ProtectReferences(std::array{resources});
            const auto metrics = CallAndroidMethod(call.vm, resources, "getDisplayMetrics", "()Landroid/util/DisplayMetrics;").ref;
            const auto metrics_root = call.vm.ProtectReferences(std::array{metrics});
            size = TextStatic(call.vm, "Landroid/util/TypedValue;", "complexToDimensionPixelSize",
                "(ILandroid/util/DisplayMetrics;)I", {dx::VmValue::Int(static_cast<std::int32_t>(item->value_data)), dx::VmValue::Ref(metrics)}).AsInt();
            if (size != 0) {
                try { static_cast<void>(ui::MeasureFixedText(u"", static_cast<float>(size))); }
                catch (const std::runtime_error& error) {
                    throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", error.what()};
                }
            }
        }
        dx::VmObjectRef face;
        if (family || family_index > 0) {
            const auto name = call.vm.NewStringUtf8(family ? *family->string_value :
                family_index == 1 ? "sans-serif" : family_index == 2 ? "serif" : "monospace");
            const auto name_root = call.vm.ProtectReferences(std::array{name});
            face = TextStatic(call.vm, "Landroid/graphics/Typeface;", "create",
                "(Ljava/lang/String;I)Landroid/graphics/Typeface;", {dx::VmValue::Ref(name), dx::VmValue::Int(family ? style : 0)}).ref;
        }
        const auto face_root = call.vm.ProtectReferences(std::array{face});
        // Validate the complete requested appearance before publishing mutations.
        const std::array setters{"setTextColor", "setHighlightColor", "setHintTextColor", "setLinkTextColor"};
        for (std::size_t i = 0; i < colors.size(); ++i) {
            if (colors[i] && (i != 1 || *colors[i] != 0))
                static_cast<void>(CallAndroidMethod(call.vm, call.receiver, setters[i], "(I)V", {dx::VmValue::Int(*colors[i])}));
        }
        if (size != 0) {
            const auto node = TextNode(call, context);
            context->ui_tree.Get(node)->text_size_px = static_cast<float>(size);
            context->ui_tree.MarkLayoutDirty(node);
        }
        return family ? CallAndroidMethod(call.vm, call.receiver, "setTypeface", "(Landroid/graphics/Typeface;)V", {dx::VmValue::Ref(face)})
            : CallAndroidMethod(call.vm, call.receiver, "setTypeface", "(Landroid/graphics/Typeface;I)V", {dx::VmValue::Ref(face), dx::VmValue::Int(style)});
    });
    builder.VirtualMethod("getPaint", "()Landroid/text/TextPaint;", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(Singleton(call, context, "text_paint", "Landroid/text/TextPaint;"));
    });
    builder.VirtualMethod("addTextChangedListener", "(Landroid/text/TextWatcher;)V",
                          [context](dx::IntrinsicContext& call) {
                              const auto watcher = call.arguments[0].ref;
                              if (watcher.IsValid()) {
                                  context->text_watchers[call.receiver.Value()].push_back(watcher);
                              }
                              return dx::VmValue::Void();
                          });
    // AOSP 4.4 ROM line 618.
    builder.Constructor(
        "(Landroid/content/Context;Landroid/util/AttributeSet;)V",
        [](dx::IntrinsicContext& c) {
            const auto target =
                c.vm.Linker().FindDirectMethod(c.vm.Linker().ResolveDescriptor("Landroid/widget/TextView;"), "<init>",
                                               "(Landroid/content/Context;Landroid/util/AttributeSet;I)V");
            const auto result =
                c.vm.Call(*target, std::vector<dx::VmValue>{dx::VmValue::Ref(c.receiver), c.arguments[0],
                                                            c.arguments[1], dx::VmValue::Int(0x1010084)});
            if (result.exception.IsValid())
                throw dx::VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                      result.exception};
            return result.value;
        },
        dx::kAccPublic);

    // AOSP 4.4 ROM line 3888.
    builder.FinalMethod(
        "setText", "(I)V",
        [](dx::IntrinsicContext& c) {
            const auto owner = CallAndroidMethod(c.vm, c.receiver, "getContext", "()Landroid/content/Context;").ref;
            const auto owner_root = c.vm.ProtectReferences(std::array{owner});
            const auto resources =
                CallAndroidMethod(c.vm, owner, "getResources", "()Landroid/content/res/Resources;").ref;
            const auto resource_root = c.vm.ProtectReferences(std::array{resources});
            const auto text =
                CallAndroidMethod(c.vm, resources, "getText", "(I)Ljava/lang/CharSequence;", {c.arguments[0]}).ref;
            const auto text_root = c.vm.ProtectReferences(std::array{text});
            return CallAndroidMethod(c.vm, c.receiver, "setText", "(Ljava/lang/CharSequence;)V",
                                     {dx::VmValue::Ref(text)});
        },
        dx::kAccPublic);

    // AOSP 4.4 ROM line 3892.
    builder.FinalMethod(
        "setText", "(ILandroid/widget/TextView$BufferType;)V",
        [](dx::IntrinsicContext& c) {
            const auto owner = CallAndroidMethod(c.vm, c.receiver, "getContext", "()Landroid/content/Context;").ref;
            const auto owner_root = c.vm.ProtectReferences(std::array{owner});
            const auto resources =
                CallAndroidMethod(c.vm, owner, "getResources", "()Landroid/content/res/Resources;").ref;
            const auto resource_root = c.vm.ProtectReferences(std::array{resources});
            const auto text =
                CallAndroidMethod(c.vm, resources, "getText", "(I)Ljava/lang/CharSequence;", {c.arguments[0]}).ref;
            const auto text_root = c.vm.ProtectReferences(std::array{text});
            return CallAndroidMethod(c.vm, c.receiver, "setText",
                                     "(Ljava/lang/CharSequence;Landroid/widget/TextView$BufferType;)V",
                                     {dx::VmValue::Ref(text), c.arguments[1]});
        },
        dx::kAccPublic);

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
    builder.VirtualMethod("show", "()V", [](dx::IntrinsicContext& call) {
        GuestLog(call, core::LogLevel::info, "Toast.show()");
        return dx::VmValue::Void();
    });
    // AOSP 4.4 ROM line 268.
    builder.StaticMethod(
        "makeText", "(Landroid/content/Context;II)Landroid/widget/Toast;",
        [](dx::IntrinsicContext& c) {
            const auto resources =
                CallAndroidMethod(c.vm, c.arguments[0].ref, "getResources", "()Landroid/content/res/Resources;").ref;
            const auto resource_root = c.vm.ProtectReferences(std::array{resources});
            const auto text =
                CallAndroidMethod(c.vm, resources, "getText", "(I)Ljava/lang/CharSequence;", {c.arguments[1]}).ref;
            const auto text_root = c.vm.ProtectReferences(std::array{text});
            const auto target = c.vm.Linker().FindDirectMethod(
                c.vm.Linker().ResolveDescriptor("Landroid/widget/Toast;"), "makeText",
                "(Landroid/content/Context;Ljava/lang/CharSequence;I)Landroid/widget/Toast;");
            const auto result =
                c.vm.Call(*target, std::vector<dx::VmValue>{c.arguments[0], dx::VmValue::Ref(text), c.arguments[2]});
            if (result.exception.IsValid())
                throw dx::VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                      result.exception};
            return result.value;
        },
        dx::kAccPublic);

    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
