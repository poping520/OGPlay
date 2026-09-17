// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from support_ui_binding.cpp ----
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"

#include <stdexcept>
#include <tuple>

namespace ogplay::runtime {

void AssignViewContext(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                       const dexvm::VmObjectRef view,
                       const dexvm::VmObjectRef owner) {
    if (!context.view_context_field.has_value() ||
        !context.view_context_field->IsValid()) {
        throw std::runtime_error("View mContext field is unbound");
    }
    dexvm::IntrinsicContext call{vm, view, {}};
    dexvm::IntrinsicCall(call).SetRef(*context.view_context_field, view, owner);
}

void BindViewToUiNode(DexVmAndroidContext& context,
                      const dexvm::VmObjectRef view,
                      const ui::UiNodeId node) {
    if (!view.IsValid() || context.ui_tree.Get(node) == nullptr) {
        throw std::runtime_error("cannot bind an invalid View or UI node");
    }
    const auto object = context.object_to_ui_node.find(view.Value());
    if (object != context.object_to_ui_node.end()) {
        if (object->second == node) return;
        throw std::runtime_error("guest View already has a different UI node");
    }
    const auto reverse = context.ui_node_to_object.find(node);
    if (reverse != context.ui_node_to_object.end()) {
        if (reverse->second == view) return;
        throw std::runtime_error("UI node already has a different guest View");
    }
    context.object_to_ui_node.emplace(view.Value(), node);
    context.ui_node_to_object.emplace(node, view);
}

ui::UiNodeId EnsureViewUiNode(DexVmAndroidContext& context,
                              const dexvm::VmObjectRef view,
                              const ui::UiClass kind) {
    if (!view.IsValid()) {
        throw std::runtime_error("cannot bind a null guest View");
    }
    const auto found = context.object_to_ui_node.find(view.Value());
    if (found != context.object_to_ui_node.end()) return found->second;
    const auto node = context.ui_tree.CreateNode(kind);
    BindViewToUiNode(context, view, node);
    return node;
}

std::optional<ui::UiNodeId> FindViewUiNode(
    const DexVmAndroidContext& context, const std::uint64_t view_handle) {
    const auto found = context.object_to_ui_node.find(view_handle);
    if (found == context.object_to_ui_node.end() ||
        context.ui_tree.Get(found->second) == nullptr) {
        return std::nullopt;
    }
    return found->second;
}

dexvm::VmObjectRef ViewObjectForUiNode(const DexVmAndroidContext& context,
                                       const ui::UiNodeId node) {
    const auto found = context.ui_node_to_object.find(node);
    return found == context.ui_node_to_object.end() ? dexvm::VmObjectRef{}
                                                     : found->second;
}

void InitializeDefaultViewBackground(dexvm::Interpreter& vm,
                                     DexVmAndroidContext& context,
                                     const dexvm::VmObjectRef view,
                                     const ui::UiNodeId node) {
    if (context.ui_tree.Get(node)->kind != ui::UiClass::Button ||
        context.ui_view_backgrounds.contains(view.Value())) {
        return;
    }
    const auto drawable =
        vm.NewIntrinsicInstance("Landroid/graphics/drawable/Drawable;");
    context.ui_view_backgrounds.emplace(view.Value(), drawable);
    context.ui_drawables.emplace(
        drawable.Value(),
        DexVmAndroidContext::UiDrawableState{
            .color = context.ui_tree.Get(node)->background_color,
            .callback_node = node});
}

void ResetViewUiState(DexVmAndroidContext& context) {
    context.ui_tree.Reset();
    context.object_to_ui_node.clear();
    context.ui_node_to_object.clear();
    context.ui_click_listeners.clear();
    context.ui_touch_listeners.clear();
    context.ui_view_layout_params.clear();
    context.ui_view_backgrounds.clear();
    context.ui_drawables.clear();
    context.text_watchers.clear();
    context.focused_edit_text = dexvm::VmObjectRef{};
}

}  // namespace ogplay::runtime

namespace ogplay::runtime::android_intrinsics {

ui::UiClass UiClassForDescriptor(const std::string_view descriptor) {
    if (descriptor == "Landroid/widget/FrameLayout;" ||
        descriptor == "Landroid/widget/AbsoluteLayout;") {
        return ui::UiClass::FrameLayout;
    }
    if (descriptor == "Landroid/widget/ScrollView;") return ui::UiClass::ScrollView;
    if (descriptor == "Landroid/widget/TableLayout;") return ui::UiClass::TableLayout;
    if (descriptor == "Landroid/widget/TableRow;") return ui::UiClass::TableRow;
    if (descriptor == "Landroid/widget/LinearLayout;") {
        return ui::UiClass::LinearLayout;
    }
    if (descriptor == "Landroid/widget/RelativeLayout;") {
        return ui::UiClass::RelativeLayout;
    }
    if (descriptor == "Landroid/widget/Button;") {
        return ui::UiClass::Button;
    }
    if (descriptor == "Landroid/widget/TextView;" ||
        descriptor == "Landroid/widget/EditText;") {
        if (descriptor == "Landroid/widget/EditText;") return ui::UiClass::EditText;
        return ui::UiClass::TextView;
    }
    if (descriptor == "Landroid/widget/ImageView;") {
        return ui::UiClass::ImageView;
    }
    if (descriptor == "Landroid/widget/ImageButton;") {
        return ui::UiClass::ImageButton;
    }
    if (descriptor == "Landroid/widget/VideoView;") {
        return ui::UiClass::VideoView;
    }
    return ui::UiClass::View;
}

ui::UiClass UiClassForObject(dexvm::Interpreter& vm,
                             const dexvm::VmObjectRef view) {
    if (!view.IsValid()) {
        throw std::runtime_error("cannot classify a null guest View");
    }
    auto type = vm.Model().ObjectClass(view);
    for (std::size_t depth = 0; depth < 64; ++depth) {
        const auto& klass = vm.Linker().Class(type);
        if (klass.descriptor == "Landroid/view/View;") break;
        const auto kind = UiClassForDescriptor(klass.descriptor);
        if (kind != ui::UiClass::View) return kind;
        if (!klass.super.has_value()) break;
        type = *klass.super;
    }
    return ui::UiClass::View;
}

}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from support_ui_inflater.cpp ----
#include "ogplay/runtime/integration/dexvm_android.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#include "ogplay/runtime/integration/host_image_decode.h"

