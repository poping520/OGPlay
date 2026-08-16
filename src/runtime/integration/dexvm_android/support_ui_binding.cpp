#include "ogplay/runtime/integration/dexvm_android.h"

#include <stdexcept>

namespace ogplay::runtime {

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

void ResetViewUiState(DexVmAndroidContext& context) {
    context.ui_tree.Reset();
    context.object_to_ui_node.clear();
    context.ui_node_to_object.clear();
    context.ui_click_listeners.clear();
    context.ui_touch_listeners.clear();
    context.ui_view_layout_params.clear();
}

}  // namespace ogplay::runtime

namespace ogplay::runtime::android_intrinsics {

ui::UiClass UiClassForDescriptor(const std::string_view descriptor) {
    if (descriptor == "Landroid/widget/FrameLayout;") {
        return ui::UiClass::FrameLayout;
    }
    if (descriptor == "Landroid/widget/LinearLayout;" ||
        descriptor == "Landroid/widget/TableLayout;" ||
        descriptor == "Landroid/widget/TableRow;") {
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

}  // namespace ogplay::runtime::android_intrinsics
