#include "ogplay/runtime/boundary/android_boundary_hle.h"
// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from support_ui_binding.cpp ----
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "catalog.h"

#include <stdexcept>
#include <tuple>
#include <bit>
#include <cmath>
#include <limits>
#include "ogplay/runtime/integration/bitmap_pixels.h"

namespace ogplay::runtime {

void RefreshAndroidImageDrawables(DexVmAndroidContext& context) {
    for (const auto& [view, drawable] : context.ui_view_images) {
        const auto node = FindViewUiNode(context, view);
        if (!node) continue;
        auto& state = context.ui_drawables.at(drawable.Value());
        std::shared_ptr<const ui::UiBitmap> image;
        std::optional<std::uint32_t> color;
        ui::Size intrinsic{};
        if (state.bitmap.IsValid()) {
            const auto found = context.bitmaps.find(state.bitmap.Value());
            if (found == context.bitmaps.end() || found->second.recycled)
                throw std::runtime_error("ImageView Drawable bitmap is recycled or unavailable");
            const auto& bitmap = found->second;
            if (!state.image_projection || state.bitmap_revision != bitmap.pixels->Revision() || bitmap.pixels->Locks() != 0) {
                auto projected = std::make_shared<ui::UiBitmap>();
                projected->width = bitmap.width; projected->height = bitmap.height;
                const auto argb = bitmap.pixels->Snapshot();
                projected->rgba8.reserve(argb.size() * 4U);
                for (const auto value : argb) {
                    projected->rgba8.push_back(static_cast<std::uint8_t>(value >> 16U));
                    projected->rgba8.push_back(static_cast<std::uint8_t>(value >> 8U));
                    projected->rgba8.push_back(static_cast<std::uint8_t>(value));
                    projected->rgba8.push_back(static_cast<std::uint8_t>(value >> 24U));
                }
                state.image_projection = std::move(projected);
                state.bitmap_revision = bitmap.pixels->Revision();
            }
            image = state.image_projection; intrinsic = {bitmap.width, bitmap.height};
        } else if (state.color) {
            color = (*state.color << 8U) | (*state.color >> 24U);
        } else if (state.resource_id != 0) {
            image = ResolveUiDrawable(context, state.resource_id);
            intrinsic = {image->width, image->height};
        } else if (!state.bitmap_drawable) {
            throw std::runtime_error("ImageView Drawable has no supported backing");
        }
        auto* target = context.ui_tree.Get(*node);
        const auto alpha = static_cast<float>(state.alpha) / 255.0F;
        if (target->intrinsic != intrinsic) {
            target->intrinsic = intrinsic; context.ui_tree.MarkLayoutDirty(*node);
        }
        if (target->image_bitmap != image || target->image_color != color || target->image_alpha != alpha) {
            target->image_bitmap = std::move(image); target->image_color = color; target->image_alpha = alpha;
            context.ui_tree.MarkDrawDirty(*node);
        }
    }
}

void AssignViewContext(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                       const dexvm::VmObjectRef view,
                       const dexvm::VmObjectRef owner) {
    if (!context.view_context_field.has_value() || !context.view_context_field->IsValid() ||
        !context.view_java_layout_fields) {
        throw std::runtime_error("View mContext field is unbound");
    }
    dexvm::IntrinsicContext call{vm, view, {}};
    dexvm::IntrinsicCall(call).SetRef(*context.view_context_field, view, owner);
}

ui::Insets ReadAndroidViewPadding(dexvm::Interpreter& vm, DexVmAndroidContext& context, const dexvm::VmObjectRef view) {
    dexvm::IntrinsicContext call{vm, view, {}};
    dexvm::IntrinsicCall fields(call);
    const auto& tokens = context.view_java_layout_fields->padding;
    return {fields.GetInt(tokens[0]), fields.GetInt(tokens[1]), fields.GetInt(tokens[2]), fields.GetInt(tokens[3])};
}

ui::Size ReadAndroidViewMinimum(dexvm::Interpreter& vm, DexVmAndroidContext& context, const dexvm::VmObjectRef view) {
    dexvm::IntrinsicContext call{vm, view, {}};
    dexvm::IntrinsicCall fields(call);
    const auto& tokens = context.view_java_layout_fields->minimum;
    return {fields.GetInt(tokens[0]), fields.GetInt(tokens[1])};
}

namespace {
void ProjectViewLayoutInputs(dexvm::Interpreter& vm, DexVmAndroidContext& context, const dexvm::VmObjectRef view) {
    const auto node = FindViewUiNode(context, view.Value());
    if (!node) return;
    const auto padding = ReadAndroidViewPadding(vm, context, view);
    const auto minimum = ReadAndroidViewMinimum(vm, context, view);
    auto* state = context.ui_tree.Get(*node);
    if (state->padding == padding && state->minimum == minimum) return;
    state->padding = padding;
    state->minimum = minimum;
    context.ui_tree.MarkLayoutDirty(*node);
}
}  // namespace

void WriteAndroidViewPadding(dexvm::Interpreter& vm, DexVmAndroidContext& context, const dexvm::VmObjectRef view,
                             const ui::Insets padding, const std::optional<bool> explicit_padding) {
    dexvm::IntrinsicContext call{vm, view, {}};
    dexvm::IntrinsicCall fields(call);
    const auto& tokens = *context.view_java_layout_fields;
    const std::array values{padding.left, padding.top, padding.right, padding.bottom};
    for (std::size_t i = 0; i < values.size(); ++i)
        fields.SetInt(tokens.padding[i], values[i]);
    const std::array user{padding.left,
                          padding.right,
                          padding.bottom,
                          std::numeric_limits<std::int32_t>::min(),
                          std::numeric_limits<std::int32_t>::min(),
                          padding.left,
                          padding.right};
    for (std::size_t i = 0; i < user.size(); ++i)
        fields.SetInt(tokens.user_padding[i], user[i]);
    if (explicit_padding)
        for (const auto token : tokens.padding_defined)
            fields.SetInt(token, *explicit_padding ? 1 : 0);
    ProjectViewLayoutInputs(vm, context, view);
}

void WriteAndroidViewMinimum(dexvm::Interpreter& vm, DexVmAndroidContext& context, const dexvm::VmObjectRef view,
                             const ui::Size minimum) {
    dexvm::IntrinsicContext call{vm, view, {}};
    dexvm::IntrinsicCall fields(call);
    const auto& tokens = context.view_java_layout_fields->minimum;
    fields.SetInt(tokens[0], minimum.width);
    fields.SetInt(tokens[1], minimum.height);
    ProjectViewLayoutInputs(vm, context, view);
}

void InitializeAndroidViewJavaLayout(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                                     const dexvm::VmObjectRef view) {
    if (context.ui_traversal_owner == AndroidUiTraversalOwner::JavaTraversal) {
        const auto type = vm.Linker().ResolveDescriptor("Landroid/view/View;");
        const auto field = vm.Linker().FindFieldRecursive(type, "mMeasuredWidth", "I");
        if (!field || vm.Linker().Field(*field).is_unimplemented)
            throw dexvm::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                     "JavaTraversal requires its paired measure payload"};
        if (android_intrinsics::UiClassForObject(vm, view) != ui::UiClass::View ||
            vm.Linker().IsAssignable(vm.Linker().ResolveDescriptor("Landroid/view/ViewGroup;"),
                                     vm.Model().ObjectClass(view))) {
            if (auto* ledger = vm.Ledger()) ledger->RecordUnimplemented("dexvm.view_measure_nonleaf", 0);
            throw dexvm::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                     "JavaTraversal only supports plain test roots/leaves"};
        }
    }
    dexvm::IntrinsicContext call{vm, view, {}};
    dexvm::IntrinsicCall fields(call);
    // View.java:3438. Context prebinding is separate from construction:
    // a failed constructor must never suppress a later resource exception.
    const auto owner = fields.GetRef(*context.view_context_field);
    if (context.ui_traversal_owner == AndroidUiTraversalOwner::JavaTraversal && !owner.IsValid())
        throw dexvm::VmJavaThrow{"Ljava/lang/NullPointerException;", "Measured View requires Context"};
    const auto roots = vm.ProtectReferences(std::array{view, owner});
    const auto resources =
        owner.IsValid()
            ? android_intrinsics::CallAndroidMethod(vm, owner, "getResources", "()Landroid/content/res/Resources;").ref
            : dexvm::VmObjectRef{};
    fields.SetRef(context.view_java_layout_fields->resources, resources);
    const auto& tokens = *context.view_java_layout_fields;
    for (const auto token : tokens.padding)
        fields.SetInt(token, 0);
    for (const auto token : tokens.minimum)
        fields.SetInt(token, 0);
    for (const auto token : tokens.user_padding)
        fields.SetInt(token, 0);
    fields.SetInt(tokens.user_padding[3], std::numeric_limits<std::int32_t>::min());
    fields.SetInt(tokens.user_padding[4], std::numeric_limits<std::int32_t>::min());
    for (const auto token : tokens.padding_defined)
        fields.SetInt(token, 0);
    if (context.ui_traversal_owner == AndroidUiTraversalOwner::JavaTraversal) {
        const auto& measure = *context.view_java_measure_fields;
        // Pinned ROM View(Context): old specs MIN_VALUE, flags2 0x22408,
        // SOUND_EFFECTS_ENABLED | HAPTIC_FEEDBACK_ENABLED. Measured fields
        // remain VM zero until ROM setMeasuredDimension writes them.
        for (const auto token : measure.old_specs)
            fields.SetInt(token, std::numeric_limits<std::int32_t>::min());
        fields.SetInt(measure.flags[0], 0);
        fields.SetInt(measure.flags[1], 0x22408);
        fields.SetInt(measure.flags[2], 0);
        fields.SetInt(measure.view_flags, 0x18000000);
    }
    if (android_intrinsics::UiClassForObject(vm, view) == ui::UiClass::Button) {
        // Preserve the existing bounded Widget.Button background projection.
        WriteAndroidViewPadding(vm, context, view, {6, 4, 6, 4}, false);
    }
    // API19 View.java:3453-3463. The sealed process target SDK is the
    // ApplicationInfo fact exposed by this bounded Context implementation.
    if (owner.IsValid() && context.view_compatibility_fields) {
        dexvm::IntrinsicCall fields(call);
        const auto& handles = *context.view_compatibility_fields;
        if (fields.GetInt(handles[0]) == 0) {
            fields.SetInt(handles[1], context.target_sdk_version <= 17U);
            fields.SetInt(handles[2], context.target_sdk_version < 19U);
            fields.SetInt(handles[0], 1);
        }
    }
    ProjectViewLayoutInputs(vm, context, view);
}