namespace ogplay::runtime {
namespace {

constexpr std::string_view kAndroidNamespace =
    "http://schemas.android.com/apk/res/android";
constexpr std::uint8_t kTypeDimension = 0x05;
constexpr std::uint8_t kTypeFloat = 0x04;
constexpr std::uint8_t kTypeReference = 0x01;
constexpr std::uint8_t kTypeAttribute = 0x02;
constexpr std::uint8_t kTypeString = 0x03;
constexpr std::uint8_t kTypeFirstColor = 0x1c;
constexpr std::uint8_t kTypeLastColor = 0x1f;

constexpr std::array<UiWidgetDescriptor, 16> kWidgets{{
    {"View", "Landroid/view/View;", ui::UiClass::View},
    {"TextView", "Landroid/widget/TextView;", ui::UiClass::TextView},
    {"Button", "Landroid/widget/Button;", ui::UiClass::Button},
    {"EditText", "Landroid/widget/EditText;", ui::UiClass::EditText},
    {"ImageView", "Landroid/widget/ImageView;", ui::UiClass::ImageView},
    {"ImageButton", "Landroid/widget/ImageButton;", ui::UiClass::ImageButton},
    {"ProgressBar", "Landroid/widget/ProgressBar;", ui::UiClass::View},
    {"VideoView", "Landroid/widget/VideoView;", ui::UiClass::VideoView},
    {"WebView", "Landroid/webkit/WebView;", ui::UiClass::View},
    {"LinearLayout", "Landroid/widget/LinearLayout;", ui::UiClass::LinearLayout},
    {"FrameLayout", "Landroid/widget/FrameLayout;", ui::UiClass::FrameLayout},
    {"RelativeLayout", "Landroid/widget/RelativeLayout;", ui::UiClass::RelativeLayout},
    {"TableLayout", "Landroid/widget/TableLayout;", ui::UiClass::TableLayout},
    {"TableRow", "Landroid/widget/TableRow;", ui::UiClass::TableRow},
    {"ScrollView", "Landroid/widget/ScrollView;", ui::UiClass::ScrollView},
    {"AbsoluteLayout", "Landroid/widget/AbsoluteLayout;", ui::UiClass::FrameLayout},
}};

[[nodiscard]] const UiWidgetDescriptor* FindWidget(
    const std::string_view tag) {
    for (const auto& widget : kWidgets) {
        if (widget.xml_tag == tag) return &widget;
    }
    return nullptr;
}

[[nodiscard]] std::uint32_t AndroidColorToRgba(const std::uint32_t argb) {
    return ((argb & 0x00ffffffU) << 8U) | (argb >> 24U);
}

}  // namespace

[[nodiscard]] const loader::ArscEntry& ResolveUiResourceEntry(
    const DexVmAndroidContext& context, std::uint32_t resource_id) {
    std::unordered_set<std::uint32_t> seen;
    for (std::size_t depth = 0; depth < 16; ++depth) {
        if (!seen.insert(resource_id).second) {
            throw std::runtime_error("UI resource reference cycle");
        }
        const loader::ArscEntry* entry = nullptr;
        const auto width_dp = static_cast<int>(
            std::lround(context.surface_width / context.ui_density));
        const auto height_dp = static_cast<int>(
            std::lround(context.surface_height / context.ui_density));
        const auto smallest_dp = std::min(width_dp, height_dp);
        const auto orientation = width_dp > height_dp ? 2U : 1U;
        const auto density = static_cast<int>(
            std::lround(context.ui_density * 160.0F));
        const auto long_dp = std::max(width_dp, height_dp);
        const auto short_dp = std::min(width_dp, height_dp);
        const auto screen_size = long_dp < 470 ? 1U
            : long_dp >= 960 && short_dp >= 720 ? 4U
            : long_dp >= 640 && short_dp >= 480 ? 3U : 2U;
        const auto density_better = [density](const std::uint16_t lhs,
                                              const std::uint16_t rhs) {
            if (lhs == rhs) return false;
            int high = lhs != 0 ? lhs : 160;
            int low = rhs != 0 ? rhs : 160;
            bool lhs_high = true;
            if (low > high) {
                std::swap(low, high);
                lhs_high = false;
            }
            const int requested = density != 0 ? density : 160;
            if (requested >= high) return lhs_high;
            if (low >= requested) return !lhs_high;
            return (((2 * low) - requested) * high >
                    requested * requested) ? !lhs_high : lhs_high;
        };
        const auto better = [&](const loader::ArscEntry& lhs,
                                const loader::ArscEntry& rhs) {
            if (lhs.smallest_width_dp != rhs.smallest_width_dp)
                return lhs.smallest_width_dp > rhs.smallest_width_dp;
            const auto lhs_delta = width_dp - lhs.screen_width_dp +
                                   height_dp - lhs.screen_height_dp;
            const auto rhs_delta = width_dp - rhs.screen_width_dp +
                                   height_dp - rhs.screen_height_dp;
            if (lhs_delta != rhs_delta) return lhs_delta < rhs_delta;
            auto lhs_size = lhs.screen_layout & 0x0fU;
            auto rhs_size = rhs.screen_layout & 0x0fU;
            const auto lhs_raw_size = lhs_size;
            const auto rhs_raw_size = rhs_size;
            if (screen_size >= 2U) {
                if (lhs_size == 0U) lhs_size = 2U;
                if (rhs_size == 0U) rhs_size = 2U;
            }
            if (lhs_size != rhs_size) return lhs_size > rhs_size;
            if (lhs_raw_size != rhs_raw_size) return lhs_raw_size != 0U;
            if (lhs.orientation != rhs.orientation)
                return lhs.orientation != 0U;
            if (lhs.density != rhs.density)
                return density_better(lhs.density, rhs.density);
            if (lhs.sdk_version != rhs.sdk_version)
                return lhs.sdk_version > rhs.sdk_version;
            return std::tie(lhs.screen_layout, lhs.value_type, lhs.value_data,
                            lhs.string_value) <
                   std::tie(rhs.screen_layout, rhs.value_type, rhs.value_data,
                            rhs.string_value);
        };
        for (const auto& candidate : context.arsc.entries) {
            if (candidate.resource_id != resource_id ||
                (candidate.sdk_version != 0 && candidate.sdk_version > 19) ||
                (candidate.orientation != 0 &&
                 candidate.orientation != orientation) ||
                candidate.smallest_width_dp > smallest_dp ||
                candidate.screen_width_dp > width_dp ||
                candidate.screen_height_dp > height_dp ||
                ((candidate.screen_layout & 0x0fU) != 0U &&
                 (candidate.screen_layout & 0x0fU) > screen_size)) {
                continue;
            }
            if (entry == nullptr || better(candidate, *entry)) {
                entry = &candidate;
            }
        }
        if (entry == nullptr) {
            throw std::runtime_error("UI resource id is missing: " +
                                     std::to_string(resource_id));
        }
        if (entry->value_type != kTypeReference) return *entry;
        if (entry->value_data == 0U) {
            throw std::runtime_error("UI resource reference is null");
        }
        resource_id = entry->value_data;
    }
    throw std::runtime_error("UI resource reference depth exceeds 16");
}

namespace {

[[nodiscard]] std::int32_t ComplexDimensionPx(
    const DexVmAndroidContext& context, const std::uint32_t data,
    const bool scaled) {
    static constexpr std::array<double, 4> kRadixMultipliers{
        1.0 / 256.0, 1.0 / 32768.0, 1.0 / 8388608.0,
        1.0 / 2147483648.0};
    const auto radix = (data >> 4U) & 0x03U;
    const auto unit = data & 0x0fU;
    const auto mantissa = static_cast<std::int32_t>(data & 0xffffff00U);
    double multiplier = 1.0;
    if (unit == 1U) {
        multiplier = context.ui_density;
    } else if (unit == 2U) {
        multiplier = scaled ? context.ui_scaled_density : context.ui_density;
    } else if (unit != 0U) {
        throw std::runtime_error("unsupported UI dimension unit");
    }
    const auto value = static_cast<double>(mantissa) *
                       kRadixMultipliers[radix] * multiplier;
    if (!std::isfinite(value) || value < 0.0 ||
        value > std::numeric_limits<std::int32_t>::max()) {
        throw std::runtime_error("UI dimension is outside integer px range");
    }
    return static_cast<std::int32_t>(std::lround(value));
}

[[nodiscard]] std::int32_t DimensionValue(
    const DexVmAndroidContext& context,
    const loader::BinaryXmlAttribute& attribute, const bool scaled = false) {
    if (attribute.value_type == kTypeReference) {
        return ResolveUiDimension(context, attribute.data, scaled);
    }
    if (attribute.value_type == kTypeDimension) {
        return ComplexDimensionPx(context, attribute.data, scaled);
    }
    return static_cast<std::int32_t>(attribute.data);
}

void ApplyInflatedWidgetDefaults(const DexVmAndroidContext& context,
                                 ui::UiNode& node) {
    const auto dp = [&context](const float value) {
        return static_cast<std::int32_t>(
            std::lround(value * context.ui_density));
    };
    const auto sp = [&context](const float value) {
        return value * context.ui_scaled_density;
    };
    if (node.kind == ui::UiClass::TextView) {
        node.text_size_px = sp(14.0F);
    } else if (node.kind == ui::UiClass::Button) {
        node.text_size_px = sp(14.0F);
        node.minimum.width = dp(64.0F);
        node.minimum.height = dp(48.0F);
        node.gravity = 0x11U;
        node.clickable = true;
    } else if (node.kind == ui::UiClass::EditText) {
        node.text_size_px = sp(18.0F);
        node.minimum.height = dp(48.0F);
        node.gravity = 0x10U;
        node.clickable = true;
    }
}

void ApplyTextAppearance(const DexVmAndroidContext& context,
                         ui::UiNode& node, const std::uint32_t resource_id) {
    // API 19 public.xml and styles.xml: the six base text appearances differ
    // here only by size; colors remain owned by the theme/app overrides.
    switch (resource_id) {
        case 0x01030042U:
        case 0x01030043U:
            node.text_size_px = 22.0F * context.ui_scaled_density;
            return;
        case 0x01030044U:
        case 0x01030045U:
            node.text_size_px = 18.0F * context.ui_scaled_density;
            return;
        case 0x01030046U:
        case 0x01030047U:
            node.text_size_px = 14.0F * context.ui_scaled_density;
            return;
        default: break;
    }
    const auto& style = ResolveUiResourceEntry(context, resource_id);
    if (!style.is_complex || style.type_name != "style") {
        throw std::runtime_error("TextView textAppearance is not a style");
    }
    for (const auto& item : style.bag) {
        if (item.name == 0x01010095U && item.value_type == kTypeDimension) {
            node.text_size_px = static_cast<float>(
                ComplexDimensionPx(context, item.value_data, true));
        } else if (item.name == 0x01010098U &&
                   item.value_type >= kTypeFirstColor &&
                   item.value_type <= kTypeLastColor) {
            node.text_color = AndroidColorToRgba(item.value_data);
        }
    }
}

void ApplyStyleAttribute(const DexVmAndroidContext& context,
                         const UiWidgetDescriptor& widget, ui::UiNode& node,
                         const loader::BinaryXmlAttribute& attribute) {
    if (attribute.value_type == kTypeAttribute &&
        attribute.data == 0x01010078U &&
        widget.dex_descriptor == "Landroid/widget/ProgressBar;") {
        // API 19 R.attr.progressBarStyleHorizontal. All registered API 19
        // platform themes resolve it to the horizontal ProgressBar family.
        // Progress state/rendering is a separate widget capability; accepting
        // this constructor style must not imply that capability.
        return;
    }
    if (attribute.value_type == kTypeReference &&
        (node.kind == ui::UiClass::TextView ||
         node.kind == ui::UiClass::Button ||
         node.kind == ui::UiClass::EditText)) {
        ApplyTextAppearance(context, node, attribute.data);
        return;
    }
    throw std::runtime_error(
        "UI style resource is outside the registered API 19 projection");
}

[[nodiscard]] std::int32_t SiblingResourceId(
    const loader::BinaryXmlAttribute& attribute) {
    if (attribute.data == 0U ||
        attribute.data >
            static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
        throw std::runtime_error(
            "RelativeLayout sibling rule requires a positive resource id");
    }
    return static_cast<std::int32_t>(attribute.data);
}

[[nodiscard]] std::u16string AsciiText(const std::string& text) {
    std::u16string value;
    value.reserve(text.size());
    for (const auto byte : text) {
        if (static_cast<unsigned char>(byte) >= 0x80U) {
            throw std::runtime_error(
                "fixed-font XML text must use the supported ASCII subset");
        }
        value.push_back(static_cast<char16_t>(byte));
    }
    static_cast<void>(ui::MeasureFixedText(value, 8.0F));
    return value;
}

void ApplyAttribute(DexVmAndroidContext& context, const ui::UiNodeId node_id,
                    const loader::BinaryXmlAttribute& attribute,
                    std::uint32_t& drawable_id) {
    if (attribute.namespace_uri.empty() && attribute.name == "style") return;
    if (attribute.namespace_uri != kAndroidNamespace) return;
    auto& node = *context.ui_tree.Get(node_id);
    const auto& name = attribute.name;
    if (name == "id") {
        context.ui_tree.SetAndroidId(
            node_id, static_cast<std::int32_t>(attribute.data));
    } else if (name == "visibility") {
        if (attribute.data > 2U) {
            throw std::runtime_error("unsupported UI visibility enum");
        }
        context.ui_tree.SetVisibility(
            node_id, attribute.data == 0U
                         ? ui::Visibility::Visible
                         : attribute.data == 1U ? ui::Visibility::Invisible
                                                : ui::Visibility::Gone);
    } else if (name == "enabled") {
        node.enabled = attribute.data != 0U;
    } else if (name == "clickable") {
        context.ui_tree.SetClickable(node_id, attribute.data != 0U);
    } else if (name == "layout_width") {
        const auto value = DimensionValue(context, attribute);
        node.layout.width = value == -1
                                ? ui::DimensionSpec{ui::SizeMode::MatchParent, 0}
                                : value == -2
                                      ? ui::DimensionSpec{ui::SizeMode::WrapContent, 0}
                                      : ui::DimensionSpec{ui::SizeMode::Fixed, value};
    } else if (name == "layout_height") {
        const auto value = DimensionValue(context, attribute);
        node.layout.height = value == -1
                                 ? ui::DimensionSpec{ui::SizeMode::MatchParent, 0}
                                 : value == -2
                                       ? ui::DimensionSpec{ui::SizeMode::WrapContent, 0}
                                       : ui::DimensionSpec{ui::SizeMode::Fixed, value};
    } else if (name == "gravity") {
        node.gravity = attribute.data;
    } else if (name == "layout_gravity") {
        node.layout.layout_gravity = attribute.data;
    } else if (name == "orientation") {
        if (attribute.data > 1U) {
            throw std::runtime_error("unsupported UI orientation enum");
        }
        node.orientation = node.kind == ui::UiClass::TableLayout
                               ? ui::Orientation::Vertical
                           : node.kind == ui::UiClass::TableRow
                               ? ui::Orientation::Horizontal
                               : attribute.data == 0U
                                     ? ui::Orientation::Horizontal
                                     : ui::Orientation::Vertical;
    } else if (name == "layout_weight") {
        if (attribute.value_type != kTypeFloat) {
            throw std::runtime_error("UI layout_weight is not a float");
        }
        node.layout.weight = std::bit_cast<float>(attribute.data);
    } else if (name == "padding") {
        const auto value = DimensionValue(context, attribute);
        node.padding = ui::Insets{value, value, value, value};
    } else if (name == "paddingLeft") {
        node.padding.left = DimensionValue(context, attribute);
    } else if (name == "paddingTop") {
        node.padding.top = DimensionValue(context, attribute);
    } else if (name == "paddingRight") {
        node.padding.right = DimensionValue(context, attribute);
    } else if (name == "paddingBottom") {
        node.padding.bottom = DimensionValue(context, attribute);
    } else if (name == "layout_margin") {
        const auto value = DimensionValue(context, attribute);
        node.layout.margin = ui::Insets{value, value, value, value};
    } else if (name == "layout_marginLeft") {
        node.layout.margin.left = DimensionValue(context, attribute);
    } else if (name == "layout_marginTop") {
        node.layout.margin.top = DimensionValue(context, attribute);
    } else if (name == "layout_marginRight") {
        node.layout.margin.right = DimensionValue(context, attribute);
    } else if (name == "layout_marginBottom") {
        node.layout.margin.bottom = DimensionValue(context, attribute);
    } else if (name == "minWidth") {
        node.minimum.width = DimensionValue(context, attribute);
    } else if (name == "minHeight") {
        node.minimum.height = DimensionValue(context, attribute);
    } else if (name == "text") {
        node.text = attribute.value_type == kTypeReference
                        ? ResolveUiString(context, attribute.data)
                        : attribute.raw_string.has_value()
                              ? AsciiText(*attribute.raw_string)
                              : throw std::runtime_error(
                                    "TextView text is not a string");
    } else if (name == "textColor") {
        node.text_color = attribute.value_type == kTypeReference
                              ? ResolveUiColor(context, attribute.data)
                              : attribute.value_type >= kTypeFirstColor &&
                                        attribute.value_type <= kTypeLastColor
                                    ? AndroidColorToRgba(attribute.data)
                                    : throw std::runtime_error(
                                          "TextView textColor is not a color");
    } else if (name == "textAppearance") {
        if (attribute.value_type != kTypeReference || attribute.data == 0U) {
            throw std::runtime_error(
                "TextView textAppearance is not a resource reference");
        }
        ApplyTextAppearance(context, node, attribute.data);
    } else if (name == "textSize") {
        const auto size = static_cast<float>(
            DimensionValue(context, attribute, true));
        static_cast<void>(ui::MeasureFixedText(node.text, size));
        node.text_size_px = size;
    } else if (name == "singleLine") {
        node.max_lines = attribute.data == 0U
                             ? std::numeric_limits<std::int32_t>::max()
                             : 1;
    } else if (name == "maxLines") {
        if (attribute.data == 0U ||
            attribute.data > static_cast<std::uint32_t>(
                                 std::numeric_limits<std::int32_t>::max())) {
            throw std::runtime_error("TextView maxLines is out of range");
        }
        node.max_lines = static_cast<std::int32_t>(attribute.data);
    } else if (name == "maxLength") {
        if (attribute.data > static_cast<std::uint32_t>(
                                 std::numeric_limits<std::int32_t>::max())) {
            throw std::runtime_error("TextView maxLength is out of range");
        }
        node.max_length = static_cast<std::int32_t>(attribute.data);
    } else if (name == "inputType") {
        node.numeric_input = (attribute.data & 0x0fU) == 0x02U;
    } else if (name == "background") {
        if (attribute.value_type == kTypeReference) {
            static_cast<void>(ResolveUiDrawable(context, attribute.data));
            node.background_color.reset();
            node.background_resource_id = attribute.data;
        } else if (attribute.value_type >= kTypeFirstColor &&
                   attribute.value_type <= kTypeLastColor) {
            node.background_color = AndroidColorToRgba(attribute.data);
            node.background_resource_id = 0;
        } else {
            throw std::runtime_error("UI background is not a drawable resource");
        }
    } else if (name == "layout_alignParentLeft") {
        node.layout.relative.align_parent_left = attribute.data != 0U;
    } else if (name == "layout_alignParentRight") {
        node.layout.relative.align_parent_right = attribute.data != 0U;
    } else if (name == "layout_alignParentTop") {
        node.layout.relative.align_parent_top = attribute.data != 0U;
    } else if (name == "layout_alignParentBottom") {
        node.layout.relative.align_parent_bottom = attribute.data != 0U;
    } else if (name == "layout_centerInParent") {
        node.layout.relative.center_in_parent = attribute.data != 0U;
    } else if (name == "layout_centerHorizontal") {
        node.layout.relative.center_horizontal = attribute.data != 0U;
    } else if (name == "layout_centerVertical") {
        node.layout.relative.center_vertical = attribute.data != 0U;
    } else if (name == "layout_toLeftOf") {
        node.layout.relative.left_of = SiblingResourceId(attribute);
    } else if (name == "layout_toRightOf") {
        node.layout.relative.right_of = SiblingResourceId(attribute);
    } else if (name == "layout_above") {
        node.layout.relative.above = SiblingResourceId(attribute);
    } else if (name == "layout_below") {
        node.layout.relative.below = SiblingResourceId(attribute);
    } else if (name == "layout_alignLeft") {
        node.layout.relative.align_left = SiblingResourceId(attribute);
    } else if (name == "layout_alignRight") {
        node.layout.relative.align_right = SiblingResourceId(attribute);
    } else if (name == "layout_alignTop") {
        node.layout.relative.align_top = SiblingResourceId(attribute);
    } else if (name == "layout_alignBottom") {
        node.layout.relative.align_bottom = SiblingResourceId(attribute);
    } else if (name == "src") {
        drawable_id = attribute.data;
        node.image_resource_id = attribute.data;
    } else if (name == "scaleType") {
        switch (attribute.data) {
            case 1: node.image_scale_type = ui::ImageScaleType::FitXy; break;
            case 3: node.image_scale_type = ui::ImageScaleType::FitCenter; break;
            case 5: node.image_scale_type = ui::ImageScaleType::Center; break;
            case 6: node.image_scale_type = ui::ImageScaleType::CenterCrop; break;
            case 7:
                node.image_scale_type = ui::ImageScaleType::CenterInside;
                break;
            default:
                throw std::runtime_error("unsupported ImageView scaleType");
        }
    } else if (name.starts_with("layout_")) {
        throw std::runtime_error("unsupported structural UI attribute: " +
                                 name);
    }
}

}  // namespace

std::string ResolveResourceString(const DexVmAndroidContext& context,
                                  const std::uint32_t resource_id) {
        const auto& entry = ResolveUiResourceEntry(context, resource_id);
    if (entry.value_type != kTypeString || !entry.string_value.has_value()) {
        throw std::runtime_error("resource is not a string");
    }
    return *entry.string_value;
}

std::u16string ResolveUiString(const DexVmAndroidContext& context,
                               const std::uint32_t resource_id) {
    return AsciiText(ResolveResourceString(context, resource_id));
}

std::uint32_t ResolveUiColor(const DexVmAndroidContext& context,
                             const std::uint32_t resource_id) {
    const auto& entry = ResolveUiResourceEntry(context, resource_id);
    if (entry.value_type < kTypeFirstColor ||
        entry.value_type > kTypeLastColor) {
        throw std::runtime_error("UI resource is not a color");
    }
    return AndroidColorToRgba(entry.value_data);
}

std::int32_t ResolveUiDimension(const DexVmAndroidContext& context,
                                const std::uint32_t resource_id,
                                const bool scaled) {
    const auto& entry = ResolveUiResourceEntry(context, resource_id);
    if (entry.value_type != kTypeDimension) {
        throw std::runtime_error("UI resource is not a dimension");
    }
    return ComplexDimensionPx(context, entry.value_data, scaled);
}

std::shared_ptr<const ui::UiBitmap> ResolveUiDrawable(
    DexVmAndroidContext& context, const std::uint32_t resource_id) {
    if (resource_id == 0U) {
        throw std::runtime_error("UI drawable resource id is null");
    }
    const auto density_bits = std::bit_cast<std::uint32_t>(context.ui_density);
    if (context.ui_bitmap_config_width != context.surface_width ||
        context.ui_bitmap_config_height != context.surface_height ||
        context.ui_bitmap_config_density_bits != density_bits) {
        context.ui_bitmaps.clear();
        context.ui_bitmap_config_width = context.surface_width;
        context.ui_bitmap_config_height = context.surface_height;
        context.ui_bitmap_config_density_bits = density_bits;
    }
    if (const auto cached = context.ui_bitmaps.find(resource_id);
        cached != context.ui_bitmaps.end()) {
        return cached->second;
    }
    const auto& drawable = ResolveUiResourceEntry(context, resource_id);
    auto bitmap = std::make_shared<ui::UiBitmap>();
    if (drawable.value_type >= kTypeFirstColor &&
        drawable.value_type <= kTypeLastColor) {
        const auto rgba = AndroidColorToRgba(drawable.value_data);
        bitmap->width = 1;
        bitmap->height = 1;
        bitmap->rgba8 = {static_cast<std::uint8_t>(rgba >> 24U),
                         static_cast<std::uint8_t>(rgba >> 16U),
                         static_cast<std::uint8_t>(rgba >> 8U),
                         static_cast<std::uint8_t>(rgba)};
    } else if (drawable.value_type == kTypeString &&
               drawable.string_value.has_value()) {
        const auto bytes = loader::ReadApkEntry(
            context.apk_bytes, context.archive, *drawable.string_value);
        const auto decoded = DecodeImageToArgb(bytes);
        if (!decoded.has_value()) {
            throw std::runtime_error("UI drawable image decode failed");
        }
        bitmap->width = decoded->width;
        bitmap->height = decoded->height;
        bitmap->rgba8.reserve(decoded->argb.size() * 4U);
        for (const auto argb : decoded->argb) {
            bitmap->rgba8.push_back(static_cast<std::uint8_t>(argb >> 16U));
            bitmap->rgba8.push_back(static_cast<std::uint8_t>(argb >> 8U));
            bitmap->rgba8.push_back(static_cast<std::uint8_t>(argb));
            bitmap->rgba8.push_back(static_cast<std::uint8_t>(argb >> 24U));
        }
        // Android's compiled nine-patch chunk is stored as big-endian words
        // in the PNG. Keep stretch divisions and content padding beside the
        // shared immutable pixels; Drawable alpha/bounds remain per instance.
        const auto be32 = [&bytes](const std::size_t offset) {
            const auto byte = [&bytes](const std::size_t at) {
                return std::to_integer<std::uint32_t>(bytes[at]);
            };
            return (byte(offset) << 24U) | (byte(offset + 1) << 16U) |
                   (byte(offset + 2) << 8U) | byte(offset + 3);
        };
        for (std::size_t offset = 8; offset + 12 <= bytes.size();) {
            const auto size = be32(offset);
            if (size > bytes.size() - offset - 12U) break;
            if (std::to_integer<char>(bytes[offset + 4]) == 'n' &&
                std::to_integer<char>(bytes[offset + 5]) == 'p' &&
                std::to_integer<char>(bytes[offset + 6]) == 'T' &&
                std::to_integer<char>(bytes[offset + 7]) == 'c' &&
                size >= 48U &&
                std::to_integer<unsigned>(bytes[offset + 9]) == 2U &&
                std::to_integer<unsigned>(bytes[offset + 10]) == 2U) {
                const auto data = offset + 8U;
                bitmap->nine_patch = true;
                bitmap->nine_patch_padding = {
                    static_cast<std::int32_t>(be32(data + 12U)),
                    static_cast<std::int32_t>(be32(data + 20U)),
                    static_cast<std::int32_t>(be32(data + 16U)),
                    static_cast<std::int32_t>(be32(data + 24U))};
                bitmap->stretch_x = {
                    static_cast<std::int32_t>(be32(data + 32U)),
                    static_cast<std::int32_t>(be32(data + 36U))};
                bitmap->stretch_y = {
                    static_cast<std::int32_t>(be32(data + 40U)),
                    static_cast<std::int32_t>(be32(data + 44U))};
                break;
            }
            offset += 12U + size;
        }
    } else {
        throw std::runtime_error("UI resource is not a drawable");
    }
    const auto immutable = std::shared_ptr<const ui::UiBitmap>{bitmap};
    context.ui_bitmaps.emplace(resource_id, immutable);
    return immutable;
}

namespace {

class IncludeExpander final {
public:
    explicit IncludeExpander(const UiLayoutLoader& loader) : loader_(loader) {}

