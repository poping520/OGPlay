#include "boot_dex.h"
// Widget state, resolved UiTree geometry and listener dispatch integration.

#include <doctest/doctest.h>

#include <array>
#include <algorithm>
#include <iterator>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"
#include "ogplay/loader/binary_xml.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/integration/bitmap_pixels.h"
#include "ogplay/loader/apk.h"
#include "ogplay/runtime/ui/ui_renderer.h"
#include "ogplay/runtime/vfs/vfs.h"
#include "ogplay/session/dex_activity_lifecycle.h"
#include "ogplay/video/fake_video_player.h"
#include "ogplay/audio/encoded_music.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"

#include "widget_fixture.h"
#include "android_value_fixture.h"

using namespace ogplay::test::widget;
using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

TEST_CASE("View id and Activity lookup share the bound guest identity") {
    ClickVm vm;
    const auto skip_node = *FindViewUiNode(*vm.context, vm.skip_button.Value());
    const auto bar_node = *vm.context->ui_tree.Get(skip_node)->parent;
    const auto bar = ViewObjectForUiNode(*vm.context, bar_node);
    vm.context->ui_tree.Detach(bar_node);
    vm.CallOn(vm.activity, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(bar)});
    CHECK(vm.CallOn(vm.skip_button, "getId", "()I").AsInt() == -1);
    CHECK_FALSE(vm.CallOn(
        vm.activity, "findViewById", "(I)Landroid/view/View;",
        {VmValue::Int(-1)}).ref.IsValid());
    vm.CallOn(vm.skip_button, "setId", "(I)V", {VmValue::Int(0x1234)});
    CHECK(vm.CallOn(vm.skip_button, "getId", "()I").AsInt() == 0x1234);
    const auto found = vm.CallOn(
        vm.activity, "findViewById", "(I)Landroid/view/View;",
        {VmValue::Int(0x1234)});
    CHECK(found.ref == vm.skip_button);

    vm.CallOn(vm.skip_button, "setId", "(I)V", {VmValue::Int(0x5678)});
    CHECK_FALSE(vm.CallOn(
        vm.activity, "findViewById", "(I)Landroid/view/View;",
        {VmValue::Int(0x1234)}).ref.IsValid());
    CHECK(vm.CallOn(
        vm.activity, "findViewById", "(I)Landroid/view/View;",
        {VmValue::Int(0x5678)}).ref == vm.skip_button);

    const auto node = FindViewUiNode(*vm.context, vm.skip_button.Value());
    REQUIRE(node.has_value());
    const auto extra = vm.context->ui_tree.CreateNode(ui::UiClass::View);
    CHECK_THROWS_WITH(BindViewToUiNode(*vm.context, vm.skip_button, extra),
                      "guest View already has a different UI node");
    CHECK_THROWS_WITH(BindViewToUiNode(
                          *vm.context, vm.other_button, *node),
                      "guest View already has a different UI node");
}

TEST_CASE("parameterless nested View hierarchy measures and receives touch") {
    ClickVm vm;
    const auto top = vm.interpreter.NewIntrinsicInstance(
        "Landroid/widget/RelativeLayout;");
    const auto frame = vm.interpreter.NewIntrinsicInstance(
        "Landroid/widget/FrameLayout;");
    const auto touch = vm.NewTouchView();
    vm.CallOn(frame, "addView", "(Landroid/view/View;)V",
              {VmValue::Ref(touch)});
    vm.CallOn(top, "addView", "(Landroid/view/View;)V",
              {VmValue::Ref(frame)});
    vm.CallOn(vm.activity, "setContentView", "(Landroid/view/View;)V",
              {VmValue::Ref(top)});

    CHECK(vm.CallOn(top, "getWidth", "()I").AsInt() == 100);
    CHECK(vm.CallOn(top, "getHeight", "()I").AsInt() == 100);
    CHECK(vm.CallOn(frame, "getWidth", "()I").AsInt() == 100);
    CHECK(vm.CallOn(frame, "getHeight", "()I").AsInt() == 100);
    CHECK(vm.CallOn(touch, "getWidth", "()I").AsInt() == 100);
    CHECK(vm.CallOn(touch, "getHeight", "()I").AsInt() == 100);
    const auto receivers = FindTouchReceiversAt(*vm.context, 50.0F, 50.0F);
    REQUIRE_FALSE(receivers.empty());
    CHECK(receivers.front() == touch.Value());

    vm.ResetTouchView(true);
    const auto result = ogplay::session::DispatchDeepTouchEvent(
        vm.interpreter, *vm.context, 0, 50.0F, 50.0F, 0U);
    CHECK_FALSE(result.error.has_value());
    CHECK(result.handled);
    CHECK(result.captured_view == touch.Value());
}