void CommitAndroidMeasuredRootProjection(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                                         const dexvm::VmObjectRef root, const ui::UiProjectionTarget& target,
                                         const std::uint64_t epoch) {
    if (context.ui_traversal_owner != AndroidUiTraversalOwner::JavaTraversal || vm.CurrentContextToken() != 1U)
        throw dexvm::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Measured projection requires Java owner thread"};
    if (root != context.content_view || target.content_revision != context.ui_content_revision ||
        target.window_generation != context.ui_tree.Generation())
        throw dexvm::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Measured projection has stale root identity"};
    const auto node = FindViewUiNode(context, root.Value());
    if (!node || android_intrinsics::UiClassForObject(vm, root) != ui::UiClass::View ||
        vm.Linker().IsAssignable(vm.Linker().ResolveDescriptor("Landroid/view/ViewGroup;"),
                                 vm.Model().ObjectClass(root)))
        throw dexvm::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                 "Measured projection requires a plain root"};
    dexvm::IntrinsicContext call{vm, root, {}};
    dexvm::IntrinsicCall fields(call);
    const auto& measure = *context.view_java_measure_fields;
    if ((fields.GetInt(measure.flags[0]) & 0x800) == 0)
        throw dexvm::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Root has no completed ROM measurement"};
    const ui::Size measured{fields.GetInt(measure.measured[0]) & 0x00ffffff,
                            fields.GetInt(measure.measured[1]) & 0x00ffffff};
    // VD-04 does not invent a Java frame. The synthetic host window root has
    // viewport geometry; the guest leaf projects measured size and empty bounds.
    const ui::UiGeometryProjection projection{
        target,
        epoch,
        {{target.root,
          std::nullopt,
          {0, 0, target.metrics.width, target.metrics.height},
          {target.metrics.width, target.metrics.height},
          ui::Visibility::Visible,
          {}},
         {*node, target.root, {}, measured, ui::Visibility::Visible, ReadAndroidViewPadding(vm, context, root)}}};
    context.ui_tree.CommitProjection(projection);
    context.ui_measure_epoch = epoch;
}

void RefreshAndroidViewLayoutInputs(dexvm::Interpreter& vm, DexVmAndroidContext& context) {
    for (const auto& [_, view] : context.ui_node_to_object)
        ProjectViewLayoutInputs(vm, context, view);
}

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

void InitializeDefaultViewBackground(dexvm::Interpreter& vm,
                                     DexVmAndroidContext& context,
                                     const dexvm::VmObjectRef view,
                                     const ui::UiNodeId node) {
    if (context.ui_tree.Get(node)->kind != ui::UiClass::Button ||
        context.ui_view_backgrounds.contains(view.Value())) {
        return;
    }
    const auto drawable =
        vm.NewIntrinsicInstance("Landroid/graphics/drawable/Drawable;");
    context.ui_view_backgrounds.emplace(view.Value(), drawable);
    context.ui_drawables.emplace(
        drawable.Value(),
        DexVmAndroidContext::UiDrawableState{
            .color = context.ui_tree.Get(node)->background_color,
            .callback_node = node});
}

void ResetViewUiState(DexVmAndroidContext& context) {
    context.ui_measure_requests.clear();
    context.ui_touch_capture = 0;
    context.ui_touch_activity = dexvm::VmObjectRef{};
    context.ui_touch_targets.clear(); context.ui_touch_clicks.clear();
    if (!context.activity_stack.empty()) {
        const auto children = context.ui_tree.Get(ActivityContentRoot(context))->children;
        for (const auto child : children) RetireViewUiSubtree(context, child);
        context.focused_edit_text = dexvm::VmObjectRef{};
        return;
    }
    context.ui_tree.Reset();
    context.ui_notified_view_sizes.clear();context.ui_layout_metrics.reset();
    context.ui_notified_window_focus.clear();
    context.object_to_ui_node.clear();
    context.ui_node_to_object.clear();
    context.ui_click_listeners.clear();
    context.ui_touch_listeners.clear();
    context.ui_view_layout_params.clear();
    context.ui_view_backgrounds.clear();
    context.ui_view_images.clear();
    context.ui_drawables.clear();
    context.text_watchers.clear();
    context.focused_edit_text = dexvm::VmObjectRef{};
}

void DetachGlSurfaceSubtree(DexVmAndroidContext& context, const ui::UiNodeId subtree) {
    std::vector<ui::UiNodeId> nodes{subtree};
    while (!nodes.empty()) {
        const auto node = nodes.back(); nodes.pop_back();
        const auto* state = context.ui_tree.Get(node);
        if (!state) continue;
        nodes.insert(nodes.end(), state->children.begin(), state->children.end());
        const auto view = ViewObjectForUiNode(context, node);
        if (context.gl_surface_runtimes.contains(view.Value()) && context.retire_gl_surface_view)
            context.retire_gl_surface_view(view);
    }
}

ui::UiNodeId ActivityContentRoot(DexVmAndroidContext& context,
    const dexvm::VmObjectRef owner) {
    if (context.activity_stack.empty()) return context.ui_tree.Root();
    const auto found = std::find_if(context.activity_stack.begin(), context.activity_stack.end(),
        [&](const auto& record) { return record.object == (owner.IsValid() ? owner : context.activity); });
    if (found == context.activity_stack.end())
        throw std::logic_error("foreground Activity has no window record");
    if (!found->window_root || !context.ui_tree.Get(*found->window_root)) {
        const auto node = context.ui_tree.CreateNode(ui::UiClass::FrameLayout);
        context.ui_tree.Get(node)->layout.width.mode = ui::SizeMode::MatchParent;
        context.ui_tree.Get(node)->layout.height.mode = ui::SizeMode::MatchParent;
        context.ui_tree.Attach(context.ui_tree.Root(), node);
        found->window_root = node;
        found->visible = !owner.IsValid() || owner == context.activity;
        context.ui_tree.SetWindowVisible(node, found->visible);
    }
    return *found->window_root;
}

void RetireViewUiSubtree(DexVmAndroidContext& context, const ui::UiNodeId node) {
    std::vector<ui::UiNodeId> nodes{node};
    while (!nodes.empty()) {
        const auto current = nodes.back(); nodes.pop_back();
        const auto* state = context.ui_tree.Get(current);
        if (!state) continue;
        context.ui_notified_view_sizes.erase(current);
        context.ui_notified_window_focus.erase(current);
        nodes.insert(nodes.end(), state->children.begin(), state->children.end());
        const auto found = context.ui_node_to_object.find(current);
        if (found != context.ui_node_to_object.end()) {
            const auto object = found->second.Value();
            if (context.gl_surface_runtimes.contains(object)) {
                if (context.retire_gl_surface_view) context.retire_gl_surface_view(found->second);
                context.gl_surface_runtimes.erase(object);
            }
            if (const auto holder = context.surface_holders.find(object); holder != context.surface_holders.end()) {
                const auto key = holder->second.Value();
                DetachAndroidMediaSurface(context, key);
                context.surface_callbacks.erase(key);
                context.holder_surfaces.erase(key);
                if (context.retire_native_surface) context.retire_native_surface(dexvm::VmObjectRef(key));
                context.active_surface_holders.erase(key);
                context.surface_callback_sizes.erase(key);
                context.surface_holders.erase(holder);
            }
            context.ui_touch_clicks.erase(object);
            context.ui_touch_targets.erase(object);
            if (context.ui_touch_capture == object) {
                context.ui_touch_capture = 0;
                context.ui_touch_targets.clear(); context.ui_touch_clicks.clear();
            }
            context.object_to_ui_node.erase(object);
            context.ui_view_layout_params.erase(object);
            context.ui_view_backgrounds.erase(object);
            context.ui_view_images.erase(object);
            context.text_watchers.erase(object);
            if (context.focused_edit_text.Value() == object)
                context.focused_edit_text = dexvm::VmObjectRef{};
            context.ui_node_to_object.erase(found);
        }
        context.ui_click_listeners.erase(current);
        context.ui_touch_listeners.erase(current);
    }
    context.ui_tree.DestroySubtree(node);
}

}  // namespace ogplay::runtime