    std::vector<loader::BinaryXmlElement> Expand(
        const std::span<const loader::BinaryXmlElement> elements,
        const std::optional<std::uint32_t> root_layout_id) {
        if (root_layout_id.has_value()) stack_.push_back(*root_layout_id);
        const auto root = Validate(elements);
        Emit(elements, root, -1, nullptr);
        if (root_layout_id.has_value()) stack_.pop_back();
        return std::move(output_);
    }

private:
    [[nodiscard]] static std::size_t Validate(
        const std::span<const loader::BinaryXmlElement> elements) {
        if (elements.empty()) throw std::runtime_error("layout has no root element");
        std::optional<std::size_t> root;
        for (std::size_t index = 0; index < elements.size(); ++index) {
            if (elements[index].parent < 0) {
                if (root.has_value()) {
                    throw std::runtime_error(
                        "layout must have exactly one root element");
                }
                root = index;
            } else if (static_cast<std::size_t>(elements[index].parent) >=
                       index) {
                throw std::runtime_error("layout parent order is invalid");
            }
        }
        if (!root.has_value()) {
            throw std::runtime_error("layout must have exactly one root element");
        }
        return *root;
    }

    [[nodiscard]] static std::uint32_t IncludeLayoutId(
        const loader::BinaryXmlElement& include) {
        for (const auto& attribute : include.attributes) {
            if (attribute.namespace_uri == kAndroidNamespace &&
                attribute.name == "layout") {
                if (attribute.value_type != kTypeReference ||
                    attribute.data == 0U) {
                    throw std::runtime_error(
                        "include layout must be a non-null resource reference");
                }
                return attribute.data;
            }
        }
        throw std::runtime_error("include is missing android:layout");
    }

