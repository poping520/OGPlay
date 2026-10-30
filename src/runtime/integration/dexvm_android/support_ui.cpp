#include "ogplay/video/rgba_canvas.h"
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

void AssignViewContext(dexvm::Interpreter& vm, DexVmAndroidContext& context, const dexvm::VmObjectRef view,
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
void ProjectScrollbarState(dexvm::Interpreter& vm, DexVmAndroidContext& context, dexvm::VmObjectRef view, bool visible,
                           std::int32_t flags) {
    const auto slot = [&](dexvm::VmObjectRef object, const char* name, const char* type) -> dexvm::Slot& {
        const auto field = vm.Linker().FindFieldRecursive(vm.Model().ObjectClass(object), name, type);
        if (!field) throw std::runtime_error("ROM scrollbar projection field missing");
        vm.RequireImplementedField(*field);
        return vm.Model().InstanceSlots(object)[vm.Linker().Field(*field).slot];
    };
    const auto cache = dexvm::VmObjectRef{slot(view, "mScrollCache", "Landroid/view/View$ScrollabilityCache;").bits};
    if (!cache.IsValid()) return;
    const auto roots = vm.ProtectReferences(std::array{view, cache});
    auto state = slot(cache, "state", "I").bits;
    if (state == 1 && slot(cache, "fadeScrollBars", "Z").bits) {
        const auto field = vm.Linker().FindFieldRecursive(vm.Model().ObjectClass(cache), "fadeStartTime", "J");
        const auto first = vm.Linker().Field(*field).slot;
        const auto slots = vm.Model().InstanceSlots(cache);
        const auto deadline =
            static_cast<std::int64_t>(slots[first].bits | (static_cast<std::uint64_t>(slots[first + 1].bits) << 32));
        const auto found = context.scrollbar_fade_deadlines.find(view.Value());
        if (found == context.scrollbar_fade_deadlines.end() || found->second != deadline) {
            const auto shared = std::shared_ptr<DexVmAndroidContext>(&context, [](DexVmAndroidContext*) {});
            if (context.view_root_handler.IsValid())
                android_intrinsics::RemoveHandlerWork(shared, context.view_root_handler, std::nullopt, cache, true,
                                                      dexvm::VmObjectRef{}, false);
            dexvm::IntrinsicContext call{vm, view, {}};
            if (!android_intrinsics::PostViewRunnable(
                    call, shared, view, cache, std::max<std::int64_t>(0, deadline - context.uptime_millis.load())))
                throw std::runtime_error("scrollbar fade scheduling rejected");
            context.scrollbar_fade_deadlines[view.Value()] = deadline;
        }
    }
    if (state == 2) {
        const auto interpolator =
            dexvm::VmObjectRef{slot(cache, "scrollBarInterpolator", "Landroid/graphics/Interpolator;").bits};
        const auto result = android_intrinsics::CallAndroidMethod(
                                vm, interpolator, "timeToValues", "(I[F)Landroid/graphics/Interpolator$Result;",
                                {dexvm::VmValue::Int(static_cast<std::int32_t>(context.uptime_millis.load())),
                                 dexvm::VmValue::Ref(dexvm::VmObjectRef{})})
                                .ref;
        if (android_intrinsics::CallAndroidMethod(vm, result, "ordinal", "()I").AsInt() == 2) {
            slot(cache, "state", "I") = {0, dexvm::SlotTag::cat1};
            context.scrollbar_fade_deadlines.erase(view.Value());
            state = 0;
        } else {
            static_cast<void>(android_intrinsics::CallAndroidMethod(vm, view, "postInvalidateOnAnimation", "()V"));
        }
    }
    if (visible && (flags & 0x300) && state != 0)
        if (auto* ledger = vm.Ledger()) ledger->RecordUnimplemented("dexvm.view_scrollbar_draw", 0);
}

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
    {
        if (vm.CurrentContextToken() != 1U || context.ui_java_owner_thread != std::this_thread::get_id())
            throw dexvm::VmJavaThrow{"Ljava/lang/IllegalStateException;", "View initialization requires owner thread"};
        const auto type = vm.Linker().ResolveDescriptor("Landroid/view/View;");
        const auto field = vm.Linker().FindFieldRecursive(type, "mMeasuredWidth", "I");
        if (!field || vm.Linker().Field(*field).is_unimplemented)
            throw dexvm::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                     "JavaTraversal requires its paired measure payload"};
        const auto kind = android_intrinsics::UiClassForObject(vm, view);
        const auto frame = vm.Linker().ResolveDescriptor("Landroid/widget/FrameLayout;");
        const auto linear = vm.Linker().ResolveDescriptor("Landroid/widget/LinearLayout;");
        const auto relative = vm.Linker().ResolveDescriptor("Landroid/widget/RelativeLayout;");
        const auto text = vm.Linker().ResolveDescriptor("Landroid/widget/TextView;");
        const auto image = vm.Linker().ResolveDescriptor("Landroid/widget/ImageView;");
        const auto video = vm.Linker().ResolveDescriptor("Landroid/widget/VideoView;");
        // AbsoluteLayout keeps the FrameLayout host kind; ROM alone owns guest geometry.
        const auto absolute = vm.Linker().ResolveDescriptor("Landroid/widget/AbsoluteLayout;");
        // TextView uses its reviewed pure leaf adapter, never host tree layout.
        if (kind != ui::UiClass::View &&
            !(kind == ui::UiClass::FrameLayout && vm.Linker().IsAssignable(absolute, vm.Model().ObjectClass(view))) &&
            !(kind == ui::UiClass::VideoView && vm.Linker().IsAssignable(video, vm.Model().ObjectClass(view))) &&
            !((kind == ui::UiClass::ImageView || kind == ui::UiClass::ImageButton) &&
              vm.Linker().IsAssignable(image, vm.Model().ObjectClass(view))) &&
            !((kind == ui::UiClass::TextView || kind == ui::UiClass::Button || kind == ui::UiClass::EditText) &&
              vm.Linker().IsAssignable(text, vm.Model().ObjectClass(view))) &&
            !((kind == ui::UiClass::ScrollView || kind == ui::UiClass::HorizontalScrollView) &&
              vm.Linker().IsAssignable(frame, vm.Model().ObjectClass(view))) &&
            !(kind == ui::UiClass::FrameLayout && vm.Linker().IsAssignable(frame, vm.Model().ObjectClass(view))) &&
            !(kind == ui::UiClass::LinearLayout && vm.Linker().IsAssignable(linear, vm.Model().ObjectClass(view))) &&
            !(kind == ui::UiClass::RelativeLayout &&
              vm.Linker().IsAssignable(relative, vm.Model().ObjectClass(view)))) {
            if (auto* ledger = vm.Ledger()) ledger->RecordUnimplemented("dexvm.view_measure_nonleaf", 0);
            throw dexvm::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                     "JavaTraversal View kind has no reviewed construction adapter"};
        }
    }
    dexvm::IntrinsicContext call{vm, view, {}};
    dexvm::IntrinsicCall fields(call);
    // View.java:3438. Context prebinding is separate from construction:
    // a failed constructor must never suppress a later resource exception.
    const auto owner = fields.GetRef(*context.view_context_field);
    if (!owner.IsValid())
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
    {
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
        fields.SetInt(context.view_java_hierarchy_fields->id, -1);
        fields.SetInt(context.view_java_hierarchy_fields->label_for_id, -1);
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

dexvm::VmObjectRef AndroidUiTraversalRoot(dexvm::Interpreter& vm, DexVmAndroidContext& context) {
    if (context.application_window_root_override.IsValid()) return context.application_window_root_override;
    // The Window owns this strong reference. These tokens only read its identity.
    if (context.activity.IsValid() && context.ui_java_activity_window && context.ui_java_window_content) {
        dexvm::IntrinsicContext activity_call{vm, context.activity, {}};
        const auto window = dexvm::IntrinsicCall(activity_call).GetRef(*context.ui_java_activity_window);
        if (window.IsValid()) {
            dexvm::IntrinsicContext window_call{vm, window, {}};
            const auto root = dexvm::IntrinsicCall(window_call).GetRef(*context.ui_java_window_content);
            if (root.IsValid()) return root;
        }
    }
    // A missing Activity is the explicit VD-04/05 standalone fixture path.
    return context.activity.IsValid() ? dexvm::VmObjectRef{} : context.content_view;
}

bool AndroidViewBelongsToRoot(dexvm::Interpreter& vm, DexVmAndroidContext& context, dexvm::VmObjectRef view,
                              const bool visible) {
    std::set<dexvm::VmObjectRef> seen;
    while (view.IsValid()) {
        if (!vm.Model().IsValidRef(view)) return false;
        if (!seen.insert(view).second || seen.size() > ui::UiTree::kMaxNodes)
            throw dexvm::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Cyclic Java View parent chain"};
        if (!vm.Linker().IsAssignable(vm.Linker().ResolveDescriptor("Landroid/view/View;"),
                                      vm.Model().ObjectClass(view)))
            throw dexvm::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "System ViewParent is not enabled"};
        dexvm::IntrinsicContext call{vm, view, {}};
        dexvm::IntrinsicCall fields(call);
        if (visible && (fields.GetInt(context.view_java_measure_fields->view_flags) & 0x0c) == 8) return false;
        if (view == AndroidUiTraversalRoot(vm, context)) return true;
        view = fields.GetRef(context.view_java_hierarchy_fields->parent);
    }
    return false;
}

void ScrollAndroidViewBy(dexvm::Interpreter& vm, DexVmAndroidContext& context, const dexvm::VmObjectRef view,
                         const std::int32_t delta_y, const std::int32_t delta_x) {
    dexvm::VmExecutionLockScope lock(vm.ExecutionLock());
    if (vm.CurrentContextToken() != 1U || context.ui_java_owner_thread != std::this_thread::get_id() ||
        context.ui_layout_dispatching)
        throw dexvm::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                 "Scroll input requires Java owner outside layout"};
    const auto type = vm.Linker().ResolveDescriptor("Landroid/widget/ScrollView;");
    const auto horizontal = vm.Linker().ResolveDescriptor("Landroid/widget/HorizontalScrollView;");
    if ((!vm.Linker().IsAssignable(type, vm.Model().ObjectClass(view)) &&
         !vm.Linker().IsAssignable(horizontal, vm.Model().ObjectClass(view))) ||
        !AndroidViewBelongsToRoot(vm, context, view, true) || !context.ui_tree.PublishedProjection())
        throw dexvm::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                 "Scroll input requires a published attached scroll view"};
    const auto roots = vm.ProtectReferences(std::array{view});
    const auto target = *context.ui_tree.ProjectionTarget();
    static_cast<void>(android_intrinsics::CallAndroidMethod(
        vm, view, "scrollBy", "(II)V", {dexvm::VmValue::Int(delta_x), dexvm::VmValue::Int(delta_y)}));
    // No requestLayout or measure/layout: child local frames and measured sizes stay Java-owned.
    CommitAndroidMeasuredRootProjection(vm, context, AndroidUiTraversalRoot(vm, context), target,
                                        context.ui_measure_epoch + 1);
}

namespace {
ui::UiGeometryProjection CollectAndroidRootProjection(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                                                      dexvm::VmObjectRef root, const ui::UiProjectionTarget& target,
                                                      std::uint64_t epoch, bool prepare_render) {
    const auto roots = vm.ProtectReferences(std::array{root});
    ui::UiGeometryProjection projection{target,
                                        epoch,
                                        {{target.root,
                                          std::nullopt,
                                          {0, 0, target.metrics.width, target.metrics.height},
                                          {target.metrics.width, target.metrics.height},
                                          ui::Visibility::Visible,
                                          {}}}};
    // No per-View construction cache owns window visibility. The synthetic
    // partition root carries the Activity lifecycle fact for the whole batch.
    projection.window_visible = context.activity_stack.empty();
    for (const auto& record : context.activity_stack)
        if (record.object == context.activity) projection.window_visible = record.visible;
    std::set<dexvm::VmObjectRef> seen;
    const auto collect = [&](auto&& self, dexvm::VmObjectRef view, dexvm::VmObjectRef expected_parent,
                             ui::UiNodeId parent, std::size_t depth, float ancestor_alpha) -> void {
        if (!seen.insert(view).second || depth > ui::UiTree::kMaxDepth || seen.size() >= ui::UiTree::kMaxNodes)
            throw dexvm::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Invalid Java View hierarchy"};
        const auto node = FindViewUiNode(context, view.Value());
        if (!node) throw dexvm::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Unbound Java child"};
        dexvm::IntrinsicContext call{vm, view, {}};
        dexvm::IntrinsicCall fields(call);
        const auto& h = *context.view_java_hierarchy_fields;
        const auto& m = *context.view_java_measure_fields;
        if (fields.GetRef(h.parent) != expected_parent)
            throw dexvm::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Java child parent mismatch"};
        const auto bits = fields.GetInt(m.view_flags) & 0x0c;
        const auto visibility = bits == 8   ? ui::Visibility::Gone
                                : bits == 4 ? ui::Visibility::Invisible
                                            : ui::Visibility::Visible;
        const auto& f = m.frame;
        ui::UiProjectedNode entry{*node,
                                  parent,
                                  {fields.GetInt(f[0]), fields.GetInt(f[1]), fields.GetInt(f[2]), fields.GetInt(f[3])},
                                  {fields.GetInt(m.measured[0]) & 0xffffff, fields.GetInt(m.measured[1]) & 0xffffff},
                                  visibility,
                                  ReadAndroidViewPadding(vm, context, view)};
        android_intrinsics::RequireAndroidAlphaOnly(vm, context, view);
        const auto info = fields.GetRef(m.unsupported_state[1]);
        float own_alpha = 1.0F;
        if (info.IsValid()) {
            const auto field = vm.Linker().FindFieldRecursive(vm.Model().ObjectClass(info), "mAlpha", "F");
            own_alpha = std::bit_cast<float>(
                static_cast<std::uint32_t>(vm.Model().InstanceSlots(info)[vm.Linker().Field(*field).slot].bits));
        }
        // onSetAlpha=true delegates opacity to the View subclass, as in software View.draw.
        entry.alpha = ancestor_alpha * ((fields.GetInt(m.flags[0]) & 0x40000) ? 1.0F : own_alpha);
        entry.scroll_x = fields.GetInt(m.scroll[0]);
        entry.scroll_y = fields.GetInt(m.scroll[1]);
        entry.android_id = fields.GetInt(h.id);
        const auto view_flags = fields.GetInt(m.view_flags);
        entry.enabled = (view_flags & 0x20) == 0;
        entry.clickable = (view_flags & 0x4000) != 0;
        entry.focusable = (view_flags & 1) != 0;
        entry.focusable_in_touch_mode = (view_flags & 0x40000) != 0;
        const bool group = vm.Linker().IsAssignable(vm.Linker().ResolveDescriptor("Landroid/view/ViewGroup;"),
                                                    vm.Model().ObjectClass(view));
        if (group) {
            const auto flags = fields.GetInt(context.view_group_java_fields->flags);
            entry.clip_children = (flags & 1) != 0;
            entry.clip_to_padding = (flags & 2) != 0;
        }
        const auto kind = context.ui_tree.Get(*node)->kind;
        if (prepare_render) {
            ProjectScrollbarState(vm, context, view, visibility == ui::Visibility::Visible && entry.alpha > 0,
                                  view_flags);
            auto& target = *context.ui_tree.Get(*node);
            target.background_color.reset();
            target.background_resource_id = 0;
            target.background_alpha = 1.0F;
            const auto background = fields.GetRef((*context.view_java_input_fields)[1]);
            const auto skin = context.ui_drawables.find(background.Value());
            if (skin != context.ui_drawables.end() && skin->second.rom_skin) {
                const auto& state = skin->second;
                const auto selected = state.skin_children.empty() ? background
                                      : state.skin_current >= 0
                                          ? state.skin_children.at(static_cast<std::size_t>(state.skin_current))
                                          : dexvm::VmObjectRef{};
                const auto leaf = context.ui_drawables.find(selected.Value());
                if (leaf == context.ui_drawables.end() || !leaf->second.leaf_pixels ||
                    !leaf->second.leaf_pixels->nine_patch) {
                    if (auto* ledger = vm.Ledger()) ledger->RecordUnimplemented("dexvm.rom_skin_draw", 0);
                    throw dexvm::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                             "ROM skin pixels unavailable"};
                }
                // Freeze the selected immutable leaf into the existing bitmap plane.
                // Java remains the owner of selector state and all View geometry.
                context.ui_bitmaps[leaf->second.resource_id] = leaf->second.leaf_pixels;
                target.background_resource_id = leaf->second.resource_id;
                target.background_alpha = state.alpha / 255.0F;
            } else if (background.IsValid()) {
                const auto type = vm.Model().ObjectClass(background);
                if (vm.Linker().IsAssignable(vm.Linker().ResolveDescriptor("Landroid/graphics/drawable/ColorDrawable;"),
                                             type)) {
                    const auto color = static_cast<std::uint32_t>(
                        android_intrinsics::CallAndroidMethod(vm, background, "getColor", "()I").AsInt());
                    if ((color >> 24U) != 0) target.background_color = AndroidColorToRgba(color);
                } else {
                    if (auto* ledger = vm.Ledger()) ledger->RecordUnimplemented("dexvm.view_background_draw", 0);
                    if (auto* logger = vm.Log())
                        logger->Write(core::LogLevel::warn, "ui.background.unreviewed",
                                      "unreviewed View background Drawable", {},
                                      {{"drawable", vm.Linker().Class(type).descriptor}},
                                      {.mode = core::RateLimitMode::first_and_count});
                }
            }
        }
        if (prepare_render && (kind == ui::UiClass::ImageView || kind == ui::UiClass::ImageButton))
            android_intrinsics::ProjectAndroidImage(vm, context, view);
        if (prepare_render &&
            (kind == ui::UiClass::TextView || kind == ui::UiClass::Button || kind == ui::UiClass::EditText) &&
            visibility != ui::Visibility::Gone) {
            const auto& refs = context.view_adapter_references.at(view.Value());
            const auto cached = refs.text_measure_cache.find(entry.measured.width);
            auto& text = *context.ui_tree.Get(*node);
            if (cached != refs.text_measure_cache.end() && cached->second.line_plan) {
                text.text_line_plan = cached->second.line_plan;
            } else {
                const auto type = vm.Model().ObjectClass(view);
                const auto measure = vm.Linker().FindVtableIndex(type, "onMeasure", "(II)V");
                if (vm.Linker().Method(vm.Linker().Class(type).vtable.at(*measure)).owner ==
                    vm.Linker().ResolveDescriptor("Landroid/widget/TextView;"))
                    throw dexvm::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                             "Text input changed after measurement"};
                // A guest onMeasure override may set its own dimensions without
                // calling super. Prepare only its raster plan on the owner,
                // after Java layout; do not call the adapter or write geometry.
                ui::TextLeafInput input;
                input.text = text.text;
                input.size_px = text.text_size_px;
                input.style = text.text_style;
                input.fallback_font = text.fallback_font;
                input.max_lines = text.max_lines;
                input.padding = entry.padding;
                input.java_text_view = true;
                input.single_line = refs.text_single_line;
                if (input.single_line) std::replace(input.text.begin(), input.text.end(), u'\n', u' ');
                for (std::size_t i = 0; i < 4; ++i)
                    input.compound[i] = {text.compound_drawables[i].width, text.compound_drawables[i].height};
                text.text_line_plan = ui::MeasureTextLeaf(input, {ui::MeasureMode::Exactly, entry.measured.width},
                                                          {ui::MeasureMode::Exactly, entry.measured.height})
                                          .line_plan;
            }
        }
        projection.nodes.push_back(entry);
        if (!group) return;
        const auto& g = *context.view_group_java_fields;
        const auto children = fields.GetRef(g.children);
        const auto count = fields.GetInt(g.count);
        if (!children.IsValid() || count < 0 || count > vm.Model().ArrayLength(children) ||
            static_cast<std::size_t>(count) > ui::UiTree::kMaxChildren)
            throw dexvm::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Invalid Java children array/count"};
        for (std::int32_t i = 0; i < count; ++i) {
            const auto child = vm.Model().GetObjectElement(children, i);
            if (!child.IsValid()) throw dexvm::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Null Java child"};
            self(self, child, view, *node, depth + 1, *entry.alpha);
        }
    };
    collect(collect, root, dexvm::VmObjectRef{}, target.root, 0, 1.0F);
    return projection;
}
}  // namespace