namespace ogplay::runtime::android_intrinsics {

ui::UiClass UiClassForDescriptor(const std::string_view descriptor) {
    if (descriptor == "Landroid/widget/FrameLayout;" ||
        descriptor == "Landroid/widget/AbsoluteLayout;") {
        return ui::UiClass::FrameLayout;
    }
    if (descriptor == "Landroid/widget/ScrollView;") return ui::UiClass::ScrollView;
    if (descriptor == "Landroid/widget/TableLayout;") return ui::UiClass::TableLayout;
    if (descriptor == "Landroid/widget/TableRow;") return ui::UiClass::TableRow;
    if (descriptor == "Landroid/widget/LinearLayout;") {
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
        if (descriptor == "Landroid/widget/EditText;") return ui::UiClass::EditText;
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

ui::UiClass UiClassForObject(dexvm::Interpreter& vm,
                             const dexvm::VmObjectRef view) {
    if (!view.IsValid()) {
        throw std::runtime_error("cannot classify a null guest View");
    }
    auto type = vm.Model().ObjectClass(view);
    for (std::size_t depth = 0; depth < 64; ++depth) {
        const auto& klass = vm.Linker().Class(type);
        if (klass.descriptor == "Landroid/view/View;") break;
        const auto kind = UiClassForDescriptor(klass.descriptor);
        if (kind != ui::UiClass::View) return kind;
        if (!klass.super.has_value()) break;
        type = *klass.super;
    }
    return ui::UiClass::View;
}

}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from support_ui_inflater.cpp ----
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
constexpr std::uint8_t kTypeAttribute = 0x02;
constexpr std::uint8_t kTypeString = 0x03;
constexpr std::uint8_t kTypeFirstColor = 0x1c;
constexpr std::uint8_t kTypeLastColor = 0x1f;

constexpr std::array<UiWidgetDescriptor, 17> kWidgets{{
    {"View", "Landroid/view/View;", ui::UiClass::View},
    {"SurfaceView", "Landroid/view/SurfaceView;", ui::UiClass::View},
    {"TextView", "Landroid/widget/TextView;", ui::UiClass::TextView},
    {"Button", "Landroid/widget/Button;", ui::UiClass::Button},
    {"EditText", "Landroid/widget/EditText;", ui::UiClass::EditText},
    {"ImageView", "Landroid/widget/ImageView;", ui::UiClass::ImageView},
    {"ImageButton", "Landroid/widget/ImageButton;", ui::UiClass::ImageButton},
    {"ProgressBar", "Landroid/widget/ProgressBar;", ui::UiClass::View},
    {"VideoView", "Landroid/widget/VideoView;", ui::UiClass::VideoView},
    {"WebView", "Landroid/webkit/WebView;", ui::UiClass::View},
    {"LinearLayout", "Landroid/widget/LinearLayout;", ui::UiClass::LinearLayout},
    {"FrameLayout", "Landroid/widget/FrameLayout;", ui::UiClass::FrameLayout},
    {"RelativeLayout", "Landroid/widget/RelativeLayout;", ui::UiClass::RelativeLayout},
    {"TableLayout", "Landroid/widget/TableLayout;", ui::UiClass::TableLayout},
    {"TableRow", "Landroid/widget/TableRow;", ui::UiClass::TableRow},
    {"ScrollView", "Landroid/widget/ScrollView;", ui::UiClass::ScrollView},
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

}  // namespace

[[nodiscard]] const loader::ArscEntry& ResolveUiResourceEntry(
    const DexVmAndroidContext& context, std::uint32_t resource_id) {
    std::unordered_set<std::uint32_t> seen;
    for (std::size_t depth = 0; depth < 16; ++depth) {
        if (!seen.insert(resource_id).second) {
            throw std::runtime_error("UI resource reference cycle");
        }
        const loader::ArscEntry* entry = nullptr;
        const auto width_dp = static_cast<int>(
            std::lround(context.surface_width / context.ui_density));
        const auto height_dp = static_cast<int>(
            std::lround(context.surface_height / context.ui_density));
        const auto smallest_dp = std::min(width_dp, height_dp);
        const auto orientation = width_dp > height_dp ? 2U : 1U;
        const auto density = static_cast<int>(
            std::lround(context.ui_density * 160.0F));
        const auto long_dp = std::max(width_dp, height_dp);
        const auto short_dp = std::min(width_dp, height_dp);
        const auto screen_size = long_dp < 470 ? 1U
            : long_dp >= 960 && short_dp >= 720 ? 4U
            : long_dp >= 640 && short_dp >= 480 ? 3U : 2U;
        const auto density_better = [density](const std::uint16_t lhs,
                                              const std::uint16_t rhs) {
            if (lhs == rhs) return false;
            int high = lhs != 0 ? lhs : 160;
            int low = rhs != 0 ? rhs : 160;
            bool lhs_high = true;
            if (low > high) {
                std::swap(low, high);
                lhs_high = false;
            }
            const int requested = density != 0 ? density : 160;
            if (requested >= high) return lhs_high;
            if (low >= requested) return !lhs_high;
            return (((2 * low) - requested) * high >
                    requested * requested) ? !lhs_high : lhs_high;
        };
        const auto better = [&](const loader::ArscEntry& lhs,
                                const loader::ArscEntry& rhs) {
            if (lhs.smallest_width_dp != rhs.smallest_width_dp)
                return lhs.smallest_width_dp > rhs.smallest_width_dp;
            const auto lhs_delta = width_dp - lhs.screen_width_dp +
                                   height_dp - lhs.screen_height_dp;
            const auto rhs_delta = width_dp - rhs.screen_width_dp +
                                   height_dp - rhs.screen_height_dp;
            if (lhs_delta != rhs_delta) return lhs_delta < rhs_delta;
            auto lhs_size = lhs.screen_layout & 0x0fU;
            auto rhs_size = rhs.screen_layout & 0x0fU;
            const auto lhs_raw_size = lhs_size;
            const auto rhs_raw_size = rhs_size;
            if (screen_size >= 2U) {
                if (lhs_size == 0U) lhs_size = 2U;
                if (rhs_size == 0U) rhs_size = 2U;
            }
            if (lhs_size != rhs_size) return lhs_size > rhs_size;
            if (lhs_raw_size != rhs_raw_size) return lhs_raw_size != 0U;
            if (lhs.orientation != rhs.orientation)
                return lhs.orientation != 0U;
            if (lhs.density != rhs.density)
                return density_better(lhs.density, rhs.density);
            if (lhs.sdk_version != rhs.sdk_version)
                return lhs.sdk_version > rhs.sdk_version;
            return std::tie(lhs.screen_layout, lhs.value_type, lhs.value_data,
                            lhs.string_value) <
                   std::tie(rhs.screen_layout, rhs.value_type, rhs.value_data,
                            rhs.string_value);
        };
        for (const auto& candidate : context.arsc.entries) {
            if (candidate.resource_id != resource_id ||
                (candidate.sdk_version != 0 && candidate.sdk_version > 19) ||
                (candidate.orientation != 0 &&
                 candidate.orientation != orientation) ||
                candidate.smallest_width_dp > smallest_dp ||
                candidate.screen_width_dp > width_dp ||
                candidate.screen_height_dp > height_dp ||
                ((candidate.screen_layout & 0x0fU) != 0U &&
                 (candidate.screen_layout & 0x0fU) > screen_size)) {
                continue;
            }
            if (entry == nullptr || better(candidate, *entry)) {
                entry = &candidate;
            }
        }
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

namespace {

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

void ApplyInflatedWidgetDefaults(dexvm::Interpreter& vm, DexVmAndroidContext& context, ui::UiNode& node) {
    const auto dp = [&context](const float value) {
        return static_cast<std::int32_t>(
            std::lround(value * context.ui_density));
    };
    const auto sp = [&context](const float value) {
        return value * context.ui_scaled_density;
    };
    if (node.kind == ui::UiClass::TextView) {
        node.text_size_px = sp(14.0F);
    } else if (node.kind == ui::UiClass::Button) {
        node.text_size_px = sp(14.0F);
        WriteAndroidViewMinimum(vm, context, ViewObjectForUiNode(context, node.id), {dp(64.0F), dp(48.0F)});
        node.gravity = 0x11U;
        node.clickable = true;
    } else if (node.kind == ui::UiClass::EditText) {
        node.text_size_px = sp(18.0F);
        WriteAndroidViewMinimum(vm, context, ViewObjectForUiNode(context, node.id), {0, dp(48.0F)});
        node.gravity = 0x10U;
        node.clickable = true;
    }
}

void ApplyTextAppearance(const DexVmAndroidContext& context,
                         ui::UiNode& node, const std::uint32_t resource_id) {
    // API 19 public.xml and styles.xml: the six base text appearances differ
    // here only by size; colors remain owned by the theme/app overrides.
    switch (resource_id) {
        case 0x01030042U:
        case 0x01030043U:
        case 0x010300fdU:
        case 0x010300feU:
            node.text_size_px = 22.0F * context.ui_scaled_density;
            return;
        case 0x01030044U:
        case 0x01030045U:
        case 0x010300ffU:
        case 0x01030100U:
            node.text_size_px = 18.0F * context.ui_scaled_density;
            return;
        case 0x01030046U:
        case 0x01030047U:
        case 0x01030101U:
        case 0x01030102U:
            node.text_size_px = 14.0F * context.ui_scaled_density;
            return;
        default: break;
    }
    const auto& style = ResolveUiResourceEntry(context, resource_id);
    if (!style.is_complex || style.type_name != "style") {
        throw std::runtime_error("TextView textAppearance is not a style");
    }
    for (const auto& item : style.bag) {
        if (item.name == 0x01010095U && item.value_type == kTypeDimension) {
            node.text_size_px = static_cast<float>(
                ComplexDimensionPx(context, item.value_data, true));
        } else if (item.name == 0x01010098U &&
                   item.value_type >= kTypeFirstColor &&
                   item.value_type <= kTypeLastColor) {
            node.text_color = AndroidColorToRgba(item.value_data);
        }
    }
}

void ApplyStyleAttribute(const DexVmAndroidContext& context,
                         const UiWidgetDescriptor& widget, ui::UiNode& node,
                         const loader::BinaryXmlAttribute& attribute) {
    if (attribute.value_type == kTypeAttribute &&
        attribute.data == 0x01010078U &&
        widget.dex_descriptor == "Landroid/widget/ProgressBar;") {
        // API 19 R.attr.progressBarStyleHorizontal. All registered API 19
        // platform themes resolve it to the horizontal ProgressBar family.
        // Progress state/rendering is a separate widget capability; accepting
        // this constructor style must not imply that capability.
        return;
    }
    if (attribute.value_type == kTypeReference &&
        (node.kind == ui::UiClass::TextView ||
         node.kind == ui::UiClass::Button ||
         node.kind == ui::UiClass::EditText)) {
        ApplyTextAppearance(context, node, attribute.data);
        return;
    }
    throw std::runtime_error(
        "UI style resource is outside the registered API 19 projection");
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

void ApplyAttribute(dexvm::Interpreter& vm, DexVmAndroidContext& context, const ui::UiNodeId node_id,
                    const loader::BinaryXmlAttribute& attribute, std::uint32_t& drawable_id) {
    if (attribute.namespace_uri.empty() && attribute.name == "style") return;
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
    } else if (name == "enabled") {
        node.enabled = attribute.data != 0U;
    } else if (name == "clickable") {
        context.ui_tree.SetClickable(node_id, attribute.data != 0U);
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
        node.orientation = node.kind == ui::UiClass::TableLayout
                               ? ui::Orientation::Vertical
                           : node.kind == ui::UiClass::TableRow
                               ? ui::Orientation::Horizontal
                               : attribute.data == 0U
                                     ? ui::Orientation::Horizontal
                                     : ui::Orientation::Vertical;
    } else if (name == "layout_weight") {
        if (attribute.value_type != kTypeFloat) {
            throw std::runtime_error("UI layout_weight is not a float");
        }
        node.layout.weight = std::bit_cast<float>(attribute.data);
    } else if (name == "padding") {
        const auto value = DimensionValue(context, attribute);
        WriteAndroidViewPadding(vm, context, ViewObjectForUiNode(context, node_id), {value, value, value, value});
    } else if (name == "paddingLeft") {
        const auto view = ViewObjectForUiNode(context, node_id);
        auto padding = ReadAndroidViewPadding(vm, context, view);
        padding.left = DimensionValue(context, attribute);
        WriteAndroidViewPadding(vm, context, view, padding, std::nullopt);
        dexvm::IntrinsicContext call{vm, view, {}};
        dexvm::IntrinsicCall(call).SetInt(context.view_java_layout_fields->padding_defined[0], 1);
    } else if (name == "paddingTop") {
        const auto view = ViewObjectForUiNode(context, node_id);
        auto padding = ReadAndroidViewPadding(vm, context, view);
        padding.top = DimensionValue(context, attribute);
        WriteAndroidViewPadding(vm, context, view, padding, std::nullopt);
    } else if (name == "paddingRight") {
        const auto view = ViewObjectForUiNode(context, node_id);
        auto padding = ReadAndroidViewPadding(vm, context, view);
        padding.right = DimensionValue(context, attribute);
        WriteAndroidViewPadding(vm, context, view, padding, std::nullopt);
        dexvm::IntrinsicContext call{vm, view, {}};
        dexvm::IntrinsicCall(call).SetInt(context.view_java_layout_fields->padding_defined[1], 1);
    } else if (name == "paddingBottom") {
        const auto view = ViewObjectForUiNode(context, node_id);
        auto padding = ReadAndroidViewPadding(vm, context, view);
        padding.bottom = DimensionValue(context, attribute);
        WriteAndroidViewPadding(vm, context, view, padding, std::nullopt);
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
    } else if (name == "minWidth") {
        const auto view = ViewObjectForUiNode(context, node_id);
        auto minimum = ReadAndroidViewMinimum(vm, context, view);
        minimum.width = DimensionValue(context, attribute);
        WriteAndroidViewMinimum(vm, context, view, minimum);
    } else if (name == "minHeight") {
        const auto view = ViewObjectForUiNode(context, node_id);
        auto minimum = ReadAndroidViewMinimum(vm, context, view);
        minimum.height = DimensionValue(context, attribute);
        WriteAndroidViewMinimum(vm, context, view, minimum);
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
    } else if (name == "textAppearance") {
        if (attribute.value_type != kTypeReference || attribute.data == 0U) {
            throw std::runtime_error(
                "TextView textAppearance is not a resource reference");
        }
        ApplyTextAppearance(context, node, attribute.data);
    } else if (name == "textSize") {
        const auto size = static_cast<float>(
            DimensionValue(context, attribute, true));
        static_cast<void>(ui::MeasureFixedText(node.text, size, 0, node.fallback_font.get()));
        node.text_size_px = size;
    } else if (name == "singleLine") {
        node.max_lines = attribute.data == 0U
                             ? std::numeric_limits<std::int32_t>::max()
                             : 1;
    } else if (name == "maxLines") {
        if (attribute.data == 0U ||
            attribute.data > static_cast<std::uint32_t>(
                                 std::numeric_limits<std::int32_t>::max())) {
            throw std::runtime_error("TextView maxLines is out of range");
        }
        node.max_lines = static_cast<std::int32_t>(attribute.data);
    } else if (name == "maxLength") {
        if (attribute.data > static_cast<std::uint32_t>(
                                 std::numeric_limits<std::int32_t>::max())) {
            throw std::runtime_error("TextView maxLength is out of range");
        }
        node.max_length = static_cast<std::int32_t>(attribute.data);
    } else if (name == "inputType") {
        node.numeric_input = (attribute.data & 0x0fU) == 0x02U;
    } else if (name == "background") {
        if (attribute.value_type == kTypeReference) {
            static_cast<void>(ResolveUiDrawable(context, attribute.data));
            node.background_color.reset();
            node.background_resource_id = attribute.data;
        } else if (attribute.value_type >= kTypeFirstColor &&
                   attribute.value_type <= kTypeLastColor) {
            node.background_color = AndroidColorToRgba(attribute.data);
            node.background_resource_id = 0;
        } else {
            throw std::runtime_error("UI background is not a drawable resource");
        }
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

std::string ResolveResourceString(const DexVmAndroidContext& context,
                                  const std::uint32_t resource_id) {
        const auto& entry = ResolveUiResourceEntry(context, resource_id);
    if (entry.value_type != kTypeString || !entry.string_value.has_value()) {
        throw std::runtime_error("resource is not a string");
    }
    return *entry.string_value;
}

std::u16string ResolveUiString(const DexVmAndroidContext& context,
                               const std::uint32_t resource_id) {
    return AsciiText(ResolveResourceString(context, resource_id));
}

std::uint32_t ResolveUiColor(const DexVmAndroidContext& context,
                             const std::uint32_t resource_id) {
    const auto& entry = ResolveUiResourceEntry(context, resource_id);
    if (entry.value_type < kTypeFirstColor ||
        entry.value_type > kTypeLastColor) {
        throw std::runtime_error("UI resource is not a color");
    }
    return AndroidColorToRgba(entry.value_data);
}

std::int32_t ResolveUiDimension(const DexVmAndroidContext& context,
                                const std::uint32_t resource_id,
                                const bool scaled) {
    const auto& entry = ResolveUiResourceEntry(context, resource_id);
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
    const auto density_bits = std::bit_cast<std::uint32_t>(context.ui_density);
    if (context.ui_bitmap_config_width != context.surface_width ||
        context.ui_bitmap_config_height != context.surface_height ||
        context.ui_bitmap_config_density_bits != density_bits) {
        context.ui_bitmaps.clear();
        context.ui_bitmap_config_width = context.surface_width;
        context.ui_bitmap_config_height = context.surface_height;
        context.ui_bitmap_config_density_bits = density_bits;
    }
    if (const auto cached = context.ui_bitmaps.find(resource_id);
        cached != context.ui_bitmaps.end()) {
        return cached->second;
    }
    const auto& drawable = ResolveUiResourceEntry(context, resource_id);
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
        // Android's compiled nine-patch chunk is stored as big-endian words
        // in the PNG. Keep stretch divisions and content padding beside the
        // shared immutable pixels; Drawable alpha/bounds remain per instance.
        const auto be32 = [&bytes](const std::size_t offset) {
            const auto byte = [&bytes](const std::size_t at) {
                return std::to_integer<std::uint32_t>(bytes[at]);
            };
            return (byte(offset) << 24U) | (byte(offset + 1) << 16U) |
                   (byte(offset + 2) << 8U) | byte(offset + 3);
        };
        for (std::size_t offset = 8; offset + 12 <= bytes.size();) {
            const auto size = be32(offset);
            if (size > bytes.size() - offset - 12U) break;
            if (std::to_integer<char>(bytes[offset + 4]) == 'n' &&
                std::to_integer<char>(bytes[offset + 5]) == 'p' &&
                std::to_integer<char>(bytes[offset + 6]) == 'T' &&
                std::to_integer<char>(bytes[offset + 7]) == 'c' &&
                size >= 48U &&
                std::to_integer<unsigned>(bytes[offset + 9]) == 2U &&
                std::to_integer<unsigned>(bytes[offset + 10]) == 2U) {
                const auto data = offset + 8U;
                bitmap->nine_patch = true;
                bitmap->nine_patch_padding = {
                    static_cast<std::int32_t>(be32(data + 12U)),
                    static_cast<std::int32_t>(be32(data + 20U)),
                    static_cast<std::int32_t>(be32(data + 16U)),
                    static_cast<std::int32_t>(be32(data + 24U))};
                bitmap->stretch_x = {
                    static_cast<std::int32_t>(be32(data + 32U)),
                    static_cast<std::int32_t>(be32(data + 36U))};
                bitmap->stretch_y = {
                    static_cast<std::int32_t>(be32(data + 40U)),
                    static_cast<std::int32_t>(be32(data + 44U))};
                break;
            }
            offset += 12U + size;
        }
    } else {
        throw std::runtime_error("UI resource is not a drawable");
    }
    const auto immutable = std::shared_ptr<const ui::UiBitmap>{bitmap};
    context.ui_bitmaps.emplace(resource_id, immutable);
    return immutable;
}

namespace android_intrinsics {
dx::VmObjectRef MakeResourceDrawable(dx::IntrinsicContext& call,
    const Context& context, const std::uint32_t resource_id) {
    const auto& entry = ResolveUiResourceEntry(*context, resource_id);
    if (entry.string_value && entry.string_value->ends_with(".xml")) {
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.drawable_resource", 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "XML Drawable objects are not implemented"};
    }
    const auto decoded = ResolveUiDrawable(*context, resource_id);
    const bool color = entry.value_type >= kTypeFirstColor && entry.value_type <= kTypeLastColor;
    if (decoded->nine_patch) {
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.drawable_resource", 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "resource NinePatch Drawable objects are not implemented"};
    }
    auto& vm = call.vm;
    const char* descriptor = color ? "Landroid/graphics/drawable/ColorDrawable;" : "Landroid/graphics/drawable/BitmapDrawable;";
    const auto object = vm.NewIntrinsicInstance(descriptor);
    const auto object_root = vm.ProtectReferences(std::array{object});
    dx::VmObjectRef bitmap;
    std::vector<dx::VmValue> args{dx::VmValue::Ref(object)};
    const char* signature = "(I)V";
    if (color) args.push_back(dx::VmValue::Int(static_cast<std::int32_t>(entry.value_data)));
    else {
        bitmap = vm.NewIntrinsicInstance("Landroid/graphics/Bitmap;");
        std::vector<std::uint32_t> argb;
        argb.reserve(decoded->rgba8.size() / 4U);
        for (std::size_t i = 0; i < decoded->rgba8.size(); i += 4U) {
            argb.push_back((static_cast<std::uint32_t>(decoded->rgba8[i + 3U]) << 24U) |
                (static_cast<std::uint32_t>(decoded->rgba8[i]) << 16U) |
                (static_cast<std::uint32_t>(decoded->rgba8[i + 1U]) << 8U) | decoded->rgba8[i + 2U]);
        }
        context->bitmaps[bitmap.Value()] = {decoded->width, decoded->height,
            std::make_shared<BitmapPixels>(decoded->width, decoded->height, 5, std::move(argb))};
        signature = "(Landroid/content/res/Resources;Landroid/graphics/Bitmap;)V";
        args.push_back(dx::VmValue::Ref(call.receiver)); args.push_back(dx::VmValue::Ref(bitmap));
    }
    const auto roots = vm.ProtectReferences(std::array{object, bitmap});
    const auto constructor = vm.Linker().FindDirectMethod(vm.Model().ObjectClass(object), "<init>", signature);
    if (!constructor) throw std::logic_error("resource Drawable constructor is unavailable");
    const auto outcome = vm.Call(*constructor, args);
    if (outcome.exception.IsValid()) throw dx::VmJavaThrow{
        vm.Linker().Class(outcome.exception_class).descriptor, outcome.exception_message, outcome.exception};
    context->ui_drawables[object.Value()].resource_id = resource_id;
    return object;
}

void BindImageDrawable(dx::Interpreter& vm, const Context& context,
    const dx::VmObjectRef view, const dx::VmObjectRef drawable) {
    const auto node = EnsureViewUiNode(*context, view, UiClassForObject(vm, view));
    if (drawable.IsValid() && !context->ui_drawables.contains(drawable.Value())) {
        if (auto* ledger = vm.Ledger()) ledger->RecordUnimplemented("dexvm.drawable_resource", 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "unregistered Drawable image backing"};
    }
    if (drawable.IsValid()) {
        const auto& state = context->ui_drawables.at(drawable.Value());
        if (!state.bitmap_drawable && !state.color && state.resource_id == 0) {
            if (auto* ledger = vm.Ledger()) ledger->RecordUnimplemented("dexvm.drawable_resource", 0);
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "Drawable image backing is not implemented"};
        }
        if (state.bitmap.IsValid()) {
            const auto bitmap = context->bitmaps.find(state.bitmap.Value());
            if (bitmap == context->bitmaps.end() || bitmap->second.recycled)
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Drawable bitmap is recycled"};
        }
    }
    if (const auto old = context->ui_view_images.find(view.Value()); old != context->ui_view_images.end()) {
        if (const auto state = context->ui_drawables.find(old->second.Value()); state != context->ui_drawables.end() && state->second.callback_node == node)
            state->second.callback_node.reset();
    }
    auto* projected = context->ui_tree.Get(node);
    projected->image_resource_id = 0; projected->image_bitmap.reset(); projected->image_color.reset();
    projected->image_alpha = 1.0F; projected->intrinsic = {};
    if (drawable.IsValid()) {
        context->ui_view_images[view.Value()] = drawable;
        context->ui_drawables.at(drawable.Value()).callback_node = node;
        RefreshAndroidImageDrawables(*context);
    } else context->ui_view_images.erase(view.Value());
    context->ui_tree.MarkLayoutDirty(node);
}
} // namespace android_intrinsics

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
        std::size_t depth = 1;
        for (auto cursor = output_parent; cursor >= 0; cursor = output_[static_cast<std::size_t>(cursor)].parent)
            if (++depth > ui::UiTree::kMaxDepth) throw std::runtime_error("expanded layout depth limit exceeded");
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
    const auto& entry = ResolveUiResourceEntry(context, layout_id);
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

namespace {
// Attribute parsing stays in the shared registry. Java LayoutParams fields become
// the sole mutable parameter state before the ordinary ViewGroup attach path.
void AssignInflatedParams(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                          dexvm::VmObjectRef parent, dexvm::VmObjectRef view,
                          ui::UiNodeId node) {
    using namespace android_intrinsics;
    if (!parent.IsValid()) return; // legacy synthetic roots are not ViewGroups
    const auto layout = context.ui_tree.Get(node)->layout;
    const auto params = CallAndroidMethod(vm, parent, "generateDefaultLayoutParams",
        "()Landroid/view/ViewGroup$LayoutParams;").ref;
    const auto roots = vm.ProtectReferences(std::array{parent, view, params});
    if (!params.IsValid()) throw dexvm::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "null generated LayoutParams"};
    const auto type = vm.Model().ObjectClass(params);
    const auto put = [&](const char* name, std::uint32_t bits, const char* descriptor = "I") {
        const auto field = vm.Linker().FindFieldRecursive(type, name, descriptor);
        if (!field) throw dexvm::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "unsupported inflated LayoutParams field: " + std::string(name)};
        vm.Model().InstanceSlots(params)[vm.Linker().Field(*field).slot] = {bits, dexvm::SlotTag::cat1};
    };
    const auto dim = [](const ui::DimensionSpec value) {
        return static_cast<std::uint32_t>(value.mode == ui::SizeMode::MatchParent ? -1 :
            value.mode == ui::SizeMode::WrapContent ? -2 : value.px);
    };
    put("width", dim(layout.width)); put("height", dim(layout.height));
    const auto is = [&](const char* descriptor) { return vm.Linker().IsAssignable(vm.Linker().ResolveDescriptor(descriptor), type); };
    if (is("Landroid/view/ViewGroup$MarginLayoutParams;")) {
        static_cast<void>(CallAndroidMethod(vm, params, "setMargins", "(IIII)V", {
            dexvm::VmValue::Int(layout.margin.left), dexvm::VmValue::Int(layout.margin.top),
            dexvm::VmValue::Int(layout.margin.right), dexvm::VmValue::Int(layout.margin.bottom)}));
    }
    if (layout.layout_gravity != 0 && (is("Landroid/widget/FrameLayout$LayoutParams;") || is("Landroid/widget/LinearLayout$LayoutParams;")))
        put("gravity", layout.layout_gravity);
    if (is("Landroid/widget/LinearLayout$LayoutParams;")) put("weight", std::bit_cast<std::uint32_t>(layout.weight), "F");
    if (is("Landroid/widget/RelativeLayout$LayoutParams;")) {
        const auto& r = layout.relative;
        const std::array<int, 16> rules{r.left_of.value_or(0), r.right_of.value_or(0),
            r.above.value_or(0), r.below.value_or(0), 0, r.align_left.value_or(0),
            r.align_top.value_or(0), r.align_right.value_or(0), r.align_bottom.value_or(0),
            r.align_parent_left ? -1 : 0, r.align_parent_top ? -1 : 0,
            r.align_parent_right ? -1 : 0, r.align_parent_bottom ? -1 : 0,
            r.center_in_parent ? -1 : 0, r.center_horizontal ? -1 : 0, r.center_vertical ? -1 : 0};
        for (std::size_t i = 0; i < rules.size(); ++i) if (rules[i] != 0)
            static_cast<void>(CallAndroidMethod(vm, params, "addRule", "(II)V", {
                dexvm::VmValue::Int(static_cast<int>(i)), dexvm::VmValue::Int(rules[i])}));
    }
    static_cast<void>(CallAndroidMethod(vm, view, "setLayoutParams", "(Landroid/view/ViewGroup$LayoutParams;)V", {dexvm::VmValue::Ref(params)}));
}

dexvm::VmObjectRef BuildUiElementsAt(
    dexvm::Interpreter& vm, DexVmAndroidContext& context,
    const std::span<const loader::BinaryXmlElement> elements,
    const dexvm::VmObjectRef inflater_context,
    const std::optional<ui::UiNodeId> parent, const bool attach) {
    const auto owner_roots = vm.ProtectReferences(std::array{inflater_context,
        parent ? ViewObjectForUiNode(context, *parent) : dexvm::VmObjectRef{}});
    std::vector<ui::UiNodeId> created;
    try {
        const auto expanded = ExpandUiIncludes(elements, [&context](std::uint32_t id) { return LoadLayout(context, id); });
        const auto document = std::span<const loader::BinaryXmlElement>{expanded};
        if (document.empty()) throw std::runtime_error("layout has no root element");
        std::size_t root_count{};
        for (std::size_t i = 0; i < document.size(); ++i) {
            if (document[i].parent < 0) { ++root_count; if (i != 0) throw std::runtime_error("layout root must be first"); }
            else if (static_cast<std::size_t>(document[i].parent) >= i) throw std::runtime_error("layout parent order is invalid");
        }
        if (root_count != 1) throw std::runtime_error("layout must have exactly one root element");
        const bool merge = document.front().name == "merge";
        if (merge && (!parent || !attach)) throw std::runtime_error("merge requires a root and attachToRoot=true");
        dexvm::VmObjectRef content_view;
        // Construct children in document order, finish each subtree before adding
        // it to its parent, and publish the document roots only after construction.
        const auto build = [&](auto&& self, std::size_t index,
                               std::optional<ui::UiNodeId> parent_node) -> dexvm::VmObjectRef {
            const auto& element = document[index];
            if (element.name == "merge") throw std::runtime_error("merge must be the layout root");
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
            const auto view_root = vm.ProtectReferences(std::array{view});
            const auto node = context.ui_tree.CreateNode(widget->kind);
            created.push_back(node);
            BindViewToUiNode(context, view, node);
            AssignViewContext(vm, context, view, inflater_context);
            const auto constructor = vm.Linker().FindDirectMethod(vm.Model().ObjectClass(view),
                "<init>", "(Landroid/content/Context;)V");
            if (!constructor) throw std::runtime_error("inflatable widget requires a Context constructor");
            const auto initialized = vm.Call(*constructor,
                std::array{dexvm::VmValue::Ref(view), dexvm::VmValue::Ref(inflater_context)});
            if (initialized.exception.IsValid())
                throw dexvm::VmJavaThrow{vm.Linker().Class(initialized.exception_class).descriptor,
                    initialized.exception_message, initialized.exception};
            ApplyInflatedWidgetDefaults(vm, context, *context.ui_tree.Get(node));
            InitializeDefaultViewBackground(vm, context, view, node);
            std::uint32_t drawable_id = element.src;
            const loader::BinaryXmlAttribute* style = nullptr;
            for (const auto& attribute : element.attributes) {
                if (!attribute.namespace_uri.empty() ||
                    attribute.name != "style") {
                    continue;
                }
                if (style != nullptr) {
                    throw std::runtime_error(
                        "UI element has duplicate style attributes");
                }
                style = &attribute;
            }
            if (style != nullptr) {
                try {
                    ApplyStyleAttribute(context, *widget,
                                        *context.ui_tree.Get(node), *style);
                } catch (const std::runtime_error&) {
                    if (auto* ledger = vm.Ledger(); ledger != nullptr) {
                        ledger->RecordUnimplemented(
                            "runtime.ui.inflate.attribute.style", 0);
                    }
                    throw;
                }
            }
            for (const auto& attribute : element.attributes) {
                if (attribute.namespace_uri.empty() &&
                    attribute.name == "style") {
                    continue;
                }
                try {
                    if (attribute.name == "textAppearance" && attribute.value_type == 2) {
                        auto resolved = attribute;
                        resolved.value_type = kTypeReference;
                        resolved.data = android_intrinsics::ResolveTextAppearanceAttribute(
                            vm, context, inflater_context, attribute.data);
                        ApplyAttribute(vm, context, node, resolved, drawable_id);
                    } else {
                        ApplyAttribute(vm, context, node, attribute, drawable_id);
                    }
                } catch (const std::runtime_error&) {
                    if (auto* ledger = vm.Ledger(); ledger != nullptr) {
                        ledger->RecordUnimplemented(
                            "runtime.ui.inflate.attribute." + attribute.name,
                            0);
                    }
                    throw;
                }
            }
            if (const auto* state = context.ui_tree.Get(node);
                state->background_resource_id != 0U) {
                const auto old = context.ui_view_backgrounds.find(view.Value());
                if (old != context.ui_view_backgrounds.end()) {
                    auto old_state = context.ui_drawables.find(old->second.Value());
                    if (old_state != context.ui_drawables.end() &&
                        old_state->second.callback_node == node) {
                        old_state->second.callback_node.reset();
                    }
                }
                const auto drawable = vm.NewIntrinsicInstance(
                    "Landroid/graphics/drawable/Drawable;");
                context.ui_drawables[drawable.Value()] = {
                    .resource_id = state->background_resource_id,
                    .callback_node = node};
                context.ui_view_backgrounds[view.Value()] = drawable;
            }
            if (element.id != 0 &&
                context.ui_tree.Get(node)->android_id < 0) {
                context.ui_tree.SetAndroidId(
                    node, static_cast<std::int32_t>(element.id));
            }

            if (parent_node) AssignInflatedParams(vm, context, ViewObjectForUiNode(context, *parent_node), view, node);
            if (drawable_id != 0U) {
                const auto image = ResolveUiDrawable(context, drawable_id);
                context.ui_tree.Get(node)->intrinsic = {image->width, image->height};
            }
            for (std::size_t child = index + 1; child < document.size(); ++child) {
                if (document[child].parent != static_cast<std::int32_t>(index)) continue;
                if (!vm.Linker().IsAssignable(vm.Linker().ResolveDescriptor("Landroid/view/ViewGroup;"), vm.Model().ObjectClass(view)))
                    throw std::runtime_error("inflated children require a ViewGroup parent");
                const auto child_view = self(self, child, node);
                static_cast<void>(android_intrinsics::CallAndroidMethod(vm, view, "addView",
                    "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
                    {dexvm::VmValue::Ref(child_view), dexvm::VmValue::Ref(context.ui_view_layout_params.at(child_view.Value()))}));
                if (FindViewUiNode(context, view.Value()) != node || !context.ui_tree.Get(node))
                    throw std::runtime_error("inflated parent retired during callback");
            }
            static_cast<void>(android_intrinsics::CallAndroidMethod(vm, view, "onFinishInflate", "()V"));
            if (FindViewUiNode(context, view.Value()) != node || !context.ui_tree.Get(node) || context.ui_tree.Get(node)->parent)
                throw std::runtime_error("inflated view retired or attached during callback");
            return view;
        };
        std::vector<dexvm::VmObjectRef> document_roots;
        if (merge) {
            for (std::size_t i = 1; i < document.size(); ++i)
                if (document[i].parent == 0) document_roots.push_back(build(build, i, parent));
        } else document_roots.push_back(build(build, 0, parent));
        if (document_roots.empty()) {
            // Empty merge is a valid append of zero children.
            return parent ? ViewObjectForUiNode(context, *parent) : dexvm::VmObjectRef{};
        }
        content_view = document_roots.front();
        const auto result_roots = vm.ProtectReferences(document_roots);
        if (parent && attach) {
            const auto parent_view = ViewObjectForUiNode(context, *parent);
            if (!context.ui_tree.Get(*parent)) throw std::runtime_error("inflation root retired during callback");
            for (const auto view : document_roots) {
                const auto node = FindViewUiNode(context, view.Value());
                if (!node) throw std::runtime_error("inflated root retired during callback");
                if (parent_view.IsValid()) {
                    static_cast<void>(android_intrinsics::CallAndroidMethod(vm, parent_view, "addView",
                        "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
                        {dexvm::VmValue::Ref(view), dexvm::VmValue::Ref(context.ui_view_layout_params.at(view.Value()))}));
                } else context.ui_tree.Attach(*parent, *node);
            }
        }
        return content_view;
    } catch (...) {
        // Do not reset existing content or reclaim an external View moved by a callback.
        for (auto it = created.rbegin(); it != created.rend(); ++it) {
            if (const auto* state = context.ui_tree.Get(*it)) {
                const auto children = state->children;
                for (const auto child : children)
                    if (std::find(created.begin(), created.end(), child) == created.end()) context.ui_tree.Detach(child);
                RetireViewUiSubtree(context, *it);
            }
        }
        throw;
    }
}
} // namespace

dexvm::VmObjectRef BuildUiElements(dexvm::Interpreter& vm, DexVmAndroidContext& context,
    std::span<const loader::BinaryXmlElement> elements, dexvm::VmObjectRef inflater_context,
    dexvm::VmObjectRef root, bool attach_to_root) {
    const auto roots = vm.ProtectReferences(std::array{root, inflater_context});
    std::optional<ui::UiNodeId> parent;
    if (root.IsValid()) {
        if (!vm.Linker().IsAssignable(vm.Linker().ResolveDescriptor("Landroid/view/ViewGroup;"), vm.Model().ObjectClass(root)))
            throw dexvm::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "inflation root is not a ViewGroup"};
        parent = EnsureViewUiNode(context, root, android_intrinsics::UiClassForObject(vm, root));
    }
    try {
        const auto view = BuildUiElementsAt(vm, context, elements, inflater_context, parent, attach_to_root);
        return root.IsValid() && attach_to_root ? root : view;
    } catch (const dexvm::VmJavaThrow&) { throw; }
      catch (const dexvm::DexVmError&) { throw; }
      catch (const std::runtime_error& error) {
        throw dexvm::VmJavaThrow{"Landroid/view/InflateException;", error.what()};
    }
}

dexvm::VmObjectRef InflateUiElements(dexvm::Interpreter& vm, DexVmAndroidContext& context,
    std::span<const loader::BinaryXmlElement> elements, dexvm::VmObjectRef inflater_context,
    std::optional<ui::UiNodeId> parent) {
    // Compatibility install API: its caller explicitly requests replacement.
    if (!parent) ResetViewUiState(context);
    else {
        const auto children = context.ui_tree.Get(*parent)->children;
        for (const auto child : children) RetireViewUiSubtree(context, child);
    }
    return BuildUiElementsAt(vm, context, elements, inflater_context,
        parent ? parent : std::optional{ActivityContentRoot(context)}, true);
}

dexvm::VmObjectRef BuildUiLayoutResource(dexvm::Interpreter& vm, DexVmAndroidContext& context,
    std::uint32_t layout_id, dexvm::VmObjectRef inflater_context, dexvm::VmObjectRef root, bool attach_to_root) {
    const auto roots = vm.ProtectReferences(std::array{inflater_context, root});
    // Resource lookup failures have the API19 Resources exception, not an empty View.
    const auto& entry = [&]() -> const loader::ArscEntry& {
        try { return ResolveUiResourceEntry(context, layout_id); }
        catch (const std::runtime_error& error) {
            throw dexvm::VmJavaThrow{"Landroid/content/res/Resources$NotFoundException;", error.what()};
        }
    }();
    if (entry.type_name != "layout" || !entry.string_value)
        throw dexvm::VmJavaThrow{"Landroid/content/res/Resources$NotFoundException;", "resource is not a layout file"};
    try {
        const auto expanded = ExpandUiIncludes(LoadLayout(context, layout_id),
            [&context](std::uint32_t id) { return LoadLayout(context, id); }, layout_id);
        return BuildUiElements(vm, context, expanded, inflater_context, root, attach_to_root);
    } catch (const dexvm::VmJavaThrow&) { throw; }
      catch (const dexvm::DexVmError&) { throw; }
      catch (const std::runtime_error& error) {
        throw dexvm::VmJavaThrow{"Landroid/view/InflateException;", error.what()};
    }
}

dexvm::VmObjectRef InflateUiLayoutResource(
    dexvm::Interpreter& vm, DexVmAndroidContext& context,
    const std::uint32_t layout_id,
    const dexvm::VmObjectRef inflater_context,
    const std::optional<ui::UiNodeId> parent) {
    try {
        const auto root = LoadLayout(context, layout_id);
        const auto expanded = ExpandUiIncludes(
            root,
            [&context](const std::uint32_t included_id) {
                return LoadLayout(context, included_id);
            },
            layout_id);
        return InflateUiElements(vm, context, expanded, inflater_context, parent);
    } catch (...) {
        if (parent) {
            const auto children = context.ui_tree.Get(*parent)->children;
            for (const auto child : children) RetireViewUiSubtree(context, child);
        } else ResetViewUiState(context);
        throw;
    }
}

}  // namespace ogplay::runtime

// ---- migrated from support_widget_dispatch.cpp ----
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

[[nodiscard]] std::optional<ui::UiNodeId> HitClickable(
    const DexVmAndroidContext& context, const ui::UiNodeId id, const float x,
    const float y) {
    const auto* node = context.ui_tree.Get(id);
    if (node == nullptr || !node->window_visible || node->visibility != ui::Visibility::Visible ||
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
    const bool has_click = click != context.ui_click_listeners.end() &&
                           click->second.IsValid();
    const bool has_touch = touch != context.ui_touch_listeners.end() &&
                           touch->second.IsValid();
    return (has_touch || (has_click && node->clickable))
               ? std::optional<ui::UiNodeId>{id}
               : std::nullopt;
}

void CollectTouchReceivers(const DexVmAndroidContext& context,
                           const ui::UiNodeId id, const float x,
                           const float y,
                           std::vector<std::uint64_t>& receivers) {
    const auto* node = context.ui_tree.Get(id);
    if (node == nullptr || !context.ui_tree.IsVisible(id) ||
        node->visibility != ui::Visibility::Visible || !node->enabled ||
        !Contains(node->screen_frame, x, y)) {
        return;
    }
    for (auto child = node->children.rbegin(); child != node->children.rend();
         ++child) {
        CollectTouchReceivers(context, *child, x, y, receivers);
    }
    const auto view = ViewObjectForUiNode(context, id);
    if (view.IsValid()) receivers.push_back(view.Value());
}

}  // namespace

}  // namespace android_intrinsics

std::optional<std::uint64_t> FindClickableViewAt(dexvm::Interpreter& vm, DexVmAndroidContext& context, const float x,
                                                 const float y) {
    EnsureAndroidUiTraversal(vm, context, AndroidUiTraversalReason::BeforeInput, AndroidUiTraversalPhase::ResolveOnly);
    const auto hit = android_intrinsics::HitClickable(
        context, context.ui_tree.Root(), x, y);
    if (!hit.has_value()) return std::nullopt;
    const auto view = ViewObjectForUiNode(context, *hit);
    return view.IsValid() ? std::optional<std::uint64_t>{view.Value()}
                          : std::nullopt;
}

std::vector<std::uint64_t> FindTouchReceiversAt(dexvm::Interpreter& vm, DexVmAndroidContext& context, const float x,
                                                const float y) {
    EnsureAndroidUiTraversal(vm, context, AndroidUiTraversalReason::BeforeInput, AndroidUiTraversalPhase::ResolveOnly);
    std::vector<std::uint64_t> receivers;
    android_intrinsics::CollectTouchReceivers(
        context, context.ui_tree.Root(), x, y, receivers);
    return receivers;
}

namespace {
using dexvm::VmObjectRef;
using dexvm::VmValue;
constexpr auto kTouchSignature = "(Landroid/view/MotionEvent;)Z";

int TouchAction(dexvm::Interpreter& vm, VmObjectRef event) {
    if (!event.IsValid()) throw dexvm::VmJavaThrow{"Ljava/lang/NullPointerException;", "event == null"};
    const auto action = android_intrinsics::CallAndroidMethod(vm, event, "getAction", "()I").AsInt() & 255;
    if (action != 0 && action != 1 && action != 2 && action != 3 && action != 5 && action != 6) {
        if (auto* ledger = vm.Ledger()) ledger->RecordUnimplemented("runtime.ui_pointer_dispatch", 0);
        throw dexvm::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "touch action outside non-split gesture support"};
    }
    return action;
}
bool TouchCall(dexvm::Interpreter& vm, VmObjectRef view, VmObjectRef event, const char* method) {
    return android_intrinsics::CallAndroidMethod(vm, view, method, kTouchSignature, {VmValue::Ref(event)}).AsInt() != 0;
}
bool TouchLive(DexVmAndroidContext& context, VmObjectRef view) {
    const auto node = FindViewUiNode(context, view.Value());
    return node && context.ui_tree.IsAttached(*node) && context.ui_tree.IsVisible(*node);
}
void ClearTouch(DexVmAndroidContext& context) {
    context.ui_touch_capture = 0;
    context.ui_touch_targets.clear();
    context.ui_touch_clicks.clear();
}
void CancelTouch(dexvm::Interpreter& vm, VmObjectRef view, VmObjectRef event) {
    const auto field = vm.Linker().FindFieldRecursive(vm.Model().ObjectClass(event), "mOgplaySnapshot", "[J");
    if (!field) throw std::logic_error("MotionEvent snapshot field missing");
    const auto data = VmObjectRef{vm.Model().InstanceSlots(event)[vm.Linker().Field(*field).slot].bits};
    const auto previous = vm.Model().GetPrimitiveElement(data, 0);
    vm.Model().SetPrimitiveElement(data, 0, 3);
    try { static_cast<void>(TouchCall(vm, view, event, "dispatchTouchEvent")); }
    catch (...) { vm.Model().SetPrimitiveElement(data, 0, previous); throw; }
    vm.Model().SetPrimitiveElement(data, 0, previous);
}
}