    [[nodiscard]] static bool HasIncludeOverrides(
        const loader::BinaryXmlElement& include) {
        return std::any_of(include.attributes.begin(), include.attributes.end(),
                           [](const auto& attribute) {
            return !(attribute.namespace_uri == kAndroidNamespace &&
                     attribute.name == "layout");
        });
    }

    static void ApplyIncludeOverrides(
        loader::BinaryXmlElement& target,
        const loader::BinaryXmlElement& include) {
        for (const auto& attribute : include.attributes) {
            if (attribute.namespace_uri == kAndroidNamespace &&
                attribute.name == "layout") {
                continue;
            }
            if (attribute.namespace_uri != kAndroidNamespace ||
                !(attribute.name == "id" || attribute.name == "visibility" ||
                  attribute.name == "layout_width" ||
                  attribute.name == "layout_height" ||
                  attribute.name.starts_with("layout_"))) {
                throw std::runtime_error(
                    "unsupported include override attribute: " +
                    attribute.name);
            }
            const auto found = std::find_if(
                target.attributes.begin(), target.attributes.end(),
                [&attribute](const auto& existing) {
                    return existing.namespace_uri == attribute.namespace_uri &&
                           existing.name == attribute.name;
                });
            if (found == target.attributes.end()) {
                target.attributes.push_back(attribute);
            } else {
                *found = attribute;
            }
        }
    }