std::set<dexvm::VmObjectRef>& AndroidUiMeasureRequests(dexvm::Interpreter& vm, DexVmAndroidContext& context) {
    if (vm.CurrentContextToken() != 1U || context.ui_java_owner_thread != std::this_thread::get_id())
        throw dexvm::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Layout requests require Java owner"};
    return context.ui_window_schedules[AndroidUiTraversalRoot(vm, context)].requests;
}

std::shared_ptr<const ui::UiTree> ReadAndroidUiGeometry(dexvm::Interpreter& vm, DexVmAndroidContext& context) {
    dexvm::VmExecutionLockScope lock(vm.ExecutionLock());
    if (vm.CurrentContextToken() != 1U || context.ui_java_owner_thread != std::this_thread::get_id())
        throw dexvm::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Geometry projection requires Java owner"};
    const auto root = AndroidUiTraversalRoot(vm, context);
    if (!root.IsValid() || !FindViewUiNode(context, root.Value()))
        throw dexvm::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Geometry requires a bound window root"};
    const auto size = context.ui_java_active_metrics.value_or(
        ui::Size{static_cast<std::int32_t>(context.surface_width), static_cast<std::int32_t>(context.surface_height)});
    const ui::UiMetrics metrics{size.width, size.height};
    const auto epoch = context.ui_measure_epoch + 1;
    const auto metrics_revision =
        context.ui_tree.ProjectionTarget() ? context.ui_tree.ProjectionTarget()->metrics_revision + 1 : 1;
    const ui::UiProjectionTarget target{context.ui_tree.Root(), context.ui_tree.Generation(),
                                        context.ui_content_revision, metrics_revision, metrics};
    auto projection = CollectAndroidRootProjection(vm, context, root, target, epoch, false);
    ui::UiTree temporary(context.ui_tree);
    temporary.SetProjectionTarget(target);
    temporary.CommitProjection(projection);
    return temporary.PublishedProjection();
}

namespace {
std::string DescribeProducer(dexvm::Interpreter& vm, DexVmAndroidContext& context, const dexvm::VmObjectRef producer) {
    const auto root = AndroidUiTraversalRoot(vm, context);
    const auto node = FindViewUiNode(context, producer.Value());
    const auto projection = context.ui_tree.PublishedProjection();
    const auto runtime = context.gl_surface_runtimes.find(producer.Value());
    std::string result = "view=" + std::to_string(producer.Value()) + " bound=" + std::to_string(node.has_value()) +
                         " in_projection=" + std::to_string(node && projection && projection->Get(*node)) +
                         " projection_available=" + std::to_string(bool(projection)) +
                         " root=" + std::to_string(root.Value()) +
                         " visible=" + std::to_string(node && context.ui_tree.IsVisible(*node)) +
                         " selected=" + std::to_string(context.gl_surface_renderer_view == producer);
    result += " projection_chain=[";
    auto projected = node;
    for (std::size_t depth = 0; projection && projected && depth <= ui::UiTree::kMaxDepth; ++depth) {
        const auto* state = projection->Get(*projected);
        if (!state) break;
        result += "{node=" + std::to_string(projected->Value()) +
                  ",window_visible=" + std::to_string(state->window_visible) +
                  ",visibility=" + std::to_string(static_cast<int>(state->visibility)) + "}";
        projected = state->parent;
    }
    result += "]";
    if (runtime != context.gl_surface_runtimes.end()) {
        const auto& state = *runtime->second;
        result += " owner=" + std::to_string(state.owner.Value()) +
                  " renderer=" + std::to_string(state.renderer.Value()) +
                  " surface_available=" + std::to_string(state.surface_available) +
                  " stopped=" + std::to_string(state.stopped);
    } else
        result += " retired=1";
    std::set<dexvm::VmObjectRef> seen;
    auto view = producer;
    dexvm::VmObjectRef first_break{}, window{};
    std::string termination = "null";
    result += " chain=[";
    const auto view_class = vm.Linker().ResolveDescriptor("Landroid/view/View;");
    while (view.IsValid()) {
        if (!seen.insert(view).second || seen.size() > ui::UiTree::kMaxDepth) {
            termination = "cycle_or_depth_limit";
            if (!first_break.IsValid()) first_break = view;
            break;
        }
        if (!vm.Model().IsValidRef(view)) {
            termination = "collected";
            if (!first_break.IsValid()) first_break = view;
            break;
        }
        const auto type = vm.Model().ObjectClass(view);
        result += "{view=" + std::to_string(view.Value()) + ",class=" + vm.Linker().Class(type).descriptor;
        const auto bound = FindViewUiNode(context, view.Value());
        result += ",bound=" + std::to_string(bound.has_value());
        if (!vm.Linker().IsAssignable(view_class, type) || !context.view_java_hierarchy_fields ||
            !context.view_java_measure_fields) {
            result += ",fields=unavailable}";
            termination = "non_view_or_missing_fields";
            if (!first_break.IsValid()) first_break = view;
            break;
        }
        dexvm::IntrinsicContext call{vm, view, {}};
        dexvm::IntrinsicCall fields(call);
        const auto parent = fields.GetRef(context.view_java_hierarchy_fields->parent);
        const auto& measure = *context.view_java_measure_fields;
        const auto visibility = fields.GetInt(measure.view_flags) & 0x0c;
        result += ",id=" + std::to_string(fields.GetInt(context.view_java_hierarchy_fields->id)) +
                  ",visibility=" + std::to_string(visibility) + ",parent=" + std::to_string(parent.Value()) +
                  ",parent_null=" + std::to_string(!parent.IsValid()) +
                  ",measured=" + std::to_string(fields.GetInt(measure.measured[0]) & 0xffffff) + "x" +
                  std::to_string(fields.GetInt(measure.measured[1]) & 0xffffff) + "}";
        for (const auto& record : context.activity_stack)
            if (record.java_root == view) window = record.object;
        if (!first_break.IsValid() && (!bound || visibility != 0 || (!parent.IsValid() && view != root)))
            first_break = view;
        if (view == root) {
            termination = "root";
            break;
        }
        view = parent;
    }
    dexvm::VmObjectRef java_window{};
    if (window.IsValid() && context.ui_java_activity_window) {
        dexvm::IntrinsicContext call{vm, window, {}};
        java_window = dexvm::IntrinsicCall(call).GetRef(*context.ui_java_activity_window);
    }
    result += "] window=" + std::to_string(java_window.Value()) +
              " first_break=" + std::to_string(first_break.Value()) +
              " window_activity=" + std::to_string(window.Value()) + " termination=" + termination;
    return result;
}
}  // namespace

void RecordAndroidSurfaceDiagnostic(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                                    const std::string_view category, const std::string& reason,
                                    std::vector<core::LogField> fields, const core::RateLimitPolicy& rate_limit) {
    const dexvm::VmExecutionLockScope lock(vm.ExecutionLock());
    std::string key = std::string(category) + ": " + reason;
    for (const auto& field : fields)
        if (field.key == "reason" || field.key == "phase")
            if (const auto* value = std::get_if<std::string>(&field.value)) key += " / " + *value;
    auto& total = context.ui_diagnostic_totals[key];
    ++total.count;
    total.epoch = context.ui_measure_epoch;
    total.tick = context.ui_owner_tick;
    if (auto* logger = vm.Log())
        logger->Write(core::LogLevel::info, category, reason, {}, std::move(fields), rate_limit);
}

void RecordAndroidProducerDiagnostic(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                                     const dexvm::VmObjectRef view, const std::string& reason) {
    const dexvm::VmExecutionLockScope lock(vm.ExecutionLock());
    auto description = "reason=" + reason + " " + DescribeProducer(vm, context, view);
    auto& previous = context.ui_producer_diagnostics[view.Value()];
    if (previous == description) return;
    previous = description;
    RecordAndroidSurfaceDiagnostic(vm, context, "ui.surface.producer_state", "producer state changed",
                                   {{"view", std::uint64_t(view.Value())},
                                    {"epoch", context.ui_measure_epoch},
                                    {"tick", context.ui_owner_tick},
                                    {"state", description}},
                                   {.mode = core::RateLimitMode::none});
}

void SummarizeAndroidUiDiagnostics(dexvm::Interpreter& vm, DexVmAndroidContext& context) {
    const dexvm::VmExecutionLockScope lock(vm.ExecutionLock());
    if (context.ui_diagnostic_summary_emitted) return;
    context.ui_diagnostic_summary_emitted = true;
    auto* logger = vm.Log();
    if (!logger) return;
    const core::RateLimitPolicy unlimited{.mode = core::RateLimitMode::none};
    logger->Write(core::LogLevel::info, "ui.diagnostics.summary", "UI diagnostics before teardown", {},
                  {{"epoch", context.ui_measure_epoch}, {"tick", context.ui_owner_tick}}, unlimited);
    for (const auto& [reason, total] : context.ui_diagnostic_totals)
        logger->Write(core::LogLevel::info, "ui.diagnostics.reason", reason, {},
                      {{"count", total.count}, {"epoch", total.epoch}, {"tick", total.tick}}, unlimited);
    for (const auto& [view, previous] : context.ui_producer_diagnostics) {
        const bool live = context.gl_surface_runtimes.contains(view);
        const auto state = live ? DescribeProducer(vm, context, dexvm::VmObjectRef{view})
                                : "retired=1 last_observed={" + previous + "}";
        logger->Write(core::LogLevel::info, "ui.diagnostics.producer", "final producer state", {},
                      {{"view", std::uint64_t(view)},
                       {"state", state},
                       {"epoch", context.ui_measure_epoch},
                       {"tick", context.ui_owner_tick}},
                      unlimited);
    }
}

void RecordAndroidTraversalDiagnostic(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                                      const dexvm::VmObjectRef root, const std::string& reason) {
    auto& total = context.ui_diagnostic_totals["ui.traversal: " + reason];
    ++total.count;
    total.epoch = context.ui_measure_epoch;
    total.tick = context.ui_owner_tick;
    const auto count = ++context.ui_traversal_diagnostics[root][reason];
    if (count != 1) return;
    if (auto* logger = vm.Log())
        logger->Write(core::LogLevel::info, "ui.traversal", reason + " root=" + std::to_string(root.Value()), {},
                      {{"root", std::uint64_t(root.Value())},
                       {"activity", std::uint64_t(context.activity.Value())},
                       {"epoch", context.ui_measure_epoch},
                       {"tick", context.ui_owner_tick},
                       {"revision", std::uint64_t(context.ui_content_revision)},
                       {"generation", std::uint64_t(context.ui_tree.Generation())},
                       {"width", std::uint64_t(context.surface_width)},
                       {"height", std::uint64_t(context.surface_height)},
                       {"count", count}});
}

void CommitAndroidMeasuredRootProjection(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                                         const dexvm::VmObjectRef root, const ui::UiProjectionTarget& target,
                                         const std::uint64_t epoch) {
    dexvm::VmExecutionLockScope lock(vm.ExecutionLock());
    if ((vm.CurrentContextToken() != 1U || context.ui_java_owner_thread != std::this_thread::get_id()))
        throw dexvm::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Measured projection requires Java owner thread"};
    if (root != AndroidUiTraversalRoot(vm, context) || target.content_revision != context.ui_content_revision ||
        target.window_generation != context.ui_tree.Generation()) {
        RecordAndroidTraversalDiagnostic(vm, context, root, "commit stale identity");
        throw dexvm::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Measured projection has stale root identity"};
    }
    if (context.input_presentation) {
        RecordAndroidTraversalDiagnostic(vm, context, root, "commit input lease");
        context.ui_tree.MarkDrawDirty(context.ui_tree.Root());
        return;
    }
    dexvm::IntrinsicContext root_call{vm, root, {}};
    if ((dexvm::IntrinsicCall(root_call).GetInt(context.view_java_measure_fields->flags[0]) & 0x800) == 0) {
        RecordAndroidTraversalDiagnostic(vm, context, root, "commit unmeasured root");
        throw dexvm::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Root has no completed ROM measurement"};
    }
    try {
        RefreshAndroidSurfaceFrames(context);
        auto projection = CollectAndroidRootProjection(vm, context, root, target, epoch, true);
        if (context.application_window_root_override.IsValid()) {
            const auto window = std::find_if(context.application_windows.begin(), context.application_windows.end(),
                                             [&](const auto& item) { return item.root == root && !item.removed; });
            if (window == context.application_windows.end()) return;
            ui::UiTree candidate(context.ui_tree);
            candidate.SetProjectionTarget(target);
            candidate.CommitProjection(projection);
            const auto published = candidate.PublishedProjection();
            const auto id = FindViewUiNode(context, root.Value());
            const auto* next_node = id ? published->Get(*id) : nullptr;
            const auto* old_node = id && window->projection ? window->projection->Get(*id) : nullptr;
            if (next_node && (!old_node || std::floor(next_node->alpha * 4.0F) != std::floor(old_node->alpha * 4.0F)))
                if (auto* logger = vm.Log())
                    logger->Write(core::LogLevel::info, "android.window", "application window opacity", {},
                                  {{"root", static_cast<std::uint64_t>(root.Value())},
                                   {"alpha", static_cast<double>(next_node->alpha)},
                                   {"uptime_ms", context.uptime_millis.load()},
                                   {"epoch", epoch}},
                                  {.mode = core::RateLimitMode::none});
            window->projection = published;
            context.ui_measure_epoch = epoch;
            return;
        }
        ResizeAndroidDialogs(vm, context);
        context.ui_tree.CommitProjection(projection);
        if (context.ui_touch_capture && !context.application_pointer_root.IsValid()) {
            const auto capture = dexvm::VmObjectRef{static_cast<std::uint32_t>(context.ui_touch_capture)};
            const auto id = FindViewUiNode(context, capture.Value());
            if (!id || !context.ui_tree.IsAttached(*id) || !context.ui_tree.IsVisible(*id) ||
                !context.ui_tree.Get(*id)->enabled || !AndroidViewBelongsToRoot(vm, context, capture)) {
                context.ui_touch_capture = 0;
                context.ui_touch_targets.clear();
                context.ui_touch_clicks.clear();
            }
        }
        for (auto& record : context.activity_stack) {
            if (record.object != context.activity) continue;
            record.java_root = root;
            record.projection = context.ui_tree.PublishedProjection();
        }
        context.ui_measure_epoch = epoch;
        if (context.ui_layout_dispatching) {
            context.ui_projection_density = context.ui_density;
            // Track device metrics consumed by the traversal, separately from an explicit test viewport.
            context.ui_layout_metrics = ui::Size{static_cast<std::int32_t>(context.surface_width),
                                                 static_cast<std::int32_t>(context.surface_height)};
        }
        if (!PublishAndroidPresentation(context))
            RecordAndroidTraversalDiagnostic(vm, context, root, "presentation retained after projection");
        RecordAndroidTraversalDiagnostic(vm, context, root, "projection committed");
    } catch (...) {
        RecordAndroidTraversalDiagnostic(vm, context, root, "projection exception");
        throw;
    }
}

