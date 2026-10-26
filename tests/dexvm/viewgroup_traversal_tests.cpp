#include "android_value_fixture.h"
#include "container_traversal_fixture.h"
#include "boot_dex.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include <doctest/doctest.h>
#include <array>
#include <fstream>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

using namespace ogplay::test::android_value;
using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

TEST_CASE("DVM-281 ROM multi-level hierarchy parameters projection scheduling and GC") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        std::function<void()> hook;
        auto hook_type =
            IntrinsicClassBuilder::Class("Ltest/TraversalHook;", "Ljava/lang/Object;", {"Ljava/lang/Runnable;"});
        hook_type.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); });
        hook_type.VirtualMethod("run", "()V", [&hook](IntrinsicContext&) {
            if (hook) hook();
            return VmValue::Void();
        });
        AndroidValueVm f(backend, {std::move(hook_type).Build()}, false, "view_measure.dex", false,
                         AndroidUiTraversalOwner::JavaTraversal, OGPLAY_DEXVM_FIXTURE_DIR "/view-measure.jar");
        f.vm.SetGcIntegration(
            {{}, {}, [&f](const VmRootVisitor& visit) { VisitAndroidSessionRoots(*f.context, visit); }});
        f.context->target_sdk_version = 19;
        const auto owner = f.New("Landroid/app/Activity;");
        const auto make = [&](bool group) {
            return f.New(group ? "Ltest/HierarchyGroup;" : "Ltest/MeasuredLeaf;", "(Landroid/content/Context;)V",
                         {VmValue::Ref(owner)});
        };
        const auto root = make(true);
        const auto root_scope = f.vm.ProtectReferences(std::array{root});
        const auto info = f.On(owner, "getApplicationInfo", "()Landroid/content/pm/ApplicationInfo;").ref;
        const auto info_flags = f.linker.FindFieldRecursive(f.model.ObjectClass(info), "flags", "I");
        REQUIRE(info_flags);
        f.model.InstanceSlots(info)[f.linker.Field(*info_flags).slot].bits |= 0x400000U;  // FLAG_SUPPORTS_RTL

        const auto first = make(true);
        const auto second = make(true);
        const auto leaf = make(false);
        const auto sibling = make(false);
        f.context->content_view = root;
        const auto add = [&](auto parent, auto child) {
            f.On(parent, "addView", "(Landroid/view/View;)V", {VmValue::Ref(child)});
        };
        const auto remove = [&](auto parent, auto child) {
            f.On(parent, "removeView", "(Landroid/view/View;)V", {VmValue::Ref(child)});
        };
        const auto get_parent = [&](auto child) { return f.On(child, "getParent", "()Landroid/view/ViewParent;").ref; };
        const auto child_at = [&](auto parent, int index) {
            return f.On(parent, "getChildAt", "(I)Landroid/view/View;", {VmValue::Int(index)}).ref;
        };
        std::vector<DexVmTraceEntry> entered;
        const auto capture_trace = [&] {
            for (const auto& event : f.vm.Trace("", 10000))
                if (entered.empty() || event.sequence > entered.back().sequence) entered.push_back(event);
        };
        const auto tick = [&] {
            const auto result = EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                                         AndroidUiTraversalPhase::ResolveOnly, ui::Size{160, 100});
            capture_trace();
            return result;
        };
        add(root, first);
        add(first, second);
        add(second, leaf);
        add(second, sibling);
        f.On(leaf, "setId", "(I)V", {VmValue::Int(101)});
        f.On(sibling, "setId", "(I)V", {VmValue::Int(102)});
        CHECK_FALSE(get_parent(root).IsValid());
        CHECK(get_parent(leaf) == second);
        CHECK(f.On(root, "findViewById", "(I)Landroid/view/View;", {VmValue::Int(101)}).ref == leaf);
        const auto duplicate = f.OnOutcome(first, "addView", "(Landroid/view/View;)V", {VmValue::Ref(leaf)});
        REQUIRE(duplicate.exception.IsValid());
        CHECK(f.linker.Class(duplicate.exception_class).descriptor == "Ljava/lang/IllegalStateException;");
        CHECK(get_parent(leaf) == second);
        REQUIRE(tick());
        const auto check_projection = [&] {
            const auto snapshot = f.context->ui_tree.PublishedProjection();
            REQUIRE(snapshot);
            const auto check = [&](auto parent, const std::vector<VmObjectRef>& children) {
                const auto id = *FindViewUiNode(*f.context, parent.Value());
                const auto* node = snapshot->Get(id);
                REQUIRE(node);
                IntrinsicContext call{f.vm, parent, {}};
                IntrinsicCall fields(call);
                const auto array = fields.GetRef(f.context->view_group_java_fields->children);
                CHECK(fields.GetInt(f.context->view_group_java_fields->count) == static_cast<int>(children.size()));
                REQUIRE(node->children.size() == children.size());
                for (std::size_t i = 0; i < children.size(); ++i) {
                    const auto child = children[i];
                    CHECK(child_at(parent, static_cast<int>(i)) == child);
                    CHECK(f.model.GetObjectElement(array, static_cast<int>(i)) == child);
                    CHECK(get_parent(child) == parent);
                    CHECK(node->children[i] == *FindViewUiNode(*f.context, child.Value()));
                    CHECK(snapshot->Get(node->children[i])->parent == id);
                }
            };
            check(root, {first});
            check(first, {second});
            check(second, {leaf, sibling});
            CHECK(snapshot->FindByAndroidId(101) == FindViewUiNode(*f.context, leaf.Value()));
            CHECK(snapshot->FindByAndroidId(102) == FindViewUiNode(*f.context, sibling.Value()));
        };
        check_projection();
        // Fixture attributes: explicit root text settings, inherited through every real parent.
        // This exercises the parent getters that were previously failure targets.
        for (const auto view : {root, first, second, leaf}) {
            IntrinsicContext call{f.vm, view, {}};
            IntrinsicCall fields(call);
            const auto token = f.context->view_java_measure_fields->flags[1];
            const auto raw = view == root ? 0x8100 : 0;  // TEXT_DIRECTION_RTL, TEXT_ALIGNMENT_CENTER
            fields.SetInt(token, (fields.GetInt(token) & ~0xfffc0) | raw);
        }
        f.On(root, "requestLayout", "()V");
        REQUIRE(tick());
        CHECK(f.On(leaf, "getTextDirection", "()I").AsInt() == 4);
        CHECK(f.On(leaf, "getTextAlignment", "()I").AsInt() == 4);

        const auto prior_id_snapshot = f.context->ui_tree.PublishedProjection();
        f.On(leaf, "setId", "(I)V", {VmValue::Int(-1)});
        CHECK(f.On(leaf, "getId", "()I").AsInt() == -1);
        CHECK_FALSE(f.On(root, "findViewById", "(I)Landroid/view/View;", {VmValue::Int(101)}).ref.IsValid());
        // setId alone does not schedule ViewRoot traversal in the ROM.
        CHECK_FALSE(tick());
        CHECK(f.context->ui_tree.PublishedProjection() == prior_id_snapshot);
        f.On(root, "requestLayout", "()V");
        REQUIRE(tick());
        CHECK_FALSE(f.context->ui_tree.PublishedProjection()->FindByAndroidId(101).has_value());
        CHECK(prior_id_snapshot->FindByAndroidId(101).has_value());
        f.On(leaf, "setId", "(I)V", {VmValue::Int(101)});
        f.On(root, "requestLayout", "()V");
        REQUIRE(tick());
        // All public add overloads share the ROM array and parameter reference.
        const auto extra = make(false);
        const auto extra_scope = f.vm.ProtectReferences(std::array{extra});
        f.On(root, "addView", "(Landroid/view/View;II)V", {VmValue::Ref(extra), VmValue::Int(23), VmValue::Int(17)});
        REQUIRE(tick());
        CHECK(f.On(extra, "getMeasuredWidth", "()I").AsInt() == 23);
        const auto extra_params = f.On(extra, "getLayoutParams", "()Landroid/view/ViewGroup$LayoutParams;").ref;
        remove(root, extra);
        f.On(root, "addView", "(Landroid/view/View;ILandroid/view/ViewGroup$LayoutParams;)V",
             {VmValue::Ref(extra), VmValue::Int(0), VmValue::Ref(extra_params)});
        CHECK(child_at(root, 0) == extra);
        REQUIRE(tick());
        CHECK(f.context->ui_tree.PublishedProjection()
                  ->Get(*FindViewUiNode(*f.context, root.Value()))
                  ->children.front() == *FindViewUiNode(*f.context, extra.Value()));
        f.On(root, "removeViewAt", "(I)V", {VmValue::Int(0)});
        f.On(root, "addView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
             {VmValue::Ref(extra), VmValue::Ref(extra_params)});
        remove(root, extra);
        f.On(root, "addView", "(Landroid/view/View;I)V", {VmValue::Ref(extra), VmValue::Int(0)});
        remove(root, extra);
        const auto invalid_index =
            f.OnOutcome(root, "addView", "(Landroid/view/View;ILandroid/view/ViewGroup$LayoutParams;)V",
                        {VmValue::Ref(extra), VmValue::Int(99), VmValue::Ref(extra_params)});
        REQUIRE(invalid_index.exception.IsValid());
        CHECK(f.linker.Class(invalid_index.exception_class).descriptor == "Ljava/lang/IndexOutOfBoundsException;");
        CHECK_FALSE(get_parent(extra).IsValid());
        CHECK(f.On(root, "checkLayoutParams", "(Landroid/view/ViewGroup$LayoutParams;)Z", {VmValue::Ref(extra_params)})
                  .AsInt());
        CHECK_FALSE(
            f.On(root, "checkLayoutParams", "(Landroid/view/ViewGroup$LayoutParams;)Z", {VmValue::Ref(VmObjectRef{})})
                .AsInt());
        CHECK(f.On(root, "generateLayoutParams",
                   "(Landroid/view/ViewGroup$LayoutParams;)Landroid/view/ViewGroup$LayoutParams;",
                   {VmValue::Ref(extra_params)})
                  .ref == extra_params);
        REQUIRE(tick());
        CHECK_FALSE(child_at(second, -1).IsValid());
        CHECK(f.On(second, "indexOfChild", "(Landroid/view/View;)I", {VmValue::Ref(root)}).AsInt() == -1);
        f.On(second, "bringChildToFront", "(Landroid/view/View;)V", {VmValue::Ref(leaf)});
        REQUIRE(tick());
        CHECK(child_at(second, 1) == leaf);
        CHECK(f.context->ui_tree.PublishedProjection()
                  ->Get(*FindViewUiNode(*f.context, second.Value()))
                  ->children.back() == *FindViewUiNode(*f.context, leaf.Value()));
        f.On(second, "bringChildToFront", "(Landroid/view/View;)V", {VmValue::Ref(root)});
        CHECK(child_at(second, 1) == leaf);
        remove(second, leaf);
        CHECK_FALSE(get_parent(leaf).IsValid());
        add(first, leaf);
        REQUIRE(tick());
        CHECK(get_parent(leaf) == first);
        CHECK(f.context->ui_tree.PublishedProjection()->Get(*FindViewUiNode(*f.context, leaf.Value()))->parent ==
              FindViewUiNode(*f.context, first.Value()));
        remove(first, leaf);
        add(second, leaf);
        {
            const auto children_scope = f.vm.ProtectReferences(std::array{leaf, sibling});
            f.On(second, "removeAllViews", "()V");
            CHECK(f.On(second, "getChildCount", "()I").AsInt() == 0);
            CHECK_FALSE(get_parent(leaf).IsValid());
            CHECK_FALSE(get_parent(sibling).IsValid());
            REQUIRE(tick());
            CHECK_FALSE(f.context->ui_tree.PublishedProjection()->FindByAndroidId(101));
            add(second, sibling);
            add(second, leaf);
        }
        const auto params = f.On(leaf, "getLayoutParams", "()Landroid/view/ViewGroup$LayoutParams;").ref;
        REQUIRE(params.IsValid());
        CHECK(f.On(second, "getChildCount", "()I").AsInt() == 2);
        const auto margin =
            f.New("Landroid/view/ViewGroup$MarginLayoutParams;", "(II)V", {VmValue::Int(30), VmValue::Int(20)});
        f.On(leaf, "setLayoutParams", "(Landroid/view/ViewGroup$LayoutParams;)V", {VmValue::Ref(margin)});
        CHECK(f.On(leaf, "getLayoutParams", "()Landroid/view/ViewGroup$LayoutParams;").ref == margin);
        f.On(second, "margins", "(Landroid/view/View;II)V",
             {VmValue::Ref(leaf), VmValue::Int(0x40000064), VmValue::Int(0x40000050)});
        CHECK(f.On(leaf, "getMeasuredWidth", "()I").AsInt() == 30);
        CHECK(f.On(leaf, "getMeasuredHeight", "()I").AsInt() == 20);
        f.On(leaf, "setLayoutParams", "(Landroid/view/ViewGroup$LayoutParams;)V", {VmValue::Ref(params)});
        REQUIRE(tick());
        const auto layouts = [&] { return f.On(root, "layouts", "()I").AsInt(); };
        auto before = layouts();
        f.On(root, "requestDuringLayout", "(Landroid/view/View;I)V", {VmValue::Ref(leaf), VmValue::Int(1)});
        REQUIRE(tick());
        CHECK(layouts() == before + 2);
        CHECK(AndroidUiMeasureRequests(f.vm, *f.context).empty());
        before = layouts();
        f.On(root, "requestDuringLayout", "(Landroid/view/View;I)V", {VmValue::Ref(leaf), VmValue::Int(2)});
        REQUIRE(tick());
        CHECK(layouts() == before + 2);
        CHECK(AndroidUiMeasureRequests(f.vm, *f.context).contains(leaf));
        before = layouts();
        REQUIRE(tick());
        CHECK(layouts() == before + 2);
        f.On(root, "requestDuringLayout", "(Landroid/view/View;I)V", {VmValue::Ref(VmObjectRef{}), VmValue::Int(0)});
        REQUIRE(tick());
        CHECK(AndroidUiMeasureRequests(f.vm, *f.context).empty());
        const auto hook_object = f.New("Ltest/TraversalHook;");
        f.On(root, "setHook", "(Ljava/lang/Runnable;)V", {VmValue::Ref(hook_object)});
        const auto before_callback = f.context->ui_tree.PublishedProjection();
        const auto old_epoch = f.context->ui_measure_epoch;
        hook = [&] { ++f.context->ui_content_revision; };
        f.On(root, "requestLayout", "()V");
        CHECK_FALSE(tick());
        CHECK(f.context->ui_tree.PublishedProjection() == before_callback);
        CHECK(f.context->ui_measure_epoch == old_epoch);
        hook = {};
        f.On(root, "setHook", "(Ljava/lang/Runnable;)V", {VmValue::Ref(VmObjectRef{})});
        REQUIRE(tick());
        const auto stable = f.context->ui_tree.PublishedProjection();
        const auto old_target = *f.context->ui_tree.ProjectionTarget();
        auto old_generation = old_target;
        --old_generation.window_generation;
        CHECK_THROWS_AS(CommitAndroidMeasuredRootProjection(f.vm, *f.context, root, old_generation,
                                                            f.context->ui_measure_epoch + 1),
                        VmJavaThrow);
        CHECK(f.context->ui_tree.PublishedProjection() == stable);
        ++f.context->ui_content_revision;
        CHECK_THROWS_AS(
            CommitAndroidMeasuredRootProjection(f.vm, *f.context, root, old_target, f.context->ui_measure_epoch + 1),
            VmJavaThrow);
        CHECK(f.context->ui_tree.PublishedProjection() == stable);
        REQUIRE(tick());
        // Malformed Java parent is a projection negative control: no partial epoch.
        const auto snapshot = f.context->ui_tree.PublishedProjection();
        IntrinsicContext call{f.vm, leaf, {}};
        IntrinsicCall fields(call);
        fields.SetRef(f.context->view_java_hierarchy_fields->parent, root);
        CHECK_THROWS_AS(CommitAndroidMeasuredRootProjection(f.vm, *f.context, root,
                                                            *f.context->ui_tree.ProjectionTarget(),
                                                            f.context->ui_measure_epoch + 1),
                        VmJavaThrow);
        CHECK(f.context->ui_tree.PublishedProjection() == snapshot);
        fields.SetRef(f.context->view_java_hierarchy_fields->parent, second);
        // Java strong edges retain both directions; binding maps are weak.
        static_cast<void>(f.vm.CollectGarbage("vd06a-live-hierarchy"));
        CHECK(f.model.IsValidRef(leaf));
        CHECK(f.model.IsValidRef(second));
        CHECK(f.model.IsValidRef(margin) == false);
        f.On(second, "requestLayout", "()V");
        remove(first, second);
        CHECK(AndroidUiMeasureRequests(f.vm, *f.context).contains(second));
        static_cast<void>(f.vm.CollectGarbage("vd06a-removed-hierarchy"));
        CHECK_FALSE(f.model.IsValidRef(second));
        CHECK_FALSE(f.model.IsValidRef(leaf));
        CHECK_FALSE(f.model.IsValidRef(sibling));
        CHECK(f.model.IsValidRef(first));
        CHECK(f.model.IsValidRef(root));
        CHECK_FALSE(FindViewUiNode(*f.context, leaf.Value()).has_value());
        REQUIRE(tick());
        CHECK_FALSE(f.context->ui_tree.PublishedProjection()->FindByAndroidId(101).has_value());
        const auto detached_first = f.vm.ProtectReferences(std::array{first});
        f.On(root, "removeViewAt", "(I)V", {VmValue::Int(0)});
        CHECK_FALSE(get_parent(first).IsValid());
        REQUIRE(tick());
        add(root, first);
        f.On(root, "removeAllViews", "()V");
        CHECK(f.On(root, "getChildCount", "()I").AsInt() == 0);
        CHECK_FALSE(get_parent(first).IsValid());
        REQUIRE(tick());
        for (const auto name : {"addView", "removeView", "bringChildToFront", "getChildAt", "measureChild", "layout"}) {
            const auto method = f.linker.FindVtableIndex(
                f.linker.ResolveDescriptor("Landroid/view/ViewGroup;"), name,
                std::string_view(name) == "addView" || std::string_view(name) == "removeView" ||
                        std::string_view(name) == "bringChildToFront"
                    ? "(Landroid/view/View;)V"
                : std::string_view(name) == "getChildAt"   ? "(I)Landroid/view/View;"
                : std::string_view(name) == "measureChild" ? "(Landroid/view/View;II)V"
                                                           : "(IIII)V");
            REQUIRE(method);
            CHECK(
                f.linker.Method(f.linker.Class(f.linker.ResolveDescriptor("Landroid/view/ViewGroup;")).vtable[*method])
                    .kind == MethodKind::interpreted);
        }
        // A child held alone keeps its Java parent alive; clearing mParent releases it.
        const auto isolated_parent = make(true);
        const auto isolated_child = make(false);
        const auto child_scope = f.vm.ProtectReferences(std::array{isolated_child});
        add(isolated_parent, isolated_child);
        static_cast<void>(f.vm.CollectGarbage("vd06a-parent-strong-edge"));
        CHECK(f.model.IsValidRef(isolated_parent));
        CHECK(get_parent(isolated_child) == isolated_parent);
        remove(isolated_parent, isolated_child);
        static_cast<void>(f.vm.CollectGarbage("vd06a-parent-edge-cleared"));
        CHECK_FALSE(f.model.IsValidRef(isolated_parent));
        CHECK(f.model.IsValidRef(isolated_child));
        CHECK(f.ledger.Unimplemented().empty());
        std::ofstream trace(std::string(OGPLAY_SOURCE_DIR) + "/.local/analysis/vd-06a/active-body/hierarchy-" +
                            (backend == InterpreterBackend::threaded ? "threaded" : "switch") + ".trace.json");
        capture_trace();
        REQUIRE_FALSE(entered.empty());
        CHECK(entered.front().sequence == 1);
        for (std::size_t i = 1; i < entered.size(); ++i)
            REQUIRE(entered[i].sequence == entered[i - 1].sequence + 1);
        trace << RenderDexVmTraceJson(entered);
    }
}