    void Emit(const std::span<const loader::BinaryXmlElement> elements,
              const std::size_t index, const std::int32_t output_parent,
              const loader::BinaryXmlElement* overrides) {
        const auto& element = elements[index];
        if (element.name == "include") {
            for (std::size_t child = index + 1; child < elements.size(); ++child) {
                if (elements[child].parent == static_cast<std::int32_t>(index)) {
                    throw std::runtime_error("include cannot have child elements");
                }
            }
            const auto layout_id = IncludeLayoutId(element);
            if (stack_.size() >= 16U) {
                throw std::runtime_error("include depth exceeds 16");
            }
            if (std::find(stack_.begin(), stack_.end(), layout_id) !=
                stack_.end()) {
                throw std::runtime_error("include layout resource cycle");
            }
            stack_.push_back(layout_id);
            const auto included = loader_(layout_id);
            const auto included_root = Validate(included);
            if (included[included_root].name == "merge") {
                if (HasIncludeOverrides(element)) {
                    throw std::runtime_error(
                        "include overrides require a concrete layout root");
                }
                EmitChildren(included, included_root, output_parent);
            } else {
                Emit(included, included_root, output_parent, &element);
            }
            stack_.pop_back();
            return;
        }

        auto emitted = element;
        emitted.parent = output_parent;
        if (overrides != nullptr) ApplyIncludeOverrides(emitted, *overrides);
        if (output_.size() >= ui::UiTree::kMaxNodes) {
            throw std::runtime_error("expanded layout node limit exceeded");
        }
        const auto output_index = static_cast<std::int32_t>(output_.size());
        output_.push_back(std::move(emitted));
        EmitChildren(elements, index, output_index);
    }