bool DispatchAndroidViewTouch(dexvm::Interpreter& vm, DexVmAndroidContext& context,
    VmObjectRef view, VmObjectRef event, const bool group) {
    const auto action = TouchAction(vm, event);
    const auto roots = vm.ProtectReferences(std::array{view, event});
    EnsureAndroidUiTraversal(vm, context, AndroidUiTraversalReason::BeforeInput, AndroidUiTraversalPhase::ResolveOnly);
    const auto node = FindViewUiNode(context, view.Value());
    if (!node) return false;
    const auto key = view.Value();
    if (action == 0) {
        context.ui_touch_targets.erase(key);
        context.ui_touch_clicks.erase(key);
    }
    if (group) {
        auto target = context.ui_touch_targets.contains(key) ? context.ui_touch_targets.at(key) : VmObjectRef{};
        if (target.IsValid() && !TouchLive(context, target)) {
            context.ui_touch_targets.erase(key);
            target = VmObjectRef{};
        }
        const bool intercepted = (action == 0 || target.IsValid()) &&
            TouchCall(vm, view, event, "onInterceptTouchEvent");
        if (target.IsValid()) {
            if (intercepted || action == 3) {
                CancelTouch(vm, target, event);
                context.ui_touch_targets.erase(key);
                context.ui_touch_capture = 0;
                // API19 interception cancels the child on this event; the
                // parent starts its own onTouchEvent with the next event.
                return true;
            }
            const bool handled = TouchCall(vm, target, event, "dispatchTouchEvent");
            if (action == 1) context.ui_touch_targets.erase(key);
            return handled;
        }
        if (!intercepted && action == 0) {
            const auto x = android_intrinsics::CallAndroidMethod(vm, event, "getX", "()F").AsFloat();
            const auto y = android_intrinsics::CallAndroidMethod(vm, event, "getY", "()F").AsFloat();
            const auto children = context.ui_tree.Get(*node)->children;
            for (auto child = children.rbegin(); child != children.rend(); ++child) {
                const auto receiver = ViewObjectForUiNode(context, *child);
                if (!receiver.IsValid() || !ViewContainsPoint(vm, context, receiver.Value(), x, y)) continue;
                if (TouchCall(vm, receiver, event, "dispatchTouchEvent")) {
                    if (TouchLive(context, receiver)) context.ui_touch_targets[key] = receiver;
                    if (!context.ui_touch_capture) context.ui_touch_capture = receiver.Value();
                    return true;
                }
            }
        }
    }
    // API19 View dispatch: an enabled OnTouchListener gets first refusal.
    const auto state = *context.ui_tree.Get(*node);
    bool listener_handled = false;
    const auto listener = context.ui_touch_listeners.find(*node);
    if (state.enabled && listener != context.ui_touch_listeners.end() && listener->second.IsValid()) {
        listener_handled = android_intrinsics::CallAndroidMethod(vm, listener->second, "onTouch",
            "(Landroid/view/View;Landroid/view/MotionEvent;)Z", {VmValue::Ref(view), VmValue::Ref(event)}).AsInt() != 0;
    }
    const bool handled = listener_handled || TouchCall(vm, view, event, "onTouchEvent");
    auto click = context.ui_touch_clicks[key];
    if (action == 0) click.eligible = state.enabled && state.clickable;
    click.consumed |= listener_handled;
    if (action == 1 && click.eligible && !click.consumed &&
        context.ui_click_listeners.contains(*node)) {
        const auto x = android_intrinsics::CallAndroidMethod(vm, event, "getX", "()F").AsFloat();
        const auto y = android_intrinsics::CallAndroidMethod(vm, event, "getY", "()F").AsFloat();
        if (ViewContainsPoint(vm, context, key, x, y)) {
            if (const auto error = InvokeViewOnClick(vm, context, key))
                throw dexvm::VmJavaThrow{"Ljava/lang/RuntimeException;", *error};
        }
    }
    if (action == 1 || action == 3) context.ui_touch_clicks.erase(key);
    else context.ui_touch_clicks[key] = click;
    if (action == 0 && handled) context.ui_touch_capture = key;
    return handled;
}