TEST_CASE("dynamic ViewGroup hierarchy shares layout params and geometry") {
    ClickVm vm;
    const auto column = vm.interpreter.NewIntrinsicInstance(
        "Landroid/widget/LinearLayout;");
    const auto weighted = vm.interpreter.NewIntrinsicInstance(
        "Landroid/view/View;");
    const auto fixed = vm.interpreter.NewIntrinsicInstance(
        "Landroid/view/View;");
    const auto content_params = vm.interpreter.NewIntrinsicInstance(
        "Landroid/view/ViewGroup$LayoutParams;");
    vm.CallDirect(content_params, "Landroid/view/ViewGroup$LayoutParams;",
                  "<init>", "(II)V",
                  {VmValue::Int(-1), VmValue::Int(-1)});
    vm.CallOn(column, "setLayoutParams",
              "(Landroid/view/ViewGroup$LayoutParams;)V",
              {VmValue::Ref(content_params)});
    vm.CallOn(column, "setOrientation", "(I)V", {VmValue::Int(1)});
    vm.CallOn(column, "setPadding", "(IIII)V",
              {VmValue::Int(5), VmValue::Int(5), VmValue::Int(5),
               VmValue::Int(5)});

    const auto weighted_params = vm.interpreter.NewIntrinsicInstance(
        "Landroid/widget/LinearLayout$LayoutParams;");
    vm.CallDirect(weighted_params,
                  "Landroid/widget/LinearLayout$LayoutParams;", "<init>",
                  "(IIF)V",
                  {VmValue::Int(-1), VmValue::Int(0), VmValue::Float(1.0F)});
    vm.CallOn(weighted_params, "setMargins", "(IIII)V",
              {VmValue::Int(2), VmValue::Int(3), VmValue::Int(4),
               VmValue::Int(5)});
    const auto fixed_params = vm.interpreter.NewIntrinsicInstance(
        "Landroid/widget/LinearLayout$LayoutParams;");
    vm.CallDirect(fixed_params,
                  "Landroid/widget/LinearLayout$LayoutParams;", "<init>",
                  "(II)V", {VmValue::Int(-1), VmValue::Int(20)});
    vm.CallOn(column, "addView",
              "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
              {VmValue::Ref(weighted), VmValue::Ref(weighted_params)});
    vm.CallOn(column, "addView",
              "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
              {VmValue::Ref(fixed), VmValue::Ref(fixed_params)});
    const auto inserted = vm.interpreter.NewIntrinsicInstance(
        "Landroid/view/View;");
    vm.CallOn(column, "addView", "(Landroid/view/View;I)V",
              {VmValue::Ref(inserted), VmValue::Int(0)});
    const auto inserted_node = FindViewUiNode(*vm.context, inserted.Value());
    REQUIRE(inserted_node.has_value());
    CHECK(vm.context->ui_tree.Get(*inserted_node)->parent ==
          FindViewUiNode(*vm.context, column.Value()));
    vm.CallOn(column, "removeView", "(Landroid/view/View;)V",
              {VmValue::Ref(inserted)});
    vm.CallOn(vm.activity, "setContentView", "(Landroid/view/View;)V",
              {VmValue::Ref(column)});

    CHECK(vm.CallOn(weighted, "getLayoutParams",
                    "()Landroid/view/ViewGroup$LayoutParams;")
              .ref == weighted_params);
    CHECK(vm.CallOn(weighted, "getLeft", "()I").AsInt() == 7);
    CHECK(vm.CallOn(weighted, "getTop", "()I").AsInt() == 8);
    CHECK(vm.CallOn(weighted, "getWidth", "()I").AsInt() == 84);
    CHECK(vm.CallOn(weighted, "getHeight", "()I").AsInt() == 62);
    CHECK(vm.CallOn(fixed, "getTop", "()I").AsInt() == 75);

    const auto replacement = vm.interpreter.NewIntrinsicInstance(
        "Landroid/widget/LinearLayout$LayoutParams;");
    vm.CallDirect(replacement,
                  "Landroid/widget/LinearLayout$LayoutParams;", "<init>",
                  "(II)V", {VmValue::Int(-1), VmValue::Int(10)});
    vm.CallOn(column, "updateViewLayout",
              "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
              {VmValue::Ref(weighted), VmValue::Ref(replacement)});
    CHECK(vm.CallOn(weighted, "getHeight", "()I").AsInt() == 10);
    CHECK(vm.CallOn(fixed, "getTop", "()I").AsInt() == 15);
    vm.CallOn(replacement, "setMargins", "(IIII)V",
              {VmValue::Int(0), VmValue::Int(2), VmValue::Int(0),
               VmValue::Int(3)});
    vm.CallOn(weighted, "requestLayout", "()V");
    CHECK(vm.CallOn(weighted, "getTop", "()I").AsInt() == 7);
    CHECK(vm.CallOn(fixed, "getTop", "()I").AsInt() == 20);

    vm.CallOn(column, "removeView", "(Landroid/view/View;)V",
              {VmValue::Ref(fixed)});
    const auto fixed_node = FindViewUiNode(*vm.context, fixed.Value());
    REQUIRE(fixed_node.has_value());
    CHECK_FALSE(vm.context->ui_tree.Get(*fixed_node)->parent.has_value());
    vm.CallOn(
        column, "addView",
        "(Landroid/view/View;ILandroid/view/ViewGroup$LayoutParams;)V",
        {VmValue::Ref(fixed), VmValue::Int(0), VmValue::Ref(fixed_params)});
    CHECK(vm.CallOn(fixed, "getTop", "()I").AsInt() == 5);
    vm.CallOn(column, "removeViews", "(II)V",
              {VmValue::Int(0), VmValue::Int(1)});
    CHECK_FALSE(vm.context->ui_tree.Get(*fixed_node)->parent.has_value());
}