    void EmitChildren(
        const std::span<const loader::BinaryXmlElement> elements,
        const std::size_t parent, const std::int32_t output_parent) {
        for (std::size_t child = parent + 1; child < elements.size(); ++child) {
            if (elements[child].parent == static_cast<std::int32_t>(parent)) {
                Emit(elements, child, output_parent, nullptr);
            }
        }
    }

    const UiLayoutLoader& loader_;
    std::vector<std::uint32_t> stack_;
    std::vector<loader::BinaryXmlElement> output_;
};

[[nodiscard]] std::vector<loader::BinaryXmlElement> LoadLayout(
    const DexVmAndroidContext& context, const std::uint32_t layout_id) {
    const auto& entry = ResolveUiResourceEntry(context, layout_id);
    if (entry.type_name != "layout" || !entry.string_value.has_value()) {
        throw std::runtime_error("UI resource is not a layout file");
    }
    const auto bytes = loader::ReadApkEntry(
        context.apk_bytes, context.archive, *entry.string_value);
    return loader::ParseBinaryXmlElements(bytes);
}

}  // namespace

std::vector<loader::BinaryXmlElement> ExpandUiIncludes(
    const std::span<const loader::BinaryXmlElement> elements,
    const UiLayoutLoader& loader,
    const std::optional<std::uint32_t> root_layout_id) {
    if (!loader) throw std::runtime_error("UI include loader is missing");
    return IncludeExpander(loader).Expand(elements, root_layout_id);
}

std::span<const UiWidgetDescriptor> UiWidgetRegistry() { return kWidgets; }

dexvm::VmObjectRef InflateUiElements(
    dexvm::Interpreter& vm, DexVmAndroidContext& context,
    const std::span<const loader::BinaryXmlElement> elements,
    const dexvm::VmObjectRef inflater_context) {
    ResetViewUiState(context);
    try {
        auto expanded = ExpandUiIncludes(
            elements, [&context](const std::uint32_t layout_id) {
                return LoadLayout(context, layout_id);
            });
        const auto document = std::span<const loader::BinaryXmlElement>{expanded};
        if (document.empty()) {
            throw std::runtime_error("layout has no root element");
        }
        std::size_t root_count = 0;
        for (std::size_t index = 0; index < document.size(); ++index) {
            const auto parent = document[index].parent;
            if (parent < 0) {
                ++root_count;
            } else if (static_cast<std::size_t>(parent) >= index) {
                throw std::runtime_error("layout parent order is invalid");
            }
        }
        if (root_count != 1) {
            throw std::runtime_error("layout must have exactly one root element");
        }

        std::vector<ui::UiNodeId> node_of(document.size());
        dexvm::VmObjectRef content_view;
        for (std::size_t index = 0; index < document.size(); ++index) {
            const auto& element = document[index];
            const auto parent_node =
                element.parent < 0
                    ? context.ui_tree.Root()
                    : node_of[static_cast<std::size_t>(element.parent)];
            if (element.name == "merge") {
                if (index != 0 || element.parent >= 0) {
                    throw std::runtime_error("merge must be the layout root");
                }
                node_of[index] = parent_node;
                continue;
            }
            const auto* widget = FindWidget(element.name);
            if (widget == nullptr) {
                if (auto* ledger = vm.Ledger(); ledger != nullptr) {
                    ledger->RecordUnimplemented(
                        "runtime.ui.inflate.tag." + element.name, 0);
                }
                throw std::runtime_error("unsupported structural UI tag: " +
                                         element.name);
            }
            const auto view = vm.NewIntrinsicInstance(widget->dex_descriptor);
            const auto node = context.ui_tree.CreateNode(widget->kind);
            BindViewToUiNode(context, view, node);
            AssignViewContext(vm, context, view, inflater_context);
            ApplyInflatedWidgetDefaults(context, *context.ui_tree.Get(node));
            InitializeDefaultViewBackground(vm, context, view, node);
            std::uint32_t drawable_id = element.src;
            const loader::BinaryXmlAttribute* style = nullptr;
            for (const auto& attribute : element.attributes) {
                if (!attribute.namespace_uri.empty() ||
                    attribute.name != "style") {
                    continue;
                }
                if (style != nullptr) {
                    throw std::runtime_error(
                        "UI element has duplicate style attributes");
                }
                style = &attribute;
            }
            if (style != nullptr) {
                try {
                    ApplyStyleAttribute(context, *widget,
                                        *context.ui_tree.Get(node), *style);
                } catch (const std::runtime_error&) {
                    if (auto* ledger = vm.Ledger(); ledger != nullptr) {
                        ledger->RecordUnimplemented(
                            "runtime.ui.inflate.attribute.style", 0);
                    }
                    throw;
                }
            }
            for (const auto& attribute : element.attributes) {
                if (attribute.namespace_uri.empty() &&
                    attribute.name == "style") {
                    continue;
                }
                try {
                    ApplyAttribute(context, node, attribute, drawable_id);
                } catch (const std::runtime_error&) {
                    if (auto* ledger = vm.Ledger(); ledger != nullptr) {
                        ledger->RecordUnimplemented(
                            "runtime.ui.inflate.attribute." + attribute.name,
                            0);
                    }
                    throw;
                }
            }
            if (const auto* state = context.ui_tree.Get(node);
                state->background_resource_id != 0U) {
                const auto old = context.ui_view_backgrounds.find(view.Value());
                if (old != context.ui_view_backgrounds.end()) {
                    auto old_state = context.ui_drawables.find(old->second.Value());
                    if (old_state != context.ui_drawables.end() &&
                        old_state->second.callback_node == node) {
                        old_state->second.callback_node.reset();
                    }
                }
                const auto drawable = vm.NewIntrinsicInstance(
                    "Landroid/graphics/drawable/Drawable;");
                context.ui_drawables[drawable.Value()] = {
                    .resource_id = state->background_resource_id,
                    .callback_node = node};
                context.ui_view_backgrounds[view.Value()] = drawable;
            }
            if (element.id != 0 &&
                context.ui_tree.Get(node)->android_id < 0) {
                context.ui_tree.SetAndroidId(
                    node, static_cast<std::int32_t>(element.id));
            }
            context.ui_tree.Attach(parent_node, node);
            node_of[index] = node;
            if (!content_view.IsValid()) content_view = view;

            if (drawable_id != 0U) {
                const auto image = ResolveUiDrawable(context, drawable_id);
                context.ui_tree.Get(node)->intrinsic = {image->width,
                                                        image->height};
            }
        }
        if (!content_view.IsValid()) {
            throw std::runtime_error("layout produced no inflatable widget");
        }
        return content_view;
    } catch (...) {
        ResetViewUiState(context);
        throw;
    }
}

dexvm::VmObjectRef InflateUiLayoutResource(
    dexvm::Interpreter& vm, DexVmAndroidContext& context,
    const std::uint32_t layout_id,
    const dexvm::VmObjectRef inflater_context) {
    try {
        const auto root = LoadLayout(context, layout_id);
        const auto expanded = ExpandUiIncludes(
            root,
            [&context](const std::uint32_t included_id) {
                return LoadLayout(context, included_id);
            },
            layout_id);
        return InflateUiElements(vm, context, expanded, inflater_context);
    } catch (...) {
        ResetViewUiState(context);
        throw;
    }
}

}  // namespace ogplay::runtime

