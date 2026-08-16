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
    return ((click != context.ui_click_listeners.end() &&
             click->second.IsValid()) ||
            (touch != context.ui_touch_listeners.end() &&
             touch->second.IsValid()))
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
    if (!node.has_value()) return false;
    const auto* state = context.ui_tree.Get(*node);
    return state != nullptr && state->visibility == ui::Visibility::Visible &&
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
    if (action == kActionDown) click_eligible = has_live_click;
    if (action == kActionUp) {
        if (!touch_consumed && click_eligible && has_live_click &&
            ViewContainsPoint(context, handle, x, y)) {
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