TEST_CASE("DVM-233 in-layout removal preserves geometry and invalidates the actual overlay") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ClickVm vm(backend);
        const auto group = vm.interpreter.NewIntrinsicInstance("Landroid/widget/LinearLayout;");
        const auto first = vm.interpreter.NewIntrinsicInstance("Landroid/view/View;");
        const auto second = vm.interpreter.NewIntrinsicInstance("Landroid/view/View;");
        const auto outsider = vm.interpreter.NewIntrinsicInstance("Landroid/view/View;");
        vm.CallOn(group, "setOrientation", "(I)V", {VmValue::Int(1)});
        vm.CallOn(first, "setId", "(I)V", {VmValue::Int(123)});
        vm.CallOn(first, "setBackgroundColor", "(I)V", {VmValue::Int(static_cast<std::int32_t>(0xffff0000U))});
        vm.CallOn(second, "setBackgroundColor", "(I)V", {VmValue::Int(static_cast<std::int32_t>(0xff00ff00U))});
        for (const auto view : {first, second})
            vm.CallOn(group, "addView", "(Landroid/view/View;II)V",
                {VmValue::Ref(view), VmValue::Int(20), VmValue::Int(20)});
        vm.CallOn(vm.activity, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(group)});
        CHECK(vm.CallOn(second, "getTop", "()I").AsInt() == 20);
        const auto first_node = *FindViewUiNode(*vm.context, first.Value());
        const auto group_node = *FindViewUiNode(*vm.context, group.Value());
        auto& tree = vm.context->ui_tree;
        ui::UiOverlayRenderer renderer;
        const auto pixel = (5U * 100U + 5U) * 4U;
        CHECK(renderer.Render(tree, vm.context->ui_bitmaps, {100, 100}).rgba8[pixel] == 255);
        CHECK_FALSE(tree.Get(tree.Root())->layout_dirty);
        CHECK_FALSE(tree.Get(tree.Root())->draw_dirty);
        vm.CallOn(group, "removeViewInLayout", "(Landroid/view/View;)V", {VmValue::Ref(first)});
        CHECK_FALSE(vm.CallOn(first, "getParent", "()Landroid/view/ViewParent;").ref.IsValid());
        CHECK(tree.Get(group_node)->children.size() == 1);
        CHECK_FALSE(tree.FindByAndroidId(123));
        CHECK(tree.Get(first_node) != nullptr); // detach is not destruction
        CHECK_FALSE(tree.Get(tree.Root())->layout_dirty);
        CHECK(tree.Get(tree.Root())->draw_dirty);
        CHECK(vm.CallOn(second, "getTop", "()I").AsInt() == 20);
        CHECK(renderer.Render(tree, vm.context->ui_bitmaps, {100, 100}).rgba8[pixel + 3] == 0);
        vm.CallOn(group, "removeViewInLayout", "(Landroid/view/View;)V", {VmValue::Ref(first)});
        vm.CallOn(group, "removeViewInLayout", "(Landroid/view/View;)V", {VmValue::Ref(outsider)});
        vm.CallOn(group, "removeViewInLayout", "(Landroid/view/View;)V", {VmValue::Ref(VmObjectRef{})});
        CHECK_FALSE(tree.Get(tree.Root())->layout_dirty);
        CHECK_FALSE(tree.Get(tree.Root())->draw_dirty);
        vm.CallOn(group, "requestLayout", "()V");
        CHECK(vm.CallOn(second, "getTop", "()I").AsInt() == 0);
        vm.CallOn(group, "addView", "(Landroid/view/View;II)V",
            {VmValue::Ref(first), VmValue::Int(20), VmValue::Int(20)});
        CHECK(vm.CallOn(first, "getTop", "()I").AsInt() == 20);
        CHECK(tree.FindByAndroidId(123) == first_node);
        vm.CallOn(first, "setVisibility", "(I)V", {VmValue::Int(8)});
        CHECK(tree.Get(tree.Root())->layout_dirty);
        vm.CallOn(group, "removeViewInLayout", "(Landroid/view/View;)V", {VmValue::Ref(first)});
        CHECK(tree.Get(tree.Root())->layout_dirty); // never erase a pre-existing request
        static_cast<void>(vm.CallOn(second, "getTop", "()I"));
        tree.ClearDrawDirty();
        vm.CallOn(group, "removeView", "(Landroid/view/View;)V", {VmValue::Ref(second)});
        CHECK(tree.Get(tree.Root())->layout_dirty);
        CHECK(tree.Get(tree.Root())->draw_dirty);

        const auto owner = vm.linker.ResolveDescriptor("Landroid/view/ViewGroup;");
        const auto slot = vm.linker.FindVtableIndex(owner, "removeViewInLayout", "(Landroid/view/View;)V");
        REQUIRE(slot);
        const auto& method = vm.linker.Method(vm.linker.Class(owner).vtable[*slot]);
        CHECK((method.access_flags & kAccPublic) != 0);
        CHECK((method.access_flags & kAccFinal) == 0);
        const auto probe = vm.interpreter.NewIntrinsicInstance("LRemovalLayoutProbe;");
        vm.CallOn(probe, "removeViewInLayout", "(Landroid/view/View;)V", {VmValue::Ref(VmObjectRef{})});
        CHECK(vm.removal_layout_requests == 0);
        vm.CallOn(probe, "removeView", "(Landroid/view/View;)V", {VmValue::Ref(VmObjectRef{})});
        CHECK(vm.removal_layout_requests == 1);

        const auto identity = vm.model.ToIdentity(first);
        {
            const auto root = vm.interpreter.ProtectReferences(std::array{first});
            vm.CallOn(first, "setVisibility", "(I)V", {VmValue::Int(0)});
            vm.CallOn(first, "setFocusable", "(Z)V", {VmValue::Int(1)});
            vm.CallOn(group, "addView", "(Landroid/view/View;II)V",
                {VmValue::Ref(first), VmValue::Int(20), VmValue::Int(20)});
            CHECK(vm.CallOn(first, "requestFocus", "()Z").AsInt() == 1);
            CHECK(vm.CallOn(group, "hasFocus", "()Z").AsInt() == 1);
            vm.CallOn(group, "removeViewInLayout", "(Landroid/view/View;)V", {VmValue::Ref(first)});
            CHECK(vm.CallOn(first, "isFocused", "()Z").AsInt() == 0);
            CHECK(vm.CallOn(group, "hasFocus", "()Z").AsInt() == 0);
            static_cast<void>(vm.interpreter.CollectGarbage("removed-view-retained"));
            CHECK(vm.model.FindIdentity(identity) == first);
            vm.CallOn(group, "addView", "(Landroid/view/View;II)V",
                {VmValue::Ref(first), VmValue::Int(20), VmValue::Int(20)});
            CHECK(vm.CallOn(first, "getParent", "()Landroid/view/ViewParent;").ref == group);
            vm.CallOn(group, "removeViewInLayout", "(Landroid/view/View;)V", {VmValue::Ref(first)});
        }
    }
}