dexvm::VmObjectRef ReadAndroidViewListener(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                                           dexvm::VmObjectRef view, AndroidViewListener listener) {
    dexvm::IntrinsicContext call{vm, view, {}};
    dexvm::IntrinsicCall fields(call);
    const auto info = fields.GetRef((*context.view_java_input_fields)[0]);
    if (!info.IsValid()) return dexvm::VmObjectRef{};
    return fields.GetRef((*context.view_java_listener_fields)[static_cast<std::size_t>(listener)], info);
}

void BindViewToUiNode(DexVmAndroidContext& context, const dexvm::VmObjectRef view, const ui::UiNodeId node) {
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

ui::UiNodeId EnsureViewUiNode(DexVmAndroidContext& context, const dexvm::VmObjectRef view, const ui::UiClass kind) {
    if (!view.IsValid()) {
        throw std::runtime_error("cannot bind a null guest View");
    }
    const auto found = context.object_to_ui_node.find(view.Value());
    if (found != context.object_to_ui_node.end()) return found->second;
    const auto node = context.ui_tree.CreateNode(kind);
    if ((kind == ui::UiClass::Button || kind == ui::UiClass::EditText)) {
        // Legacy defaults have no authority over the ROM theme consumption path.
        auto& text = *context.ui_tree.Get(node);
        text.background_color.reset();
        text.padding = {};
        text.clickable = false;
        text.max_lines = 0x7fffffff;
    }
    BindViewToUiNode(context, view, node);
    return node;
}

std::optional<ui::UiNodeId> FindViewUiNode(const DexVmAndroidContext& context, const std::uint64_t view_handle) {
    const auto found = context.object_to_ui_node.find(view_handle);
    if (found == context.object_to_ui_node.end() || context.ui_tree.Get(found->second) == nullptr) {
        return std::nullopt;
    }
    return found->second;
}

dexvm::VmObjectRef ViewObjectForUiNode(const DexVmAndroidContext& context, const ui::UiNodeId node) {
    const auto found = context.ui_node_to_object.find(node);
    return found == context.ui_node_to_object.end() ? dexvm::VmObjectRef{} : found->second;
}

void ResetViewUiState(DexVmAndroidContext& context) {
    context.ui_window_schedules.clear();
    context.ui_traversal_diagnostics.clear();
    context.ui_touch_capture = 0;
    context.ui_touch_activity = dexvm::VmObjectRef{};
    context.ui_touch_targets.clear();
    context.ui_touch_clicks.clear();
    if (!context.activity_stack.empty()) {
        const auto children = context.ui_tree.Get(ActivityContentRoot(context))->children;
        for (const auto child : children)
            RetireViewUiSubtree(context, child);
        return;
    }
    context.ui_tree.Reset();
    context.ui_layout_metrics.reset();
    context.object_to_ui_node.clear();
    context.ui_node_to_object.clear();
    context.ui_view_layout_params.clear();
    context.ui_view_backgrounds.clear();
    context.ui_view_images.clear();
    context.ui_drawables.clear();
    context.paint_measure_states.clear();
    context.text_watchers.clear();
}

void DetachGlSurfaceSubtree(DexVmAndroidContext& context, const ui::UiNodeId subtree) {
    std::vector<ui::UiNodeId> nodes{subtree};
    while (!nodes.empty()) {
        const auto node = nodes.back();
        nodes.pop_back();
        const auto* state = context.ui_tree.Get(node);
        if (!state) continue;
        nodes.insert(nodes.end(), state->children.begin(), state->children.end());
        const auto view = ViewObjectForUiNode(context, node);
        if (context.gl_surface_runtimes.contains(view.Value()) && context.retire_gl_surface_view)
            context.retire_gl_surface_view(view);
    }
}

ui::UiNodeId ActivityContentRoot(DexVmAndroidContext& context, const dexvm::VmObjectRef owner) {
    if (context.activity_stack.empty()) return context.ui_tree.Root();
    const auto found =
        std::find_if(context.activity_stack.begin(), context.activity_stack.end(),
                     [&](const auto& record) { return record.object == (owner.IsValid() ? owner : context.activity); });
    if (found == context.activity_stack.end()) throw std::logic_error("foreground Activity has no window record");
    if (!found->window_root || !context.ui_tree.Get(*found->window_root)) {
        // The owner barrier materializes the Java Window content root. Until
        // then an empty window has only the synthetic projection root.
        return context.ui_tree.Root();
    }
    return *found->window_root;
}

void RetireViewUiSubtree(DexVmAndroidContext& context, const ui::UiNodeId node) {
    std::shared_ptr<const ui::UiTree> retained;
    {
        if (const auto object = context.ui_node_to_object.find(node); object != context.ui_node_to_object.end()) {
            context.ui_window_schedules.erase(object->second);
            context.ui_traversal_diagnostics.erase(object->second);
        }
        for (const auto& record : context.activity_stack) {
            if (record.window_root != node) continue;
            retained = record.projection;
            context.ui_window_schedules.erase(record.java_root);
            context.ui_traversal_diagnostics.erase(record.java_root);
            break;
        }
    }
    const auto& tree = retained ? *retained : context.ui_tree;
    std::vector<ui::UiNodeId> retired;
    std::vector<ui::UiNodeId> nodes{node};
    while (!nodes.empty()) {
        const auto current = nodes.back();
        nodes.pop_back();
        const auto* state = tree.Get(current);
        if (!state) continue;
        retired.push_back(current);
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
                context.surface_requested_formats.erase(key);
                context.surface_active_formats.erase(key);
                context.surface_callback_formats.erase(key);

                context.holder_surfaces.erase(key);
                if (context.retire_native_surface) context.retire_native_surface(dexvm::VmObjectRef(key));
                context.active_surface_holders.erase(key);
                {
                    std::scoped_lock lock(context.surface_pixels_mutex);
                    context.surface_pixels.erase(key);
                    context.surface_buffer_queued.erase(key);
                }
                context.surface_callback_sizes.erase(key);
                context.surface_holders.erase(holder);
            }
            context.ui_touch_clicks.erase(object);
            context.ui_touch_targets.erase(object);
            if (context.ui_touch_capture == object) {
                context.ui_touch_capture = 0;
                context.ui_touch_targets.clear();
                context.ui_touch_clicks.clear();
            }
            context.object_to_ui_node.erase(object);
            context.ui_view_layout_params.erase(object);
            context.ui_view_backgrounds.erase(object);
            context.ui_view_images.erase(object);
            context.text_watchers.erase(object);
            context.ui_node_to_object.erase(found);
        }
    }
    {
        // Background nodes have been disconnected in the foreground builder;
        // retire the saved complete hierarchy, not only its now-empty root.
        for (auto it = retired.rbegin(); it != retired.rend(); ++it)
            context.ui_tree.RetireProjectedIdentity(*it);
    }
}

}  // namespace ogplay::runtime

