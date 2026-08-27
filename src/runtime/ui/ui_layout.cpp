#include "ogplay/runtime/ui/ui_tree.h"

#include "ogplay/runtime/ui/ui_renderer.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <unordered_map>

namespace ogplay::runtime::ui {
namespace {

constexpr std::uint32_t kHorizontalMask = 0x07;
constexpr std::uint32_t kVerticalMask = 0x70;
constexpr std::uint32_t kRight = 0x05;
constexpr std::uint32_t kCenterHorizontal = 0x01;
constexpr std::uint32_t kBottom = 0x50;
constexpr std::uint32_t kCenterVertical = 0x10;

[[nodiscard]] std::int32_t ClampSize(const std::int32_t desired,
                                     const MeasureSpec spec) {
    const auto safe = std::max(0, desired);
    switch (spec.mode) {
        case MeasureMode::Exactly: return std::max(0, spec.size);
        case MeasureMode::AtMost: return std::min(safe, std::max(0, spec.size));
        case MeasureMode::Unspecified: return safe;
    }
    throw std::runtime_error("invalid UI measure mode");
}

[[nodiscard]] MeasureSpec ChildSpec(const MeasureSpec parent,
                                    const std::int32_t consumed,
                                    const DimensionSpec dimension) {
    const auto available = std::max(0, parent.size - consumed);
    if (dimension.mode == SizeMode::Fixed) {
        return {MeasureMode::Exactly, std::max(0, dimension.px)};
    }
    if (parent.mode == MeasureMode::Unspecified) {
        return {MeasureMode::Unspecified, 0};
    }
    return {dimension.mode == SizeMode::MatchParent &&
                    parent.mode == MeasureMode::Exactly
                ? MeasureMode::Exactly
                : MeasureMode::AtMost,
            available};
}

void Measure(UiTree& tree, const UiNodeId id, const MeasureSpec width_spec,
             const MeasureSpec height_spec) {
    auto& node = *tree.Get(id);
    if (node.visibility == Visibility::Gone) {
        node.measured = {};
        return;
    }
    std::int32_t desired_width = node.intrinsic.width + node.padding.left +
                                 node.padding.right;
    std::int32_t desired_height = node.intrinsic.height + node.padding.top +
                                  node.padding.bottom;
    if ((node.kind == UiClass::TextView || node.kind == UiClass::Button) &&
        !node.text.empty()) {
        const auto text = MeasureFixedText(node.text, node.text_size_px);
        desired_width = std::max(
            desired_width, text.width + node.padding.left + node.padding.right);
        desired_height = std::max(
            desired_height,
            text.height + node.padding.top + node.padding.bottom);
    }
    // Android View.getDefaultSize uses the bounded MeasureSpec size for a
    // plain leaf View. This lets a custom drawing/input View without explicit
    // LayoutParams fill the available content instead of collapsing to 0x0.
    if (node.kind == UiClass::View && node.children.empty()) {
        if (width_spec.mode != MeasureMode::Unspecified) {
            desired_width = std::max(
                desired_width, std::max(0, width_spec.size));
        }
        if (height_spec.mode != MeasureMode::Unspecified) {
            desired_height = std::max(
                desired_height, std::max(0, height_spec.size));
        }
    }
    if (node.kind == UiClass::LinearLayout) {
        const bool vertical = node.orientation == Orientation::Vertical;
        const auto main_spec = vertical ? height_spec : width_spec;
        std::int32_t total_main = 0;
        std::int32_t max_cross = 0;
        float total_weight = 0.0F;
        for (const auto child_id : node.children) {
            auto& child = *tree.Get(child_id);
            if (child.visibility == Visibility::Gone) continue;
            if (!std::isfinite(child.layout.weight) ||
                child.layout.weight < 0.0F) {
                throw std::runtime_error(
                    "LinearLayout child weight must be finite and non-negative");
            }
            const auto horizontal = node.padding.left + node.padding.right +
                                    child.layout.margin.left +
                                    child.layout.margin.right;
            const auto vertical_insets =
                node.padding.top + node.padding.bottom +
                child.layout.margin.top + child.layout.margin.bottom;
            auto child_width =
                ChildSpec(width_spec, horizontal, child.layout.width);
            auto child_height =
                ChildSpec(height_spec, vertical_insets, child.layout.height);
            const auto& main_dimension =
                vertical ? child.layout.height : child.layout.width;
            if (child.layout.weight > 0.0F &&
                main_dimension.mode == SizeMode::Fixed &&
                main_dimension.px == 0 &&
                main_spec.mode == MeasureMode::Exactly) {
                (vertical ? child_height : child_width) =
                    {MeasureMode::Exactly, 0};
            }
            Measure(tree, child_id, child_width, child_height);
            total_main += vertical
                              ? child.measured.height +
                                    child.layout.margin.top +
                                    child.layout.margin.bottom
                              : child.measured.width +
                                    child.layout.margin.left +
                                    child.layout.margin.right;
            max_cross = std::max(
                max_cross,
                vertical ? child.measured.width + child.layout.margin.left +
                               child.layout.margin.right
                         : child.measured.height + child.layout.margin.top +
                               child.layout.margin.bottom);
            total_weight += child.layout.weight;
        }
        const auto main_padding =
            vertical ? node.padding.top + node.padding.bottom
                     : node.padding.left + node.padding.right;
        if (total_weight > 0.0F &&
            main_spec.mode != MeasureMode::Unspecified) {
            auto remaining = main_spec.size - main_padding - total_main;
            auto weight_left = total_weight;
            for (const auto child_id : node.children) {
                auto& child = *tree.Get(child_id);
                if (child.visibility == Visibility::Gone ||
                    child.layout.weight == 0.0F) {
                    continue;
                }
                const auto share = static_cast<std::int32_t>(
                    static_cast<double>(remaining) * child.layout.weight /
                    weight_left);
                remaining -= share;
                weight_left -= child.layout.weight;
                const auto base =
                    vertical ? child.measured.height : child.measured.width;
                const auto target = std::max(0, base + share);
                const auto horizontal =
                    node.padding.left + node.padding.right +
                    child.layout.margin.left + child.layout.margin.right;
                const auto vertical_insets =
                    node.padding.top + node.padding.bottom +
                    child.layout.margin.top + child.layout.margin.bottom;
                Measure(tree, child_id,
                        vertical
                            ? ChildSpec(width_spec, horizontal,
                                        child.layout.width)
                            : MeasureSpec{MeasureMode::Exactly, target},
                        vertical
                            ? MeasureSpec{MeasureMode::Exactly, target}
                            : ChildSpec(height_spec, vertical_insets,
                                        child.layout.height));
            }
            total_main = 0;
            max_cross = 0;
            for (const auto child_id : node.children) {
                const auto& child = *tree.Get(child_id);
                if (child.visibility == Visibility::Gone) continue;
                total_main += vertical
                                  ? child.measured.height +
                                        child.layout.margin.top +
                                        child.layout.margin.bottom
                                  : child.measured.width +
                                        child.layout.margin.left +
                                        child.layout.margin.right;
                max_cross = std::max(
                    max_cross,
                    vertical
                        ? child.measured.width + child.layout.margin.left +
                              child.layout.margin.right
                        : child.measured.height + child.layout.margin.top +
                              child.layout.margin.bottom);
            }
        }
        desired_width = std::max(
            desired_width,
            (vertical ? max_cross : total_main) + node.padding.left +
                node.padding.right);
        desired_height = std::max(
            desired_height,
            (vertical ? total_main : max_cross) + node.padding.top +
                node.padding.bottom);
    } else if (!node.children.empty()) {
        std::int32_t max_width = 0;
        std::int32_t max_height = 0;
        for (const auto child_id : node.children) {
            auto& child = *tree.Get(child_id);
            if (child.visibility == Visibility::Gone) continue;
            const auto horizontal = node.padding.left + node.padding.right +
                                    child.layout.margin.left +
                                    child.layout.margin.right;
            const auto vertical = node.padding.top + node.padding.bottom +
                                  child.layout.margin.top +
                                  child.layout.margin.bottom;
            Measure(tree, child_id,
                    ChildSpec(width_spec, horizontal, child.layout.width),
                    ChildSpec(height_spec, vertical, child.layout.height));
            max_width = std::max(max_width,
                                 child.measured.width + child.layout.margin.left +
                                     child.layout.margin.right);
            max_height = std::max(max_height,
                                  child.measured.height + child.layout.margin.top +
                                      child.layout.margin.bottom);
        }
        desired_width = std::max(desired_width, max_width + node.padding.left +
                                                    node.padding.right);
        desired_height = std::max(desired_height, max_height + node.padding.top +
                                                     node.padding.bottom);
    }
    node.measured = {ClampSize(desired_width, width_spec),
                     ClampSize(desired_height, height_spec)};

    // A FrameLayout with an exact final size remeasures match-parent children
    // against its content box, matching the observable AOSP second pass.
    for (const auto child_id : node.children) {
        auto& child = *tree.Get(child_id);
        if (child.visibility == Visibility::Gone) continue;
        if (node.kind != UiClass::LinearLayout &&
            (child.layout.width.mode == SizeMode::MatchParent ||
             child.layout.height.mode == SizeMode::MatchParent)) {
            const MeasureSpec final_width{MeasureMode::Exactly, node.measured.width};
            const MeasureSpec final_height{MeasureMode::Exactly, node.measured.height};
            const auto horizontal = node.padding.left + node.padding.right +
                                    child.layout.margin.left +
                                    child.layout.margin.right;
            const auto vertical = node.padding.top + node.padding.bottom +
                                  child.layout.margin.top +
                                  child.layout.margin.bottom;
            Measure(tree, child_id,
                    ChildSpec(final_width, horizontal, child.layout.width),
                    ChildSpec(final_height, vertical, child.layout.height));
        }
    }
}

void LayoutNode(UiTree& tree, UiNodeId id, std::int32_t screen_x,
                std::int32_t screen_y);

void LayoutFrameChildren(UiTree& tree, const UiNodeId id,
                         const std::int32_t screen_x,
                         const std::int32_t screen_y) {
    auto& parent = *tree.Get(id);
    const auto content_left = parent.padding.left;
    const auto content_top = parent.padding.top;
    const auto content_right = parent.measured.width - parent.padding.right;
    const auto content_bottom = parent.measured.height - parent.padding.bottom;
    for (const auto child_id : parent.children) {
        auto& child = *tree.Get(child_id);
        if (child.visibility == Visibility::Gone) {
            child.frame = {};
            child.screen_frame = {};
            continue;
        }
        const auto gravity = child.layout.layout_gravity;
        std::int32_t left = content_left + child.layout.margin.left;
        std::int32_t top = content_top + child.layout.margin.top;
        if ((gravity & kHorizontalMask) == kCenterHorizontal) {
            left = content_left +
                   (content_right - content_left - child.measured.width) / 2 +
                   child.layout.margin.left - child.layout.margin.right;
        } else if ((gravity & kHorizontalMask) == kRight) {
            left = content_right - child.measured.width -
                   child.layout.margin.right;
        }
        if ((gravity & kVerticalMask) == kCenterVertical) {
            top = content_top +
                  (content_bottom - content_top - child.measured.height) / 2 +
                  child.layout.margin.top - child.layout.margin.bottom;
        } else if ((gravity & kVerticalMask) == kBottom) {
            top = content_bottom - child.measured.height -
                  child.layout.margin.bottom;
        }
        child.frame = {left, top, left + child.measured.width,
                       top + child.measured.height};
        LayoutNode(tree, child_id, screen_x + left, screen_y + top);
    }
}

enum class RelativeVisit : std::uint8_t { Unvisited, Visiting, Resolved };

void LayoutRelativeChildren(UiTree& tree, const UiNodeId id,
                            const std::int32_t screen_x,
                            const std::int32_t screen_y) {
    auto& parent = *tree.Get(id);
    const auto content_left = parent.padding.left;
    const auto content_top = parent.padding.top;
    const auto content_right = parent.measured.width - parent.padding.right;
    const auto content_bottom = parent.measured.height - parent.padding.bottom;
    std::unordered_map<std::int32_t, UiNodeId> siblings;
    for (const auto child_id : parent.children) {
        const auto android_id = tree.Get(child_id)->android_id;
        if (android_id < 0) continue;
        if (!siblings.emplace(android_id, child_id).second) {
            throw std::runtime_error(
                "RelativeLayout sibling ids must be unique");
        }
    }

    std::unordered_map<UiNodeId, RelativeVisit, UiNodeIdHash> horizontal_visit;
    std::unordered_map<UiNodeId, RelativeVisit, UiNodeIdHash> vertical_visit;
    std::unordered_map<UiNodeId, std::int32_t, UiNodeIdHash> lefts;
    std::unordered_map<UiNodeId, std::int32_t, UiNodeIdHash> tops;

    const auto anchor_for = [&siblings](const std::int32_t android_id) {
        const auto found = siblings.find(android_id);
        if (found == siblings.end()) {
            throw std::runtime_error(
                "RelativeLayout rule references a missing sibling id");
        }
        return found->second;
    };

    std::function<std::int32_t(UiNodeId)> resolve_horizontal;
    resolve_horizontal = [&](const UiNodeId child_id) -> std::int32_t {
        auto& visit = horizontal_visit[child_id];
        if (visit == RelativeVisit::Visiting) {
            throw std::runtime_error(
                "RelativeLayout horizontal dependency cycle");
        }
        if (visit == RelativeVisit::Resolved) return lefts.at(child_id);
        visit = RelativeVisit::Visiting;
        const auto& child = *tree.Get(child_id);
        const auto& rules = child.layout.relative;
        const bool centered = rules.center_in_parent || rules.center_horizontal;
        const auto rule_count = static_cast<unsigned>(centered) +
                                static_cast<unsigned>(rules.align_parent_left) +
                                static_cast<unsigned>(rules.align_parent_right) +
                                static_cast<unsigned>(rules.left_of.has_value()) +
                                static_cast<unsigned>(rules.right_of.has_value()) +
                                static_cast<unsigned>(rules.align_left.has_value()) +
                                static_cast<unsigned>(rules.align_right.has_value());
        if (rule_count > 1U) {
            throw std::runtime_error(
                "RelativeLayout has conflicting horizontal rules");
        }
        std::int32_t left = content_left + child.layout.margin.left;
        if (centered) {
            left = content_left +
                   (content_right - content_left - child.measured.width) / 2 +
                   child.layout.margin.left - child.layout.margin.right;
        } else if (rules.align_parent_right) {
            left = content_right - child.measured.width -
                   child.layout.margin.right;
        } else if (rules.left_of.has_value()) {
            const auto anchor = anchor_for(*rules.left_of);
            left = resolve_horizontal(anchor) - child.layout.margin.right -
                   child.measured.width;
        } else if (rules.right_of.has_value()) {
            const auto anchor = anchor_for(*rules.right_of);
            const auto& anchor_node = *tree.Get(anchor);
            left = resolve_horizontal(anchor) + anchor_node.measured.width +
                   child.layout.margin.left;
        } else if (rules.align_left.has_value()) {
            left = resolve_horizontal(anchor_for(*rules.align_left)) +
                   child.layout.margin.left;
        } else if (rules.align_right.has_value()) {
            const auto anchor = anchor_for(*rules.align_right);
            const auto& anchor_node = *tree.Get(anchor);
            left = resolve_horizontal(anchor) + anchor_node.measured.width -
                   child.measured.width - child.layout.margin.right;
        }
        lefts.emplace(child_id, left);
        visit = RelativeVisit::Resolved;
        return left;
    };

    std::function<std::int32_t(UiNodeId)> resolve_vertical;
    resolve_vertical = [&](const UiNodeId child_id) -> std::int32_t {
        auto& visit = vertical_visit[child_id];
        if (visit == RelativeVisit::Visiting) {
            throw std::runtime_error(
                "RelativeLayout vertical dependency cycle");
        }
        if (visit == RelativeVisit::Resolved) return tops.at(child_id);
        visit = RelativeVisit::Visiting;
        const auto& child = *tree.Get(child_id);
        const auto& rules = child.layout.relative;
        const bool centered = rules.center_in_parent || rules.center_vertical;
        const auto rule_count = static_cast<unsigned>(centered) +
                                static_cast<unsigned>(rules.align_parent_top) +
                                static_cast<unsigned>(rules.align_parent_bottom) +
                                static_cast<unsigned>(rules.above.has_value()) +
                                static_cast<unsigned>(rules.below.has_value()) +
                                static_cast<unsigned>(rules.align_top.has_value()) +
                                static_cast<unsigned>(rules.align_bottom.has_value());
        if (rule_count > 1U) {
            throw std::runtime_error(
                "RelativeLayout has conflicting vertical rules");
        }
        std::int32_t top = content_top + child.layout.margin.top;
        if (centered) {
            top = content_top +
                  (content_bottom - content_top - child.measured.height) / 2 +
                  child.layout.margin.top - child.layout.margin.bottom;
        } else if (rules.align_parent_bottom) {
            top = content_bottom - child.measured.height -
                  child.layout.margin.bottom;
        } else if (rules.above.has_value()) {
            const auto anchor = anchor_for(*rules.above);
            top = resolve_vertical(anchor) - child.layout.margin.bottom -
                  child.measured.height;
        } else if (rules.below.has_value()) {
            const auto anchor = anchor_for(*rules.below);
            const auto& anchor_node = *tree.Get(anchor);
            top = resolve_vertical(anchor) + anchor_node.measured.height +
                  child.layout.margin.top;
        } else if (rules.align_top.has_value()) {
            top = resolve_vertical(anchor_for(*rules.align_top)) +
                  child.layout.margin.top;
        } else if (rules.align_bottom.has_value()) {
            const auto anchor = anchor_for(*rules.align_bottom);
            const auto& anchor_node = *tree.Get(anchor);
            top = resolve_vertical(anchor) + anchor_node.measured.height -
                  child.measured.height - child.layout.margin.bottom;
        }
        tops.emplace(child_id, top);
        visit = RelativeVisit::Resolved;
        return top;
    };

    for (const auto child_id : parent.children) {
        static_cast<void>(resolve_horizontal(child_id));
        static_cast<void>(resolve_vertical(child_id));
    }
    for (const auto child_id : parent.children) {
        auto& child = *tree.Get(child_id);
        if (child.visibility == Visibility::Gone) {
            child.frame = {};
            child.screen_frame = {};
            continue;
        }
        const auto left = lefts.at(child_id);
        const auto top = tops.at(child_id);
        child.frame = {left, top, left + child.measured.width,
                       top + child.measured.height};
        LayoutNode(tree, child_id, screen_x + left, screen_y + top);
    }
}

void LayoutHorizontalChildren(UiTree& tree, const UiNodeId id,
                              const std::int32_t screen_x,
                              const std::int32_t screen_y) {
    auto& parent = *tree.Get(id);
    std::int32_t total_width = 0;
    for (const auto child_id : parent.children) {
        const auto& child = *tree.Get(child_id);
        if (child.visibility == Visibility::Gone) continue;
        total_width += child.measured.width + child.layout.margin.left +
                       child.layout.margin.right;
    }
    const auto available_width = parent.measured.width - parent.padding.left -
                                 parent.padding.right;
    std::int32_t cursor = parent.padding.left;
    if ((parent.gravity & kHorizontalMask) == kCenterHorizontal) {
        cursor += (available_width - total_width) / 2;
    } else if ((parent.gravity & kHorizontalMask) == kRight) {
        cursor += available_width - total_width;
    }
    const auto content_height = parent.measured.height - parent.padding.top -
                                parent.padding.bottom;
    for (const auto child_id : parent.children) {
        auto& child = *tree.Get(child_id);
        if (child.visibility == Visibility::Gone) {
            child.frame = {};
            child.screen_frame = {};
            continue;
        }
        cursor += child.layout.margin.left;
        const auto cross_gravity = child.layout.layout_gravity != 0
                                       ? child.layout.layout_gravity
                                       : parent.gravity;
        std::int32_t top = parent.padding.top + child.layout.margin.top;
        if ((cross_gravity & kVerticalMask) == kCenterVertical) {
            top = parent.padding.top +
                  (content_height - child.measured.height) / 2 +
                  child.layout.margin.top - child.layout.margin.bottom;
        } else if ((cross_gravity & kVerticalMask) == kBottom) {
            top = parent.measured.height - parent.padding.bottom -
                  child.measured.height - child.layout.margin.bottom;
        }
        child.frame = {cursor, top, cursor + child.measured.width,
                       top + child.measured.height};
        LayoutNode(tree, child_id, screen_x + cursor, screen_y + top);
        cursor += child.measured.width + child.layout.margin.right;
    }
}

void LayoutVerticalChildren(UiTree& tree, const UiNodeId id,
                            const std::int32_t screen_x,
                            const std::int32_t screen_y) {
    auto& parent = *tree.Get(id);
    std::int32_t total_height = 0;
    for (const auto child_id : parent.children) {
        const auto& child = *tree.Get(child_id);
        if (child.visibility == Visibility::Gone) continue;
        total_height += child.measured.height + child.layout.margin.top +
                        child.layout.margin.bottom;
    }
    const auto available_height = parent.measured.height - parent.padding.top -
                                  parent.padding.bottom;
    std::int32_t cursor = parent.padding.top;
    if ((parent.gravity & kVerticalMask) == kCenterVertical) {
        cursor += (available_height - total_height) / 2;
    } else if ((parent.gravity & kVerticalMask) == kBottom) {
        cursor += available_height - total_height;
    }
    const auto content_width = parent.measured.width - parent.padding.left -
                               parent.padding.right;
    for (const auto child_id : parent.children) {
        auto& child = *tree.Get(child_id);
        if (child.visibility == Visibility::Gone) {
            child.frame = {};
            child.screen_frame = {};
            continue;
        }
        cursor += child.layout.margin.top;
        const auto cross_gravity = child.layout.layout_gravity != 0
                                       ? child.layout.layout_gravity
                                       : parent.gravity;
        std::int32_t left = parent.padding.left + child.layout.margin.left;
        if ((cross_gravity & kHorizontalMask) == kCenterHorizontal) {
            left = parent.padding.left +
                   (content_width - child.measured.width) / 2 +
                   child.layout.margin.left - child.layout.margin.right;
        } else if ((cross_gravity & kHorizontalMask) == kRight) {
            left = parent.measured.width - parent.padding.right -
                   child.measured.width - child.layout.margin.right;
        }
        child.frame = {left, cursor, left + child.measured.width,
                       cursor + child.measured.height};
        LayoutNode(tree, child_id, screen_x + left, screen_y + cursor);
        cursor += child.measured.height + child.layout.margin.bottom;
    }
}

void LayoutNode(UiTree& tree, const UiNodeId id, const std::int32_t screen_x,
                const std::int32_t screen_y) {
    auto& node = *tree.Get(id);
    node.screen_frame = {screen_x, screen_y, screen_x + node.measured.width,
                         screen_y + node.measured.height};
    if (node.kind == UiClass::LinearLayout) {
        if (node.orientation == Orientation::Horizontal) {
            LayoutHorizontalChildren(tree, id, screen_x, screen_y);
        } else {
            LayoutVerticalChildren(tree, id, screen_x, screen_y);
        }
    } else if (node.kind == UiClass::RelativeLayout) {
        LayoutRelativeChildren(tree, id, screen_x, screen_y);
    } else {
        LayoutFrameChildren(tree, id, screen_x, screen_y);
    }
}

}  // namespace

void LayoutUiTree(UiTree& tree, const UiMetrics metrics) {
    if (metrics.width < 0 || metrics.height < 0) {
        throw std::runtime_error("UI surface metrics must be non-negative");
    }
    auto& root = *tree.Get(tree.Root());
    root.layout.width = {SizeMode::MatchParent, 0};
    root.layout.height = {SizeMode::MatchParent, 0};
    Measure(tree, tree.Root(), {MeasureMode::Exactly, metrics.width},
            {MeasureMode::Exactly, metrics.height});
    root.frame = {0, 0, metrics.width, metrics.height};
    LayoutNode(tree, tree.Root(), 0, 0);
    tree.ClearLayoutDirty();
}

}  // namespace ogplay::runtime::ui