TEST_CASE("View bringToFront reorders paint through the virtual ViewParent") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ClickVm vm(backend);
        const auto group = vm.interpreter.NewIntrinsicInstance("Landroid/widget/FrameLayout;");
        const auto first = vm.interpreter.NewIntrinsicInstance("Landroid/view/View;");
        const auto second = vm.interpreter.NewIntrinsicInstance("Landroid/view/View;");
        const auto outsider = vm.interpreter.NewIntrinsicInstance("Landroid/view/View;");
        vm.CallOn(first, "setBackgroundColor", "(I)V", {VmValue::Int(static_cast<std::int32_t>(0xffff0000U))});
        vm.CallOn(second, "setBackgroundColor", "(I)V", {VmValue::Int(static_cast<std::int32_t>(0xff00ff00U))});
        for (const auto view : {first, second})
            vm.CallOn(group, "addView", "(Landroid/view/View;II)V",
                {VmValue::Ref(view), VmValue::Int(20), VmValue::Int(20)});
        vm.CallOn(vm.activity, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(group)});
        vm.CallOn(first, "setFocusable", "(Z)V", {VmValue::Int(1)});
        REQUIRE(vm.CallOn(first, "requestFocus", "()Z").AsInt() == 1);
        const auto first_node = *FindViewUiNode(*vm.context, first.Value());
        const auto second_node = *FindViewUiNode(*vm.context, second.Value());
        const auto group_node = *FindViewUiNode(*vm.context, group.Value());
        auto& tree = vm.context->ui_tree;
        ui::UiOverlayRenderer renderer;
        const auto pixel = (5U * 100U + 5U) * 4U;
        static_cast<void>(vm.CallOn(first, "getTop", "()I"));
        CHECK(renderer.Render(tree, vm.context->ui_bitmaps, {100, 100}).rgba8[pixel + 1] == 255);

        vm.CallOn(first, "bringToFront", "()V");
        CHECK(tree.Get(group_node)->children == std::vector<ui::UiNodeId>{second_node, first_node});
        CHECK(vm.CallOn(first, "getParent", "()Landroid/view/ViewParent;").ref == group);
        CHECK(vm.CallOn(first, "isFocused", "()Z").AsInt() == 1);
        CHECK(tree.Get(tree.Root())->layout_dirty); // requestLayout() dispatched
        static_cast<void>(vm.CallOn(first, "getTop", "()I"));
        CHECK(renderer.Render(tree, vm.context->ui_bitmaps, {100, 100}).rgba8[pixel] == 255);

        // Non-children, null and detached views follow API 19 and do nothing.
        tree.ClearLayoutDirty();
        tree.ClearDrawDirty();
        vm.CallOn(group, "bringChildToFront", "(Landroid/view/View;)V", {VmValue::Ref(outsider)});
        vm.CallOn(group, "bringChildToFront", "(Landroid/view/View;)V", {VmValue::Ref(VmObjectRef{})});
        vm.CallOn(outsider, "bringToFront", "()V");
        CHECK_FALSE(tree.Get(tree.Root())->layout_dirty);
        CHECK_FALSE(tree.Get(tree.Root())->draw_dirty);
        // The window container's parent is the synthetic root (ViewRootImpl).
        const auto container = vm.CallOn(group, "getParent", "()Landroid/view/ViewParent;").ref;
        REQUIRE(container.IsValid());
        CHECK_FALSE(vm.CallOn(container, "getParent", "()Landroid/view/ViewParent;").ref.IsValid());
        vm.CallOn(container, "bringToFront", "()V");
        CHECK_FALSE(tree.Get(tree.Root())->draw_dirty);

        const auto probe = vm.interpreter.NewIntrinsicInstance("LBringFrontProbe;");
        vm.CallOn(probe, "addView", "(Landroid/view/View;)V", {VmValue::Ref(outsider)});
        vm.CallOn(outsider, "bringToFront", "()V");
        CHECK(vm.bring_front_requests == 1);
        const auto layout_probe = vm.interpreter.NewIntrinsicInstance("LRemovalLayoutProbe;");
        const auto lone = vm.interpreter.NewIntrinsicInstance("Landroid/view/View;");
        vm.CallOn(layout_probe, "addView", "(Landroid/view/View;)V", {VmValue::Ref(lone)});
        vm.CallOn(lone, "bringToFront", "()V");
        CHECK(vm.removal_layout_requests == 1);

        for (const auto& [descriptor, name, signature] : {
                 std::tuple{"Landroid/view/View;", "bringToFront", "()V"},
                 std::tuple{"Landroid/view/ViewGroup;", "bringChildToFront", "(Landroid/view/View;)V"}}) {
            const auto owner = vm.linker.ResolveDescriptor(descriptor);
            const auto slot = vm.linker.FindVtableIndex(owner, name, signature);
            REQUIRE(slot);
            const auto& method = vm.linker.Method(vm.linker.Class(owner).vtable[*slot]);
            CHECK((method.access_flags & kAccPublic) != 0);
            CHECK((method.access_flags & kAccFinal) == 0);
        }
    }
}