TEST_CASE("DVM-281 animated removal explicitly fails and is accounted") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend, {}, false, "view_measure.dex", false, AndroidUiTraversalOwner::JavaTraversal,
                         OGPLAY_DEXVM_FIXTURE_DIR "/view-measure.jar");
        const auto owner = f.New("Landroid/app/Activity;");
        const auto group = f.New("Ltest/HierarchyGroup;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto leaf = f.New("Ltest/MeasuredLeaf;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        f.On(group, "addView", "(Landroid/view/View;)V", {VmValue::Ref(leaf)});
        const auto animation = f.vm.NewIntrinsicInstance("Landroid/view/animation/Animation;");
        IntrinsicContext call{f.vm, leaf, {}};
        IntrinsicCall(call).SetRef(f.context->view_java_measure_fields->unsupported_state[3], animation);
        const auto failed = f.OnOutcome(group, "removeView", "(Landroid/view/View;)V", {VmValue::Ref(leaf)});
        REQUIRE(failed.exception.IsValid());
        CHECK(f.linker.Class(failed.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        const auto hits = f.ledger.Unimplemented();
        REQUIRE(hits.size() == 1);
        CHECK(hits[0].id == "dexvm.view_group_animation");
    }
}

TEST_CASE("DVM-282 ROM flags listeners focus and bounded capture use Java state") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend, {}, false, "view_measure.dex", false, AndroidUiTraversalOwner::JavaTraversal,
                         OGPLAY_DEXVM_FIXTURE_DIR "/view-measure.jar");
        f.vm.SetGcIntegration(
            {{}, {}, [&f](const VmRootVisitor& visit) { VisitAndroidSessionRoots(*f.context, visit); }});
        f.context->ui_test_accessibility_disabled = true;
        f.context->surface_width = 160;
        f.context->surface_height = 100;
        const auto owner = f.New("Landroid/app/Activity;");
        const auto make = [&](bool group) {
            return f.New(group ? "Ltest/HierarchyGroup;" : "Ltest/MeasuredLeaf;", "(Landroid/content/Context;)V",
                         {VmValue::Ref(owner)});
        };
        const auto root = make(true), parent = make(true), leaf = make(false), sibling = make(false);
        const auto roots = f.vm.ProtectReferences(std::array{root, parent, leaf, sibling});
        f.context->content_view = root;
        const auto add = [&](auto p, auto c) { f.On(p, "addView", "(Landroid/view/View;)V", {VmValue::Ref(c)}); };
        add(root, parent);
        add(parent, leaf);
        add(parent, sibling);
        std::vector<DexVmTraceEntry> entered;
        const auto capture = [&] {
            for (const auto& entry : f.vm.Trace("", 8192))
                if (entered.empty() || entry.sequence > entered.back().sequence) entered.push_back(entry);
        };
        const auto tick = [&] {
            const auto result = EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                                         AndroidUiTraversalPhase::ResolveOnly, ui::Size{160, 100});
            capture();
            return result;
        };
        REQUIRE(tick());
        const auto first = f.New("Ltest/StateListener;"), second = f.New("Ltest/StateListener;");
        const auto set = [&](auto view, auto listener) {
            for (const auto* type :
                 {"OnClickListener", "OnTouchListener", "OnLongClickListener", "OnFocusChangeListener"}) {
                const auto name = std::string("set") + type;
                const auto sig = std::string("(Landroid/view/View$") + type + ";)V";
                f.On(view, name.c_str(), sig.c_str(), {VmValue::Ref(listener)});
            }
        };
        set(leaf, first);
        set(sibling, second);
        const auto bits = [&](auto object, const char* name, const char* type = "I") {
            const auto field = f.linker.FindFieldRecursive(f.model.ObjectClass(object), name, type);
            REQUIRE(field);
            return f.model.InstanceSlots(object)[f.linker.Field(*field).slot].bits;
        };
        CHECK(ReadAndroidViewListener(f.vm, *f.context, leaf, AndroidViewListener::Click) == first);
        CHECK(ReadAndroidViewListener(f.vm, *f.context, sibling, AndroidViewListener::Click) == second);
        CHECK_FALSE(InvokeViewOnClick(f.vm, *f.context, sibling.Value()).has_value());
        CHECK(bits(first, "clicks") == 0);
        CHECK(bits(second, "clicks") == 1);
        CHECK(bits(second, "last", "Landroid/view/View;") == sibling.Value());
        CHECK(f.On(leaf, "isClickable", "()Z").AsInt());
        CHECK(f.On(leaf, "isLongClickable", "()Z").AsInt());
        CHECK_FALSE(f.On(leaf, "isFocusable", "()Z").AsInt());
        f.On(leaf, "setFocusableInTouchMode", "(Z)V", {VmValue::Int(1)});
        CHECK(f.On(leaf, "isFocusable", "()Z").AsInt());
        CHECK(f.On(leaf, "isFocusableInTouchMode", "()Z").AsInt());
        f.On(leaf, "setFocusable", "(Z)V", {VmValue::Int(0)});
        CHECK_FALSE(f.On(leaf, "isFocusableInTouchMode", "()Z").AsInt());
        CHECK_FALSE(f.On(leaf, "requestFocus", "()Z").AsInt());
        f.On(leaf, "setFocusable", "(Z)V", {VmValue::Int(1)});
        CHECK(f.On(leaf, "requestFocus", "()Z").AsInt());
        CHECK(f.On(root, "findFocus", "()Landroid/view/View;").ref == leaf);
        CHECK(f.On(root, "getFocusedChild", "()Landroid/view/View;").ref == parent);
        CHECK(f.On(parent, "getFocusedChild", "()Landroid/view/View;").ref == leaf);
        CHECK(f.On(root, "hasFocus", "()Z").AsInt());
        CHECK_FALSE(f.context->ui_tree.Focused());
        f.On(sibling, "setFocusable", "(Z)V", {VmValue::Int(1)});
        CHECK(f.On(sibling, "requestFocus", "()Z").AsInt());
        CHECK(f.On(root, "findFocus", "()Landroid/view/View;").ref == sibling);
        CHECK_FALSE(f.On(leaf, "isFocused", "()Z").AsInt());
        f.On(root, "setDescendantFocusability", "(I)V", {VmValue::Int(0x60000)});
        f.On(sibling, "clearFocus", "()V");
        CHECK_FALSE(f.On(root, "findFocus", "()Landroid/view/View;").ref.IsValid());
        CHECK_FALSE(f.On(leaf, "requestFocus", "()Z").AsInt());
        CHECK(bits(first, "focuses") == 2);
        CHECK(bits(second, "focuses") == 2);
        f.On(root, "setFocusable", "(Z)V", {VmValue::Int(1)});
        CHECK(f.On(root, "requestFocus", "()Z").AsInt());
        CHECK(f.On(root, "findFocus", "()Landroid/view/View;").ref == root);
        f.On(root, "setFocusable", "(Z)V", {VmValue::Int(0)});
        CHECK_FALSE(f.On(root, "findFocus", "()Landroid/view/View;").ref.IsValid());
        f.On(root, "setDescendantFocusability", "(I)V", {VmValue::Int(0x20000)});
        static_cast<void>(f.vm.CollectGarbage("vd06b-java-listeners"));
        CHECK(f.model.IsValidRef(first));
        CHECK(f.model.IsValidRef(second));
        // Focus/clickability changes do not themselves request ROM layout.
        f.On(root, "invalidate", "()V");
        CHECK_FALSE(tick());
        const auto node = *FindViewUiNode(*f.context, leaf.Value());
        for (const auto visibility : {4, 8, 0}) {
            f.On(leaf, "setVisibility", "(I)V", {VmValue::Int(visibility)});
            f.context->ui_tree.Get(node)->visibility = ui::Visibility::Visible;
            // INVISIBLE only invalidates; GONE transitions request layout.
            CHECK(tick() == (visibility != 4));
            CHECK(f.On(leaf, "getVisibility", "()I").AsInt() == visibility);
            CHECK(f.context->ui_tree.PublishedProjection()->Get(node)->visibility ==
                  (visibility == 4   ? ui::Visibility::Invisible
                   : visibility == 8 ? ui::Visibility::Gone
                                     : ui::Visibility::Visible));
            CHECK(ViewContainsPoint(f.vm, *f.context, leaf.Value(), 1, 1) == (visibility == 0));
        }
        f.On(sibling, "setVisibility", "(I)V", {VmValue::Int(4)});
        const auto touch = [&](int action) {
            const auto event = MakeMotionEvent(f.vm, action, 1, 1, 0);
            const auto result = DispatchAndroidViewTouch(f.vm, *f.context, root, event, true);
            capture();
            return result;
        };
        CHECK(touch(0));
        CHECK(f.context->ui_touch_capture == leaf.Value());
        CHECK(touch(1));
        CHECK(bits(first, "clicks") == 1);
        CHECK(bits(second, "clicks") == 1);
        CHECK(bits(first, "last", "Landroid/view/View;") == leaf.Value());
        f.On(leaf, "setEnabled", "(Z)V", {VmValue::Int(0)});
        f.context->ui_tree.Get(node)->enabled = true;
        f.context->ui_tree.Get(node)->clickable = false;
        CHECK_FALSE(tick());
        CHECK_FALSE(f.context->ui_tree.PublishedProjection()->Get(node)->enabled);
        CHECK(f.context->ui_tree.PublishedProjection()->Get(node)->clickable);
        CHECK_FALSE(ViewContainsPoint(f.vm, *f.context, leaf.Value(), 1, 1));
        static_cast<void>(touch(0));
        static_cast<void>(touch(1));
        CHECK(bits(first, "clicks") == 1);
        f.On(leaf, "setEnabled", "(Z)V", {VmValue::Int(1)});
        CHECK(touch(0));
        f.On(parent, "removeView", "(Landroid/view/View;)V", {VmValue::Ref(leaf)});
        CHECK(f.context->ui_touch_capture == 0);
        REQUIRE(tick());
        CHECK(f.context->ui_touch_capture == 0);
        add(parent, leaf);
        CHECK(touch(0));
        const auto replacement = make(true);
        f.context->content_view = replacement;
        ++f.context->ui_content_revision;
        REQUIRE(tick());
        CHECK(f.context->ui_touch_capture == 0);
        CHECK(f.context->ui_touch_targets.empty());
        CHECK(f.context->ui_click_listeners.empty());
        CHECK(f.context->ui_touch_listeners.empty());
        CHECK(f.context->ui_view_layout_params.empty());
        CHECK_FALSE(f.context->ui_tree.Focused());
        for (const auto& [_, references] : f.context->view_adapter_references)
            CHECK_FALSE(references.focus_listener.IsValid());
        CHECK_THROWS_AS(static_cast<void>(f.context->ui_tree.RequestFocus(node, false)), std::runtime_error);
        set(leaf, VmObjectRef{});
        set(sibling, VmObjectRef{});
        static_cast<void>(f.vm.CollectGarbage("vd06b-unbound-listeners"));
        CHECK_FALSE(f.model.IsValidRef(first));
        CHECK_FALSE(f.model.IsValidRef(second));
        CHECK(f.ledger.Unimplemented().empty());
        capture();
        REQUIRE_FALSE(entered.empty());
        CHECK(entered.front().sequence == 1);
        for (std::size_t i = 1; i < entered.size(); ++i)
            CHECK(entered[i].sequence == entered[i - 1].sequence + 1);
        std::ofstream trace(std::string(OGPLAY_SOURCE_DIR) + "/.local/analysis/vd-06b/state-" +
                            (backend == InterpreterBackend::threaded ? "threaded" : "switch") + ".trace.json");
        trace << RenderDexVmTraceJson(entered);
    }
}