namespace ogplay::runtime::android_intrinsics {

ui::UiClass UiClassForDescriptor(const std::string_view descriptor) {
    if (descriptor == "Landroid/widget/FrameLayout;" || descriptor == "Landroid/widget/AbsoluteLayout;") {
        return ui::UiClass::FrameLayout;
    }
    if (descriptor == "Landroid/widget/HorizontalScrollView;") return ui::UiClass::HorizontalScrollView;
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
    if (descriptor == "Landroid/widget/TextView;" || descriptor == "Landroid/widget/EditText;") {
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

ui::UiClass UiClassForObject(dexvm::Interpreter& vm, const dexvm::VmObjectRef view) {
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

constexpr std::string_view kAndroidNamespace = "http://schemas.android.com/apk/res/android";
constexpr std::uint8_t kTypeDimension = 0x05;

constexpr std::uint8_t kTypeReference = 0x01;

constexpr std::uint8_t kTypeString = 0x03;
constexpr std::uint8_t kTypeFirstColor = 0x1c;
constexpr std::uint8_t kTypeLastColor = 0x1f;

}  // namespace

const loader::ArscEntry* SelectUiResourceEntry(const DexVmAndroidContext& context, std::uint32_t resource_id) {
    const loader::ArscEntry* entry = nullptr;
    const auto width_dp = static_cast<int>(std::lround(context.surface_width / context.ui_density));
    const auto height_dp = static_cast<int>(std::lround(context.surface_height / context.ui_density));
    const auto smallest_dp = std::min(width_dp, height_dp);
    const auto orientation = width_dp > height_dp ? 2U : 1U;
    const auto density = static_cast<int>(std::lround(context.ui_density * 160.0F));
    const auto long_dp = std::max(width_dp, height_dp);
    const auto short_dp = std::min(width_dp, height_dp);
    const auto screen_size = long_dp < 470                       ? 1U
                             : long_dp >= 960 && short_dp >= 720 ? 4U
                             : long_dp >= 640 && short_dp >= 480 ? 3U
                                                                 : 2U;
    const auto density_better = [density](const std::uint16_t lhs, const std::uint16_t rhs) {
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
        return (((2 * low) - requested) * high > requested * requested) ? !lhs_high : lhs_high;
    };
    const auto better = [&](const loader::ArscEntry& lhs, const loader::ArscEntry& rhs) {
        if (lhs.smallest_width_dp != rhs.smallest_width_dp) return lhs.smallest_width_dp > rhs.smallest_width_dp;
        const auto lhs_delta = width_dp - lhs.screen_width_dp + height_dp - lhs.screen_height_dp;
        const auto rhs_delta = width_dp - rhs.screen_width_dp + height_dp - rhs.screen_height_dp;
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
        if (lhs.orientation != rhs.orientation) return lhs.orientation != 0U;
        if (lhs.density != rhs.density) return density_better(lhs.density, rhs.density);
        if (lhs.sdk_version != rhs.sdk_version) return lhs.sdk_version > rhs.sdk_version;
        return std::tie(lhs.screen_layout, lhs.value_type, lhs.value_data, lhs.string_value) <
               std::tie(rhs.screen_layout, rhs.value_type, rhs.value_data, rhs.string_value);
    };
    for (const auto& candidate : context.arsc.entries) {
        if (candidate.resource_id != resource_id || (candidate.sdk_version != 0 && candidate.sdk_version > 19) ||
            (candidate.orientation != 0 && candidate.orientation != orientation) ||
            candidate.smallest_width_dp > smallest_dp || candidate.screen_width_dp > width_dp ||
            candidate.screen_height_dp > height_dp ||
            ((candidate.screen_layout & 0x0fU) != 0U && (candidate.screen_layout & 0x0fU) > screen_size)) {
            continue;
        }
        if (entry == nullptr || better(candidate, *entry)) {
            entry = &candidate;
        }
    }
    if (entry == nullptr) {
        throw std::runtime_error("UI resource id is missing: " + std::to_string(resource_id));
    }
    return entry;
}

[[nodiscard]] const loader::ArscEntry& ResolveUiResourceEntry(const DexVmAndroidContext& context,
                                                              std::uint32_t resource_id) {
    std::unordered_set<std::uint32_t> seen;
    for (std::size_t depth = 0; depth < 16; ++depth) {
        if (!seen.insert(resource_id).second) {
            throw std::runtime_error("UI resource reference cycle");
        }
        const auto* entry = SelectUiResourceEntry(context, resource_id);
        if (entry->value_type != kTypeReference) return *entry;
        if (entry->value_data == 0U) {
            throw std::runtime_error("UI resource reference is null");
        }
        resource_id = entry->value_data;
    }
    throw std::runtime_error("UI resource reference depth exceeds 16");
}

namespace {

[[nodiscard]] std::int32_t ComplexDimensionPx(const DexVmAndroidContext& context, const std::uint32_t data,
                                              const bool scaled) {
    static constexpr std::array<double, 4> kRadixMultipliers{1.0 / 256.0, 1.0 / 32768.0, 1.0 / 8388608.0,
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
    const auto value = static_cast<double>(mantissa) * kRadixMultipliers[radix] * multiplier;
    if (!std::isfinite(value) || value < 0.0 || value > std::numeric_limits<std::int32_t>::max()) {
        throw std::runtime_error("UI dimension is outside integer px range");
    }
    return static_cast<std::int32_t>(std::lround(value));
}

[[nodiscard]] std::u16string AsciiText(const std::string& text) {
    std::u16string value;
    value.reserve(text.size());
    for (const auto byte : text) {
        if (static_cast<unsigned char>(byte) >= 0x80U) {
            throw std::runtime_error("fixed-font XML text must use the supported ASCII subset");
        }
        value.push_back(static_cast<char16_t>(byte));
    }
    static_cast<void>(ui::MeasureFixedText(value, 8.0F));
    return value;
}

}  // namespace

std::string ResolveResourceString(const DexVmAndroidContext& context, const std::uint32_t resource_id) {
    const auto& entry = ResolveUiResourceEntry(context, resource_id);
    if (entry.value_type != kTypeString || !entry.string_value.has_value()) {
        throw std::runtime_error("resource is not a string");
    }
    return *entry.string_value;
}

std::u16string ResolveUiString(const DexVmAndroidContext& context, const std::uint32_t resource_id) {
    return AsciiText(ResolveResourceString(context, resource_id));
}

std::uint32_t ResolveUiColor(const DexVmAndroidContext& context, const std::uint32_t resource_id) {
    const auto& entry = ResolveUiResourceEntry(context, resource_id);
    if (entry.value_type < kTypeFirstColor || entry.value_type > kTypeLastColor) {
        throw std::runtime_error("UI resource is not a color");
    }
    return AndroidColorToRgba(entry.value_data);
}

std::int32_t ResolveUiDimension(const DexVmAndroidContext& context, const std::uint32_t resource_id,
                                const bool scaled) {
    const auto& entry = ResolveUiResourceEntry(context, resource_id);
    if (entry.value_type != kTypeDimension) {
        throw std::runtime_error("UI resource is not a dimension");
    }
    return ComplexDimensionPx(context, entry.value_data, scaled);
}

std::shared_ptr<const ui::UiBitmap> DecodeLeafBitmap(const DexVmAndroidContext& context,
                                                     const loader::ArscEntry& entry) {
    if (entry.is_complex || entry.value_type != kTypeString || !entry.string_value)
        throw std::runtime_error("leaf bitmap requires a scalar APK file");
    const auto& path = *entry.string_value;
    if (entry.type_name != "drawable" || !path.starts_with("res/") || path.ends_with(".9.png") ||
        !(path.ends_with(".png") || path.ends_with(".jpg") || path.ends_with(".jpeg")))
        throw std::runtime_error("unsupported selector/ninepatch/XML/unknown bitmap file: " + path);
    const auto bytes = loader::ReadApkEntry(context.apk_bytes, context.archive, path);
    const auto u8 = [&](std::size_t i) { return std::to_integer<std::uint32_t>(bytes[i]); };
    const bool png = bytes.size() >= 8 && u8(0) == 137 && u8(1) == 80 && u8(2) == 78 && u8(3) == 71 && u8(4) == 13 &&
                     u8(5) == 10 && u8(6) == 26 && u8(7) == 10;
    const bool jpeg = bytes.size() >= 3 && u8(0) == 255 && u8(1) == 216 && u8(2) == 255;
    if ((path.ends_with(".png") && !png) || (!path.ends_with(".png") && !jpeg))
        throw std::runtime_error("bitmap extension and encoding disagree");
    if (png) {
        for (std::size_t at = 8; at < bytes.size();) {
            if (bytes.size() - at < 12) throw std::runtime_error("truncated PNG chunk");
            const auto length = (u8(at) << 24U) | (u8(at + 1) << 16U) | (u8(at + 2) << 8U) | u8(at + 3);
            if (length > bytes.size() - at - 12) throw std::runtime_error("invalid PNG chunk length");
            if (u8(at + 4) == 'n' && u8(at + 5) == 'p' && u8(at + 6) == 'T' && u8(at + 7) == 'c')
                throw std::runtime_error("ninepatch PNG chunk is unsupported");
            if (u8(at + 4) == 'n' && u8(at + 5) == 'p' && u8(at + 6) == 'L' && u8(at + 7) == 'b')
                throw std::runtime_error("optical layout PNG chunk is unsupported");
            at += length + 12U;
        }
    }
    const auto decoded = DecodeImageToArgb(bytes);
    if (!decoded || decoded->width <= 0 || decoded->height <= 0 || decoded->width > 4096 || decoded->height > 4096)
        throw std::runtime_error("leaf bitmap decode failed or exceeds bounds");
    auto result = std::make_shared<ui::UiBitmap>();
    result->width = decoded->width;
    result->height = decoded->height;
    result->rgba8.reserve(decoded->argb.size() * 4U);
    for (auto pixel : decoded->argb) {
        result->rgba8.push_back(static_cast<std::uint8_t>(pixel >> 16U));
        result->rgba8.push_back(static_cast<std::uint8_t>(pixel >> 8U));
        result->rgba8.push_back(static_cast<std::uint8_t>(pixel));
        result->rgba8.push_back(static_cast<std::uint8_t>(pixel >> 24U));
    }
    return result;
}

std::shared_ptr<const ui::UiBitmap> ResolveUiDrawable(DexVmAndroidContext& context, const std::uint32_t resource_id) {
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
    if (const auto cached = context.ui_bitmaps.find(resource_id); cached != context.ui_bitmaps.end()) {
        return cached->second;
    }
    const auto& drawable = ResolveUiResourceEntry(context, resource_id);
    auto bitmap = std::make_shared<ui::UiBitmap>();
    if (drawable.value_type >= kTypeFirstColor && drawable.value_type <= kTypeLastColor) {
        const auto rgba = AndroidColorToRgba(drawable.value_data);
        bitmap->width = 1;
        bitmap->height = 1;
        bitmap->rgba8 = {static_cast<std::uint8_t>(rgba >> 24U), static_cast<std::uint8_t>(rgba >> 16U),
                         static_cast<std::uint8_t>(rgba >> 8U), static_cast<std::uint8_t>(rgba)};
    } else if (drawable.value_type == kTypeString && drawable.string_value.has_value()) {
        const auto bytes = loader::ReadApkEntry(context.apk_bytes, context.archive, *drawable.string_value);
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
            const auto byte = [&bytes](const std::size_t at) { return std::to_integer<std::uint32_t>(bytes[at]); };
            return (byte(offset) << 24U) | (byte(offset + 1) << 16U) | (byte(offset + 2) << 8U) | byte(offset + 3);
        };
        for (std::size_t offset = 8; offset + 12 <= bytes.size();) {
            const auto size = be32(offset);
            if (size > bytes.size() - offset - 12U) break;
            if (std::to_integer<char>(bytes[offset + 4]) == 'n' && std::to_integer<char>(bytes[offset + 5]) == 'p' &&
                std::to_integer<char>(bytes[offset + 6]) == 'T' && std::to_integer<char>(bytes[offset + 7]) == 'c' &&
                size >= 48U && std::to_integer<unsigned>(bytes[offset + 9]) == 2U &&
                std::to_integer<unsigned>(bytes[offset + 10]) == 2U) {
                const auto data = offset + 8U;
                bitmap->nine_patch = true;
                bitmap->nine_patch_padding = {
                    static_cast<std::int32_t>(be32(data + 12U)), static_cast<std::int32_t>(be32(data + 20U)),
                    static_cast<std::int32_t>(be32(data + 16U)), static_cast<std::int32_t>(be32(data + 24U))};
                bitmap->stretch_x = {static_cast<std::int32_t>(be32(data + 32U)),
                                     static_cast<std::int32_t>(be32(data + 36U))};
                bitmap->stretch_y = {static_cast<std::int32_t>(be32(data + 40U)),
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
dx::VmObjectRef MakeResourceDrawable(dx::IntrinsicContext& call, const Context& context,
                                     const std::uint32_t resource_id) {
    loader::ArscEntry system_color;
    const bool system = (resource_id >> 24U) == 0x01U;
    if (system) {
        const auto* color = context->theme_resources.Color(resource_id);
        if (!color || color->value.type < kTypeFirstColor || color->value.type > kTypeLastColor ||
            !color->xml.empty() || !color->value.unsupported.empty())
            throw dx::VmJavaThrow{"Landroid/content/res/Resources$NotFoundException;",
                                  "system Drawable resource is not a supported scalar color"};
        system_color.value_type = static_cast<std::uint8_t>(color->value.type);
        system_color.value_data = color->value.data;
    }
    const auto& entry = system ? system_color : ResolveUiResourceEntry(*context, resource_id);
    if (entry.string_value && entry.string_value->ends_with(".xml")) {
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.drawable_resource", 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "XML Drawable objects are not implemented"};
    }
    const auto decoded = system ? std::shared_ptr<const ui::UiBitmap>{} : ResolveUiDrawable(*context, resource_id);
    const bool color = entry.value_type >= kTypeFirstColor && entry.value_type <= kTypeLastColor;
    if (decoded && decoded->nine_patch) {
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.drawable_resource", 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                              "resource NinePatch Drawable objects are not implemented"};
    }
    auto& vm = call.vm;
    const char* descriptor =
        color ? "Landroid/graphics/drawable/ColorDrawable;" : "Landroid/graphics/drawable/BitmapDrawable;";
    const auto object = vm.NewIntrinsicInstance(descriptor);
    const auto object_root = vm.ProtectReferences(std::array{object});
    dx::VmObjectRef bitmap;
    std::vector<dx::VmValue> args{dx::VmValue::Ref(object)};
    const char* signature = "(I)V";
    if (color)
        args.push_back(dx::VmValue::Int(static_cast<std::int32_t>(entry.value_data)));
    else {
        bitmap = vm.NewIntrinsicInstance("Landroid/graphics/Bitmap;");
        std::vector<std::uint32_t> argb;
        argb.reserve(decoded->rgba8.size() / 4U);
        for (std::size_t i = 0; i < decoded->rgba8.size(); i += 4U) {
            argb.push_back((static_cast<std::uint32_t>(decoded->rgba8[i + 3U]) << 24U) |
                           (static_cast<std::uint32_t>(decoded->rgba8[i]) << 16U) |
                           (static_cast<std::uint32_t>(decoded->rgba8[i + 1U]) << 8U) | decoded->rgba8[i + 2U]);
        }
        context->bitmaps[bitmap.Value()] = {
            decoded->width, decoded->height,
            std::make_shared<BitmapPixels>(decoded->width, decoded->height, 5, std::move(argb))};
        signature = "(Landroid/content/res/Resources;Landroid/graphics/Bitmap;)V";
        args.push_back(dx::VmValue::Ref(call.receiver));
        args.push_back(dx::VmValue::Ref(bitmap));
    }
    const auto roots = vm.ProtectReferences(std::array{object, bitmap});
    const auto constructor = vm.Linker().FindDirectMethod(vm.Model().ObjectClass(object), "<init>", signature);
    if (!constructor) throw std::logic_error("resource Drawable constructor is unavailable");
    const auto outcome = vm.Call(*constructor, args);
    if (outcome.exception.IsValid())
        throw dx::VmJavaThrow{vm.Linker().Class(outcome.exception_class).descriptor, outcome.exception_message,
                              outcome.exception};
    context->ui_drawables[object.Value()].resource_id = resource_id;
    return object;
}

void ProjectAndroidImage(dx::Interpreter& vm, DexVmAndroidContext& context, dx::VmObjectRef view) {
    const auto type = vm.Linker().ResolveDescriptor("Landroid/widget/ImageView;");
    const auto read = [&](const char* name, const char* descriptor) {
        const auto id = vm.Linker().FindFieldRecursive(type, name, descriptor);
        if (!id || vm.Linker().Field(*id).is_unimplemented) throw std::logic_error("ImageView field unavailable");
        return vm.Model().InstanceSlots(view)[vm.Linker().Field(*id).slot].bits;
    };
    const dx::VmObjectRef drawable{read("mDrawable", "Landroid/graphics/drawable/Drawable;")};
    const dx::VmObjectRef scale{read("mScaleType", "Landroid/widget/ImageView$ScaleType;")};
    const auto ordinal = CallAndroidMethod(vm, scale, "ordinal", "()I").AsInt();
    ui::ImageScaleType scale_type;
    switch (ordinal) {
    case 1: scale_type = ui::ImageScaleType::FitXy; break;
    case 3: scale_type = ui::ImageScaleType::FitCenter; break;
    case 5: scale_type = ui::ImageScaleType::Center; break;
    case 6: scale_type = ui::ImageScaleType::CenterCrop; break;
    case 7: scale_type = ui::ImageScaleType::CenterInside; break;
    default: throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "unsupported ImageView scaleType"};
    }
    std::shared_ptr<const ui::UiBitmap> image;
    std::optional<std::uint32_t> color;
    ui::Size intrinsic{};
    if (drawable.IsValid()) {
        const auto drawable_type = vm.Model().ObjectClass(drawable);
        const bool is_color = vm.Linker().IsAssignable(
            vm.Linker().ResolveDescriptor("Landroid/graphics/drawable/ColorDrawable;"), drawable_type);
        const auto found = context.ui_drawables.find(drawable.Value());
        if (!is_color && (found == context.ui_drawables.end() || !found->second.bitmap_drawable)) {
            if (auto* ledger = vm.Ledger()) ledger->RecordUnimplemented("dexvm.image_leaf", 0);
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "unreviewed image Drawable"};
        }
        auto& state = context.ui_drawables[drawable.Value()];
        const auto source = state.bitmap_drawable
                                ? CallAndroidMethod(vm, drawable, "getBitmap", "()Landroid/graphics/Bitmap;").ref
                                : dx::VmObjectRef{};
        if (source.IsValid()) {
            const auto bitmap = context.bitmaps.find(source.Value());
            if (bitmap == context.bitmaps.end() || bitmap->second.recycled)
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "ImageView bitmap recycled"};
            if (state.leaf_pixels)
                image = state.leaf_pixels;
            else {
                const auto& b = bitmap->second;
                if (!state.image_projection || state.bitmap_revision != b.pixels->Revision() ||
                    b.pixels->Locks() != 0) {
                    auto pixels = std::make_shared<ui::UiBitmap>();
                    pixels->width = b.width;
                    pixels->height = b.height;
                    for (const auto argb : b.pixels->Snapshot()) {
                        pixels->rgba8.push_back(static_cast<std::uint8_t>(argb >> 16U));
                        pixels->rgba8.push_back(static_cast<std::uint8_t>(argb >> 8U));
                        pixels->rgba8.push_back(static_cast<std::uint8_t>(argb));
                        pixels->rgba8.push_back(static_cast<std::uint8_t>(argb >> 24U));
                    }
                    state.image_projection = std::move(pixels);
                    state.bitmap_revision = b.pixels->Revision();
                }
                image = state.image_projection;
            }
        } else if (is_color)
            color = AndroidColorToRgba(
                static_cast<std::uint32_t>(CallAndroidMethod(vm, drawable, "getColor", "()I").AsInt()));
        else if (!state.bitmap_drawable)
            throw std::logic_error("missing image backing");
        intrinsic = {CallAndroidMethod(vm, drawable, "getIntrinsicWidth", "()I").AsInt(),
                     CallAndroidMethod(vm, drawable, "getIntrinsicHeight", "()I").AsInt()};
    }
    const auto node = EnsureViewUiNode(context, view, UiClassForObject(vm, view));
    auto& target = *context.ui_tree.Get(node);
    if (target.intrinsic != intrinsic) context.ui_tree.MarkLayoutDirty(node);
    target.intrinsic = intrinsic;
    target.image_bitmap = std::move(image);
    target.image_color = color;
    target.image_scale_type = scale_type;
    target.image_alpha = static_cast<float>(read("mAlpha", "I")) / 255.0F;
    target.image_resource_id = 0;
    // Weak derived index: Java mDrawable is the only identity and GC edge.
    if (drawable.IsValid())
        context.ui_view_images[view.Value()] = drawable;
    else
        context.ui_view_images.erase(view.Value());
    context.ui_tree.MarkDrawDirty(node);
}

}  // namespace android_intrinsics

namespace {

class IncludeExpander final {
public:
    explicit IncludeExpander(const UiLayoutLoader& loader) : loader_(loader) {}

    std::vector<loader::BinaryXmlElement> Expand(const std::span<const loader::BinaryXmlElement> elements,
                                                 const std::optional<std::uint32_t> root_layout_id) {
        if (root_layout_id.has_value()) stack_.push_back(*root_layout_id);
        const auto root = Validate(elements);
        Emit(elements, root, -1, nullptr);
        if (root_layout_id.has_value()) stack_.pop_back();
        return std::move(output_);
    }

private:
    [[nodiscard]] static std::size_t Validate(const std::span<const loader::BinaryXmlElement> elements) {
        if (elements.empty()) throw std::runtime_error("layout has no root element");
        std::optional<std::size_t> root;
        for (std::size_t index = 0; index < elements.size(); ++index) {
            if (elements[index].parent < 0) {
                if (root.has_value()) {
                    throw std::runtime_error("layout must have exactly one root element");
                }
                root = index;
            } else if (static_cast<std::size_t>(elements[index].parent) >= index) {
                throw std::runtime_error("layout parent order is invalid");
            }
        }
        if (!root.has_value()) {
            throw std::runtime_error("layout must have exactly one root element");
        }
        return *root;
    }

    [[nodiscard]] static std::uint32_t IncludeLayoutId(const loader::BinaryXmlElement& include) {
        for (const auto& attribute : include.attributes) {
            if (attribute.namespace_uri == kAndroidNamespace && attribute.name == "layout") {
                if (attribute.value_type != kTypeReference || attribute.data == 0U) {
                    throw std::runtime_error("include layout must be a non-null resource reference");
                }
                return attribute.data;
            }
        }
        throw std::runtime_error("include is missing android:layout");
    }

    [[nodiscard]] static bool HasIncludeOverrides(const loader::BinaryXmlElement& include) {
        return std::any_of(include.attributes.begin(), include.attributes.end(), [](const auto& attribute) {
            return !(attribute.namespace_uri == kAndroidNamespace && attribute.name == "layout");
        });
    }

    static void ApplyIncludeOverrides(loader::BinaryXmlElement& target, const loader::BinaryXmlElement& include) {
        for (const auto& attribute : include.attributes) {
            if (attribute.namespace_uri == kAndroidNamespace && attribute.name == "layout") {
                continue;
            }
            if (attribute.namespace_uri != kAndroidNamespace ||
                !(attribute.name == "id" || attribute.name == "visibility" || attribute.name == "layout_width" ||
                  attribute.name == "layout_height" || attribute.name.starts_with("layout_"))) {
                throw std::runtime_error("unsupported include override attribute: " + attribute.name);
            }
            const auto found =
                std::find_if(target.attributes.begin(), target.attributes.end(), [&attribute](const auto& existing) {
                    return existing.namespace_uri == attribute.namespace_uri && existing.name == attribute.name;
                });
            if (found == target.attributes.end()) {
                target.attributes.push_back(attribute);
            } else {
                *found = attribute;
            }
        }
    }

    void Emit(const std::span<const loader::BinaryXmlElement> elements, const std::size_t index,
              const std::int32_t output_parent, const loader::BinaryXmlElement* overrides) {
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
            if (std::find(stack_.begin(), stack_.end(), layout_id) != stack_.end()) {
                throw std::runtime_error("include layout resource cycle");
            }
            stack_.push_back(layout_id);
            const auto included = loader_(layout_id);
            const auto included_root = Validate(included);
            if (included[included_root].name == "merge") {
                if (HasIncludeOverrides(element)) {
                    throw std::runtime_error("include overrides require a concrete layout root");
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

    void EmitChildren(const std::span<const loader::BinaryXmlElement> elements, const std::size_t parent,
                      const std::int32_t output_parent) {
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

}  // namespace

std::vector<loader::BinaryXmlElement> ExpandUiIncludes(const std::span<const loader::BinaryXmlElement> elements,
                                                       const UiLayoutLoader& loader,
                                                       const std::optional<std::uint32_t> root_layout_id) {
    if (!loader) throw std::runtime_error("UI include loader is missing");
    return IncludeExpander(loader).Expand(elements, root_layout_id);
}

}  // namespace ogplay::runtime

// ---- migrated from support_widget_dispatch.cpp ----
// Widget click dispatch over the UiTree's most recent resolved geometry.

#include <optional>
#include <vector>

#include "shared.h"

namespace ogplay::runtime {
namespace {
struct InputTreeLease final {
    std::shared_ptr<const DexVmAndroidContext::PresentationSnapshot> snapshot;
    const ui::UiTree& tree;

    explicit InputTreeLease(const DexVmAndroidContext& context)
        : snapshot(context.input_presentation ? context.input_presentation : AcquireAndroidPresentation(context)),
          tree([&]() -> const ui::UiTree& {
              if (!snapshot || snapshot->guest_window_epoch_refs.empty())
                  throw dexvm::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                           "Input requires a complete presentation"};
              if (context.application_input_projection) return *context.application_input_projection;
              return *snapshot->main_window;
          }()) {}
};

void EnsureInputProjection(dexvm::Interpreter& vm, DexVmAndroidContext& context) {
    if (!context.ui_layout_dispatching && context.input_presentation) {
        if (vm.CurrentContextToken() != 1U || context.ui_java_owner_thread != std::this_thread::get_id())
            throw dexvm::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Input requires Java owner thread"};
        return;
    }
    EnsureAndroidUiTraversal(vm, context, AndroidUiTraversalReason::BeforeInput, AndroidUiTraversalPhase::ResolveOnly);
}

struct InputDispatchScope final {
    DexVmAndroidContext& context;
    std::shared_ptr<const DexVmAndroidContext::PresentationSnapshot> previous;

    InputDispatchScope(dexvm::Interpreter& vm, DexVmAndroidContext& state)
        : context(state), previous(state.input_presentation) {
        {
            EnsureInputProjection(vm, state);
            if (!state.input_presentation) state.input_presentation = AcquireAndroidPresentation(state);
        }
    }

    ~InputDispatchScope() { context.input_presentation = std::move(previous); }
};
}  // namespace

DexVmAndroidContext::UiOwnerIdentity CaptureAndroidUiOwner(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                                                           dexvm::VmObjectRef view) {
    DexVmAndroidContext::UiOwnerIdentity identity{context.activity,
                                                  AndroidUiTraversalRoot(vm, context),
                                                  view,
                                                  context.ui_tree.Generation(),
                                                  context.ui_content_revision,
                                                  {}};
    for (auto current = view; current.IsValid() && identity.mounts.size() <= ui::UiTree::kMaxDepth;) {
        identity.mounts.emplace_back(current, context.ui_mount_generations[current]);
        if (current == identity.root) break;
        dexvm::IntrinsicContext call{vm, current, {}};
        current = dexvm::IntrinsicCall(call).GetRef(context.view_java_hierarchy_fields->parent);
    }
    return identity;
}

bool IsAndroidUiOwnerCurrent(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                             const DexVmAndroidContext::UiOwnerIdentity& identity, bool require_enabled) {
    if (context.application_window_root_override.IsValid() &&
        std::ranges::none_of(context.application_windows, [&](const auto& window) {
            return !window.removed && window.root == identity.root && window.owner == context.activity;
        }))
        return false;
    if (identity.activity != context.activity || identity.root != AndroidUiTraversalRoot(vm, context) ||
        identity.generation != context.ui_tree.Generation() || identity.revision != context.ui_content_revision ||
        identity.mounts.empty() || identity.mounts.back().first != identity.root)
        return false;
    auto current = identity.view;
    for (const auto& [view, generation] : identity.mounts) {
        if (view != current || context.ui_mount_generations[view] != generation) return false;
        dexvm::IntrinsicContext call{vm, view, {}};
        dexvm::IntrinsicCall fields(call);
        const auto flags = fields.GetInt(context.view_java_measure_fields->view_flags);
        if ((flags & 0xc) != 0 || (require_enabled && (flags & 0x20) != 0)) return false;
        current = fields.GetRef(context.view_java_hierarchy_fields->parent);
    }
    return true;
}

AndroidInputEvent::AndroidInputEvent(dexvm::Interpreter& vm, DexVmAndroidContext& context)
    : context_(context), java_(true) {
    if (!java_) return;
    if (context.input_presentation)
        throw dexvm::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Reentrant owner input event"};
    if (vm.CurrentContextToken() != 1U || context.ui_java_owner_thread != std::this_thread::get_id() ||
        context.ui_layout_dispatching)
        throw dexvm::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Input requires Java owner outside traversal"};
    // The frame/previous event barrier owns mutation processing. In particular,
    // suspend's synthetic CANCEL must not create an extra logical layout cycle.
    context.input_presentation = AcquireAndroidPresentation(context);
    if (!context.input_presentation)
        throw dexvm::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Input requires a complete presentation"};
    if (context.input_presentation->activity != context.activity) {
        context.input_presentation.reset();
        throw dexvm::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                 "Input presentation has a different window owner"};
    }
}

AndroidInputEvent::~AndroidInputEvent() {
    if (java_) context_.input_presentation.reset();
}

namespace android_intrinsics {

std::int32_t VisibilityOf(const DexVmAndroidContext& context, const std::uint64_t handle) {
    const auto binding = FindViewUiNode(context, handle);
    if (!binding.has_value()) return kVisible;
    switch (context.ui_tree.Get(*binding)->visibility) {
    case ui::Visibility::Visible: return kVisible;
    case ui::Visibility::Invisible: return kInvisible;
    case ui::Visibility::Gone: return kGone;
    }
    return kVisible;
}

namespace {
[[nodiscard]] bool Contains(const ui::Rect rect, const float x, const float y) {
    return x >= static_cast<float>(rect.left) && y >= static_cast<float>(rect.top) &&
           x < static_cast<float>(rect.right) && y < static_cast<float>(rect.bottom);
}

[[nodiscard]] std::optional<ui::UiNodeId> HitClickable(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                                                       const ui::UiNodeId id, const float x, const float y) {
    const InputTreeLease lease(context);
    const auto& tree = lease.tree;
    const auto* node = tree.Get(id);
    if (node == nullptr || !node->window_visible || node->visibility != ui::Visibility::Visible || !node->enabled ||
        !Contains(node->screen_frame, x, y)) {
        return std::nullopt;
    }
    for (auto child = node->children.rbegin(); child != node->children.rend(); ++child) {
        if (const auto hit = HitClickable(vm, context, *child, x, y); hit.has_value()) {
            return hit;
        }
    }
    {
        const auto view = ViewObjectForUiNode(context, id);
        if (!view.IsValid() || !Contains(node->screen_clip, x, y)) return std::nullopt;
        const auto touch = ReadAndroidViewListener(vm, context, view, AndroidViewListener::Touch);
        const auto click = ReadAndroidViewListener(vm, context, view, AndroidViewListener::Click);
        return (touch.IsValid() || (click.IsValid() && node->clickable)) ? std::optional{id} : std::nullopt;
    }
}

void CollectTouchReceivers(const DexVmAndroidContext& context, const ui::UiNodeId id, const float x, const float y,
                           std::vector<std::uint64_t>& receivers) {
    const InputTreeLease lease(context);
    const auto& tree = lease.tree;
    const auto* node = tree.Get(id);
    if (node == nullptr || !tree.IsVisible(id) || node->visibility != ui::Visibility::Visible || !node->enabled ||
        !Contains(node->screen_frame, x, y)) {
        return;
    }
    for (auto child = node->children.rbegin(); child != node->children.rend(); ++child) {
        CollectTouchReceivers(context, *child, x, y, receivers);
    }
    const auto view = ViewObjectForUiNode(context, id);
    if (view.IsValid()) receivers.push_back(view.Value());
}

}  // namespace

}  // namespace android_intrinsics

std::optional<std::uint64_t> FindClickableViewAt(dexvm::Interpreter& vm, DexVmAndroidContext& context, const float x,
                                                 const float y) {
    EnsureInputProjection(vm, context);
    const InputTreeLease lease(context);
    const auto& tree = lease.tree;
    const auto hit = android_intrinsics::HitClickable(vm, context, tree.Root(), x, y);
    if (!hit.has_value()) return std::nullopt;
    const auto view = ViewObjectForUiNode(context, *hit);
    return view.IsValid() ? std::optional<std::uint64_t>{view.Value()} : std::nullopt;
}

std::vector<std::uint64_t> FindTouchReceiversAt(dexvm::Interpreter& vm, DexVmAndroidContext& context, const float x,
                                                const float y) {
    EnsureInputProjection(vm, context);
    const InputTreeLease lease(context);
    const auto& tree = lease.tree;
    std::vector<std::uint64_t> receivers;
    android_intrinsics::CollectTouchReceivers(context, tree.Root(), x, y, receivers);
    return receivers;
}

namespace {
dexvm::VmObjectRef InputListener(dexvm::Interpreter& vm, DexVmAndroidContext& context, dexvm::VmObjectRef view,
                                 bool touch) {
    return ReadAndroidViewListener(vm, context, view, touch ? AndroidViewListener::Touch : AndroidViewListener::Click);
}
}  // namespace

namespace {
using dexvm::VmObjectRef;
using dexvm::VmValue;
constexpr auto kTouchSignature = "(Landroid/view/MotionEvent;)Z";

int TouchAction(dexvm::Interpreter& vm, VmObjectRef event) {
    if (!event.IsValid()) throw dexvm::VmJavaThrow{"Ljava/lang/NullPointerException;", "event == null"};
    const auto action = android_intrinsics::CallAndroidMethod(vm, event, "getAction", "()I").AsInt() & 255;
    if (action != 0 && action != 1 && action != 2 && action != 3 && action != 5 && action != 6) {
        if (auto* ledger = vm.Ledger()) ledger->RecordUnimplemented("runtime.ui_pointer_dispatch", 0);
        throw dexvm::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                 "touch action outside non-split gesture support"};
    }
    return action;
}

bool TouchCall(dexvm::Interpreter& vm, VmObjectRef view, VmObjectRef event, const char* method) {
    return android_intrinsics::CallAndroidMethod(vm, view, method, kTouchSignature, {VmValue::Ref(event)}).AsInt() != 0;
}

bool TouchLive(DexVmAndroidContext& context, VmObjectRef view) {
    const InputTreeLease lease(context);
    const auto& tree = lease.tree;
    const auto node = FindViewUiNode(context, view.Value());
    return node && tree.Get(*node) && tree.IsAttached(*node) && tree.IsVisible(*node) && tree.Get(*node)->enabled;
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
    try {
        static_cast<void>(TouchCall(vm, view, event, "dispatchTouchEvent"));
    } catch (...) {
        vm.Model().SetPrimitiveElement(data, 0, previous);
        throw;
    }
    vm.Model().SetPrimitiveElement(data, 0, previous);
}
}  // namespace

bool DispatchAndroidViewTouch(dexvm::Interpreter& vm, DexVmAndroidContext& context, VmObjectRef view, VmObjectRef event,
                              const bool group) {
    const InputDispatchScope input_scope(vm, context);
    const InputTreeLease lease(context);
    const auto& tree = lease.tree;
    const auto action = TouchAction(vm, event);
    const auto roots = vm.ProtectReferences(std::array{view, event});
    EnsureInputProjection(vm, context);
    const auto node = FindViewUiNode(context, view.Value());
    if (!node || !tree.Get(*node)) {
        if (action == 3) {
            const auto listener = InputListener(vm, context, view, true);
            if (listener.IsValid())
                return android_intrinsics::CallAndroidMethod(vm, listener, "onTouch",
                                                             "(Landroid/view/View;Landroid/view/MotionEvent;)Z",
                                                             {VmValue::Ref(view), VmValue::Ref(event)})
                           .AsInt() != 0;
        }
        return false;
    }
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
        const bool intercepted =
            (action == 0 || target.IsValid()) && TouchCall(vm, view, event, "onInterceptTouchEvent");
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
            const auto children = tree.Get(*node)->children;
            for (auto child = children.rbegin(); child != children.rend(); ++child) {
                const auto receiver = ViewObjectForUiNode(context, *child);
                if (!receiver.IsValid() || !ViewContainsPoint(vm, context, receiver.Value(), x, y)) continue;
                const auto identity = std::optional{CaptureAndroidUiOwner(vm, context, receiver)};
                if (TouchCall(vm, receiver, event, "dispatchTouchEvent")) {
                    if (identity && !context.input_capture_identity) context.input_capture_identity = identity;
                    if (TouchLive(context, receiver)) context.ui_touch_targets[key] = receiver;
                    if (!context.ui_touch_capture) context.ui_touch_capture = receiver.Value();
                    return true;
                }
            }
        }
    }
    // API19 View dispatch: an enabled OnTouchListener gets first refusal.
    const auto state = *tree.Get(*node);
    const auto owner_identity = action == 0 ? std::optional{CaptureAndroidUiOwner(vm, context, view)} : std::nullopt;
    bool listener_handled = false;
    const auto listener = InputListener(vm, context, view, true);
    const auto listener_root = vm.ProtectReferences(std::array{listener});
    if ((state.enabled || action == 3) && listener.IsValid()) {
        listener_handled = android_intrinsics::CallAndroidMethod(vm, listener, "onTouch",
                                                                 "(Landroid/view/View;Landroid/view/MotionEvent;)Z",
                                                                 {VmValue::Ref(view), VmValue::Ref(event)})
                               .AsInt() != 0;
    }
    const bool handled = listener_handled || TouchCall(vm, view, event, "onTouchEvent");
    if (owner_identity && handled) context.input_capture_identity = owner_identity;
    auto click = context.ui_touch_clicks[key];
    bool enabled = state.enabled, clickable = state.clickable;
    bool live = TouchLive(context, view);
    {
        dexvm::IntrinsicContext call{vm, view, {}};
        const auto flags = dexvm::IntrinsicCall(call).GetInt(context.view_java_measure_fields->view_flags);
        enabled = (flags & 0x20) == 0;
        clickable = (flags & 0x4000) != 0;
        live = (flags & 0x0c) == 0 && AndroidViewBelongsToRoot(vm, context, view);
    }
    if (action == 0) click.eligible = enabled && clickable && live;
    click.consumed |= listener_handled;
    if (action == 1 && enabled && clickable && live && click.eligible && !click.consumed &&
        InputListener(vm, context, view, false).IsValid()) {
        const auto x = android_intrinsics::CallAndroidMethod(vm, event, "getX", "()F").AsFloat();
        const auto y = android_intrinsics::CallAndroidMethod(vm, event, "getY", "()F").AsFloat();
        if (ViewContainsPoint(vm, context, key, x, y)) {
            if (const auto error = InvokeViewOnClick(vm, context, key))
                throw dexvm::VmJavaThrow{"Ljava/lang/RuntimeException;", *error};
        }
    }
    if (action == 1 || action == 3)
        context.ui_touch_clicks.erase(key);
    else
        context.ui_touch_clicks[key] = click;
    if (action == 0 && handled && enabled && live) context.ui_touch_capture = key;
    return handled;
}

bool DispatchContentTouchEvent(dexvm::Interpreter& vm, DexVmAndroidContext& context, VmObjectRef event) {
    const InputDispatchScope input_scope(vm, context);
    const InputTreeLease lease(context);
    const auto& tree = lease.tree;
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
        EnsureInputProjection(vm, context);
        const auto root = ActivityContentRoot(context, context.activity);
        if (!tree.Get(root))
            throw dexvm::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Input window has no complete projection"};
        const auto children = tree.Get(root)->children;
        const auto x = android_intrinsics::CallAndroidMethod(vm, event, "getX", "()F").AsFloat();
        const auto y = android_intrinsics::CallAndroidMethod(vm, event, "getY", "()F").AsFloat();
        for (auto child = children.rbegin(); child != children.rend(); ++child) {
            const auto receiver = ViewObjectForUiNode(context, *child);
            if (!receiver.IsValid() || !ViewContainsPoint(vm, context, receiver.Value(), x, y)) continue;
            const auto identity = std::optional{CaptureAndroidUiOwner(vm, context, receiver)};
            if (TouchCall(vm, receiver, event, "dispatchTouchEvent")) {
                if (identity && !context.input_capture_identity) context.input_capture_identity = identity;
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

DeepTouchDispatchResult DispatchDeepTouchEvent(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                                               const std::int32_t action, const float x, const float y,
                                               const std::uint64_t captured_view,
                                               const AndroidBoundaryInput* snapshot) {
    if (context.input_deep_dispatching)
        throw dexvm::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Reentrant pointer event"};

    struct DeepScope {
        bool& flag;
        bool previous;

        ~DeepScope() { flag = previous; }
    } deep_scope{context.input_deep_dispatching, context.input_deep_dispatching};

    context.input_deep_dispatching = true;
    EnsureInputProjection(vm, context);
    const auto previous_presentation = context.input_presentation;
    if (!context.input_presentation) context.input_presentation = AcquireAndroidPresentation(context);

    struct RestorePresentation {
        DexVmAndroidContext& context;
        std::shared_ptr<const DexVmAndroidContext::PresentationSnapshot> previous;

        ~RestorePresentation() { context.input_presentation = std::move(previous); }
    } restore_presentation{context, previous_presentation};

    const auto previous = context.ui_bounded_scroll_dispatch;
    context.ui_bounded_scroll_dispatch = true;

    struct RestoreDispatch {
        bool& flag;
        bool previous;

        ~RestoreDispatch() { flag = previous; }
    } restore_dispatch{context.ui_bounded_scroll_dispatch, previous};

    // Capture belongs to the context; the legacy argument is only a projection.
    static_cast<void>(captured_view);
    auto adjusted = snapshot ? *snapshot : AndroidBoundaryInput{};
    if (snapshot && action == 3) adjusted.action = 3;
    const auto event = snapshot ? MakeMotionEvent(vm, adjusted) : MakeMotionEvent(vm, action, x, y, 0);
    try {
        if (action == 0) {
            context.input_pointer_tail = false;
            context.input_capture_identity.reset();
        }
        const auto cancel_invalid = [&] {
            if (!context.input_capture_identity ||
                IsAndroidUiOwnerCurrent(vm, context, *context.input_capture_identity))
                return false;
            const auto identity = *context.input_capture_identity;
            context.input_capture_identity.reset();
            context.input_pointer_tail = true;
            ClearTouch(context);
            // Dispatch exactly once to the captured receiver, even if its tree binding retired.
            CancelTouch(vm, identity.view, event);
            ClearTouch(context);
            return true;
        };
        {
            static_cast<void>(cancel_invalid());
            if (context.input_pointer_tail && action != 0) {
                if (action == 1 || action == 3) context.input_pointer_tail = false;
                return {.handled = true};
            }
        }
        const auto handled = DispatchContentTouchEvent(vm, context, event);
        {
            static_cast<void>(cancel_invalid());
            if (action == 1 || action == 3) context.input_capture_identity.reset();
        }
        return {.handled = handled, .captured_view = context.ui_touch_capture};
    } catch (const dexvm::VmJavaThrow& error) {
        return {.error = "View dispatchTouchEvent failed: " + error.message};
    }
}

bool ViewContainsPoint(dexvm::Interpreter& vm, DexVmAndroidContext& context, const std::uint64_t handle, const float x,
                       const float y) {
    EnsureInputProjection(vm, context);
    const InputTreeLease lease(context);
    const auto& tree = lease.tree;
    const auto node = FindViewUiNode(context, handle);
    if (!node.has_value()) return false;
    const auto* state = tree.Get(*node);
    return state != nullptr && tree.IsAttached(*node) && tree.IsVisible(*node) &&
           state->visibility == ui::Visibility::Visible && state->enabled &&
           (android_intrinsics::Contains(state->screen_clip, x, y)) &&
           android_intrinsics::Contains(state->screen_frame, x, y);
}

ViewTouchResult InvokeViewOnTouch(dexvm::Interpreter& vm, DexVmAndroidContext& context, const std::uint64_t handle,
                                  const std::int32_t action, const float x, const float y,
                                  const AndroidBoundaryInput* snapshot) {
    EnsureInputProjection(vm, context);
    const InputTreeLease lease(context);
    const auto& tree = lease.tree;
    const auto node = FindViewUiNode(context, handle);
    if (!node.has_value() || tree.Get(*node) == nullptr) return {};
    const auto view = ViewObjectForUiNode(context, *node);
    const auto listener = InputListener(vm, context, view, true);
    if (!listener.IsValid() || !tree.Get(*node)->enabled) return {};
    const auto roots = vm.ProtectReferences(std::array{view, listener});
    auto adjusted = snapshot ? *snapshot : AndroidBoundaryInput{};
    if (snapshot && action == 3) adjusted.action = 3;
    const auto event = snapshot ? MakeMotionEvent(vm, adjusted) : MakeMotionEvent(vm, action, x, y, 0);
    auto& linker = vm.Linker();
    const auto listener_class = vm.Model().ObjectClass(listener);
    const auto index =
        linker.FindVtableIndex(listener_class, "onTouch", "(Landroid/view/View;Landroid/view/MotionEvent;)Z");
    if (!index.has_value()) return {false, "touch listener has no onTouch method"};
    const auto outcome = vm.Call(linker.Class(listener_class).vtable[*index],
                                 std::vector<dexvm::VmValue>{dexvm::VmValue::Ref(listener), dexvm::VmValue::Ref(view),
                                                             dexvm::VmValue::Ref(event)});
    if (outcome.exception.IsValid()) {
        return {false, "onTouch raised: " + outcome.exception_message};
    }
    return {outcome.value.AsInt() != 0, std::nullopt};
}

ViewGestureDispatchResult DispatchViewGestureEvent(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                                                   const std::uint64_t handle, const std::int32_t action, const float x,
                                                   const float y, bool click_eligible, bool touch_consumed,
                                                   const AndroidBoundaryInput* snapshot) {
    const InputTreeLease lease(context);
    const auto& tree = lease.tree;
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
        if (!node.has_value() || tree.Get(*node) == nullptr) {
            return false;
        }
        return InputListener(vm, context, ViewObjectForUiNode(context, *node), false).IsValid();
    }();
    const auto is_clickable = [&]() {
        if (!node.has_value()) return false;
        const auto* state = tree.Get(*node);
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
            if (const auto error = InvokeViewOnClick(vm, context, handle); error.has_value()) {
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

std::optional<std::string> InvokeViewOnClick(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                                             const std::uint64_t handle) {
    const auto node = FindViewUiNode(context, handle);
    if (!node.has_value()) return "view has no UI binding";
    const auto view = ViewObjectForUiNode(context, *node);
    if (!view.IsValid()) return "UI node has no guest View";
    const auto listener = InputListener(vm, context, view, false);
    if (!listener.IsValid()) return "view has no click listener";
    const auto roots = vm.ProtectReferences(std::array{view, listener});
    auto& linker = vm.Linker();
    const auto listener_class = vm.Model().ObjectClass(listener);
    const auto index = linker.FindVtableIndex(listener_class, "onClick", "(Landroid/view/View;)V");
    if (!index.has_value()) {
        return "click listener has no onClick method";
    }
    const auto outcome = vm.Call(linker.Class(listener_class).vtable[*index],
                                 std::vector<dexvm::VmValue>{dexvm::VmValue::Ref(listener), dexvm::VmValue::Ref(view)});
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

ui::UiTree& DialogTree(DexVmAndroidContext&, const Modal& modal) {
    if (!modal.host_tree) throw std::logic_error("Dialog requires an isolated HostLayout tree");
    return *modal.host_tree;
}

bool DialogOwnerVisible(const DexVmAndroidContext& context, const dexvm::VmObjectRef owner) {
    if (context.process_stopping) return false;
    if (context.activity.IsValid() && context.activity != owner) return false;
    if (context.activity_stack.empty()) return true;
    const auto found = std::find_if(context.activity_stack.begin(), context.activity_stack.end(),
                                    [owner](const auto& record) { return record.object == owner; });
    return found != context.activity_stack.end() && found->visible;
}

bool DialogVisible(const DexVmAndroidContext& context, const Modal& modal) {
    return modal.visible && DialogOwnerVisible(context, modal.owner);
}

Modal* TopDialog(DexVmAndroidContext& context) {
    for (auto item = context.dialog_presentations.rbegin(); item != context.dialog_presentations.rend(); ++item)
        if (DialogVisible(context, *item)) return &*item;
    return nullptr;
}

void FocusModal(DexVmAndroidContext& context, const Modal& modal) {
    if (!DialogVisible(context, modal)) return;
    auto& tree = DialogTree(context, modal);
    const auto focused = tree.Focused();
    for (const auto node : modal.buttons)
        if (node && node == focused) return;
    for (const auto node : modal.buttons)
        if (node) {
            static_cast<void>(tree.RequestFocus(*node, false));
            return;
        }
}

void ClearModalCapture(DexVmAndroidContext& context) {
    if (context.dialog_pressed_button && context.dialog_capture)
        for (const auto& modal : context.dialog_presentations)
            if (modal.partition == context.dialog_capture->partition)
                modal.host_tree->SetPressed(*context.dialog_pressed_button, false);
    context.dialog_pointer_tail |= context.dialog_pointer_active;
    context.dialog_key_tails.insert(context.dialog_keys_down.begin(), context.dialog_keys_down.end());
    context.dialog_capture.reset();
    context.dialog_pointer_active = false;
    context.dialog_pressed_button.reset();
    context.dialog_keys_down.clear();
}

void CheckModalFit(const Modal& modal, const ui::Size viewport) {
    const auto& tree = *modal.host_tree;
    const auto* panel = tree.Get(modal.panel);
    const auto bounds = panel->screen_frame;
    if (bounds.top < 8 || bounds.bottom > viewport.height - 8)
        throw std::runtime_error("Dialog content cannot fit the bounded viewport");
    for (const auto button : modal.buttons)
        if (button && tree.Get(*button)->screen_frame.right > bounds.right - panel->padding.right)
            throw std::runtime_error("Dialog buttons cannot fit the bounded viewport");
}
}  // namespace

std::shared_ptr<const DexVmAndroidContext::PresentationSnapshot>
AcquireAndroidPresentation(const DexVmAndroidContext& context) {
    return std::atomic_load(&context.presentation_snapshot);
}

void SnapshotAndroidPresentationDiagnostics(const DexVmAndroidContext& context, AndroidUiSnapshot& result) {
    constexpr std::array names{"none",
                               "input_lease",
                               "missing_guest",
                               "pending_metrics_without_snapshot",
                               "active_window",
                               "application_window_metrics",
                               "background_window_metrics",
                               "producer_pixels",
                               "modal_tree",
                               "modal_viewport",
                               "modal_density",
                               "pending_metrics_without_modal",
                               "pending_metrics_unchanged"};
    static_assert(names.size() == static_cast<std::size_t>(DexVmAndroidContext::PresentationRejectReason::count));
    result.presentation_last_reject = names.at(static_cast<std::size_t>(context.presentation_last_reject));
    for (std::size_t i = 1; i < names.size(); ++i)
        result.presentation_reject_counts.emplace(names[i], context.presentation_reject_counts[i]);
    const auto snapshot = AcquireAndroidPresentation(context);
    for (const auto& modal : context.dialog_presentations) {
        AndroidUiSnapshot::Modal row;
        row.partition = modal.partition;
        row.revision = modal.revision;
        row.visible = modal.visible;
        row.owner_visible = DialogOwnerVisible(context, modal.owner);
        row.live_root_visible = modal.host_tree && modal.host_tree->IsVisible(modal.root);
        if (modal.host_tree) row.live_modifications = modal.host_tree->ModificationCount();
        if (snapshot)
            for (const auto& frozen : snapshot->modal_stack)
                if (frozen.partition == modal.partition) {
                    row.frozen_root_visible = frozen.tree->IsVisible(modal.root);
                    row.frozen_modifications = frozen.tree->ModificationCount();
                }
        result.modals.push_back(row);
    }
}

bool PublishAndroidPresentation(DexVmAndroidContext& context) {
    if (context.ui_java_owner_thread != std::this_thread::get_id())
        throw std::logic_error("Presentation publication requires Java owner thread");
    using Reason = DexVmAndroidContext::PresentationRejectReason;
    const auto reject = [&](Reason reason) {
        context.presentation_last_reject = reason;
        auto& count = context.presentation_reject_counts[static_cast<std::size_t>(reason)];
        if (count == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("Presentation rejection counter exhausted");
        ++count;
        return false;
    };
    if (context.input_presentation) return reject(Reason::input_lease);
    auto guest = context.ui_tree.PublishedProjection();
    if (!guest || !guest->ProjectionTarget()) return reject(Reason::missing_guest);
    const auto previous = AcquireAndroidPresentation(context);
    auto viewport = ui::Size{guest->ProjectionTarget()->metrics.width, guest->ProjectionTarget()->metrics.height};
    const bool pending_metrics =
        context.ui_projection_density != context.ui_density ||
        context.ui_layout_metrics != ui::Size{static_cast<std::int32_t>(context.surface_width),
                                              static_cast<std::int32_t>(context.surface_height)};
    if (pending_metrics) {
        if (!previous) return reject(Reason::pending_metrics_without_snapshot);
        // Visibility/retirement can still remove old partitions at the old complete metrics.
        // Newly built geometry must wait for the matching guest epoch.
        guest = previous->main_window;
        viewport = previous->viewport;
    }
    // A failed switch must not relabel the previous window as the new input owner.
    const auto active = std::find_if(context.activity_stack.begin(), context.activity_stack.end(),
                                     [&](const auto& record) { return record.object == context.activity; });
    if (active != context.activity_stack.end() &&
        (!active->window_root || !guest->Get(*active->window_root) || !guest->IsAttached(*active->window_root)))
        return reject(Reason::active_window);
    const auto& target = *guest->ProjectionTarget();
    using Snapshot = DexVmAndroidContext::PresentationSnapshot;
    auto next = std::make_shared<Snapshot>();
    next->activity = context.activity;
    next->main_window = guest;
    const auto add_windows = [&](dexvm::VmObjectRef owner) {
        const auto activity = std::find_if(context.activity_stack.begin(), context.activity_stack.end(),
                                           [&](const auto& record) { return record.object == owner; });
        if (activity == context.activity_stack.end() || !activity->visible) return true;
        for (auto window = context.application_windows.rbegin(); window != context.application_windows.rend();
             ++window) {
            if (window->removed || window->owner != owner) continue;
            if (!window->projection || !window->projection->ProjectionTarget() ||
                (window->projection->ProjectionTarget()->metrics.width != target.metrics.width ||
                 window->projection->ProjectionTarget()->metrics.height != target.metrics.height))
                return reject(Reason::application_window_metrics);
            next->application_windows.push_back(
                {window->root, window->owner, window->generation, window->flags, window->format, window->projection});
            next->guest_window_epoch_refs.push_back(window->projection);
        }
        return true;
    };
    if (!add_windows(context.activity)) return false;  // add_windows records the reason.
    next->guest_window_epoch_refs.push_back(guest);
    // Explicit main_window serves Activity input; attached windows precede it in
    // descending Z order. Composition visits all partitions in reverse order.
    for (auto record = context.activity_stack.rbegin(); record != context.activity_stack.rend(); ++record) {
        if (!context.activity.IsValid() || record->object == context.activity || !record->visible) continue;
        const auto& partition = record->projection;
        if (!partition || !partition->ProjectionTarget() ||
            partition->ProjectionTarget()->metrics.width != target.metrics.width ||
            partition->ProjectionTarget()->metrics.height != target.metrics.height)
            return reject(Reason::background_window_metrics);
        if (!add_windows(record->object)) return false;
        next->guest_window_epoch_refs.push_back(partition);
    }
    next->bitmaps = context.ui_bitmaps;
    {
        std::scoped_lock lock(context.video_views_mutex);
        for (const auto& [handle, state] : context.video_views) {
            if (!state.latest_frame) continue;
            const auto& frame = *state.latest_frame;
            if (!frame.width || !frame.height ||
                frame.rgba8.size() != static_cast<std::uint64_t>(frame.width) * frame.height * 4U)
                throw std::runtime_error("Invalid presentation producer pixels");
            const auto id = FindViewUiNode(context, state.media_player ? state.display_view.Value() : handle);
            if (!id) continue;
            for (const auto& partition : next->guest_window_epoch_refs) {
                if (!partition->Get(*id) || !partition->IsAttached(*id) || !partition->IsVisible(*id)) continue;
                auto pixels = frame;
                if (state.media_player) {
                    const auto holder = context.surface_holders.find(state.display_view.Value());
                    if (holder == context.surface_holders.end()) continue;
                    const auto size = context.surface_callback_sizes.find(holder->second.Value());
                    if (size == context.surface_callback_sizes.end()) continue;
                    if (pixels.width != size->second.first || pixels.height != size->second.second) {
                        pixels.rgba8 = video::ComposeRgbaOnCanvas(frame, size->second.first, size->second.second);
                        pixels.width = size->second.first;
                        pixels.height = size->second.second;
                    }
                    next->scaled_surface_frames.insert(*id);
                }
                if (state.media_player) {
                    const auto holder = context.surface_holders.at(state.display_view.Value());
                    const auto format = context.surface_active_formats.find(holder.Value());
                    if (format != context.surface_active_formats.end() && AndroidPixelFormatHasAlpha(format->second))
                        next->alpha_surface_frames.insert(*id);
                }
                next->video_frames.emplace(*id, std::move(pixels));
                break;
            }
        }
    }
    {
        std::scoped_lock lock(context.surface_pixels_mutex);
        for (const auto& [view, holder] : context.surface_holders) {
            if (!context.active_surface_holders.contains(holder.Value())) continue;
            const auto pixels = context.surface_pixels.find(holder.Value());
            if (pixels == context.surface_pixels.end()) continue;
            const auto id = FindViewUiNode(context, view);
            if (!id) continue;
            for (const auto& partition : next->guest_window_epoch_refs) {
                if (!partition->Get(*id) || !partition->IsAttached(*id) || !partition->IsVisible(*id)) continue;
                const auto format = context.surface_active_formats.find(holder.Value());
                if (format != context.surface_active_formats.end() && AndroidPixelFormatHasAlpha(format->second))
                    next->alpha_surface_frames.insert(*id);
                if (!next->video_frames.emplace(*id, pixels->second).second)
                    throw std::runtime_error("Surface has competing producers");
                next->scaled_surface_frames.insert(*id);
                break;
            }
        }
    }
    // A modal mutation may precede the next owner draw barrier. Do not pair
    // newly decoded/removed pixels with an older opening in the guest epoch.
    for (const auto& partition : next->guest_window_epoch_refs) {
        for (const auto& [id, object] : context.ui_node_to_object) {
            static_cast<void>(object);
            const auto* node = partition->Get(id);
            if (node && partition->IsAttached(id) && node->surface_has_frame != next->video_frames.contains(id))
                return reject(Reason::producer_pixels);
        }
    }
    next->metrics_revision = target.metrics_revision;
    next->viewport = viewport;
    for (const auto& modal : context.dialog_presentations) {
        if (!DialogVisible(context, modal)) continue;
        if (!modal.host_tree) return reject(Reason::modal_tree);
        if (modal.viewport != viewport) return reject(Reason::modal_viewport);
        if (!pending_metrics && modal.density != context.ui_density) return reject(Reason::modal_density);
        // Published host trees never share mutable focus/visibility with builders.
        std::shared_ptr<const ui::UiTree> frozen;
        if (previous)
            for (const auto& old : previous->modal_stack)
                if (old.partition == modal.partition && old.revision == modal.revision &&
                    old.tree->ModificationCount() == modal.host_tree->ModificationCount())
                    frozen = old.tree;
        if (!frozen && pending_metrics) return reject(Reason::pending_metrics_without_modal);
        if (!frozen) frozen = std::make_shared<const ui::UiTree>(*modal.host_tree);
        next->modal_stack.push_back({modal.dialog, modal.owner, modal.partition, modal.generation, modal.revision,
                                     std::move(frozen), modal.buttons});
    }
    const auto same_modals = [&] {
        if (!previous || previous->modal_stack.size() != next->modal_stack.size()) return false;
        for (std::size_t i = 0; i < next->modal_stack.size(); ++i)
            if (previous->modal_stack[i].tree != next->modal_stack[i].tree) return false;
        return true;
    }();
    const auto same_video = [&] {
        if (!previous || previous->video_frames.size() != next->video_frames.size() ||
            previous->alpha_surface_frames != next->alpha_surface_frames)
            return false;
        for (const auto& [id, frame] : next->video_frames) {
            const auto old = previous->video_frames.find(id);
            if (old == previous->video_frames.end() || old->second.width != frame.width ||
                old->second.height != frame.height || old->second.position_ms != frame.position_ms ||
                old->second.rgba8 != frame.rgba8)
                return false;
        }
        return true;
    }();
    if (same_modals && same_video && previous->activity == next->activity && previous->bitmaps == next->bitmaps &&
        previous->guest_window_epoch_refs == next->guest_window_epoch_refs &&
        previous->application_windows.size() == next->application_windows.size() &&
        std::equal(previous->application_windows.begin(), previous->application_windows.end(),
                   next->application_windows.begin(), [](const auto& a, const auto& b) {
                       return a.root == b.root && a.generation == b.generation && a.flags == b.flags &&
                              a.format == b.format;
                   }))
        return pending_metrics ? reject(Reason::pending_metrics_unchanged) : true;
    const auto max = std::numeric_limits<std::uint64_t>::max();
    if ((previous && previous->presentation_epoch == max) || (!same_modals && context.dialog_host_revision == max))
        throw std::overflow_error("Presentation sequence exhausted");
    next->presentation_epoch = previous ? previous->presentation_epoch + 1 : 1;
    next->host_modal_revision = context.dialog_host_revision + (same_modals ? 0 : 1);
    // Clear a gesture before exposing a replacement, hidden, or retired modal.
    if (context.dialog_capture) {
        const auto& capture = *context.dialog_capture;
        if (next->modal_stack.empty() || next->modal_stack.back().partition != capture.partition ||
            next->modal_stack.back().generation != capture.generation)
            ClearModalCapture(context);
    }
    context.dialog_host_revision = next->host_modal_revision;
    std::atomic_store(&context.presentation_snapshot, std::shared_ptr<const Snapshot>{std::move(next)});
    return true;
}

void ResizeAndroidDialogs(dexvm::Interpreter& vm, DexVmAndroidContext& context) {
    const ui::Size viewport{static_cast<std::int32_t>(context.surface_width),
                            static_cast<std::int32_t>(context.surface_height)};
    auto prepared = context.dialog_presentations;
    bool changed{};
    try {
        for (auto& modal : prepared) {
            if (modal.viewport == viewport && modal.density == context.ui_density) continue;
            const auto density = context.ui_density;
            if (!std::isfinite(density) || density <= 0 || density > 8 || viewport.width < 48 || viewport.height < 48)
                throw std::runtime_error("Dialog viewport is unavailable or too small");
            modal.host_tree = std::make_shared<ui::UiTree>(*modal.host_tree);
            auto& tree = *modal.host_tree;
            for (const auto button : modal.buttons)
                if (button) tree.SetPressed(*button, false);
            auto* panel = tree.Get(modal.panel);
            panel->layout.width.px = std::min(static_cast<std::int32_t>(480 * density), viewport.width - 32);
            const auto padding = static_cast<std::int32_t>(12 * density);
            panel->padding = {padding, padding, padding, padding};
            const auto rescale = [&](auto&& self, ui::UiNodeId id) -> void {
                auto* node = tree.Get(id);
                if (node->kind == ui::UiClass::TextView) {
                    node->text_size_px = (node->text_style ? 18 : 14) * density;
                    node->padding.bottom = static_cast<std::int32_t>(8 * density);
                } else if (node->kind == ui::UiClass::Button) {
                    node->text_size_px = 14 * density;
                    node->padding = {padding, padding / 2, padding, padding / 2};
                }
                for (auto child : node->children)
                    self(self, child);
            };
            rescale(rescale, modal.root);
            const ui::UiMetrics managed_viewport{viewport.width, viewport.height};
            ui::LayoutUiTree(*modal.host_tree, managed_viewport);
            CheckModalFit(modal, viewport);
            if (context.dialog_host_revision == std::numeric_limits<std::uint64_t>::max())
                throw std::runtime_error("Dialog host revision exhausted");
            modal.revision = context.dialog_host_revision + 1;
            modal.viewport = viewport;
            modal.density = density;
            changed = true;
        }
    } catch (const std::exception& error) {
        if (auto* ledger = vm.Ledger()) ledger->RecordUnimplemented("runtime.ui.dialog.presentation", 0);
        throw dexvm::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", error.what()};
    }
    if (changed) {
        ClearModalCapture(context);
        context.dialog_presentations = std::move(prepared);
    }
}

bool HasAndroidModalDialog(const DexVmAndroidContext& context) {
    for (const auto& modal : context.dialog_presentations)
        if (DialogVisible(context, modal)) return true;
    return false;
}

void ReleaseAndroidDialogs(DexVmAndroidContext& context) {
    context.dialog_presentations.clear();
    context.dialog_pointer_active = false;
    context.dialog_pressed_button.reset();
    context.dialog_keys_down.clear();
    {
        ClearModalCapture(context);
        PublishAndroidPresentation(context);
    }
}

void DismissAndroidActivityDialogs(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                                   const dexvm::VmObjectRef owner) {
    for (unsigned pass = 0; pass < 8; ++pass) {
        std::vector<dexvm::VmObjectRef> dialogs;
        for (const auto& modal : context.dialog_presentations)
            if (modal.owner == owner) dialogs.push_back(modal.dialog);
        if (dialogs.empty()) return;
        const auto roots = vm.ProtectReferences(dialogs);
        for (const auto dialog : dialogs)
            static_cast<void>(android_intrinsics::CallAndroidMethod(vm, dialog, "dismiss", "()V"));
    }
    throw dexvm::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                             "Dialog callbacks did not settle during Activity retirement"};
}

void SynchronizeAndroidDialogs(DexVmAndroidContext& context) {
    std::erase_if(context.dialog_presentations, [&](const Modal& modal) {
        const bool alive = context.activity_stack.empty() ||
                           std::any_of(context.activity_stack.begin(), context.activity_stack.end(),
                                       [&](const auto& record) { return record.object == modal.owner; });
        if (alive) return false;
        return true;
    });
    const auto* top = TopDialog(context);
    if (top) FocusModal(context, *top);
    if (context.dialog_capture && (!top || top->partition != context.dialog_capture->partition ||
                                   top->generation != context.dialog_capture->generation))
        ClearModalCapture(context);
    PublishAndroidPresentation(context);
}

namespace android_intrinsics {
void DismissAndroidDialog(DexVmAndroidContext& context, const dx::VmObjectRef dialog) {
    const auto found = std::find_if(context.dialog_presentations.begin(), context.dialog_presentations.end(),
                                    [dialog](const Modal& modal) { return modal.dialog == dialog; });
    if (found == context.dialog_presentations.end()) return;
    context.dialog_presentations.erase(found);
    context.dialog_pressed_button.reset();

    if (const auto* top = TopDialog(context)) FocusModal(context, *top);
    ClearModalCapture(context);
    PublishAndroidPresentation(context);
}

void SetAndroidDialogVisible(DexVmAndroidContext& context, const dx::VmObjectRef dialog, bool visible) {
    for (auto& modal : context.dialog_presentations)
        if (modal.dialog == dialog) {
            modal.visible = visible;
        }
    if (const auto* top = TopDialog(context)) FocusModal(context, *top);
    ClearModalCapture(context);
    PublishAndroidPresentation(context);
}

std::int32_t FocusAndroidDialog(DexVmAndroidContext& context, const dx::VmObjectRef dialog,
                                const std::int32_t direction) {
    auto* modal = TopDialog(context);
    if (!modal || modal->dialog != dialog) return 0;
    std::vector<std::size_t> buttons;
    std::size_t current{};
    for (std::size_t i = 0; i < 3; ++i)
        if (modal->buttons[i]) {
            if (modal->buttons[i] == DialogTree(context, *modal).Focused()) current = buttons.size();
            buttons.push_back(i);
        }
    if (buttons.empty()) return 0;
    if (direction != 0)
        current = direction > 0 ? (current + 1) % buttons.size() : (current + buttons.size() - 1) % buttons.size();
    static_cast<void>(DialogTree(context, *modal).RequestFocus(*modal->buttons[buttons[current]], false));
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
        if (call.vm.Linker().IsAssignable(activity, call.vm.Model().ObjectClass(source))) {
            owner = source;
            break;
        }
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
    if (!context->activity_stack.empty() &&
        !std::any_of(context->activity_stack.begin(), context->activity_stack.end(),
                     [owner](const auto& record) { return record.object == owner; }))
        unsupported("Dialog Activity is retired");
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
    if (!labels.IsValid() || call.vm.Model().ArrayLength(labels) != 3)
        unsupported("Dialog button inventory is invalid");
    std::array<std::u16string, 3> button_labels;
    for (JniSize i = 0; i < 3; ++i)
        button_labels[static_cast<std::size_t>(i)] = text(call.vm.Model().GetObjectElement(labels, i), 128);
    if (dx::IntrinsicCall(call).GetLong(generation) != call.arguments[5].AsLong()) return;
    const auto previous = std::find_if(context->dialog_presentations.begin(), context->dialog_presentations.end(),
                                       [&](const Modal& modal) { return modal.dialog == call.receiver; });
    if (previous == context->dialog_presentations.end() &&
        context->dialog_presentations.size() +
                static_cast<std::size_t>(std::ranges::count_if(context->application_windows,
                                                               [](const auto& window) { return !window.removed; })) >=
            8)
        unsupported("Dialog window limit exceeded");

    const auto prior_focus =
        previous == context->dialog_presentations.end() ? (std::optional<ui::UiNodeId>{}) : previous->prior_focus;
    std::optional<Modal::GuestFocus> prior_guest_focus;
    {
        if (previous != context->dialog_presentations.end())
            prior_guest_focus = previous->prior_guest_focus;
        else {
            const auto guest_root = AndroidUiTraversalRoot(call.vm, *context);
            dx::VmObjectRef focused;
            // PFLAG_FOCUSED is Java's fact. Read binding tokens rather than
            // invoking a potentially overridden findFocus during construction.
            if (context->view_java_measure_fields && context->view_java_hierarchy_fields) {
                for (const auto& [node, view] : context->ui_node_to_object) {
                    static_cast<void>(node);
                    if (!call.vm.Model().IsValidRef(view)) continue;
                    dx::IntrinsicContext view_call{call.vm, view, {}};
                    if ((dx::IntrinsicCall(view_call).GetInt(context->view_java_measure_fields->flags[0]) & 2) != 0 &&
                        AndroidViewBelongsToRoot(call.vm, *context, view)) {
                        focused = view;
                        break;
                    }
                }
            }
            prior_guest_focus = Modal::GuestFocus{focused, guest_root, context->ui_tree.Generation()};
        }
    }

    const auto density = context->ui_density;
    if (!std::isfinite(density) || density <= 0 || density > 8 || context->surface_width < 48 ||
        context->surface_height < 48)
        unsupported("Dialog viewport is unavailable or too small");
    Modal modal{.dialog = call.receiver, .owner = owner, .root = {}, .buttons = {}, .prior_focus = prior_focus};
    {
        if (context->dialog_host_revision == std::numeric_limits<std::uint64_t>::max())
            unsupported("Dialog host revision exhausted");
        modal.host_tree = std::make_shared<ui::UiTree>(context->CreateHostUiTree());
        modal.partition = modal.revision = context->dialog_host_revision + 1;
        modal.generation = static_cast<std::uint64_t>(call.arguments[5].AsLong());
        modal.prior_guest_focus = prior_guest_focus;
    }
    auto& tree = DialogTree(*context, modal);
    const auto root = tree.CreateNode(ui::UiClass::FrameLayout);
    modal.root = root;
    try {
        auto* overlay = tree.Get(root);
        overlay->modal_layer = true;
        overlay->layout.width.mode = ui::SizeMode::MatchParent;
        overlay->layout.height.mode = ui::SizeMode::MatchParent;
        overlay->background_color = AndroidColorToRgba(0x88000000U);
        tree.Attach(tree.Root(), root);
        const auto panel = tree.CreateNode(ui::UiClass::LinearLayout);
        modal.panel = panel;
        modal.viewport = {static_cast<std::int32_t>(context->surface_width),
                          static_cast<std::int32_t>(context->surface_height)};
        modal.density = density;
        auto* body = tree.Get(panel);
        body->orientation = ui::Orientation::Vertical;
        body->layout.width = {ui::SizeMode::Fixed, std::min(static_cast<std::int32_t>(480 * density),
                                                            static_cast<std::int32_t>(context->surface_width) - 32)};
        body->layout.height.mode = ui::SizeMode::WrapContent;
        body->layout.layout_gravity = 0x11;
        body->background_color = AndroidColorToRgba(0xffffffffU);
        const auto padding = static_cast<std::int32_t>(12 * density);
        body->padding = {padding, padding, padding, padding};
        tree.Attach(root, panel);
        const auto add_text = [&](const std::u16string& value, float size, bool bold) {
            if (value.empty()) return;
            const auto id = tree.CreateNode(ui::UiClass::TextView);
            auto* node = tree.Get(id);
            node->text = value;
            node->text_color = 0x000000ffU;
            node->text_size_px = size * density;
            node->text_style = bold ? 1U : 0U;
            node->layout.width.mode = ui::SizeMode::MatchParent;
            node->layout.height.mode = ui::SizeMode::WrapContent;
            node->padding.bottom = static_cast<std::int32_t>(8 * density);
            tree.Attach(panel, id);
        };
        add_text(title, 18, true);
        add_text(message, 14, false);
        const auto row = tree.CreateNode(ui::UiClass::LinearLayout);
        tree.Get(row)->orientation = ui::Orientation::Horizontal;
        tree.Get(row)->layout.width.mode = ui::SizeMode::MatchParent;
        tree.Get(row)->layout.height.mode = ui::SizeMode::WrapContent;
        tree.Get(row)->gravity = 0x05;
        tree.Attach(panel, row);
        for (const std::size_t i : {2U, 1U, 0U})
            if (!button_labels[i].empty()) {
                const auto id = tree.CreateNode(ui::UiClass::Button);
                modal.buttons[i] = id;
                auto* node = tree.Get(id);
                node->text = button_labels[i];
                node->text_size_px = 14 * density;
                node->text_color = 0x000000ffU;
                node->background_color = AndroidColorToRgba(0xffddddddU);
                node->padding = {padding, padding / 2, padding, padding / 2};
                node->focusable = true;
                node->focusable_in_touch_mode = true;
                node->layout.width.mode = ui::SizeMode::WrapContent;
                node->layout.height.mode = ui::SizeMode::WrapContent;
                tree.Attach(row, id);
            }
        {
            const ui::UiMetrics managed_viewport{static_cast<std::int32_t>(context->surface_width),
                                                 static_cast<std::int32_t>(context->surface_height)};
            ui::LayoutUiTree(*modal.host_tree, managed_viewport);
        }
        const auto bounds = tree.Get(panel)->screen_frame;
        if (bounds.top < 8 || bounds.bottom > static_cast<std::int32_t>(context->surface_height) - 8)
            unsupported("Dialog content cannot fit the bounded viewport");
        for (const auto button : modal.buttons)
            if (button && tree.Get(*button)->screen_frame.right > bounds.right - padding)
                unsupported("Dialog buttons cannot fit the bounded viewport");
        // No guest callbacks occur during geometry construction. Commit after validation.
        if (previous != context->dialog_presentations.end()) {
            *previous = modal;
        } else
            context->dialog_presentations.push_back(modal);
        context->dialog_host_revision = modal.revision;
        FocusModal(*context, modal);
        ClearModalCapture(*context);
        PublishAndroidPresentation(*context);
    } catch (const std::runtime_error& error) {
        if (tree.Get(root)) tree.DestroySubtree(root);
        unsupported(error.what());
    } catch (...) {
        if (tree.Get(root)) tree.DestroySubtree(root);
        throw;
    }
}
}  // namespace android_intrinsics

// Shared timed KeyEvent materialization for normal and modal input.
dexvm::VmObjectRef MakeAndroidKeyEvent(dexvm::Interpreter& vm, const AndroidBoundaryInput& input) {
    using namespace dexvm;
    const auto event = vm.NewIntrinsicInstance("Landroid/view/KeyEvent;");
    const auto roots = vm.ProtectReferences(std::array{event});
    const auto constructor = vm.Linker().FindDirectMethod(vm.Model().ObjectClass(event), "<init>", "(JJIIIIIIII)V");
    if (!constructor)
        throw DexVmError(DexVmErrorReason::unresolved_reference, "KeyEvent timed constructor is unavailable");
    const auto result =
        vm.Call(*constructor, std::vector<VmValue>{VmValue::Ref(event), VmValue::Long(input.down_time_ns / 1000000),
                                                   VmValue::Long(input.event_time_ns / 1000000),
                                                   VmValue::Int(input.action), VmValue::Int(input.code),
                                                   VmValue::Int(input.repeat_count), VmValue::Int(input.meta_state),
                                                   VmValue::Int(input.device_id), VmValue::Int(input.scan_code),
                                                   VmValue::Int(input.flags), VmValue::Int(input.source)});
    if (result.exception.IsValid())
        throw VmJavaThrow{vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                          result.exception};
    SetAndroidKeyEventUnicode(vm, event, input.unicode_char);
    return event;
}

bool DispatchAndroidDialogInput(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                                const AndroidBoundaryInput& input) {
    using namespace dexvm;
    {
        if (vm.CurrentContextToken() != 1U || context.ui_java_owner_thread != std::this_thread::get_id())
            throw VmJavaThrow{"Ljava/lang/IllegalStateException;", "Modal input requires owner thread"};
        SynchronizeAndroidDialogs(context);
        const auto snapshot =
            context.input_presentation ? context.input_presentation : AcquireAndroidPresentation(context);
        if (input.type == AndroidBoundaryInputType::key) {
            if (input.action == 0)
                context.dialog_key_tails.erase(input.code);
            else if (context.dialog_key_tails.erase(input.code))
                return true;
        } else if (context.dialog_pointer_tail) {
            const auto action = input.action & 0xff;
            if (action == 0)
                context.dialog_pointer_tail = false;
            else {
                if (action == 1 || action == 3) context.dialog_pointer_tail = false;
                return true;
            }
        }
        if (!snapshot) return HasAndroidModalDialog(context);
        const auto* top = snapshot->modal_stack.empty() ? nullptr : &snapshot->modal_stack.back();
        const auto* live = TopDialog(context);
        if (top && (!live || live->partition != top->partition || live->generation != top->generation ||
                    snapshot->viewport != ui::Size{static_cast<std::int32_t>(context.surface_width),
                                                   static_cast<std::int32_t>(context.surface_height)})) {
            ClearModalCapture(context);
            return true;  // Retained pixels cannot dispatch into a hidden/retired owner or new metrics.
        }
        const auto capture = [&] {
            if (top)
                context.dialog_capture =
                    DexVmAndroidContext::ModalCapture{top->partition, top->generation, top->dialog};
        };
        const auto contains = [&](ui::UiNodeId id) {
            const auto* node = top ? top->tree->Get(id) : nullptr;
            return node && top->tree->IsVisible(id) &&
                   android_intrinsics::Contains(node->screen_frame, input.x, input.y);
        };
        if (input.type == AndroidBoundaryInputType::key) {
            if (!top) return false;
            const bool tracked = context.dialog_keys_down.contains(input.code);
            if (input.action == 0) {
                capture();
                context.dialog_keys_down.insert(input.code);
            }
            if (input.action == 1) context.dialog_keys_down.erase(input.code);
            if (input.action == 0 || tracked) {
                const auto dialog = top->dialog;
                const auto event = MakeAndroidKeyEvent(vm, input);
                const auto roots = vm.ProtectReferences(std::array{dialog, event});
                static_cast<void>(android_intrinsics::CallAndroidMethod(
                    vm, dialog, "dispatchKeyEvent", "(Landroid/view/KeyEvent;)Z", {VmValue::Ref(event)}));
            }
            if (context.dialog_keys_down.empty() && !context.dialog_pointer_active) context.dialog_capture.reset();
            return true;
        }
        const auto action = input.action & 0xff;
        if (!top) return false;
        if (action == 0) {
            ClearModalCapture(context);
            context.dialog_pointer_tail = false;
            capture();
            context.dialog_pointer_active = true;
            for (const auto button : top->buttons)
                if (button && contains(*button)) {
                    context.dialog_pressed_button = button;
                    if (auto* modal = TopDialog(context)) {
                        modal->host_tree->SetPressed(*button, true);
                        static_cast<void>(modal->host_tree->RequestFocus(*button, true));
                    }
                    break;
                }
        } else if (action == 1) {
            const auto pressed = context.dialog_pressed_button;
            const bool active = context.dialog_pointer_active && context.dialog_capture &&
                                context.dialog_capture->partition == top->partition &&
                                context.dialog_capture->generation == top->generation;
            ClearModalCapture(context);
            context.dialog_pointer_tail = false;
            if (active && pressed && contains(*pressed)) {
                for (std::size_t i = 0; i < 3; ++i)
                    if (top->buttons[i] == pressed) {
                        const auto roots = vm.ProtectReferences(std::array{top->dialog});
                        static_cast<void>(
                            android_intrinsics::CallAndroidMethod(vm, top->dialog, "performButtonClick", "(I)V",
                                                                  {VmValue::Int(-static_cast<std::int32_t>(i) - 1)}));
                        break;
                    }
            }
        } else if (action == 2 && context.dialog_pressed_button && !contains(*context.dialog_pressed_button)) {
            if (auto* modal = TopDialog(context)) modal->host_tree->SetPressed(*context.dialog_pressed_button, false);
            context.dialog_pressed_button.reset();
        } else if (action == 3) {
            ClearModalCapture(context);
            context.dialog_pointer_tail = false;
        }
        return true;
    }
}
}  // namespace ogplay::runtime

namespace ogplay::runtime {
bool DispatchAndroidBoundedEditInput(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                                     const AndroidBoundaryInput& input) {
    if (input.type != AndroidBoundaryInputType::key || !context.content_view.IsValid()) return false;
    const auto focused =
        android_intrinsics::CallAndroidMethod(vm, context.content_view, "findFocus", "()Landroid/view/View;").ref;
    if (!focused.IsValid() || !AndroidViewBelongsToRoot(vm, context, focused) ||
        android_intrinsics::UiClassForObject(vm, focused) != ui::UiClass::EditText)
        return false;
    if (input.code != 21 && input.code != 22 && input.code != 67 && input.code != 112 && input.unicode_char <= 0)
        return false;
    if (!input.pressed) return true;
    if (input.meta_state != 0 && (input.code == 21 || input.code == 22 || input.code == 67 || input.code == 112)) {
        if (auto* ledger = vm.Ledger()) ledger->RecordUnimplemented("dexvm.text_constructor", 0);
        throw dexvm::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "modified movement/deletion unsupported"};
    }
    auto& refs = context.view_adapter_references.at(focused.Value());
    const auto node = FindViewUiNode(context, focused.Value());
    const auto length = static_cast<std::int32_t>(context.ui_tree.Get(*node)->text.size());
    auto start = std::min(refs.selection_start, refs.selection_end);
    auto end = std::max(refs.selection_start, refs.selection_end);
    if (input.code == 21 || input.code == 22) {
        const auto next = input.code == 21 ? (start != end ? start : std::max(0, start - 1))
                                           : (start != end ? end : std::min(length, end + 1));
        refs.selection_start = refs.selection_end = next;
        return true;
    }
    std::u16string replacement;
    if (input.code == 67) {
        if (start == end) start = std::max(0, start - 1);
    } else if (input.code == 112) {
        if (start == end) end = std::min(length, end + 1);
    } else {
        if (input.unicode_char > 0xffff) {
            if (auto* ledger = vm.Ledger()) ledger->RecordUnimplemented("dexvm.text_constructor", 0);
            throw dexvm::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "supplementary editing unsupported"};
        }
        replacement.push_back(static_cast<char16_t>(input.unicode_char));
    }
    // Collapse at commit, before on/after callbacks; reentrant callbacks retain
    // authority over the resulting selection. Validation failures change nothing.
    static_cast<void>(android_intrinsics::ApplyTextEdit(vm, context, focused, start, end - start, replacement, true));
    return true;
}
}  // namespace ogplay::runtime

namespace ogplay::runtime {
namespace {
struct ApplicationRootScope final {
    DexVmAndroidContext& context;
    dexvm::VmObjectRef previous;
    std::shared_ptr<const ui::UiTree> projection;

    ApplicationRootScope(DexVmAndroidContext& state, dexvm::VmObjectRef root,
                         std::shared_ptr<const ui::UiTree> input = {})
        : context(state), previous(state.application_window_root_override),
          projection(state.application_input_projection) {
        context.application_window_root_override = root;
        context.application_input_projection = std::move(input);
    }

    ~ApplicationRootScope() {
        context.application_window_root_override = previous;
        context.application_input_projection = std::move(projection);
    }
};

void RevokeApplicationPointer(dexvm::Interpreter& vm, DexVmAndroidContext& context) {
    if (!context.application_pointer_active ||
        context.application_pointer_revision == context.application_window_revision)
        return;
    const auto snapshot = context.input_presentation;
    if (!snapshot) context.input_presentation = AcquireAndroidPresentation(context);

    struct Restore {
        DexVmAndroidContext& context;
        std::shared_ptr<const DexVmAndroidContext::PresentationSnapshot> snapshot;

        ~Restore() { context.input_presentation = std::move(snapshot); }
    } restore{context, snapshot};

    ApplicationRootScope scope(context, context.application_pointer_root, context.application_pointer_projection);
    auto input = *context.application_pointer_last_input;
    input.action = 3;
    const auto event = MakeMotionEvent(vm, input);
    const auto roots = vm.ProtectReferences(std::array{event, context.application_pointer_root});
    if (context.application_pointer_root.IsValid())
        static_cast<void>(android_intrinsics::CallAndroidMethod(vm, context.application_pointer_root,
                                                                "dispatchTouchEvent", "(Landroid/view/MotionEvent;)Z",
                                                                {dexvm::VmValue::Ref(event)}));
    else {
        const auto result = DispatchDeepTouchEvent(vm, context, 3, input.x, input.y, context.ui_touch_capture, &input);
        if (result.error) throw std::runtime_error(*result.error);
    }
    context.ui_touch_capture = 0;
    context.ui_touch_targets.clear();
    context.ui_touch_clicks.clear();
    context.input_capture_identity.reset();
    context.input_scroll_identity.reset();
    context.application_pointer_active = false;
    context.application_pointer_tail = true;
    context.application_pointer_root = dexvm::VmObjectRef{};
    context.application_pointer_projection.reset();
}
}  // namespace

dexvm::VmObjectRef AndroidApplicationWindowRoot(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                                                dexvm::VmObjectRef view) {
    for (std::size_t depth = 0; view.IsValid() && depth <= ui::UiTree::kMaxDepth; ++depth) {
        for (const auto& window : context.application_windows)
            if (window.root == view && !window.removed) return view;
        if (!context.view_java_hierarchy_fields) return dexvm::VmObjectRef{};
        dexvm::IntrinsicContext call{vm, view, {}};
        view = dexvm::IntrinsicCall(call).GetRef(context.view_java_hierarchy_fields->parent);
    }
    return dexvm::VmObjectRef{};
}

void SynchronizeAndroidApplicationWindows(dexvm::Interpreter& vm, DexVmAndroidContext& context) {
    if (context.input_presentation || context.ui_layout_dispatching) return;
    RevokeApplicationPointer(vm, context);
    std::vector<std::uint64_t> generations, first_ready;
    for (const auto& window : context.application_windows)
        generations.push_back(window.generation);
    for (const auto generation : generations) {
        auto found = std::find_if(context.application_windows.begin(), context.application_windows.end(),
                                  [&](const auto& window) { return window.generation == generation; });
        if (found == context.application_windows.end() || found->removed) continue;
        const auto owner = std::find_if(context.activity_stack.begin(), context.activity_stack.end(),
                                        [&](const auto& record) { return record.object == found->owner; });
        if (owner == context.activity_stack.end() || !owner->visible || owner->object != context.activity) continue;
        const auto root = found->root;
        const bool first_projection = !found->projection;
        ApplicationRootScope scope(context, root);
        const auto roots = vm.ProtectReferences(std::array{root});
        const auto traversed = EnsureAndroidUiTraversal(vm, context, AndroidUiTraversalReason::BeforePresent,
                                                        AndroidUiTraversalPhase::ResolveAndNotifySizes);
        if (traversed) DispatchAndroidGlobalLayout(vm, context);
        if (traversed && first_projection) first_ready.push_back(generation);
    }
    std::vector<dexvm::VmObjectRef> removed;
    for (const auto& window : context.application_windows)
        if (window.removed) removed.push_back(window.root);
    std::erase_if(context.application_windows, [](const auto& window) { return window.removed; });
    for (const auto root : removed) {
        if (std::ranges::any_of(context.application_windows, [&](const auto& window) { return window.root == root; }))
            continue;
        context.ui_window_schedules.erase(root);
        context.ui_traversal_diagnostics.erase(root);
        ++context.ui_mount_generations[root];
    }
    if (PublishAndroidPresentation(context)) {
        const auto snapshot = AcquireAndroidPresentation(context);
        for (const auto& window : snapshot->application_windows) {
            if (std::ranges::find(first_ready, window.generation) == first_ready.end()) continue;
            const auto id = FindViewUiNode(context, window.root.Value());
            const auto* node = id ? window.projection->Get(*id) : nullptr;
            if (auto* logger = vm.Log())
                logger->Write(core::LogLevel::info, "android.window", "application window published above Activity", {},
                              {{"owner", static_cast<std::uint64_t>(window.owner.Value())},
                               {"root", static_cast<std::uint64_t>(window.root.Value())},
                               {"generation", window.generation},
                               {"width", static_cast<std::uint64_t>(snapshot->viewport.width)},
                               {"height", static_cast<std::uint64_t>(snapshot->viewport.height)},
                               {"partitions", static_cast<std::uint64_t>(snapshot->guest_window_epoch_refs.size())},
                               {"image_bytes", static_cast<std::uint64_t>(
                                                   node && node->image_bitmap ? node->image_bitmap->rgba8.size() : 0)},
                               {"presentation_epoch", snapshot->presentation_epoch}});
        }
    }
}

void CloseAndroidApplicationWindows(dexvm::Interpreter& vm, DexVmAndroidContext& context, dexvm::VmObjectRef owner) {
    for (auto& window : context.application_windows) {
        if (window.owner != owner || window.removed) continue;
        window.removed = true;
        ++context.application_window_revision;
        if (auto* logger = vm.Log())
            logger->Write(core::LogLevel::warn, "android.window", "Activity has leaked an application window", {},
                          {{"owner", static_cast<std::uint64_t>(owner.Value())},
                           {"root", static_cast<std::uint64_t>(window.root.Value())},
                           {"generation", window.generation}});
    }
}

bool DispatchAndroidApplicationWindowInput(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                                           const AndroidBoundaryInput& input) {
    using android_intrinsics::CallAndroidMethod;
    if (input.type == AndroidBoundaryInputType::key) {
        const auto root = context.focused_application_window;
        if (!root.IsValid()) return false;
        const auto event = MakeAndroidKeyEvent(vm, input);
        const auto roots = vm.ProtectReferences(std::array{root, event});
        static_cast<void>(CallAndroidMethod(vm, root, "dispatchKeyEvent", "(Landroid/view/KeyEvent;)Z",
                                            {dexvm::VmValue::Ref(event)}));
        return true;  // An unconsumed subwindow key never falls through to Activity.
    }
    RevokeApplicationPointer(vm, context);
    const auto action = input.action & 0xff;
    if (context.application_pointer_tail && action != 0) {
        if (action == 1 || action == 3) context.application_pointer_tail = false;
        return true;
    }
    if (action == 0) {
        context.application_pointer_tail = false;
        context.application_pointer_root = dexvm::VmObjectRef{};
        context.application_pointer_projection = context.input_presentation->main_window;
        for (const auto& window : context.input_presentation->application_windows) {
            if (window.owner != context.activity || (window.flags & 16)) continue;
            const auto node = FindViewUiNode(context, window.root.Value());
            if (!node || !window.projection->Get(*node) || !window.projection->IsVisible(*node)) continue;
            const auto frame = window.projection->Get(*node)->screen_frame;
            if (input.x < frame.left || input.x >= frame.right || input.y < frame.top || input.y >= frame.bottom)
                continue;
            context.application_pointer_root = window.root;
            context.application_pointer_projection = window.projection;
            break;
        }
        context.application_pointer_revision = context.application_window_revision;
        context.application_pointer_active = true;
    }
    context.application_pointer_last_input = std::make_shared<AndroidBoundaryInput>(input);
    const auto root = context.application_pointer_root;
    if (action == 1 || action == 3) context.application_pointer_active = false;
    if (!root.IsValid()) return false;
    ApplicationRootScope scope(context, root, context.application_pointer_projection);
    const auto event = MakeMotionEvent(vm, input);
    const auto roots = vm.ProtectReferences(std::array{root, event});
    static_cast<void>(CallAndroidMethod(vm, root, "dispatchTouchEvent", "(Landroid/view/MotionEvent;)Z",
                                        {dexvm::VmValue::Ref(event)}));
    if (action == 1 || action == 3) {
        context.application_pointer_root = dexvm::VmObjectRef{};
        context.application_pointer_projection.reset();
        context.application_pointer_last_input.reset();
    }
    return true;  // Full-frame window blocks lower windows regardless of consumption.
}
}  // namespace ogplay::runtime
