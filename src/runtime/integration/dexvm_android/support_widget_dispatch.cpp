// Widget click dispatch over the UiTree's most recent resolved geometry.

#include <optional>

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
    const auto listener = context.ui_click_listeners.find(id);
    return listener != context.ui_click_listeners.end() &&
                   listener->second.IsValid()
               ? std::optional<ui::UiNodeId>{id}
               : std::nullopt;
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

bool ViewContainsPoint(DexVmAndroidContext& context,
                       const std::uint64_t handle, const float x,
                       const float y) {
    android_intrinsics::EnsureLayout(context);
    const auto node = FindViewUiNode(context, handle);
    return node.has_value() && context.ui_tree.Get(*node) != nullptr &&
           android_intrinsics::Contains(
               context.ui_tree.Get(*node)->screen_frame, x, y);
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
