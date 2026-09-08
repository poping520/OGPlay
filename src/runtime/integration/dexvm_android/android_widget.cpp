// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from android_widget_AbsoluteLayout_LayoutParams.cpp ----
#include "catalog.h"

// Defined by the TextView section below: constructor handler that also
// resolves the framework default style carried by defStyleAttr.
namespace ogplay::runtime::android_intrinsics {
[[nodiscard]] dx::IntrinsicHandler ViewDefaultStyleInitHandler(const Context& context);
}

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
    builder.Constructor("(Landroid/content/Context;Landroid/util/AttributeSet;I)V",
                        ViewDefaultStyleInitHandler(context));
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
            const auto node = EnsureViewUiNode(
                *context, call.receiver,
                UiClassForObject(call.vm, call.receiver));
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

void MergeTextStyle(dx::Interpreter& vm, const Context& context, std::uint32_t id,
                    TextAttributes& values, std::unordered_set<std::uint32_t>& seen) {
    if (id == 0) return;
    if (seen.size() >= 16 || !seen.insert(id).second)
        UnsupportedTextStyle(vm, "text style parent/reference cycle or depth exceeds 16");
    // AOSP core/res/res/values/themes.xml + colors.xml. This is the TextAppearance
    // projection of these themes, not a full framework resource table.
    const bool legacy = id >= 0x01030005U && id <= 0x01030011U;
    const bool holo = id == 0x0103006bU || id == 0x01030128U;
    if (legacy || holo) {
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
    const auto* entry = context->arsc.FindById(id);
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

TextAttributes ResolveTextTheme(dx::Interpreter& vm, const Context& context,
                                dx::VmObjectRef owner) {
    if (!owner.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "text appearance Context is null"};
    auto theme = context->application_theme;
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
    if (theme == 0) theme = context->target_sdk_version < 11 ? 0x01030005U :
                            context->target_sdk_version < 14 ? 0x0103006bU : 0x01030128U;
    TextAttributes theme_values;
    std::unordered_set<std::uint32_t> seen;
    MergeTextStyle(vm, context, theme, theme_values, seen);
    return theme_values;
}

TextAttributes ResolveTextAppearance(dx::Interpreter& vm, const Context& context,
                                      dx::VmObjectRef owner, std::uint32_t id) {
    const auto theme_values = ResolveTextTheme(vm, context, owner);
    auto values = theme_values;
    std::unordered_set<std::uint32_t> seen;
    MergeTextStyle(vm, context, id, values, seen);
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

dx::IntrinsicHandler ViewDefaultStyleInitHandler(const Context& context) {
    return dx::IntrinsicHandler(
        [context](dx::IntrinsicContext& call) {
            const auto owner = call.arguments[0].ref;
            if (!owner.IsValid())
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "View Context is null"};
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
            const auto theme = ResolveTextTheme(call.vm, context, owner);
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
            state->gravity = 0x11U;  // center_horizontal | center_vertical
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

Decl Declare_android_widget_TextView(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/TextView;", "Landroid/view/View;");
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