TEST_CASE("ViewGroup child access and removal follow API 19 semantics") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ClickVm vm(backend);
        vm.context->activity = vm.activity;
        vm.context->managed_host_surface_open = true;
        const auto group = vm.interpreter.NewIntrinsicInstance("Landroid/widget/FrameLayout;");
        const auto first = vm.interpreter.NewIntrinsicInstance("Landroid/view/View;");
        const auto second = vm.interpreter.NewIntrinsicInstance("Landroid/view/View;");
        const auto outsider = vm.interpreter.NewIntrinsicInstance("Landroid/view/View;");
        const auto surface = vm.interpreter.NewIntrinsicInstance("Landroid/view/SurfaceView;");
        const auto roots = vm.interpreter.ProtectReferences(std::array{group, first, second, outsider, surface});
        vm.CallDirect(surface, "Landroid/view/SurfaceView;", "<init>", "(Landroid/content/Context;)V",
            {VmValue::Ref(vm.activity)});
        const auto count = [&] { return vm.CallOn(group, "getChildCount", "()I").AsInt(); };
        const auto at = [&](int index) {
            return vm.CallOn(group, "getChildAt", "(I)Landroid/view/View;", {VmValue::Int(index)}).ref;
        };
        const auto index_of = [&](VmObjectRef view) {
            return vm.CallOn(group, "indexOfChild", "(Landroid/view/View;)I", {VmValue::Ref(view)}).AsInt();
        };
        const auto thrown = [&](const char* name, const char* signature, std::vector<VmValue> arguments) {
            const auto owner = vm.model.ObjectClass(group);
            const auto slot = vm.linker.FindVtableIndex(owner, name, signature);
            REQUIRE(slot);
            arguments.insert(arguments.begin(), VmValue::Ref(group));
            const auto outcome = vm.interpreter.Call(vm.linker.Class(owner).vtable[*slot], arguments);
            REQUIRE(outcome.exception.IsValid());
            return vm.linker.Class(vm.model.ObjectClass(outcome.exception)).descriptor;
        };
        CHECK(count() == 0);
        CHECK_FALSE(at(0).IsValid());
        vm.CallOn(vm.activity, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(group)});
        for (const auto view : {first, surface, second})
            vm.CallOn(group, "addView", "(Landroid/view/View;)V", {VmValue::Ref(view)});
        const auto holder = vm.CallOn(surface, "getHolder", "()Landroid/view/SurfaceHolder;").ref;
        const auto callback = vm.interpreter.NewIntrinsicInstance("LContentSurfaceCallback;");
        vm.CallOn(holder, "addCallback", "(Landroid/view/SurfaceHolder$Callback;)V", {VmValue::Ref(callback)});
        CHECK(count() == 3);
        CHECK(at(0) == first);
        CHECK(at(2) == second);
        CHECK_FALSE(at(-1).IsValid());
        CHECK_FALSE(at(3).IsValid());
        CHECK(index_of(surface) == 1);
        CHECK(index_of(outsider) == -1);
        CHECK(index_of(VmObjectRef{}) == -1);
        vm.CallOn(first, "bringToFront", "()V");
        CHECK(index_of(first) == 2);

        // removeViewAt shares removeView retirement: focus, Surface, layout.
        vm.CallOn(second, "setFocusable", "(Z)V", {VmValue::Int(1)});
        REQUIRE(vm.CallOn(second, "requestFocus", "()Z").AsInt() == 1);
        auto& tree = vm.context->ui_tree;
        tree.ClearLayoutDirty();
        tree.ClearDrawDirty();
        CHECK(thrown("removeViewAt", "(I)V", {VmValue::Int(3)}) == "Ljava/lang/NullPointerException;");
        CHECK(count() == 3);
        CHECK_FALSE(tree.Get(tree.Root())->layout_dirty);
        vm.CallOn(group, "removeViewAt", "(I)V", {VmValue::Int(index_of(second))});
        CHECK(vm.CallOn(second, "isFocused", "()Z").AsInt() == 0);
        CHECK_FALSE(vm.CallOn(second, "getParent", "()Landroid/view/ViewParent;").ref.IsValid());
        CHECK(tree.Get(tree.Root())->layout_dirty);
        const int destroyed_before = vm.content_surface_destroys;
        vm.CallOn(group, "removeViewAt", "(I)V", {VmValue::Int(index_of(surface))});
        CHECK(vm.content_surface_destroys == destroyed_before + 1);
        CHECK(count() == 1);

        // InLayout variants keep layout requests untouched.
        vm.CallOn(group, "addView", "(Landroid/view/View;)V", {VmValue::Ref(second)});
        tree.ClearLayoutDirty();
        vm.CallOn(group, "removeViewsInLayout", "(II)V", {VmValue::Int(1), VmValue::Int(1)});
        CHECK(count() == 1);
        CHECK_FALSE(tree.Get(tree.Root())->layout_dirty);
        CHECK(thrown("removeViewsInLayout", "(II)V", {VmValue::Int(1), VmValue::Int(1)}) ==
              "Ljava/lang/IndexOutOfBoundsException;");
        vm.CallOn(group, "removeAllViewsInLayout", "()V");
        CHECK(count() == 0);
        CHECK_FALSE(tree.Get(tree.Root())->layout_dirty);
        vm.CallOn(group, "removeAllViewsInLayout", "()V");
        for (const auto view : {first, second})
            vm.CallOn(group, "addView", "(Landroid/view/View;)V", {VmValue::Ref(view)});
        vm.CallOn(group, "removeAllViews", "()V");
        CHECK(count() == 0);
        CHECK(tree.Get(tree.Root())->layout_dirty);
        vm.CallOn(group, "addView", "(Landroid/view/View;)V", {VmValue::Ref(first)});
        CHECK(at(0) == first); // removed Views remain reusable

        // Public hooks dispatch virtually like API 19.
        const auto remove_all = vm.interpreter.NewIntrinsicInstance("LRemoveAllProbe;");
        vm.CallOn(remove_all, "addView", "(Landroid/view/View;)V", {VmValue::Ref(outsider)});
        vm.CallOn(remove_all, "removeAllViews", "()V");
        CHECK(vm.remove_all_in_layout_requests == 1);
        CHECK(vm.CallOn(remove_all, "getChildCount", "()I").AsInt() == 1);
        const auto layout_probe = vm.interpreter.NewIntrinsicInstance("LRemovalLayoutProbe;");
        vm.CallOn(layout_probe, "addView", "(Landroid/view/View;)V", {VmValue::Ref(second)});
        vm.CallOn(layout_probe, "removeViewAt", "(I)V", {VmValue::Int(0)});
        CHECK(vm.removal_layout_requests == 1);
        const auto owner = vm.linker.ResolveDescriptor("Landroid/view/ViewGroup;");
        for (const auto& [name, signature] : {
                 std::pair{"getChildCount", "()I"}, std::pair{"getChildAt", "(I)Landroid/view/View;"},
                 std::pair{"indexOfChild", "(Landroid/view/View;)I"}, std::pair{"removeViewAt", "(I)V"},
                 std::pair{"removeView", "(Landroid/view/View;)V"}, std::pair{"removeViews", "(II)V"},
                 std::pair{"removeViewsInLayout", "(II)V"}, std::pair{"removeAllViews", "()V"},
                 std::pair{"removeAllViewsInLayout", "()V"}}) {
            const auto slot = vm.linker.FindVtableIndex(owner, name, signature);
            REQUIRE_MESSAGE(slot, name);
            const auto& method = vm.linker.Method(vm.linker.Class(owner).vtable[*slot]);
            CHECK((method.access_flags & kAccPublic) != 0);
            CHECK((method.access_flags & kAccFinal) == 0);
        }
    }
}

