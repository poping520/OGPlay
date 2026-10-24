#include "ogplay/runtime/ui/ui_tree.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <stdexcept>
#include <utility>

namespace ogplay::runtime::ui {

std::size_t UiNodeIdHash::operator()(const UiNodeId id) const noexcept {
    return std::hash<std::uint64_t>{}(id.Value());
}

UiTree::UiTree(const UiGeometryOwner owner) : geometry_owner_(owner) {
    Reset();
}

namespace {

bool SameTarget(const UiProjectionTarget& a, const UiProjectionTarget& b) {
    return a.root == b.root && a.window_generation == b.window_generation && a.content_revision == b.content_revision &&
           a.metrics_revision == b.metrics_revision && a.metrics.width == b.metrics.width &&
           a.metrics.height == b.metrics.height;
}

std::int32_t Coordinate(const std::int64_t value) {
    if (value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::int32_t>::max())
        throw std::runtime_error("UI projection coordinate overflow");
    return static_cast<std::int32_t>(value);
}

Rect Clip(const Rect a, const Rect b) {
    const auto left = std::max(a.left, b.left);
    const auto top = std::max(a.top, b.top);
    return {left, top, std::max(left, std::min(a.right, b.right)), std::max(top, std::min(a.bottom, b.bottom))};
}

bool ContainsPoint(const Rect rect, const std::int32_t x, const std::int32_t y) {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

}  // namespace

void UiTree::SetProjectionTarget(const UiProjectionTarget& target) {
    if (geometry_owner_ != UiGeometryOwner::ExternalProjection || target.root != root_ ||
        target.window_generation == 0 || target.metrics.width < 0 || target.metrics.height < 0)
        throw std::runtime_error("Invalid UI projection target");
    if (projection_target_) {
        const auto& old = *projection_target_;
        if (target.window_generation < old.window_generation ||
            (target.window_generation == old.window_generation &&
             (target.content_revision < old.content_revision || target.metrics_revision < old.metrics_revision ||
              (target.metrics_revision == old.metrics_revision &&
               (target.metrics.width != old.metrics.width || target.metrics.height != old.metrics.height)))))
            throw std::runtime_error("Stale UI projection target");
    }
    projection_target_ = target;
}

void UiTree::CommitProjection(const UiGeometryProjection& projection) {
    if (geometry_owner_ != UiGeometryOwner::ExternalProjection || !projection_target_ ||
        !SameTarget(projection.target, *projection_target_) || projection.epoch == 0 ||
        projection.epoch <= projection_epoch_ || projection.nodes.empty() || projection.nodes.size() > kMaxNodes)
        throw std::runtime_error("Invalid or stale UI projection batch");

    // Prepare and validate in isolation. All potentially throwing work, including
    // allocation of the immutable consumer snapshot, precedes the final swaps.
    UiTree candidate(*this);
    candidate.published_projection_.reset();
    for (auto& [id, node] : candidate.nodes_) {
        static_cast<void>(id);
        node.parent.reset();
        node.children.clear();
        node.frame = {};
        node.screen_frame = {};
        node.screen_clip = {};
        node.layout_dirty = false;
        node.draw_dirty = true;
    }
    std::unordered_map<UiNodeId, const UiProjectedNode*, UiNodeIdHash> entries;
    for (const auto& input : projection.nodes) {
        if (!candidate.Get(input.id) || !entries.emplace(input.id, &input).second ||
            (input.id == root_ ? input.parent.has_value() : !input.parent.has_value()) ||
            input.frame.right < input.frame.left || input.frame.bottom < input.frame.top ||
            static_cast<std::int64_t>(input.frame.right) - input.frame.left >
                std::numeric_limits<std::int32_t>::max() ||
            static_cast<std::int64_t>(input.frame.bottom) - input.frame.top >
                std::numeric_limits<std::int32_t>::max() ||
            input.measured.width < 0 || input.measured.height < 0 || input.padding.left < 0 || input.padding.top < 0 ||
            input.padding.right < 0 || input.padding.bottom < 0 ||
            (input.visibility != Visibility::Visible && input.visibility != Visibility::Invisible &&
             input.visibility != Visibility::Gone))
            throw std::runtime_error("Invalid UI projected node");
        auto& node = candidate.Require(input.id);
        node.parent = input.parent;
        node.frame = input.frame;
        node.measured = input.measured;
        node.visibility = input.visibility;
        node.padding = input.padding;
        node.scroll_y = input.scroll_y;
        node.clip_children = input.clip_children;
        node.clip_to_padding = input.clip_to_padding;
    }
    if (!entries.contains(root_)) throw std::runtime_error("UI projection has no window root");
    for (const auto& input : projection.nodes) {
        if (!input.parent) continue;
        if (!entries.contains(*input.parent)) throw std::runtime_error("UI projection parent missing from batch");
        auto& children = candidate.Require(*input.parent).children;
        if (children.size() >= kMaxChildren) throw std::runtime_error("UI projection child limit exceeded");
        children.push_back(input.id);
    }
    std::size_t visited{};
    const auto resolve = [&](auto&& self, const UiNodeId id, const std::int64_t x, const std::int64_t y,
                             const Rect inherited_clip, const bool parent_clips, const std::size_t depth) -> void {
        if (depth > kMaxDepth) throw std::runtime_error("UI projection cycle or depth limit exceeded");
        ++visited;
        auto& node = candidate.Require(id);
        const auto left = x + node.frame.left;
        const auto top = y + node.frame.top;
        node.screen_frame = {Coordinate(left), Coordinate(top), Coordinate(x + node.frame.right),
                             Coordinate(y + node.frame.bottom)};
        node.screen_clip = parent_clips ? Clip(inherited_clip, node.screen_frame) : inherited_clip;
        auto child_clip = node.screen_clip;
        if (node.clip_to_padding) {
            const Rect content{Coordinate(left + node.padding.left), Coordinate(top + node.padding.top),
                               Coordinate(x + node.frame.right - node.padding.right),
                               Coordinate(y + node.frame.bottom - node.padding.bottom)};
            child_clip = Clip(child_clip, content);
        }
        for (const auto child : node.children)
            self(self, child, left, top - node.scroll_y, child_clip, node.clip_children, depth + 1);
    };
    resolve(resolve, root_, 0, 0, {0, 0, projection.target.metrics.width, projection.target.metrics.height}, true, 1);
    if (visited != entries.size()) throw std::runtime_error("UI projection contains disconnected nodes or a cycle");
    candidate.RebuildIndex();
    if (candidate.focused_ && (!entries.contains(*candidate.focused_) || !candidate.IsVisible(*candidate.focused_)))
        candidate.focused_.reset();
    candidate.projection_epoch_ = projection.epoch;
    candidate.projection_snapshot_ = true;
    auto snapshot = std::make_shared<const UiTree>(candidate);
    nodes_.swap(candidate.nodes_);
    id_index_.swap(candidate.id_index_);
    focused_ = candidate.focused_;
    projection_epoch_ = projection.epoch;
    std::atomic_store(&published_projection_, std::move(snapshot));
}

std::optional<UiNodeId> UiTree::HitTestProjection(const std::int32_t x, const std::int32_t y) const {
    if (!projection_snapshot_) throw std::runtime_error("UI hit test requires a complete projection snapshot");
    const auto hit = [&](auto&& self, const UiNodeId id) -> std::optional<UiNodeId> {
        const auto& node = Require(id);
        if (!node.window_visible || node.visibility != Visibility::Visible || !node.enabled) return std::nullopt;
        for (auto child = node.children.rbegin(); child != node.children.rend(); ++child)
            if (const auto found = self(self, *child)) return found;
        if (node.clickable && ContainsPoint(node.screen_frame, x, y) && ContainsPoint(node.screen_clip, x, y))
            return id;
        return std::nullopt;
    };
    return hit(hit, root_);
}

UiNodeId UiTree::CreateNode(const UiClass kind) {
    if (nodes_.size() >= kMaxNodes ||
        next_node_ == std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("UI tree node limit exceeded");
    }
    const UiNodeId id{(static_cast<std::uint64_t>(generation_) << 32U) |
                      ++next_node_};
    auto [created, inserted] =
        nodes_.emplace(id, UiNode{.id = id, .kind = kind});
    static_cast<void>(inserted);
    created->second.fallback_font = fallback_font_;
    if (kind == UiClass::Button) {
        created->second.background_color = 0x404040ffU;
        created->second.padding = {6, 4, 6, 4};
        created->second.clickable = true;
        created->second.max_lines = 1;
    } else if (kind == UiClass::EditText) {
        created->second.max_lines = 1;
    } else if (kind == UiClass::TableLayout) {
        created->second.orientation = Orientation::Vertical;
    } else if (kind == UiClass::TableRow) {
        created->second.orientation = Orientation::Horizontal;
    }
    return id;
}

void UiTree::Attach(const UiNodeId parent, const UiNodeId child,
                    const std::optional<std::size_t> index) {
    if (geometry_owner_ == UiGeometryOwner::ExternalProjection)
        throw std::runtime_error("External UI hierarchy requires a projection commit");
    auto& parent_node = Require(parent);
    auto& child_node = Require(child);
    if (parent == child || child == root_) {
        throw std::runtime_error("UI tree attachment would create a cycle");
    }
    if (child_node.parent.has_value()) {
        throw std::runtime_error("UI tree child is already attached");
    }
    if (parent_node.children.size() >= kMaxChildren) {
        throw std::runtime_error("UI tree child limit exceeded");
    }
    std::size_t depth = 1;
    for (auto cursor = std::optional<UiNodeId>{parent}; cursor.has_value();
         cursor = Require(*cursor).parent) {
        if (*cursor == child || ++depth > kMaxDepth) {
            throw std::runtime_error("UI tree attachment would create a cycle");
        }
    }
    // Detached subtrees can be built bottom-up; count their descendants too.
    std::vector<std::pair<UiNodeId, std::size_t>> subtree{{child, depth}};
    while (!subtree.empty()) {
        const auto [node, level] = subtree.back(); subtree.pop_back();
        if (level > kMaxDepth) throw std::runtime_error("UI tree depth limit exceeded");
        for (const auto descendant : Require(node).children) subtree.emplace_back(descendant, level + 1);
    }
    const auto position = index.value_or(parent_node.children.size());
    if (position > parent_node.children.size()) {
        throw std::runtime_error("UI tree child index is out of range");
    }
    child_node.parent = parent;
    parent_node.children.insert(parent_node.children.begin() +
                                    static_cast<std::ptrdiff_t>(position),
                                child);
    if (IsAttached(parent)) RebuildIndex();
    MarkAncestors(parent, true, true);
}

void UiTree::Detach(const UiNodeId child, const DetachLayoutPolicy layout) {
    if (geometry_owner_ == UiGeometryOwner::ExternalProjection)
        throw std::runtime_error("External UI hierarchy requires a projection commit");
    auto& child_node = Require(child);
    if (child == root_) {
        throw std::runtime_error("UI content root cannot be detached");
    }
    if (!child_node.parent.has_value()) return;
    const auto parent = *child_node.parent;
    auto& siblings = Require(parent).children;
    const auto found = std::find(siblings.begin(), siblings.end(), child);
    if (found == siblings.end()) {
        throw std::runtime_error("UI tree hierarchy is inconsistent");
    }
    const bool was_attached = IsAttached(child);
    if (focused_.has_value() && HasFocus(child)) focused_.reset();
    siblings.erase(found);
    child_node.parent.reset();
    if (was_attached) RebuildIndex();
    MarkAncestors(parent, layout == DetachLayoutPolicy::request_layout, true);
}

bool UiTree::BringChildToFront(const UiNodeId parent, const UiNodeId child) {
    if (geometry_owner_ == UiGeometryOwner::ExternalProjection)
        throw std::runtime_error("External UI hierarchy requires a projection commit");
    auto& siblings = Require(parent).children;
    static_cast<void>(Require(child));
    const auto found = std::find(siblings.begin(), siblings.end(), child);
    if (found == siblings.end()) return false;
    std::rotate(found, found + 1, siblings.end());
    if (IsAttached(parent)) RebuildIndex();
    MarkAncestors(parent, false, true);
    return true;
}

void UiTree::DestroySubtree(const UiNodeId node) {
    if (node == root_) {
        throw std::runtime_error("UI content root cannot be destroyed");
    }
    static_cast<void>(Require(node));
    Detach(node);
    std::vector<UiNodeId> pending{node};
    while (!pending.empty()) {
        const auto current = pending.back();
        pending.pop_back();
        const auto found = nodes_.find(current);
        pending.insert(pending.end(), found->second.children.begin(),
                       found->second.children.end());
        nodes_.erase(found);
    }
}

void UiTree::Reset() {
    if (generation_ == std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("UI tree generation limit exceeded");
    }
    projection_target_.reset();
    projection_epoch_ = 0;
    std::atomic_store(&published_projection_, std::shared_ptr<const UiTree>{});
    ++generation_;
    next_node_ = 0;
    nodes_.clear();
    id_index_.clear();
    focused_.reset();
    root_ = CreateNode(UiClass::ContentRoot);
}

UiNode* UiTree::Get(const UiNodeId id) {
    const auto found = nodes_.find(id);
    return found == nodes_.end() ? nullptr : &found->second;
}

const UiNode* UiTree::Get(const UiNodeId id) const {
    const auto found = nodes_.find(id);
    return found == nodes_.end() ? nullptr : &found->second;
}

std::optional<UiNodeId> UiTree::FindByAndroidId(
    const std::int32_t android_id) const {
    if (android_id < 0) return std::nullopt;
    const auto found = id_index_.find(android_id);
    if (found == id_index_.end() || found->second.empty()) return std::nullopt;
    return found->second.front();
}

void UiTree::SetAndroidId(const UiNodeId node,
                          const std::int32_t android_id) {
    auto& target = Require(node);
    if (target.android_id == android_id) return;
    const bool attached = IsAttached(node);
    target.android_id = android_id;
    if (attached) RebuildIndex();
}

void UiTree::SetVisibility(const UiNodeId node,
                           const Visibility visibility) {
    if (geometry_owner_ == UiGeometryOwner::ExternalProjection)
        throw std::runtime_error("External UI visibility requires a projection commit");
    auto& target = Require(node);
    if (target.visibility == visibility) return;
    const bool changes_layout = target.visibility == Visibility::Gone ||
                                visibility == Visibility::Gone;
    target.visibility = visibility;
    if (visibility != Visibility::Visible && focused_.has_value() &&
        HasFocus(node)) {
        focused_.reset();
    }
    MarkAncestors(node, changes_layout, true);
}

void UiTree::SetEnabled(const UiNodeId node, const bool enabled) {
    auto& target = Require(node);
    if (target.enabled == enabled) return;
    target.enabled = enabled;
    if (!enabled && focused_.has_value() && HasFocus(node)) focused_.reset();
    MarkAncestors(node, false, true);
}

void UiTree::SetWindowVisible(const UiNodeId node, const bool visible) {
    auto& target = Require(node);
    if (target.window_visible == visible) return;
    target.window_visible = visible;
    if (!visible && focused_.has_value() && HasFocus(node)) focused_.reset();
    MarkAncestors(node, false, true);
}

void UiTree::SetFallbackFont(std::shared_ptr<const UiFallbackFont> font) {
    fallback_font_ = std::move(font);
    for (auto& [_, node] : nodes_) node.fallback_font = fallback_font_;
    MarkLayoutDirty(root_);
}

bool UiTree::IsVisible(const UiNodeId node) const {
    if (!IsAttached(node)) return false;
    auto current = std::optional<UiNodeId>{node};
    while (current) {
        const auto* state = Get(*current);
        if (!state || !state->window_visible || state->visibility != Visibility::Visible) return false;
        current = state->parent;
    }
    return true;
}

bool UiTree::IsFocused(const UiNodeId node) const {
    static_cast<void>(Require(node));
    return focused_ == node;
}

bool UiTree::HasFocus(const UiNodeId node) const {
    static_cast<void>(Require(node));
    for (auto cursor = focused_; cursor.has_value();
         cursor = Require(*cursor).parent) {
        if (*cursor == node) return true;
    }
    return false;
}

bool UiTree::RequestFocus(const UiNodeId node, const bool touch_mode) {
    const auto& target = Require(node);
    if (!target.focusable || (touch_mode && !target.focusable_in_touch_mode)) {
        return false;
    }
    for (auto cursor = std::optional<UiNodeId>{node}; cursor.has_value();
         cursor = Require(*cursor).parent) {
        const auto& ancestor = Require(*cursor);
        if (!ancestor.enabled || !ancestor.window_visible || ancestor.visibility != Visibility::Visible) {
            return false;
        }
    }
    focused_ = node;
    return true;
}

void UiTree::ClearFocus(const UiNodeId node) {
    static_cast<void>(Require(node));
    if (focused_ == node) focused_.reset();
}

void UiTree::SetClickable(const UiNodeId node, const bool clickable) {
    Require(node).clickable = clickable;
}

void UiTree::MarkLayoutDirty(const UiNodeId node) {
    MarkAncestors(node, true, true);
}

void UiTree::MarkDrawDirty(const UiNodeId node) {
    MarkAncestors(node, false, true);
}

void UiTree::ClearLayoutDirty() {
    for (auto& [id, node] : nodes_) {
        static_cast<void>(id);
        node.layout_dirty = false;
    }
}

void UiTree::ClearDrawDirty() {
    for (auto& [id, node] : nodes_) {
        static_cast<void>(id);
        node.draw_dirty = false;
    }
}

UiNode& UiTree::Require(const UiNodeId id) {
    const auto found = nodes_.find(id);
    if (found == nodes_.end()) throw std::runtime_error("UI node is stale");
    return found->second;
}

const UiNode& UiTree::Require(const UiNodeId id) const {
    const auto found = nodes_.find(id);
    if (found == nodes_.end()) throw std::runtime_error("UI node is stale");
    return found->second;
}

bool UiTree::IsAttached(const UiNodeId id) const {
    auto cursor = id;
    for (std::size_t depth = 0; depth < kMaxDepth; ++depth) {
        if (cursor == root_) return true;
        const auto& node = Require(cursor);
        if (!node.parent.has_value()) return false;
        cursor = *node.parent;
    }
    throw std::runtime_error("UI tree depth limit exceeded");
}

void UiTree::IndexSubtree(const UiNodeId node) {
    auto& target = Require(node);
    if (target.android_id >= 0) {
        auto& entries = id_index_[target.android_id];
        if (std::find(entries.begin(), entries.end(), node) == entries.end()) {
            entries.push_back(node);
        }
    }
    for (const auto child : target.children) IndexSubtree(child);
}

void UiTree::RebuildIndex() {
    id_index_.clear();
    IndexSubtree(root_);
}

void UiTree::MarkAncestors(UiNodeId node, const bool layout,
                           const bool draw) {
    for (std::size_t depth = 0; depth < kMaxDepth; ++depth) {
        auto& target = Require(node);
        if (layout) target.layout_dirty = true;
        if (draw) target.draw_dirty = true;
        if (!target.parent.has_value()) return;
        node = *target.parent;
    }
    throw std::runtime_error("UI tree depth limit exceeded");
}

}  // namespace ogplay::runtime::ui