bool DispatchContentTouchEvent(dexvm::Interpreter& vm, DexVmAndroidContext& context,
    VmObjectRef event) {
    const auto action = TouchAction(vm, event);
    const auto roots = vm.ProtectReferences(std::array{event});
    if (action == 0 || context.ui_touch_activity != context.activity) {
        ClearTouch(context);
        context.ui_touch_activity = context.activity;
    }
    if (action != 0 && context.ui_touch_capture) {
        if (!TouchLive(context, VmObjectRef{static_cast<std::uint32_t>(context.ui_touch_capture)})) ClearTouch(context);
    }
    bool handled = false;
    if (action == 0) {
        EnsureAndroidUiTraversal(vm, context, AndroidUiTraversalReason::BeforeInput,
                                 AndroidUiTraversalPhase::ResolveOnly);
        const auto root = ActivityContentRoot(context, context.activity);
        const auto children = context.ui_tree.Get(root)->children;
        const auto x = android_intrinsics::CallAndroidMethod(vm, event, "getX", "()F").AsFloat();
        const auto y = android_intrinsics::CallAndroidMethod(vm, event, "getY", "()F").AsFloat();
        for (auto child = children.rbegin(); child != children.rend(); ++child) {
            const auto receiver = ViewObjectForUiNode(context, *child);
            if (!receiver.IsValid() || !ViewContainsPoint(vm, context, receiver.Value(), x, y)) continue;
            if (TouchCall(vm, receiver, event, "dispatchTouchEvent")) {
                if (TouchLive(context, receiver)) context.ui_touch_targets[0] = receiver;
                if (!context.ui_touch_capture) context.ui_touch_capture = receiver.Value();
                handled = true;
                break;
            }
        }
    } else if (context.ui_touch_targets.contains(0)) {
        handled = TouchCall(vm, context.ui_touch_targets.at(0), event, "dispatchTouchEvent");
    }
    if (action == 1 || action == 3) ClearTouch(context);
    return handled;
}