TEST_CASE("RelativeLayout Java rules update attached geometry") {
    ClickVm vm;
    const auto relative = vm.interpreter.NewIntrinsicInstance(
        "Landroid/widget/RelativeLayout;");
    const auto anchor = vm.interpreter.NewIntrinsicInstance(
        "Landroid/view/View;");
    const auto dependent = vm.interpreter.NewIntrinsicInstance(
        "Landroid/view/View;");
    vm.CallOn(anchor, "setId", "(I)V", {VmValue::Int(1001)});

    const auto content_params = vm.interpreter.NewIntrinsicInstance(
        "Landroid/view/ViewGroup$LayoutParams;");
    vm.CallDirect(content_params, "Landroid/view/ViewGroup$LayoutParams;",
                  "<init>", "(II)V",
                  {VmValue::Int(-1), VmValue::Int(-1)});
    vm.CallOn(relative, "setLayoutParams",
              "(Landroid/view/ViewGroup$LayoutParams;)V",
              {VmValue::Ref(content_params)});
    const auto anchor_params = vm.interpreter.NewIntrinsicInstance(
        "Landroid/widget/RelativeLayout$LayoutParams;");
    vm.CallDirect(anchor_params,
                  "Landroid/widget/RelativeLayout$LayoutParams;", "<init>",
                  "(II)V", {VmValue::Int(20), VmValue::Int(10)});
    vm.CallOn(anchor_params, "addRule", "(I)V", {VmValue::Int(9)});
    vm.CallOn(anchor_params, "addRule", "(I)V", {VmValue::Int(10)});
    const auto dependent_params = vm.interpreter.NewIntrinsicInstance(
        "Landroid/widget/RelativeLayout$LayoutParams;");
    vm.CallDirect(dependent_params,
                  "Landroid/widget/RelativeLayout$LayoutParams;", "<init>",
                  "(II)V", {VmValue::Int(5), VmValue::Int(5)});
    vm.CallOn(dependent_params, "addRule", "(II)V",
              {VmValue::Int(1), VmValue::Int(1001)});
    vm.CallOn(dependent_params, "addRule", "(II)V",
              {VmValue::Int(3), VmValue::Int(1001)});
    vm.CallOn(relative, "addView",
              "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
              {VmValue::Ref(dependent), VmValue::Ref(dependent_params)});
    vm.CallOn(relative, "addView",
              "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
              {VmValue::Ref(anchor), VmValue::Ref(anchor_params)});
    vm.CallOn(vm.activity, "setContentView", "(Landroid/view/View;)V",
              {VmValue::Ref(relative)});

    CHECK(vm.CallOn(dependent, "getLeft", "()I").AsInt() == 20);
    CHECK(vm.CallOn(dependent, "getTop", "()I").AsInt() == 10);
    vm.CallOn(dependent_params, "addRule", "(II)V",
              {VmValue::Int(1), VmValue::Int(0)});
    vm.CallOn(dependent_params, "addRule", "(I)V", {VmValue::Int(14)});
    vm.CallOn(dependent, "requestLayout", "()V");
    CHECK(vm.CallOn(dependent, "getLeft", "()I").AsInt() == 47);
    CHECK(vm.CallOn(dependent, "getTop", "()I").AsInt() == 10);
}

