// Widget click dispatch: real storage behind setOnClickListener /
// setVisibility plus bounds derivation for the layout subset the inflater
// records (fullscreen roots and an edge-anchored horizontal button row).
// Views whose bounds cannot be derived are a recorded gap: they never
// consume touches, which then fall through to Activity.onTouchEvent.

#include <algorithm>
#include <optional>
#include <vector>

#include "ogplay/loader/binary_xml.h"

#include "dexvm_android_internal.h"

namespace ogplay::runtime {
namespace android_intrinsics {
namespace {

constexpr std::int32_t kVisible = 0;
constexpr std::int32_t kInvisible = 4;
constexpr std::int32_t kGone = 8;

// android.view.Gravity axis masks.
constexpr std::uint32_t kHorizontalGravityMask = 0x07;
constexpr std::uint32_t kVerticalGravityMask = 0x70;
constexpr std::uint32_t kGravityCenterHorizontal = 0x01;
constexpr std::uint32_t kGravityBottom = 0x50;
constexpr std::uint32_t kGravityTop = 0x30;

struct Rect final {
    std::int32_t x{};
    std::int32_t y{};
    std::int32_t width{};
    std::int32_t height{};

    [[nodiscard]] bool Contains(const float px, const float py) const {
        return px >= static_cast<float>(x) && py >= static_cast<float>(y) &&
               px < static_cast<float>(x + width) &&
               py < static_cast<float>(y + height);
    }
};

[[nodiscard]] std::int32_t VisibilityOf(const DexVmAndroidContext& context,
                                        const std::uint64_t handle) {
    const auto found = context.widget_states.find(handle);
    return found == context.widget_states.end() ? kVisible
                                                : found->second.visibility;
}

// Derives bounds for every recorded layout view, honouring the current
// visibility facts (GONE children take no space). Unsupported shapes stay
// nullopt. Supported subset:
//   - root views sized fill_parent x fill_parent -> the whole surface;
//   - a root LinearLayout with fill_parent width, wrap_content height and
//     layout_gravity top/bottom -> an edge bar whose visible children stack
//     horizontally (measured via their drawable), optionally centered by
//     android:gravity center_horizontal.
[[nodiscard]] std::vector<std::optional<Rect>> DeriveBounds(
    const DexVmAndroidContext& context) {
    const auto& views = context.layout_views;
    std::vector<std::optional<Rect>> bounds(views.size());
    const auto surface_width =
        static_cast<std::int32_t>(context.surface_width);
    const auto surface_height =
        static_cast<std::int32_t>(context.surface_height);

    for (std::size_t index = 0; index < views.size(); ++index) {
        const auto& view = views[index];
        if (view.parent != -1) continue;
        constexpr auto kFill = loader::BinaryXmlElement::kSizeFillParent;
        constexpr auto kWrap = loader::BinaryXmlElement::kSizeWrapContent;
        if (view.layout_width == kFill && view.layout_height == kFill) {
            bounds[index] = Rect{0, 0, surface_width, surface_height};
            continue;
        }
        const auto vertical = view.layout_gravity & kVerticalGravityMask;
        const bool edge_bar =
            view.tag == "LinearLayout" && view.layout_width == kFill &&
            view.layout_height == kWrap &&
            (vertical == kGravityTop || vertical == kGravityBottom);
        if (!edge_bar) continue;

        // Measure the visible children; any unmeasurable child leaves the
        // whole bar (and its children) unresolved rather than guessing.
        std::vector<std::size_t> row;
        std::int32_t total_width = 0;
        std::int32_t tallest = 0;
        bool measurable = true;
        for (std::size_t child = 0; child < views.size(); ++child) {
            if (views[child].parent != static_cast<std::int32_t>(index)) {
                continue;
            }
            if (VisibilityOf(context, views[child].view.Value()) == kGone) {
                continue;
            }
            if (views[child].measured_width <= 0 ||
                views[child].measured_height <= 0) {
                measurable = false;
                break;
            }
            row.push_back(child);
            total_width += views[child].measured_width;
            tallest = std::max(tallest, views[child].measured_height);
        }
        if (!measurable) continue;

        const auto bar_height = view.padding_top + tallest;
        const auto bar_y =
            vertical == kGravityBottom ? surface_height - bar_height : 0;
        bounds[index] = Rect{0, bar_y, surface_width, bar_height};

        auto cursor =
            (view.gravity & kHorizontalGravityMask) ==
                    kGravityCenterHorizontal
                ? (surface_width - total_width) / 2
                : 0;
        for (const auto child : row) {
            bounds[child] = Rect{cursor, bar_y + view.padding_top,
                                 views[child].measured_width,
                                 views[child].measured_height};
            cursor += views[child].measured_width;
        }
    }
    return bounds;
}

}  // namespace

void RegisterWidgetDispatch(dx::IntrinsicRegistry& registry,
                            const Context& context) {
    registry.Register("android.view.set_on_click_listener",
                      [context](dx::IntrinsicContext& call) {
        const auto handle = call.receiver.Value();
        context->widget_states[handle].click_listener =
            call.arguments[0].ref;
        const auto known = std::any_of(
            context->layout_views.begin(), context->layout_views.end(),
            [handle](const auto& fact) {
                return fact.view.Value() == handle;
            });
        if (!known && call.arguments[0].ref.IsValid()) {
            GuestLog(call, core::LogLevel::warn,
                     "setOnClickListener: the view has no layout bounds; "
                     "clicks fall through to Activity.onTouchEvent "
                     "(recorded gap)");
        }
        return dx::VmValue::Void();
    });
    registry.Register("android.view.set_visibility",
                      [context](dx::IntrinsicContext& call) {
        const auto value = call.arguments[0].AsInt();
        if (value != kVisible && value != kInvisible && value != kGone) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                  "setVisibility value is not one of "
                                  "VISIBLE/INVISIBLE/GONE: " +
                                      std::to_string(value)};
        }
        context->widget_states[call.receiver.Value()].visibility = value;
        return dx::VmValue::Void();
    });
    registry.Register("android.view.get_visibility",
                      [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(
            VisibilityOf(*context, call.receiver.Value()));
    });
}

}  // namespace android_intrinsics

