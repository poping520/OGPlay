#include "ogplay/runtime/ui/ui_tree.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <stdexcept>

namespace ogplay::runtime::ui {

std::size_t UiNodeIdHash::operator()(const UiNodeId id) const noexcept {
    return std::hash<std::uint64_t>{}(id.Value());
}

UiTree::UiTree() { Reset(); }

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
    if (kind == UiClass::Button) {
        created->second.background_color = 0x404040ffU;
        created->second.padding = {6, 4, 6, 4};
        created->second.clickable = true;
    }
    return id;
}

void UiTree::Attach(const UiNodeId parent, const UiNodeId child,
                    const std::optional<std::size_t> index) {
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

void UiTree::Detach(const UiNodeId child) {
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
    siblings.erase(found);
    child_node.parent.reset();
    if (was_attached) RebuildIndex();
    MarkAncestors(parent, true, true);
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
    ++generation_;
    next_node_ = 0;
    nodes_.clear();
    id_index_.clear();
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
    auto& target = Require(node);
    if (target.visibility == visibility) return;
    const bool changes_layout = target.visibility == Visibility::Gone ||
                                visibility == Visibility::Gone;
    target.visibility = visibility;
    MarkAncestors(node, changes_layout, true);
}

void UiTree::SetEnabled(const UiNodeId node, const bool enabled) {
    Require(node).enabled = enabled;
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

void UiTree::ClearDirty() {
    for (auto& [id, node] : nodes_) {
        static_cast<void>(id);
        node.layout_dirty = false;
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