TEST_CASE("guest View subclass resolves the platform kind by inheritance") {
    ClickVm vm;
    const auto java_class = vm.linker.FindClass("LTestPanel;");
    REQUIRE(java_class.has_value());
    const auto initialized = vm.interpreter.EnsureClassInitialized(*java_class);
    REQUIRE_FALSE(initialized.exception.IsValid());
    const auto panel = vm.model.NewInstance(
        *java_class, vm.linker.Class(*java_class).instance_slots);
    vm.CallDirect(panel, "LTestPanel;", "<init>",
                  "(Landroid/content/Context;)V", {VmValue::Ref(vm.activity)});
    const auto node = FindViewUiNode(*vm.context, panel.Value());
    REQUIRE(node.has_value());
    CHECK(vm.context->ui_tree.Get(*node)->kind == ui::UiClass::LinearLayout);
    vm.CallOn(panel, "setOrientation", "(I)V", {VmValue::Int(1)});
    vm.CallOn(vm.activity, "setContentView", "(Landroid/view/View;)V",
              {VmValue::Ref(panel)});
    CHECK(vm.context->ui_tree.Get(*node)->orientation ==
          ui::Orientation::Vertical);
}

TEST_CASE("RelativeLayout TRUE rules behave as absent sibling anchors") {
    ClickVm vm;
    const auto relative = vm.interpreter.NewIntrinsicInstance(
        "Landroid/widget/RelativeLayout;");
    const auto child = vm.interpreter.NewIntrinsicInstance(
        "Landroid/view/View;");
    const auto params = vm.interpreter.NewIntrinsicInstance(
        "Landroid/widget/RelativeLayout$LayoutParams;");
    vm.CallDirect(params, "Landroid/widget/RelativeLayout$LayoutParams;",
                  "<init>", "(II)V", {VmValue::Int(5), VmValue::Int(5)});
    // addRule(7) stores TRUE (-1) for ALIGN_RIGHT: no anchor, default slot.
    vm.CallOn(params, "addRule", "(I)V", {VmValue::Int(7)});
    vm.CallOn(relative, "addView",
              "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
              {VmValue::Ref(child), VmValue::Ref(params)});
    vm.CallOn(vm.activity, "setContentView", "(Landroid/view/View;)V",
              {VmValue::Ref(relative)});
    CHECK(vm.CallOn(child, "getLeft", "()I").AsInt() == 0);
    CHECK(vm.CallOn(child, "getTop", "()I").AsInt() == 0);
}

