#include "ogplay/runtime/ui/ui_tree.h"

#include <algorithm>
#include <stdexcept>

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
    if (node.kind == UiClass::LinearLayout &&
        node.orientation == Orientation::Horizontal) {
        std::int32_t total_width = 0;
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
            total_width += child.measured.width + child.layout.margin.left +
                           child.layout.margin.right;
            max_height = std::max(max_height,
                                  child.measured.height + child.layout.margin.top +
                                      child.layout.margin.bottom);
        }
        desired_width = std::max(desired_width, total_width + node.padding.left +
                                                    node.padding.right);
        desired_height = std::max(desired_height, max_height + node.padding.top +
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

void LayoutNode(UiTree& tree, const UiNodeId id, const std::int32_t screen_x,
                const std::int32_t screen_y) {
    auto& node = *tree.Get(id);
    node.screen_frame = {screen_x, screen_y, screen_x + node.measured.width,
                         screen_y + node.measured.height};
    if (node.kind == UiClass::LinearLayout &&
        node.orientation == Orientation::Horizontal) {
        LayoutHorizontalChildren(tree, id, screen_x, screen_y);
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
    tree.ClearDirty();
}

}  // namespace ogplay::runtime::ui