std::optional<std::uint64_t> FindClickableViewAt(
    const DexVmAndroidContext& context, const float x, const float y) {
    const auto bounds = android_intrinsics::DeriveBounds(context);
    // Later document order draws on top, so scan back to front.
    for (std::size_t offset = context.layout_views.size(); offset > 0;
         --offset) {
        const auto index = offset - 1;
        const auto& fact = context.layout_views[index];
        if (!bounds[index].has_value() ||
            !bounds[index]->Contains(x, y)) {
            continue;
        }
        const auto state = context.widget_states.find(fact.view.Value());
        if (state == context.widget_states.end()) continue;
        if (state->second.visibility != android_intrinsics::kVisible) {
            continue;
        }
        if (!state->second.click_listener.IsValid()) continue;
        return fact.view.Value();
    }
    return std::nullopt;
}

bool ViewContainsPoint(const DexVmAndroidContext& context,
                       const std::uint64_t handle, const float x,
                       const float y) {
    const auto bounds = android_intrinsics::DeriveBounds(context);
    for (std::size_t index = 0; index < context.layout_views.size();
         ++index) {
        if (context.layout_views[index].view.Value() == handle) {
            return bounds[index].has_value() &&
                   bounds[index]->Contains(x, y);
        }
    }
    return false;
}

std::optional<std::string> InvokeViewOnClick(dexvm::Interpreter& vm,
                                             DexVmAndroidContext& context,
                                             const std::uint64_t handle) {
    const auto state = context.widget_states.find(handle);
    if (state == context.widget_states.end() ||
        !state->second.click_listener.IsValid()) {
        return "view has no click listener";
    }
    dexvm::VmObjectRef view;
    for (const auto& fact : context.layout_views) {
        if (fact.view.Value() == handle) {
            view = fact.view;
            break;
        }
    }
    auto& linker = vm.Linker();
    const auto listener = state->second.click_listener;
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