TEST_CASE("DVM-263 ROM superclass and interface checks execute as DEX instructions") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::android_value::AndroidValueVm f(backend, {}, false, "framework_override.dex", true);
        CHECK(f.Static("Lfixture/FrameworkHierarchyProbe;", "catchAndroidException", "()I").AsInt() == 1);
        for (const auto& [name, owner] : std::array{
                 std::pair{"motion", "Landroid/view/MotionEvent;"}, std::pair{"scroll", "Landroid/widget/ScrollView;"},
                 std::pair{"web", "Landroid/webkit/WebView;"}, std::pair{"application", "Landroid/app/Application;"},
                 std::pair{"parcelable", "Landroid/graphics/PointF;"},
                 std::pair{"zip", "Ljava/util/zip/ZipInputStream;"}}) {
            INFO(std::string_view(name));
            const auto object = f.vm.NewIntrinsicInstance(owner);
            const auto root = f.vm.ProtectReferences(std::array{object});
            CHECK(f.Static("Lfixture/FrameworkHierarchyProbe;", name, ("(" + std::string(owner) + ")Z").c_str(),
                           {VmValue::Ref(object)})
                      .AsInt() == 1);
        }
    }
}

TEST_CASE("DVM-263 corrected parents initialize inherited state through their real constructors") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::android_value::AndroidValueVm f(backend, {}, false, nullptr, true);
        f.Static("Landroid/os/Looper;", "prepareMainLooper", "()V");
        std::array<int, 7> calls{};
        const std::array parents{std::pair{"Landroid/util/AndroidException;", "(Ljava/lang/String;)V"},
                                 std::pair{"Landroid/view/InputEvent;", "()V"},
                                 std::pair{"Landroid/widget/FrameLayout;", "(Landroid/content/Context;)V"},
                                 std::pair{"Landroid/widget/AbsoluteLayout;", "(Landroid/content/Context;)V"},
                                 std::pair{"Landroid/view/ViewGroup$LayoutParams;", "(II)V"},
                                 std::pair{"Landroid/widget/LinearLayout;", "(Landroid/content/Context;)V"},
                                 std::pair{"Landroid/os/Handler;", "()V"}};
        for (std::size_t i = 0; i < parents.size(); ++i) {
            const auto& [owner, signature] = parents[i];
            const auto method = f.linker.FindDirectMethod(f.linker.ResolveDescriptor(owner), "<init>", signature);
            REQUIRE(method.has_value());
            const auto original = f.linker.Method(*method).implementation;
            const auto kind = f.linker.Method(*method).kind;
            // Wrap native parents without replacing their behavior. BootDex
            // parents execute bytecode; verify their resulting state below.
            if (kind == MethodKind::intrinsic) {
                f.linker.MutableMethod(*method).implementation = [original, &calls, i](IntrinsicContext& c) {
                    ++calls[i];
                    return original(c);
                };
            }
        }
        const auto message = f.vm.NewStringUtf8("missing package");
        const auto error = f.New("Landroid/content/pm/PackageManager$NameNotFoundException;", "(Ljava/lang/String;)V",
                                 {VmValue::Ref(message)});
        CHECK(f.vm.StringUtf8(f.On(error, "getMessage", "()Ljava/lang/String;").ref) == "missing package");
        const auto first = MakeMotionEvent(f.vm, 0, 10, 20, 0);
        const auto first_root = f.vm.ProtectReferences(std::array{first});
        const auto second = MakeMotionEvent(f.vm, 0, 30, 40, 0);
        CHECK(f.On(first, "getSequenceNumber", "()I").AsInt() != f.On(second, "getSequenceNumber", "()I").AsInt());
        CHECK(f.On(first, "getX", "()F").AsFloat() == 10.0F);
        const auto owner = f.vm.NewIntrinsicInstance("Landroid/content/Context;");
        const auto owner_root = f.vm.ProtectReferences(std::array{owner});
        for (const auto descriptor : {"Landroid/widget/ScrollView;", "Landroid/webkit/WebView;",
                                      "Landroid/widget/TableLayout;", "Landroid/widget/TableRow;"}) {
            const auto object = f.New(descriptor, "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
            CHECK(f.On(object, "getContext", "()Landroid/content/Context;").ref == owner);
        }
        const auto params = f.New("Landroid/widget/AbsoluteLayout$LayoutParams;", "(IIII)V",
                                  {VmValue::Int(10), VmValue::Int(20), VmValue::Int(30), VmValue::Int(40)});
        for (const auto& [name, value] :
             std::array{std::pair{"width", 10}, std::pair{"height", 20}, std::pair{"x", 30}, std::pair{"y", 40}}) {
            const auto field = f.linker.FindFieldRecursive(f.model.ObjectClass(params), name, "I");
            REQUIRE(field.has_value());
            CHECK(f.model.InstanceSlots(params)[f.linker.Field(*field).slot].bits == static_cast<std::uint32_t>(value));
        }
        const auto auth = f.New("Landroid/webkit/HttpAuthHandler;");
        CHECK(f.On(auth, "getLooper", "()Landroid/os/Looper;").ref ==
              f.Static("Landroid/os/Looper;", "myLooper", "()Landroid/os/Looper;").ref);
        // Native parents run their real handlers once; the two table classes
        // share LinearLayout's existing initialization.
        CHECK(calls[2] == 1);
        CHECK(calls[3] == 1);
        CHECK(calls[5] == 2);
        CHECK(calls[6] == 1);
    }
}
