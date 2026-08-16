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
constexpr std::uint8_t kTypeString = 0x03;
constexpr std::uint8_t kTypeFirstColor = 0x1c;
constexpr std::uint8_t kTypeLastColor = 0x1f;

constexpr std::array<UiWidgetDescriptor, 16> kWidgets{{
    {"View", "Landroid/view/View;", ui::UiClass::View},
    {"TextView", "Landroid/widget/TextView;", ui::UiClass::TextView},
    {"Button", "Landroid/widget/Button;", ui::UiClass::Button},
    {"EditText", "Landroid/widget/EditText;", ui::UiClass::TextView},
    {"ImageView", "Landroid/widget/ImageView;", ui::UiClass::ImageView},
    {"ImageButton", "Landroid/widget/ImageButton;", ui::UiClass::ImageButton},
    {"ProgressBar", "Landroid/widget/ProgressBar;", ui::UiClass::View},
    {"VideoView", "Landroid/widget/VideoView;", ui::UiClass::VideoView},
    {"WebView", "Landroid/webkit/WebView;", ui::UiClass::View},
    {"LinearLayout", "Landroid/widget/LinearLayout;", ui::UiClass::LinearLayout},
    {"FrameLayout", "Landroid/widget/FrameLayout;", ui::UiClass::FrameLayout},
    {"RelativeLayout", "Landroid/widget/RelativeLayout;", ui::UiClass::RelativeLayout},
    {"TableLayout", "Landroid/widget/TableLayout;", ui::UiClass::LinearLayout},
    {"TableRow", "Landroid/widget/TableRow;", ui::UiClass::LinearLayout},
    {"ScrollView", "Landroid/widget/ScrollView;", ui::UiClass::FrameLayout},
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

[[nodiscard]] const loader::ArscEntry& ResolveEntry(
    const DexVmAndroidContext& context, std::uint32_t resource_id) {
    std::unordered_set<std::uint32_t> seen;
    for (std::size_t depth = 0; depth < 16; ++depth) {
        if (!seen.insert(resource_id).second) {
            throw std::runtime_error("UI resource reference cycle");
        }
        const auto* entry = context.arsc.FindById(resource_id);
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
    if (attribute.namespace_uri.empty() && attribute.name == "style") {
        throw std::runtime_error(
            "UI style resource is unsupported without a matched fixture");
    }
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
        node.orientation = attribute.data == 0U
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
    } else if (name == "textSize") {
        const auto size = static_cast<float>(
            DimensionValue(context, attribute, true));
        static_cast<void>(ui::MeasureFixedText(node.text, size));
        node.text_size_px = size;
    } else if (name == "singleLine") {
        if (attribute.data == 0U) {
            throw std::runtime_error("multiline TextView is unsupported");
        }
        node.max_lines = 1;
    } else if (name == "maxLines") {
        if (attribute.data != 1U) {
            throw std::runtime_error("multiline TextView is unsupported");
        }
        node.max_lines = 1;
    } else if (name == "background") {
        node.background_color =
            attribute.value_type == kTypeReference
                ? ResolveUiColor(context, attribute.data)
                : attribute.value_type >= kTypeFirstColor &&
                          attribute.value_type <= kTypeLastColor
                      ? AndroidColorToRgba(attribute.data)
                      : throw std::runtime_error(
                            "UI background is not a color resource");
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

std::u16string ResolveUiString(const DexVmAndroidContext& context,
                               const std::uint32_t resource_id) {
    const auto& entry = ResolveEntry(context, resource_id);
    if (entry.value_type != kTypeString || !entry.string_value.has_value()) {
        throw std::runtime_error("UI resource is not a string");
    }
    return AsciiText(*entry.string_value);
}

std::uint32_t ResolveUiColor(const DexVmAndroidContext& context,
                             const std::uint32_t resource_id) {
    const auto& entry = ResolveEntry(context, resource_id);
    if (entry.value_type < kTypeFirstColor ||
        entry.value_type > kTypeLastColor) {
        throw std::runtime_error("UI resource is not a color");
    }
    return AndroidColorToRgba(entry.value_data);
}

std::int32_t ResolveUiDimension(const DexVmAndroidContext& context,
                                const std::uint32_t resource_id,
                                const bool scaled) {
    const auto& entry = ResolveEntry(context, resource_id);
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
    if (const auto cached = context.ui_bitmaps.find(resource_id);
        cached != context.ui_bitmaps.end()) {
        return cached->second;
    }
    const auto& drawable = ResolveEntry(context, resource_id);
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
    const auto& entry = ResolveEntry(context, layout_id);
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
    const std::span<const loader::BinaryXmlElement> elements) {
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
        std::vector<std::int32_t> fact_of(document.size(), -1);
        dexvm::VmObjectRef content_view;
        for (std::size_t index = 0; index < document.size(); ++index) {
            const auto& element = document[index];
            const auto parent_node =
                element.parent < 0
                    ? context.ui_tree.Root()
                    : node_of[static_cast<std::size_t>(element.parent)];
            const auto parent_fact =
                element.parent < 0
                    ? -1
                    : fact_of[static_cast<std::size_t>(element.parent)];
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
            std::uint32_t drawable_id = element.src;
            for (const auto& attribute : element.attributes) {
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
            if (element.id != 0 &&
                context.ui_tree.Get(node)->android_id < 0) {
                context.ui_tree.SetAndroidId(
                    node, static_cast<std::int32_t>(element.id));
            }
            context.ui_tree.Attach(parent_node, node);
            node_of[index] = node;
            if (!content_view.IsValid()) content_view = view;

            DexVmAndroidContext::LayoutViewFact fact;
            fact.view = view;
            fact.parent = parent_fact;
            fact.tag = element.name;
            const auto& state = *context.ui_tree.Get(node);
            const auto legacy_size = [](const ui::DimensionSpec spec) {
                return spec.mode == ui::SizeMode::MatchParent
                           ? loader::BinaryXmlElement::kSizeFillParent
                           : spec.mode == ui::SizeMode::WrapContent
                                 ? loader::BinaryXmlElement::kSizeWrapContent
                                 : spec.px;
            };
            fact.layout_width = element.layout_width != 0
                                    ? element.layout_width
                                    : legacy_size(state.layout.width);
            fact.layout_height = element.layout_height != 0
                                     ? element.layout_height
                                     : legacy_size(state.layout.height);
            fact.gravity = element.gravity != 0 ? element.gravity
                                                : state.gravity;
            fact.layout_gravity =
                element.layout_gravity != 0
                    ? element.layout_gravity
                    : state.layout.layout_gravity;
            fact.padding_top = element.padding_top != 0
                                   ? element.padding_top
                                   : state.padding.top;
            if (drawable_id != 0U) {
                const auto image = ResolveUiDrawable(context, drawable_id);
                context.ui_tree.Get(node)->intrinsic = {image->width,
                                                        image->height};
                fact.measured_width = image->width;
                fact.measured_height = image->height;
            }
            fact_of[index] =
                static_cast<std::int32_t>(context.layout_views.size());
            context.layout_views.push_back(std::move(fact));
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
    const std::uint32_t layout_id) {
    try {
        const auto root = LoadLayout(context, layout_id);
        const auto expanded = ExpandUiIncludes(
            root,
            [&context](const std::uint32_t included_id) {
                return LoadLayout(context, included_id);
            },
            layout_id);
        return InflateUiElements(vm, context, expanded);
    } catch (...) {
        ResetViewUiState(context);
        throw;
    }
}

}  // namespace ogplay::runtime