DeepTouchDispatchResult DispatchDeepTouchEvent(dexvm::Interpreter& vm,
    DexVmAndroidContext& context, const std::int32_t action, const float x, const float y,
    const std::uint64_t captured_view, const AndroidBoundaryInput* snapshot) {
    // Capture belongs to the context; the legacy argument is only a projection.
    static_cast<void>(captured_view);
    auto adjusted = snapshot ? *snapshot : AndroidBoundaryInput{};
    if (snapshot && action == 3) adjusted.action = 3;
    const auto event = snapshot ? MakeMotionEvent(vm, adjusted) : MakeMotionEvent(vm, action, x, y, 0);
    try {
        const auto handled = DispatchContentTouchEvent(vm, context, event);
        return {.handled = handled, .captured_view = context.ui_touch_capture};
    } catch (const dexvm::VmJavaThrow& error) {
        return {.error = "View dispatchTouchEvent failed: " + error.message};
    }
}

bool ViewContainsPoint(dexvm::Interpreter& vm, DexVmAndroidContext& context, const std::uint64_t handle, const float x,
                       const float y) {
    EnsureAndroidUiTraversal(vm, context, AndroidUiTraversalReason::BeforeInput, AndroidUiTraversalPhase::ResolveOnly);
    const auto node = FindViewUiNode(context, handle);
    if (!node.has_value()) return false;
    const auto* state = context.ui_tree.Get(*node);
    return state != nullptr && context.ui_tree.IsAttached(*node) &&
           state->visibility == ui::Visibility::Visible &&
           state->enabled &&
           android_intrinsics::Contains(
               state->screen_frame, x, y);
}

