#include "ogplay/runtime/integration/dexvm_android.h"

#include <array>
#include <bit>
#include <stdexcept>
#include <string>

#include "ogplay/runtime/integration/host_image_decode.h"

namespace ogplay::runtime {
namespace {

constexpr std::string_view kAndroidNamespace =
    "http://schemas.android.com/apk/res/android";
constexpr std::uint8_t kTypeDimension = 0x05;
constexpr std::uint8_t kTypeFloat = 0x04;
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

[[nodiscard]] std::int32_t DimensionValue(
    const loader::BinaryXmlAttribute& attribute) {
    if (attribute.value_type == kTypeDimension) {
        return static_cast<std::int32_t>(attribute.data) >> 8;
    }
    return static_cast<std::int32_t>(attribute.data);
}

[[nodiscard]] std::uint32_t AndroidColorToRgba(const std::uint32_t argb) {
    return ((argb & 0x00ffffffU) << 8U) | (argb >> 24U);
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
        const auto value = DimensionValue(attribute);
        node.layout.width = value == -1
                                ? ui::DimensionSpec{ui::SizeMode::MatchParent, 0}
                                : value == -2
                                      ? ui::DimensionSpec{ui::SizeMode::WrapContent, 0}
                                      : ui::DimensionSpec{ui::SizeMode::Fixed, value};
    } else if (name == "layout_height") {
        const auto value = DimensionValue(attribute);
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
        const auto value = DimensionValue(attribute);
        node.padding = ui::Insets{value, value, value, value};
    } else if (name == "paddingLeft") {
        node.padding.left = DimensionValue(attribute);
    } else if (name == "paddingTop") {
        node.padding.top = DimensionValue(attribute);
    } else if (name == "paddingRight") {
        node.padding.right = DimensionValue(attribute);
    } else if (name == "paddingBottom") {
        node.padding.bottom = DimensionValue(attribute);
    } else if (name == "layout_margin") {
        const auto value = DimensionValue(attribute);
        node.layout.margin = ui::Insets{value, value, value, value};
    } else if (name == "layout_marginLeft") {
        node.layout.margin.left = DimensionValue(attribute);
    } else if (name == "layout_marginTop") {
        node.layout.margin.top = DimensionValue(attribute);
    } else if (name == "layout_marginRight") {
        node.layout.margin.right = DimensionValue(attribute);
    } else if (name == "layout_marginBottom") {
        node.layout.margin.bottom = DimensionValue(attribute);
    } else if (name == "text") {
        if (!attribute.raw_string.has_value()) {
            throw std::runtime_error(
                "TextView text resource resolution is not implemented");
        }
        node.text = AsciiText(*attribute.raw_string);
    } else if (name == "textColor") {
        if (attribute.value_type < kTypeFirstColor ||
            attribute.value_type > kTypeLastColor) {
            throw std::runtime_error(
                "TextView textColor resource resolution is not implemented");
        }
        node.text_color = AndroidColorToRgba(attribute.data);
    } else if (name == "textSize") {
        const auto size = static_cast<float>(DimensionValue(attribute));
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
    } else if (name == "background" &&
               attribute.value_type >= kTypeFirstColor &&
               attribute.value_type <= kTypeLastColor) {
        node.background_color = AndroidColorToRgba(attribute.data);
    } else if (name == "src") {
        drawable_id = attribute.data;
        node.image_resource_id = attribute.data;
    } else if (name.starts_with("layout_")) {
        throw std::runtime_error("unsupported structural UI attribute: " +
                                 name);
    }
}

[[nodiscard]] std::shared_ptr<const ui::UiBitmap> DecodeDrawable(
    DexVmAndroidContext& context, const std::uint32_t drawable_id) {
    if (drawable_id == 0) return {};
    if (const auto cached = context.ui_bitmaps.find(drawable_id);
        cached != context.ui_bitmaps.end()) {
        return cached->second;
    }
    const auto* drawable = context.arsc.FindById(drawable_id);
    if (drawable == nullptr || !drawable->string_value.has_value()) {
        return {};
    }
    try {
        const auto bytes = loader::ReadApkEntry(
            context.apk_bytes, context.archive, *drawable->string_value);
        const auto decoded = DecodeImageToArgb(bytes);
        if (!decoded.has_value()) return {};
        auto bitmap = std::make_shared<ui::UiBitmap>();
        bitmap->width = decoded->width;
        bitmap->height = decoded->height;
        bitmap->rgba8.reserve(decoded->argb.size() * 4U);
        for (const auto argb : decoded->argb) {
            bitmap->rgba8.push_back(static_cast<std::uint8_t>(argb >> 16U));
            bitmap->rgba8.push_back(static_cast<std::uint8_t>(argb >> 8U));
            bitmap->rgba8.push_back(static_cast<std::uint8_t>(argb));
            bitmap->rgba8.push_back(static_cast<std::uint8_t>(argb >> 24U));
        }
        const auto immutable = std::shared_ptr<const ui::UiBitmap>{bitmap};
        context.ui_bitmaps.emplace(drawable_id, immutable);
        return immutable;
    } catch (const std::exception&) {
        return {};
    }
}

}  // namespace

std::span<const UiWidgetDescriptor> UiWidgetRegistry() { return kWidgets; }

dexvm::VmObjectRef InflateUiElements(
    dexvm::Interpreter& vm, DexVmAndroidContext& context,
    const std::span<const loader::BinaryXmlElement> elements) {
    ResetViewUiState(context);
    try {
        if (elements.empty()) {
            throw std::runtime_error("layout has no root element");
        }
        std::size_t root_count = 0;
        for (std::size_t index = 0; index < elements.size(); ++index) {
            const auto parent = elements[index].parent;
            if (parent < 0) {
                ++root_count;
            } else if (static_cast<std::size_t>(parent) >= index) {
                throw std::runtime_error("layout parent order is invalid");
            }
        }
        if (root_count != 1) {
            throw std::runtime_error("layout must have exactly one root element");
        }

        std::vector<ui::UiNodeId> node_of(elements.size());
        std::vector<std::int32_t> fact_of(elements.size(), -1);
        dexvm::VmObjectRef content_view;
        for (std::size_t index = 0; index < elements.size(); ++index) {
            const auto& element = elements[index];
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
            if (const auto image = DecodeDrawable(context, drawable_id);
                image != nullptr) {
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

}  // namespace ogplay::runtime