TEST_CASE("DVM-282 absent providers and real attached system state fail explicitly") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::container_traversal::ContainerVm f(backend);
        const auto owner = f.New("Landroid/app/Activity;");
        const auto root = f.New("Ltest/HierarchyGroup;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        f.context->content_view = root;
        const auto denied = f.OnOutcome(root, "setClickable", "(Z)V", {VmValue::Int(1)});
        REQUIRE(denied.exception.IsValid());
        CHECK(f.linker.Class(denied.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        CHECK_FALSE(f.ledger.Unimplemented().empty());
        CHECK_FALSE(f.On(root, "isClickable", "()Z").AsInt());
        f.context->ui_test_accessibility_disabled = true;
        f.On(root, "setClickable", "(Z)V", {VmValue::Int(1)});
        CHECK(f.On(root, "isClickable", "()Z").AsInt());
        f.context->activity = owner;
        f.On(owner, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(root)});
        f.context->window_has_focus = true;
        f.context->window_focus_activity = owner.Value();
        const auto input = f.vm.NewIntrinsicInstance("Landroid/view/inputmethod/InputMethodManager;");
        f.On(root, "setFocusable", "(Z)V", {VmValue::Int(1)});
        CHECK(f.On(root, "requestFocus", "()Z").AsInt());
        const auto focused = f.OnOutcome(input, "restartInput", "(Landroid/view/View;)V", {VmValue::Ref(root)});
        REQUIRE(focused.exception.IsValid());
        CHECK(f.linker.Class(focused.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        CHECK_FALSE(f.context->ui_tree.Focused());
        f.On(root, "setFocusable", "(Z)V", {VmValue::Int(0)});
        f.On(root, "clearFocus", "()V");
        CHECK_FALSE(f.On(root, "findFocus", "()Landroid/view/View;").ref.IsValid());
        CHECK_FALSE(
            f.OnOutcome(input, "restartInput", "(Landroid/view/View;)V", {VmValue::Ref(root)}).exception.IsValid());
        REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                         AndroidUiTraversalPhase::ResolveOnly, ui::Size{160, 100}));
        const auto before = f.context->ui_tree.PublishedProjection();
        const auto attach = f.vm.NewIntrinsicInstance("Landroid/view/View$AttachInfo;");
        IntrinsicContext call{f.vm, root, {}};
        IntrinsicCall(call).SetRef(f.context->view_java_measure_fields->unsupported_state[4], attach);
        const auto attached = f.OnOutcome(root, "setVisibility", "(I)V", {VmValue::Int(4)});
        REQUIRE(attached.exception.IsValid());
        CHECK(f.linker.Class(attached.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        CHECK(f.context->ui_tree.PublishedProjection() == before);
        CHECK(f.On(root, "getVisibility", "()I").AsInt() == 4);
    }
}