ViewTouchResult InvokeViewOnTouch(dexvm::Interpreter& vm,
                                  DexVmAndroidContext& context,
                                  const std::uint64_t handle,
                                  const std::int32_t action, const float x,
                                  const float y, const AndroidBoundaryInput* snapshot) {
    const auto node = FindViewUiNode(context, handle);
    if (!node.has_value() || context.ui_tree.Get(*node) == nullptr) return {};
    const auto found = context.ui_touch_listeners.find(*node);
    if (found == context.ui_touch_listeners.end() ||
        !found->second.IsValid()) return {};
    const auto view = ViewObjectForUiNode(context, *node);
    auto adjusted = snapshot ? *snapshot : AndroidBoundaryInput{};
    if (snapshot && action == 3) adjusted.action = 3;
    const auto event = snapshot ? MakeMotionEvent(vm, adjusted) : MakeMotionEvent(vm, action, x, y, 0);
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
    const float y, bool click_eligible, bool touch_consumed, const AndroidBoundaryInput* snapshot) {
    constexpr std::int32_t kActionDown = 0;
    constexpr std::int32_t kActionUp = 1;
    constexpr std::int32_t kActionCancel = 3;
    const auto touch = InvokeViewOnTouch(vm, context, handle, action, x, y, snapshot);
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
    const auto is_clickable = [&]() {
        if (!node.has_value()) return false;
        const auto* state = context.ui_tree.Get(*node);
        return state != nullptr && state->clickable;
    }();
    if (action == kActionDown) click_eligible = has_live_click && is_clickable;
    if (action == kActionCancel) {
        return {.handled = touch_consumed || click_eligible,
                .keep_capture = false,
                .click_eligible = false,
                .touch_consumed = touch_consumed};
    }
    if (action == kActionUp) {
        if (!touch_consumed && click_eligible && has_live_click && is_clickable &&
            ViewContainsPoint(vm, context, handle, x, y)) {
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

// Dialog Java owns protocol state; this is the temporary, visible UI projection.
namespace ogplay::runtime {
namespace {
using Modal = DexVmAndroidContext::DialogPresentation;

bool DialogOwnerVisible(const DexVmAndroidContext& context, const dexvm::VmObjectRef owner) {
    if (context.process_stopping) return false;
    if (context.activity.IsValid() && context.activity != owner) return false;
    if (context.activity_stack.empty()) return true;
    const auto found = std::find_if(context.activity_stack.begin(), context.activity_stack.end(),
        [owner](const auto& record) { return record.object == owner; });
    return found != context.activity_stack.end() && found->visible;
}
Modal* TopDialog(DexVmAndroidContext& context) {
    for (auto item = context.dialog_presentations.rbegin(); item != context.dialog_presentations.rend(); ++item)
        if (item->visible && DialogOwnerVisible(context, item->owner) && context.ui_tree.IsVisible(item->root)) return &*item;
    return nullptr;
}
void FocusModal(DexVmAndroidContext& context, const Modal& modal) {
    const auto focused = context.ui_tree.Focused();
    for (const auto node : modal.buttons) if (node && node == focused) return;
    for (const auto node : modal.buttons) if (node) {
        static_cast<void>(context.ui_tree.RequestFocus(*node, false)); return;
    }
}
bool ContainsDialogButton(const DexVmAndroidContext& context, ui::UiNodeId button, float x, float y) {
    const auto* node = context.ui_tree.Get(button);
    if (!node || !context.ui_tree.IsVisible(button)) return false;
    const auto& rect = node->screen_frame;
    return x >= static_cast<float>(rect.left) && x < static_cast<float>(rect.right) &&
           y >= static_cast<float>(rect.top) && y < static_cast<float>(rect.bottom);
}
} // namespace

bool HasAndroidModalDialog(const DexVmAndroidContext& context) {
    for (const auto& modal : context.dialog_presentations)
        if (modal.visible && DialogOwnerVisible(context, modal.owner) && context.ui_tree.IsVisible(modal.root)) return true;
    return false;
}
void ReleaseAndroidDialogs(DexVmAndroidContext& context) {
    for (const auto& modal : context.dialog_presentations)
        if (context.ui_tree.Get(modal.root)) context.ui_tree.DestroySubtree(modal.root);
    context.dialog_presentations.clear();
    context.dialog_pointer_active = false; context.dialog_pressed_button.reset(); context.dialog_keys_down.clear();
}
void DismissAndroidActivityDialogs(dexvm::Interpreter& vm, DexVmAndroidContext& context,
    const dexvm::VmObjectRef owner) {
    for (unsigned pass = 0; pass < 8; ++pass) {
        std::vector<dexvm::VmObjectRef> dialogs;
        for (const auto& modal : context.dialog_presentations) if (modal.owner == owner) dialogs.push_back(modal.dialog);
        if (dialogs.empty()) return;
        const auto roots = vm.ProtectReferences(dialogs);
        for (const auto dialog : dialogs)
            static_cast<void>(android_intrinsics::CallAndroidMethod(vm, dialog, "dismiss", "()V"));
    }
    throw dexvm::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Dialog callbacks did not settle during Activity retirement"};
}
void SynchronizeAndroidDialogs(DexVmAndroidContext& context) {
    std::erase_if(context.dialog_presentations, [&](const Modal& modal) {
        const bool alive = context.activity_stack.empty() || std::any_of(context.activity_stack.begin(), context.activity_stack.end(),
            [&](const auto& record) { return record.object == modal.owner; });
        if (alive) return false;
        if (context.ui_tree.Get(modal.root)) context.ui_tree.DestroySubtree(modal.root);
        return true;
    });
    for (const auto& modal : context.dialog_presentations)
        context.ui_tree.SetWindowVisible(modal.root, modal.visible && DialogOwnerVisible(context, modal.owner));
    if (const auto* modal = TopDialog(context)) FocusModal(context, *modal);
}

namespace android_intrinsics {
void DismissAndroidDialog(DexVmAndroidContext& context, const dx::VmObjectRef dialog) {
    const auto found = std::find_if(context.dialog_presentations.begin(), context.dialog_presentations.end(),
        [dialog](const Modal& modal) { return modal.dialog == dialog; });
    if (found == context.dialog_presentations.end()) return;
    const auto prior_focus = found->prior_focus;
    if (context.ui_tree.Get(found->root)) context.ui_tree.DestroySubtree(found->root);
    context.dialog_presentations.erase(found);
    context.dialog_pressed_button.reset();
    if (prior_focus && context.ui_tree.Get(*prior_focus)) static_cast<void>(context.ui_tree.RequestFocus(*prior_focus, false));
    if (const auto* top = TopDialog(context)) FocusModal(context, *top);
}
void SetAndroidDialogVisible(DexVmAndroidContext& context, const dx::VmObjectRef dialog, bool visible) {
    for (auto& modal : context.dialog_presentations) if (modal.dialog == dialog) {
        modal.visible = visible;
        context.ui_tree.SetWindowVisible(modal.root, visible && DialogOwnerVisible(context, modal.owner));
        if (!visible) {
            context.dialog_pressed_button.reset();
            if (modal.prior_focus && context.ui_tree.Get(*modal.prior_focus))
                static_cast<void>(context.ui_tree.RequestFocus(*modal.prior_focus, false));
        }
    }
    if (const auto* top = TopDialog(context)) FocusModal(context, *top);
}
std::int32_t FocusAndroidDialog(DexVmAndroidContext& context, const dx::VmObjectRef dialog, const std::int32_t direction) {
    auto* modal = TopDialog(context);
    if (!modal || modal->dialog != dialog) return 0;
    std::vector<std::size_t> buttons;
    std::size_t current{};
    for (std::size_t i = 0; i < 3; ++i) if (modal->buttons[i]) {
        if (modal->buttons[i] == context.ui_tree.Focused()) current = buttons.size();
        buttons.push_back(i);
    }
    if (buttons.empty()) return 0;
    if (direction != 0) current = direction > 0 ? (current + 1) % buttons.size() : (current + buttons.size() - 1) % buttons.size();
    static_cast<void>(context.ui_tree.RequestFocus(*modal->buttons[buttons[current]], false));
    return -static_cast<std::int32_t>(buttons[current]) - 1;
}
void ShowAndroidDialog(dx::IntrinsicContext& call, const Context& context, dx::IntrinsicFieldHandle generation) {
    const auto unsupported = [&call](const char* reason) -> void {
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("runtime.ui.dialog.presentation", 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", reason};
    };
    if (call.vm.CurrentContextToken() != 1) unsupported("Dialog presentation requires the root guest thread");
    auto owner = call.arguments[1].ref;
    const auto activity = call.vm.Linker().ResolveDescriptor("Landroid/app/Activity;");
    auto source = call.arguments[0].ref;
    for (unsigned depth = 0; !owner.IsValid() && source.IsValid() && depth < 16; ++depth) {
        if (call.vm.Linker().IsAssignable(activity, call.vm.Model().ObjectClass(source))) { owner = source; break; }
        const auto wrapper = call.vm.Linker().ResolveDescriptor("Landroid/content/ContextWrapper;");
        if (!call.vm.Linker().IsAssignable(wrapper, call.vm.Model().ObjectClass(source))) break;
        const auto next = CallAndroidMethod(call.vm, source, "getBaseContext", "()Landroid/content/Context;").ref;
        if (source == next) break;
        source = next;
    }
    if (!owner.IsValid() || !call.vm.Linker().IsAssignable(activity, call.vm.Model().ObjectClass(owner)))
        unsupported("Dialog requires an Activity owner");
    const auto owner_root = call.vm.ProtectReferences(std::array{owner});
    if (context->process_stopping) unsupported("Dialog presentation during process stop");
    if (!context->activity_stack.empty() && !std::any_of(context->activity_stack.begin(), context->activity_stack.end(),
        [owner](const auto& record) { return record.object == owner; })) unsupported("Dialog Activity is retired");
    const auto text = [&](dx::VmObjectRef value, std::size_t maximum) {
        if (!value.IsValid()) return std::u16string{};
        const auto string_type = call.vm.Linker().ResolveDescriptor("Ljava/lang/String;");
        if (!call.vm.Linker().IsAssignable(string_type, call.vm.Model().ObjectClass(value)))
            unsupported("Dialog supports plain String text only");
        auto result = call.vm.Model().StringValue(value);
        if (result.size() > maximum) unsupported("Dialog text exceeds the bounded presentation budget");
        return result;
    };
    const auto title = text(call.arguments[2].ref, 512);
    const auto message = text(call.arguments[3].ref, 4096);
    const auto labels = call.arguments[4].ref;
    if (!labels.IsValid() || call.vm.Model().ArrayLength(labels) != 3) unsupported("Dialog button inventory is invalid");
    std::array<std::u16string, 3> button_labels;
    for (JniSize i = 0; i < 3; ++i) button_labels[static_cast<std::size_t>(i)] = text(call.vm.Model().GetObjectElement(labels, i), 128);
    if (dx::IntrinsicCall(call).GetLong(generation) != call.arguments[5].AsLong()) return;
    const auto previous = std::find_if(context->dialog_presentations.begin(), context->dialog_presentations.end(),
        [&](const Modal& modal) { return modal.dialog == call.receiver; });
    if (previous == context->dialog_presentations.end() && context->dialog_presentations.size() >= 8)
        unsupported("Dialog window limit exceeded");
    const auto prior_focus = previous == context->dialog_presentations.end() ? context->ui_tree.Focused() : previous->prior_focus;
    const auto density = context->ui_density;
    if (!std::isfinite(density) || density <= 0 || density > 8 || context->surface_width < 48 || context->surface_height < 48)
        unsupported("Dialog viewport is unavailable or too small");
    auto& tree = context->ui_tree;
    const auto root = tree.CreateNode(ui::UiClass::FrameLayout);
    Modal modal{.dialog = call.receiver, .owner = owner, .root = root, .buttons = {}, .prior_focus = prior_focus};
    try {
        auto* overlay = tree.Get(root);
        overlay->modal_layer = true;
        overlay->layout.width.mode = ui::SizeMode::MatchParent; overlay->layout.height.mode = ui::SizeMode::MatchParent;
        overlay->background_color = 0x00000088U;
        tree.Attach(tree.Root(), root);
        const auto panel = tree.CreateNode(ui::UiClass::LinearLayout);
        auto* body = tree.Get(panel);
        body->orientation = ui::Orientation::Vertical;
        body->layout.width = {ui::SizeMode::Fixed, std::min(static_cast<std::int32_t>(480 * density), static_cast<std::int32_t>(context->surface_width) - 32)};
        body->layout.height.mode = ui::SizeMode::WrapContent; body->layout.layout_gravity = 0x11;
        body->background_color = 0xffffffffU;
        const auto padding = static_cast<std::int32_t>(12 * density);
        body->padding = {padding, padding, padding, padding};
        tree.Attach(root, panel);
        const auto add_text = [&](const std::u16string& value, float size, bool bold) {
            if (value.empty()) return;
            const auto id = tree.CreateNode(ui::UiClass::TextView);
            auto* node = tree.Get(id); node->text = value; node->text_color = 0x000000ffU;
            node->text_size_px = size * density; node->text_style = bold ? 1U : 0U;
            node->layout.width.mode = ui::SizeMode::MatchParent; node->layout.height.mode = ui::SizeMode::WrapContent;
            node->padding.bottom = static_cast<std::int32_t>(8 * density);
            tree.Attach(panel, id);
        };
        add_text(title, 18, true); add_text(message, 14, false);
        const auto row = tree.CreateNode(ui::UiClass::LinearLayout);
        tree.Get(row)->orientation = ui::Orientation::Horizontal;
        tree.Get(row)->layout.width.mode = ui::SizeMode::MatchParent;
        tree.Get(row)->layout.height.mode = ui::SizeMode::WrapContent;
        tree.Get(row)->gravity = 0x05;
        tree.Attach(panel, row);
        for (const std::size_t i : {2U, 1U, 0U}) if (!button_labels[i].empty()) {
            const auto id = tree.CreateNode(ui::UiClass::Button);
            modal.buttons[i] = id;
            auto* node = tree.Get(id); node->text = button_labels[i]; node->text_size_px = 14 * density;
            node->text_color = 0x000000ffU; node->background_color = 0xddddddffU;
            node->padding = {padding, padding / 2, padding, padding / 2};
            node->focusable = true; node->focusable_in_touch_mode = true;
            node->layout.width.mode = ui::SizeMode::WrapContent; node->layout.height.mode = ui::SizeMode::WrapContent;
            tree.Attach(row, id);
        }
        EnsureAndroidUiTraversal(call.vm, *context, AndroidUiTraversalReason::HostDialog,
                                 AndroidUiTraversalPhase::ResolveOnly);
        const auto bounds = tree.Get(panel)->screen_frame;
        if (bounds.top < 8 || bounds.bottom > static_cast<std::int32_t>(context->surface_height) - 8)
            unsupported("Dialog content cannot fit the bounded viewport");
        for (const auto button : modal.buttons) if (button && tree.Get(*button)->screen_frame.right > bounds.right - padding)
            unsupported("Dialog buttons cannot fit the bounded viewport");
        // No guest callbacks occur during geometry construction. Commit after validation.
        if (previous != context->dialog_presentations.end()) {
            tree.DestroySubtree(previous->root);
            *previous = modal;
        } else context->dialog_presentations.push_back(modal);
        context->dialog_pressed_button.reset();
        tree.SetWindowVisible(root, DialogOwnerVisible(*context, owner));
        FocusModal(*context, modal);
    } catch (...) {
        if (tree.Get(root)) tree.DestroySubtree(root);
        throw;
    }
}
} // namespace android_intrinsics

// Shared timed KeyEvent materialization for normal and modal input.
dexvm::VmObjectRef MakeAndroidKeyEvent(dexvm::Interpreter& vm, const AndroidBoundaryInput& input) {
    using namespace dexvm;
    const auto event = vm.NewIntrinsicInstance("Landroid/view/KeyEvent;");
    const auto roots = vm.ProtectReferences(std::array{event});
    const auto constructor = vm.Linker().FindDirectMethod(vm.Model().ObjectClass(event), "<init>", "(JJIIIIIIII)V");
    if (!constructor) throw DexVmError(DexVmErrorReason::unresolved_reference, "KeyEvent timed constructor is unavailable");
    const auto result = vm.Call(*constructor, std::vector<VmValue>{VmValue::Ref(event),
        VmValue::Long(input.down_time_ns / 1000000), VmValue::Long(input.event_time_ns / 1000000),
        VmValue::Int(input.action), VmValue::Int(input.code), VmValue::Int(input.repeat_count),
        VmValue::Int(input.meta_state), VmValue::Int(input.device_id), VmValue::Int(input.scan_code),
        VmValue::Int(input.flags), VmValue::Int(input.source)});
    if (result.exception.IsValid()) throw VmJavaThrow{vm.Linker().Class(result.exception_class).descriptor, result.exception_message, result.exception};
    SetAndroidKeyEventUnicode(vm, event, input.unicode_char);
    return event;
}

bool DispatchAndroidDialogInput(dexvm::Interpreter& vm, DexVmAndroidContext& context, const AndroidBoundaryInput& input) {
    using namespace dexvm;
    SynchronizeAndroidDialogs(context);
    auto* top = TopDialog(context);
    if (input.type == AndroidBoundaryInputType::key) {
        if (!top && !context.dialog_keys_down.contains(input.code)) return false;
        if (input.action == 0) context.dialog_keys_down.insert(input.code);
        if (input.action == 1) context.dialog_keys_down.erase(input.code);
        if (top) {
            const auto dialog = top->dialog;
            const auto event = MakeAndroidKeyEvent(vm, input);
            const auto roots = vm.ProtectReferences(std::array{dialog, event});
            static_cast<void>(android_intrinsics::CallAndroidMethod(vm, dialog, "dispatchKeyEvent", "(Landroid/view/KeyEvent;)Z", {VmValue::Ref(event)}));
        }
        return true;
    }
    const auto action = input.action & 0xff;
    if (!top && !context.dialog_pointer_active) return false;
    EnsureAndroidUiTraversal(vm, context, AndroidUiTraversalReason::HostDialog, AndroidUiTraversalPhase::ResolveOnly);
    if (action == 0) {
        context.dialog_pointer_active = true; context.dialog_pressed_button.reset();
        if (top) for (const auto button : top->buttons)
            if (button && ContainsDialogButton(context, *button, input.x, input.y)) {
                context.dialog_pressed_button = button;
                static_cast<void>(context.ui_tree.RequestFocus(*button, true));
            }
    } else if (action == 1) {
        const auto pressed = context.dialog_pressed_button;
        context.dialog_pressed_button.reset(); context.dialog_pointer_active = false;
        if (top && pressed && ContainsDialogButton(context, *pressed, input.x, input.y)) {
            const auto dialog = top->dialog;
            for (std::size_t i = 0; i < 3; ++i) if (top->buttons[i] == pressed) {
                const auto roots = vm.ProtectReferences(std::array{dialog});
                static_cast<void>(android_intrinsics::CallAndroidMethod(vm, dialog, "performButtonClick", "(I)V", {VmValue::Int(-static_cast<std::int32_t>(i) - 1)}));
                break;
            }
        }
    } else if (action == 2 && context.dialog_pressed_button &&
               !ContainsDialogButton(context, *context.dialog_pressed_button, input.x, input.y)) {
        context.dialog_pressed_button.reset();
    } else if (action == 3) { context.dialog_pressed_button.reset(); context.dialog_pointer_active = false; }
    return true;
}
} // namespace ogplay::runtime