// ---- migrated from support_widget_dispatch.cpp ----
// Widget click dispatch over the UiTree's most recent resolved geometry.

#include <optional>
#include <vector>

#include "shared.h"

namespace ogplay::runtime {
namespace android_intrinsics {

std::int32_t VisibilityOf(const DexVmAndroidContext& context,
                          const std::uint64_t handle) {
    const auto binding = FindViewUiNode(context, handle);
    if (!binding.has_value()) return kVisible;
    switch (context.ui_tree.Get(*binding)->visibility) {
        case ui::Visibility::Visible:
            return kVisible;
        case ui::Visibility::Invisible:
            return kInvisible;
        case ui::Visibility::Gone:
            return kGone;
    }
    return kVisible;
}

namespace {
[[nodiscard]] bool Contains(const ui::Rect rect, const float x, const float y) {
    return x >= static_cast<float>(rect.left) &&
           y >= static_cast<float>(rect.top) &&
           x < static_cast<float>(rect.right) &&
           y < static_cast<float>(rect.bottom);
}

void EnsureLayout(DexVmAndroidContext& context) {
    if (context.ui_tree.Get(context.ui_tree.Root())->layout_dirty) {
        ui::LayoutUiTree(
            context.ui_tree,
            {static_cast<std::int32_t>(context.surface_width),
             static_cast<std::int32_t>(context.surface_height)});
    }
}

[[nodiscard]] std::optional<ui::UiNodeId> HitClickable(
    const DexVmAndroidContext& context, const ui::UiNodeId id, const float x,
    const float y) {
    const auto* node = context.ui_tree.Get(id);
    if (node == nullptr || node->visibility != ui::Visibility::Visible ||
        !node->enabled || !Contains(node->screen_frame, x, y)) {
        return std::nullopt;
    }
    for (auto child = node->children.rbegin(); child != node->children.rend();
         ++child) {
        if (const auto hit = HitClickable(context, *child, x, y);
            hit.has_value()) {
            return hit;
        }
    }
    const auto click = context.ui_click_listeners.find(id);
    const auto touch = context.ui_touch_listeners.find(id);
    const bool has_click = click != context.ui_click_listeners.end() &&
                           click->second.IsValid();
    const bool has_touch = touch != context.ui_touch_listeners.end() &&
                           touch->second.IsValid();
    return (has_touch || (has_click && node->clickable))
               ? std::optional<ui::UiNodeId>{id}
               : std::nullopt;
}

void CollectTouchReceivers(const DexVmAndroidContext& context,
                           const ui::UiNodeId id, const float x,
                           const float y,
                           std::vector<std::uint64_t>& receivers) {
    const auto* node = context.ui_tree.Get(id);
    if (node == nullptr || !context.ui_tree.IsAttached(id) ||
        node->visibility != ui::Visibility::Visible || !node->enabled ||
        !Contains(node->screen_frame, x, y)) {
        return;
    }
    for (auto child = node->children.rbegin(); child != node->children.rend();
         ++child) {
        CollectTouchReceivers(context, *child, x, y, receivers);
    }
    const auto view = ViewObjectForUiNode(context, id);
    if (view.IsValid()) receivers.push_back(view.Value());
}

}  // namespace

}  // namespace android_intrinsics

std::optional<std::uint64_t> FindClickableViewAt(
    DexVmAndroidContext& context, const float x, const float y) {
    android_intrinsics::EnsureLayout(context);
    const auto hit = android_intrinsics::HitClickable(
        context, context.ui_tree.Root(), x, y);
    if (!hit.has_value()) return std::nullopt;
    const auto view = ViewObjectForUiNode(context, *hit);
    return view.IsValid() ? std::optional<std::uint64_t>{view.Value()}
                          : std::nullopt;
}

std::vector<std::uint64_t> FindTouchReceiversAt(
    DexVmAndroidContext& context, const float x, const float y) {
    android_intrinsics::EnsureLayout(context);
    std::vector<std::uint64_t> receivers;
    android_intrinsics::CollectTouchReceivers(
        context, context.ui_tree.Root(), x, y, receivers);
    return receivers;
}

bool ViewContainsPoint(DexVmAndroidContext& context,
                       const std::uint64_t handle, const float x,
                       const float y) {
    android_intrinsics::EnsureLayout(context);
    const auto node = FindViewUiNode(context, handle);
    if (!node.has_value()) return false;
    const auto* state = context.ui_tree.Get(*node);
    return state != nullptr && context.ui_tree.IsAttached(*node) &&
           state->visibility == ui::Visibility::Visible &&
           state->enabled &&
           android_intrinsics::Contains(
               state->screen_frame, x, y);
}

ViewTouchResult InvokeViewOnTouch(dexvm::Interpreter& vm,
                                  DexVmAndroidContext& context,
                                  const std::uint64_t handle,
                                  const std::int32_t action, const float x,
                                  const float y) {
    const auto node = FindViewUiNode(context, handle);
    if (!node.has_value() || context.ui_tree.Get(*node) == nullptr) return {};
    const auto found = context.ui_touch_listeners.find(*node);
    if (found == context.ui_touch_listeners.end() ||
        !found->second.IsValid()) return {};
    const auto view = ViewObjectForUiNode(context, *node);
    const auto event = MakeMotionEvent(vm, action, x, y, 0);
    auto& linker = vm.Linker();
    const auto listener_class = vm.Model().ObjectClass(found->second);
    const auto index = linker.FindVtableIndex(
        listener_class, "onTouch",
        "(Landroid/view/View;Landroid/view/MotionEvent;)Z");
    if (!index.has_value()) return {false, "touch listener has no onTouch method"};
    const auto outcome = vm.Call(
        linker.Class(listener_class).vtable[*index],
        std::vector<dexvm::VmValue>{dexvm::VmValue::Ref(found->second),
                                    dexvm::VmValue::Ref(view),
                                    dexvm::VmValue::Ref(event)});
    if (outcome.exception.IsValid()) {
        return {false, "onTouch raised: " + outcome.exception_message};
    }
    return {outcome.value.AsInt() != 0, std::nullopt};
}

ViewGestureDispatchResult DispatchViewGestureEvent(
    dexvm::Interpreter& vm, DexVmAndroidContext& context,
    const std::uint64_t handle, const std::int32_t action, const float x,
    const float y, bool click_eligible, bool touch_consumed) {
    constexpr std::int32_t kActionDown = 0;
    constexpr std::int32_t kActionUp = 1;
    constexpr std::int32_t kActionCancel = 3;
    const auto touch = InvokeViewOnTouch(vm, context, handle, action, x, y);
    if (touch.error.has_value()) {
        return {.error = touch.error};
    }
    touch_consumed = touch_consumed || touch.handled;
    const auto node = FindViewUiNode(context, handle);
    const auto has_live_click = [&]() {
        if (!node.has_value() || context.ui_tree.Get(*node) == nullptr) {
            return false;
        }
        const auto found = context.ui_click_listeners.find(*node);
        return found != context.ui_click_listeners.end() &&
               found->second.IsValid();
    }();
    const auto is_clickable = [&]() {
        if (!node.has_value()) return false;
        const auto* state = context.ui_tree.Get(*node);
        return state != nullptr && state->clickable;
    }();
    if (action == kActionDown) click_eligible = has_live_click && is_clickable;
    if (action == kActionCancel) {
        return {.handled = touch_consumed || click_eligible,
                .keep_capture = false,
                .click_eligible = false,
                .touch_consumed = touch_consumed};
    }
    if (action == kActionUp) {
        if (!touch_consumed && click_eligible && has_live_click &&
            is_clickable && ViewContainsPoint(context, handle, x, y)) {
            if (const auto error = InvokeViewOnClick(vm, context, handle);
                error.has_value()) {
                return {.error = error};
            }
        }
        return {.handled = touch_consumed || click_eligible};
    }
    const bool owns_gesture = touch_consumed || click_eligible;
    return {.handled = owns_gesture,
            .keep_capture = owns_gesture,
            .click_eligible = click_eligible,
            .touch_consumed = touch_consumed};
}

std::optional<std::string> InvokeViewOnClick(dexvm::Interpreter& vm,
                                             DexVmAndroidContext& context,
                                             const std::uint64_t handle) {
    const auto node = FindViewUiNode(context, handle);
    if (!node.has_value()) return "view has no UI binding";
    const auto listener_binding = context.ui_click_listeners.find(*node);
    if (listener_binding == context.ui_click_listeners.end() ||
        !listener_binding->second.IsValid()) {
        return "view has no click listener";
    }
    const auto view = ViewObjectForUiNode(context, *node);
    if (!view.IsValid()) return "UI node has no guest View";
    auto& linker = vm.Linker();
    const auto listener = listener_binding->second;
    const auto listener_class = vm.Model().ObjectClass(listener);
    const auto index = linker.FindVtableIndex(listener_class, "onClick",
                                              "(Landroid/view/View;)V");
    if (!index.has_value()) {
        return "click listener has no onClick method";
    }
    const auto outcome =
        vm.Call(linker.Class(listener_class).vtable[*index],
                std::vector<dexvm::VmValue>{dexvm::VmValue::Ref(listener),
                                            dexvm::VmValue::Ref(view)});
    if (outcome.exception.IsValid()) {
        return "onClick raised: " + outcome.exception_message;
    }
    return std::nullopt;
}

}  // namespace ogplay::runtime
