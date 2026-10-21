#include "boot_dex.h"
// Widget state, resolved UiTree geometry and listener dispatch integration.

#include <doctest/doctest.h>

#include <array>
#include <bit>
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

using namespace ogplay::test::widget;
using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

TEST_CASE("DVM-245 Window content appends overlays with stable parents and replacement") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ClickVm vm(backend);
        const auto activity = vm.interpreter.NewIntrinsicInstance("LContentActivityProbe;");
        const auto activity_root = vm.interpreter.ProtectReferences(std::array{activity});
        vm.context->activity = activity;
        const auto window = vm.CallOn(activity, "getWindow", "()Landroid/view/Window;").ref;
        const auto content = vm.CallOn(activity, "findViewById", "(I)Landroid/view/View;", {VmValue::Int(0x01020002)}).ref;
        REQUIRE(content.IsValid());
        CHECK(vm.linker.Class(vm.model.ObjectClass(content)).descriptor == "Landroid/widget/FrameLayout;");
        CHECK_FALSE(vm.context->content_view.IsValid());
        const auto make = [&](int id, std::uint32_t color) {
            const auto view = vm.interpreter.NewIntrinsicInstance("Landroid/view/View;");
            const auto root = vm.interpreter.ProtectReferences(std::array{view});
            vm.CallDirect(view, "Landroid/view/View;", "<init>", "(Landroid/content/Context;)V", {VmValue::Ref(activity)});
            vm.CallOn(view, "setId", "(I)V", {VmValue::Int(id)});
            vm.CallOn(view, "setBackgroundColor", "(I)V", {VmValue::Int(static_cast<std::int32_t>(color))});
            return view;
        };
        const auto first = make(101, 0xffff0000);
        vm.CallOn(activity, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(first)});
        CHECK(vm.content_change_events == 1);
        CHECK(vm.context->content_view == first);
        CHECK(vm.CallOn(first, "getParent", "()Landroid/view/ViewParent;").ref == content);
        const auto overlay = make(102, 0xff0000ff);
        const auto params = vm.interpreter.NewIntrinsicInstance("Landroid/view/ViewGroup$LayoutParams;");
        const auto params_root = vm.interpreter.ProtectReferences(std::array{params});
        vm.CallDirect(params, "Landroid/view/ViewGroup$LayoutParams;", "<init>", "(II)V", {VmValue::Int(20), VmValue::Int(10)});
        vm.CallOn(window, "addContentView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
            {VmValue::Ref(overlay), VmValue::Ref(params)});
        CHECK(vm.content_change_events == 2);
        CHECK(vm.context->content_view == first);
        CHECK(vm.CallOn(overlay, "getParent", "()Landroid/view/ViewParent;").ref == content);
        CHECK(vm.CallOn(activity, "findViewById", "(I)Landroid/view/View;", {VmValue::Int(102)}).ref == overlay);
        CHECK(vm.CallOn(window, "findViewById", "(I)Landroid/view/View;", {VmValue::Int(101)}).ref == first);
        const auto node = *FindViewUiNode(*vm.context, content.Value());
        CHECK(vm.context->ui_tree.Get(node)->children.size() == 2);
        ui::LayoutUiTree(vm.context->ui_tree, {100, 100});
        ui::UiOverlayRenderer renderer;
        const auto& frame = renderer.Render(vm.context->ui_tree, vm.context->ui_bitmaps, {100, 100});
        CHECK(frame.rgba8[2] == 255);
        CHECK(frame.rgba8[(30 * 100 + 30) * 4] == 255);
        CHECK(vm.CallOn(overlay, "getWidth", "()I").AsInt() == 20);
        CHECK(vm.CallOn(overlay, "getHeight", "()I").AsInt() == 10);
        vm.CallOn(content, "removeView", "(Landroid/view/View;)V", {VmValue::Ref(overlay)});
        CHECK_FALSE(vm.CallOn(overlay, "getParent", "()Landroid/view/ViewParent;").ref.IsValid());
        vm.CallOn(activity, "addContentView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
            {VmValue::Ref(overlay), VmValue::Ref(VmObjectRef{})});
        CHECK(vm.CallOn(overlay, "getWidth", "()I").AsInt() == 100);
        const auto replacement = make(103, 0xff00ff00);
        vm.CallOn(window, "setContentView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
            {VmValue::Ref(replacement), VmValue::Ref(params)});
        CHECK(vm.content_change_events == 4);
        CHECK(vm.context->content_view == replacement);
        CHECK(vm.context->ui_tree.Get(node)->children.size() == 1);
        CHECK_FALSE(vm.CallOn(first, "getParent", "()Landroid/view/ViewParent;").ref.IsValid());
        CHECK_FALSE(vm.CallOn(overlay, "getParent", "()Landroid/view/ViewParent;").ref.IsValid());
        CHECK(vm.CallOn(activity, "findViewById", "(I)Landroid/view/View;", {VmValue::Int(0x01020002)}).ref == content);
        CHECK_FALSE(vm.CallOn(activity, "findViewById", "(I)Landroid/view/View;", {VmValue::Int(101)}).ref.IsValid());
        static_cast<void>(vm.interpreter.CollectGarbage("window-content-owner"));
        CHECK(vm.CallOn(window, "findViewById", "(I)Landroid/view/View;", {VmValue::Int(103)}).ref == replacement);
        const auto method = *vm.linker.FindVtableIndex(vm.model.ObjectClass(activity), "addContentView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V");
        for (const auto bad : {VmObjectRef{}, replacement}) {
            const auto result = vm.interpreter.Call(vm.linker.Class(vm.model.ObjectClass(activity)).vtable[method],
                std::array{VmValue::Ref(activity), VmValue::Ref(bad), VmValue::Ref(params)});
            REQUIRE(result.exception.IsValid());
            CHECK(vm.context->ui_tree.Get(node)->children.size() == 1);
            CHECK(vm.content_change_events == 4);
        }
        CHECK(vm.context->ui_content_mutation_depth == 0);
        // Reentry replaces the just-published tree; the outer operation stops.
        const auto final = make(104, 0xffffffff);
        bool reentered{};
        vm.content_change_hook = [&](IntrinsicContext&) {
            if (reentered) return;
            reentered = true;
            vm.CallOn(activity, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(final)});
        };
        vm.CallOn(activity, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(replacement)});
        CHECK(vm.context->content_view == final);
        CHECK(vm.context->ui_tree.Get(node)->children.size() == 1);
        CHECK(vm.CallOn(final, "getParent", "()Landroid/view/ViewParent;").ref == content);
        CHECK(vm.context->ui_content_mutation_depth == 0);
        const auto exception = vm.interpreter.NewIntrinsicInstance("Ljava/lang/IllegalStateException;");
        const auto exception_root = vm.interpreter.ProtectReferences(std::array{exception});
        vm.content_change_hook = [exception](IntrinsicContext&) {
            throw VmJavaThrow{"Ljava/lang/IllegalStateException;", "content callback", exception};
        };
        const auto result = vm.interpreter.Call(vm.linker.Class(vm.model.ObjectClass(activity)).vtable[method],
            std::array{VmValue::Ref(activity), VmValue::Ref(replacement), VmValue::Ref(params)});
        CHECK(result.exception == exception);
        CHECK(vm.context->ui_tree.Get(node)->children.size() == 2);
        CHECK(vm.context->ui_content_mutation_depth == 0);
    }
}

TEST_CASE("DVM-245 Window append preserves Surface and destruction reentry stops stale installs") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ClickVm vm(backend);
        vm.context->activity = vm.activity;
        vm.context->managed_host_surface_open = true;
        const auto surface = vm.interpreter.NewIntrinsicInstance("Landroid/view/SurfaceView;");
        const auto surface_root = vm.interpreter.ProtectReferences(std::array{surface});
        vm.CallDirect(surface, "Landroid/view/SurfaceView;", "<init>", "(Landroid/content/Context;)V", {VmValue::Ref(vm.activity)});
        const auto holder = vm.CallOn(surface, "getHolder", "()Landroid/view/SurfaceHolder;").ref;
        const auto callback = vm.interpreter.NewIntrinsicInstance("LContentSurfaceCallback;");
        vm.CallOn(holder, "addCallback", "(Landroid/view/SurfaceHolder$Callback;)V", {VmValue::Ref(callback)});
        vm.CallOn(vm.activity, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(surface)});
        REQUIRE(vm.content_surface_creates == 1);
        const auto backing = vm.CallOn(holder, "getSurface", "()Landroid/view/Surface;").ref;
        const auto overlay = vm.interpreter.NewIntrinsicInstance("Landroid/view/View;");
        const auto overlay_root = vm.interpreter.ProtectReferences(std::array{overlay});
        vm.CallOn(vm.activity, "addContentView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
            {VmValue::Ref(overlay), VmValue::Ref(VmObjectRef{})});
        CHECK(vm.content_surface_creates == 1);
        CHECK(vm.content_surface_destroys == 0);
        CHECK(vm.context->holder_surfaces.at(holder.Value()) == backing);
        const auto parent = vm.CallOn(surface, "getParent", "()Landroid/view/ViewParent;").ref;
        const auto final = vm.interpreter.NewIntrinsicInstance("Landroid/view/View;");
        const auto temporary = vm.interpreter.NewIntrinsicInstance("Landroid/view/View;");
        const auto roots = vm.interpreter.ProtectReferences(std::array{final, temporary});
        bool reentered{};
        vm.content_surface_destroy_hook = [&] {
            if (reentered) return;
            reentered = true;
            vm.CallOn(vm.activity, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(final)});
        };
        vm.CallOn(vm.activity, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(temporary)});
        CHECK(reentered);
        CHECK(vm.content_surface_destroys == 1);
        CHECK(vm.context->content_view == final);
        CHECK(vm.CallOn(final, "getParent", "()Landroid/view/ViewParent;").ref == parent);
        CHECK_FALSE(vm.CallOn(temporary, "getParent", "()Landroid/view/ViewParent;").ref.IsValid());
        CHECK_FALSE(vm.CallOn(overlay, "getParent", "()Landroid/view/ViewParent;").ref.IsValid());
        CHECK_FALSE(vm.context->active_surface_holders.contains(holder.Value()));
        CHECK(vm.context->ui_content_mutation_depth == 0);
    }
}

TEST_CASE("APS-11 SurfaceView native window retires after surfaceDestroyed callbacks") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ClickVm vm(backend);
        vm.context->activity = vm.activity;
        vm.context->managed_host_surface_open = true;
        std::vector<std::uint32_t> retired;
        vm.context->retire_native_surface = [&](VmObjectRef holder) { retired.push_back(holder.Value()); };
        const auto surface = vm.interpreter.NewIntrinsicInstance("Landroid/view/SurfaceView;");
        const auto surface_root = vm.interpreter.ProtectReferences(std::array{surface});
        vm.CallDirect(surface, "Landroid/view/SurfaceView;", "<init>", "(Landroid/content/Context;)V", {VmValue::Ref(vm.activity)});
        const auto holder = vm.CallOn(surface, "getHolder", "()Landroid/view/SurfaceHolder;").ref;
        const auto callback = vm.interpreter.NewIntrinsicInstance("LContentSurfaceCallback;");
        vm.CallOn(holder, "addCallback", "(Landroid/view/SurfaceHolder$Callback;)V", {VmValue::Ref(callback)});
        vm.CallOn(vm.activity, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(surface)});
        REQUIRE(vm.content_surface_creates == 1);
        const auto backing = vm.CallOn(holder, "getSurface", "()Landroid/view/Surface;").ref;
        bool observed{};
        vm.content_surface_destroy_hook = [&] {
            // AOSP SurfaceView: the Surface is still valid inside surfaceDestroyed.
            observed = true;
            CHECK(retired.empty());
            CHECK(vm.CallOn(backing, "isValid", "()Z").AsInt() == 1);
        };
        const auto replacement = vm.interpreter.NewIntrinsicInstance("Landroid/view/View;");
        const auto replacement_root = vm.interpreter.ProtectReferences(std::array{replacement});
        vm.CallOn(vm.activity, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(replacement)});
        CHECK(observed);
        CHECK(vm.content_surface_destroys == 1);
        CHECK(retired == std::vector<std::uint32_t>{holder.Value()});
        CHECK(vm.CallOn(backing, "isValid", "()Z").AsInt() == 0);
    }
}

TEST_CASE("managed dynamic SurfaceView attach exposes its holder at the next traversal") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ClickVm vm(backend);
        vm.context->activity = vm.activity;
        vm.context->managed_host_surface_open = true;
        vm.context->defer_content_surface_callbacks = true;
        const auto group = vm.interpreter.NewIntrinsicInstance("Landroid/widget/FrameLayout;");
        const auto group_root = vm.interpreter.ProtectReferences(std::array{group});
        vm.CallOn(vm.activity, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(group)});
        const auto window = ActivityContentRoot(*vm.context);
        CHECK(SurfaceTraversalPending(*vm.context, *FindViewUiNode(*vm.context, group.Value())));
        const auto traverse = [&] {
            const auto error = AttachSurfaceViewSubtree(vm.interpreter, *vm.context, window);
            REQUIRE_MESSAGE(!error, error.value_or(""));
        };
        traverse();
        CHECK(vm.context->surface_traversal_pending.empty());

        // API19: addView, then getHolder/addCallback in the same message, still
        // observe surfaceCreated because the Surface is made at traversal.
        const auto make_surface = [&] {
            const auto surface = vm.interpreter.NewIntrinsicInstance("Landroid/view/SurfaceView;");
            vm.CallDirect(surface, "Landroid/view/SurfaceView;", "<init>", "(Landroid/content/Context;)V",
                {VmValue::Ref(vm.activity)});
            return surface;
        };
        const auto surface = make_surface();
        const auto surface_root = vm.interpreter.ProtectReferences(std::array{surface});
        vm.CallOn(group, "addView", "(Landroid/view/View;)V", {VmValue::Ref(surface)});
        const auto holder = vm.CallOn(surface, "getHolder", "()Landroid/view/SurfaceHolder;").ref;
        const auto callback = vm.interpreter.NewIntrinsicInstance("LContentSurfaceCallback;");
        vm.CallOn(holder, "addCallback", "(Landroid/view/SurfaceHolder$Callback;)V", {VmValue::Ref(callback)});
        CHECK(vm.content_surface_creates == 0);
        CHECK_FALSE(vm.context->active_surface_holders.contains(holder.Value()));
        CHECK(SurfaceTraversalPending(*vm.context, *FindViewUiNode(*vm.context, surface.Value())));
        traverse();
        CHECK(vm.content_surface_creates == 1);
        CHECK(vm.context->active_surface_holders.contains(holder.Value()));
        CHECK(vm.context->surface_traversal_pending.empty());
        traverse();
        CHECK(vm.content_surface_creates == 1);

        // After a traversal, a late holder observer misses the past creation.
        const auto late = make_surface();
        const auto late_root = vm.interpreter.ProtectReferences(std::array{late});
        vm.CallOn(group, "addView", "(Landroid/view/View;)V", {VmValue::Ref(late)});
        traverse();
        const auto late_holder = vm.CallOn(late, "getHolder", "()Landroid/view/SurfaceHolder;").ref;
        vm.CallOn(late_holder, "addCallback", "(Landroid/view/SurfaceHolder$Callback;)V", {VmValue::Ref(callback)});
        CHECK(vm.context->active_surface_holders.contains(late_holder.Value()));
        CHECK(vm.content_surface_creates == 1);
        CHECK_FALSE(RetireSurfaceHolderGeneration(vm.interpreter, *vm.context));
        CHECK(vm.content_surface_destroys == 2);
        CHECK(vm.context->surface_traversal_pending.empty());
    }
}

TEST_CASE("DVM-246 managed Dialog caches instances prepares args and preserves exceptional entries") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ClickVm vm(backend);
        const auto owner = vm.interpreter.NewIntrinsicInstance("LManagedDialogActivity;");
        const auto listener = vm.interpreter.NewIntrinsicInstance("LDialogObserver;");
        const auto roots = vm.interpreter.ProtectReferences(std::array{owner, listener});
        const auto dialog = vm.NewTextDialog(owner, listener);
        vm.CallOn(owner, "supply", "(Landroid/app/Dialog;)V", {VmValue::Ref(dialog)});
        vm.CallOn(owner, "showDialog", "(I)V", {VmValue::Int(1763356438)});
        REQUIRE(vm.context->dialog_presentations.size() == 1);
        CHECK(vm.context->dialog_presentations.front().dialog == dialog);
        CHECK(vm.CallOn(dialog, "getOwnerActivity", "()Landroid/app/Activity;").ref == owner);
        CHECK(vm.CallOn(dialog, "isShowing", "()Z").AsInt() == 1);
        const auto second = vm.NewTextDialog(owner, listener);
        const auto second_root = vm.interpreter.ProtectReferences(std::array{second});
        vm.CallOn(owner, "supply", "(Landroid/app/Dialog;)V", {VmValue::Ref(second)});
        const auto args = vm.interpreter.NewIntrinsicInstance("Landroid/os/Bundle;");
        const auto args_root = vm.interpreter.ProtectReferences(std::array{args});
        CHECK(vm.CallOn(owner, "showDialog", "(ILandroid/os/Bundle;)Z", {VmValue::Int(1763356438), VmValue::Ref(args)}).AsInt() == 1);
        CHECK(vm.dialog_creates == 1); CHECK(vm.dialog_prepares == 2);
        CHECK(vm.context->dialog_presentations.front().dialog == dialog);
        const auto prepared = *vm.linker.FindFieldRecursive(vm.model.ObjectClass(owner), "lastArgs", "Landroid/os/Bundle;");
        CHECK(vm.model.InstanceSlots(owner)[vm.linker.Field(prepared).slot].bits == args.Value());
        vm.CallOn(owner, "supply", "(Landroid/app/Dialog;)V", {VmValue::Ref(VmObjectRef{})});
        CHECK(vm.CallOn(owner, "showDialog", "(ILandroid/os/Bundle;)Z", {VmValue::Int(77), VmValue::Ref(args)}).AsInt() == 0);
        CHECK(vm.dialog_creates == 2); CHECK(vm.dialog_prepares == 2);
        vm.CallOn(owner, "removeDialog", "(I)V", {VmValue::Int(77)});
        const auto invoke = [&](const char* method, int id) {
            const auto type = vm.model.ObjectClass(owner);
            const auto slot = *vm.linker.FindVtableIndex(type, method, "(I)V");
            return vm.interpreter.Call(vm.linker.Class(type).vtable[slot], std::array{VmValue::Ref(owner), VmValue::Int(id)});
        };
        const auto absent = invoke("dismissDialog", 77);
        REQUIRE(absent.exception.IsValid());
        CHECK(vm.linker.Class(absent.exception_class).descriptor == "Ljava/lang/IllegalArgumentException;");
        const auto exception = vm.interpreter.NewIntrinsicInstance("Ljava/lang/IllegalStateException;");
        const auto exception_root = vm.interpreter.ProtectReferences(std::array{exception});
        vm.dialog_prepare_hook = [exception](IntrinsicContext&) { throw VmJavaThrow{"Ljava/lang/IllegalStateException;", "prepare", exception}; };
        CHECK(invoke("showDialog", 1763356438).exception == exception);
        CHECK(vm.context->dialog_presentations.size() == 1);
        vm.dialog_prepare_hook = {};
        vm.CallOn(owner, "dismissDialog", "(I)V", {VmValue::Int(1763356438)});
        CHECK(vm.context->dialog_presentations.empty());
        CHECK(vm.CallOn(dialog, "isShowing", "()Z").AsInt() == 0);
        static_cast<void>(vm.interpreter.CollectGarbage("managed-dialog-messages"));
        CHECK_FALSE(PumpJavaThreads(vm.interpreter, *vm.context).has_value());
        CHECK(vm.dialog_events == std::vector<std::string>{"onShow", "onDismiss"});
        vm.CallOn(owner, "showDialog", "(I)V", {VmValue::Int(1763356438)});
        CHECK(vm.context->dialog_presentations.front().dialog == dialog);
        CHECK(vm.dialog_creates == 2);
        vm.CallOn(owner, "removeDialog", "(I)V", {VmValue::Int(1763356438)});
        vm.CallOn(owner, "supply", "(Landroid/app/Dialog;)V", {VmValue::Ref(second)});
        vm.CallOn(owner, "showDialog", "(I)V", {VmValue::Int(1763356438)});
        CHECK(vm.context->dialog_presentations.front().dialog == second);
        CHECK(vm.dialog_creates == 3);
        vm.CallOn(owner, "onDestroy", "()V");
        CHECK(vm.context->dialog_presentations.empty());
    }
}

TEST_CASE("DVM-246 Dialog draws text blocks underlying input and queues button cancel key semantics") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ClickVm vm(backend);
        const auto listener = vm.interpreter.NewIntrinsicInstance("LDialogObserver;");
        const auto listener_root = vm.interpreter.ProtectReferences(std::array{listener});
        const auto dialog = vm.NewTextDialog(vm.activity, listener, false);
        const auto roots = vm.interpreter.ProtectReferences(std::array{vm.activity, dialog});
        vm.CallOn(dialog, "show", "()V"); vm.CallOn(dialog, "show", "()V");
        REQUIRE(vm.context->dialog_presentations.size() == 1);
        auto& modal = vm.context->dialog_presentations.front();
        const auto button = *modal.buttons[2];
        const auto rect = vm.context->ui_tree.Get(button)->screen_frame;
        CHECK(vm.context->ui_tree.Get(button)->text == u"OK");
        ui::UiOverlayRenderer renderer;
        const auto& frame = renderer.Render(vm.context->ui_tree, vm.context->ui_bitmaps, {800, 480});
        CHECK(frame.rgba8[3] >= 128);
        CHECK(frame.rgba8[(static_cast<std::size_t>(rect.top + 1) * 800 + static_cast<std::size_t>(rect.left + 1)) * 4] > 128);
        const auto pointer = [&](int action, float x, float y) {
            return DispatchAndroidDialogInput(vm.interpreter, *vm.context,
                {.type=AndroidBoundaryInputType::pointer_button, .x=x, .y=y, .pressed=action==0, .action=action});
        };
        const auto key = [&](int action, int code) {
            return DispatchAndroidDialogInput(vm.interpreter, *vm.context,
                {.type=AndroidBoundaryInputType::key, .code=code, .pressed=action==0, .action=action});
        };
        CHECK(pointer(0, 1, 1)); CHECK(pointer(1, 1, 1));
        CHECK(key(0, 4)); CHECK(key(1, 4));
        CHECK(vm.CallOn(dialog, "isShowing", "()Z").AsInt() == 1);
        CHECK_FALSE(PumpJavaThreads(vm.interpreter, *vm.context).has_value());
        CHECK(vm.dialog_events == std::vector<std::string>{"onShow"});
        vm.dialog_click_hook = [&](IntrinsicContext&) {
            vm.CallOn(dialog, "setTitle", "(Ljava/lang/CharSequence;)V", {VmValue::Ref(vm.interpreter.NewStringUtf8("Updated"))});
        };
        CHECK(pointer(0, static_cast<float>(rect.left + 2), static_cast<float>(rect.top + 2)));
        CHECK(pointer(1, static_cast<float>(rect.left + 2), static_cast<float>(rect.top + 2)));
        CHECK(vm.CallOn(dialog, "isShowing", "()Z").AsInt() == 1); // callback is queued, not inline.
        CHECK_FALSE(PumpJavaThreads(vm.interpreter, *vm.context).has_value());
        CHECK(vm.dialog_events == std::vector<std::string>{"onShow", "click:-3", "onDismiss"});
        CHECK(vm.context->dialog_presentations.empty());
        vm.dialog_click_hook = {};
        vm.dialog_events.clear();
        vm.CallOn(dialog, "setCancelable", "(Z)V", {VmValue::Int(1)});
        vm.CallOn(dialog, "show", "()V");
        vm.dialog_key_consumed = true;
        CHECK(key(0, 4)); CHECK(key(1, 4));
        CHECK(vm.CallOn(dialog, "isShowing", "()Z").AsInt() == 1);
        vm.dialog_key_consumed = false;
        CHECK(key(0, 4)); CHECK(key(1, 4));
        CHECK_FALSE(PumpJavaThreads(vm.interpreter, *vm.context).has_value());
        CHECK(vm.dialog_events == std::vector<std::string>{"onShow", "onCancel", "onDismiss"});
        CHECK(vm.context->dialog_presentations.empty());
        vm.dialog_events.clear();
        vm.CallOn(dialog, "show", "()V");
        vm.dialog_click_hook = [&](IntrinsicContext&) {
            vm.CallOn(dialog, "dismiss", "()V"); vm.CallOn(dialog, "show", "()V");
        };
        CHECK(key(0, 66)); CHECK(key(1, 66));
        CHECK_FALSE(PumpJavaThreads(vm.interpreter, *vm.context).has_value());
        CHECK(vm.CallOn(dialog, "isShowing", "()Z").AsInt() == 1); // old click cannot dismiss the new generation.
        CHECK(vm.context->dialog_presentations.size() == 1);
        vm.CallOn(dialog, "hide", "()V");
        CHECK_FALSE(HasAndroidModalDialog(*vm.context)); CHECK(vm.CallOn(dialog, "isShowing", "()Z").AsInt() == 1);
        vm.CallOn(dialog, "show", "()V"); CHECK(HasAndroidModalDialog(*vm.context));
        vm.CallOn(dialog, "dismiss", "()V");
        CHECK_FALSE(pointer(0, 1, 1));
    }
}

TEST_CASE("unconfigured dialog and strict WebView presentation fail explicitly") {
    ClickVm vm;
    vm.context->strict_webview_errors = true;
    const auto invoke = [&](const VmObjectRef receiver, const char* name,
                            const char* descriptor,
                            std::vector<VmValue> arguments = {}) {
        const auto owner = vm.model.ObjectClass(receiver);
        const auto index = vm.linker.FindVtableIndex(owner, name, descriptor);
        REQUIRE(index.has_value());
        arguments.insert(arguments.begin(), VmValue::Ref(receiver));
        return vm.interpreter.Call(vm.linker.Class(owner).vtable[*index],
                                   arguments);
    };
    const auto dialog = vm.interpreter.NewIntrinsicInstance(
        "Landroid/app/AlertDialog;");
    const auto dialog_result = invoke(dialog, "show", "()V");
    REQUIRE(dialog_result.exception.IsValid());
    CHECK(vm.linker.Class(dialog_result.exception_class).descriptor ==
          "Ljava/lang/UnsupportedOperationException;");
    REQUIRE(vm.ledger.Unimplemented().size() == 1);
    CHECK(vm.ledger.Unimplemented().front().id ==
          "runtime.ui.dialog.presentation");

    const auto web = vm.interpreter.NewIntrinsicInstance(
        "Landroid/webkit/WebView;");
    const auto web_result = invoke(
        web, "loadUrl", "(Ljava/lang/String;)V",
        {VmValue::Ref(vm.interpreter.NewStringUtf8("https://example.invalid"))});
    REQUIRE(web_result.exception.IsValid());
    CHECK(vm.linker.Class(web_result.exception_class).descriptor ==
          "Ljava/lang/UnsupportedOperationException;");
}

TEST_CASE("LUI18 Window attributes copy API19 values with stable per-window identity") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ClickVm vm(backend);
        const auto first = vm.CallOn(vm.activity, "getWindow", "()Landroid/view/Window;").ref;
        const auto second = vm.interpreter.NewIntrinsicInstance("Landroid/view/Window;");
        const auto source = vm.interpreter.NewIntrinsicInstance("Landroid/view/WindowManager$LayoutParams;");
        const auto roots = vm.interpreter.ProtectReferences(std::array{first, second, source});
        vm.CallDirect(source, "Landroid/view/WindowManager$LayoutParams;", "<init>", "()V");
        const auto attributes = [&](VmObjectRef window) {
            return vm.CallOn(window, "getAttributes", "()Landroid/view/WindowManager$LayoutParams;").ref;
        };
        const auto a = attributes(first), b = attributes(second);
        REQUIRE(a != b);
        const auto slot = [&](VmObjectRef object, const char* name, const char* type) -> Slot& {
            const auto field = vm.linker.FindFieldRecursive(vm.model.ObjectClass(object), name, type);
            REQUIRE(field.has_value());
            return vm.model.InstanceSlots(object)[vm.linker.Field(*field).slot];
        };
        CHECK(slot(a, "screenBrightness", "F").bits == std::bit_cast<std::uint32_t>(-1.0F));
        CHECK(static_cast<std::int32_t>(slot(a, "width", "I").bits) == -1);
        CHECK(slot(a, "alpha", "F").bits == std::bit_cast<std::uint32_t>(1.0F));
        const auto retained = vm.interpreter.NewStringUtf8("original");
        const auto replacement = vm.interpreter.NewStringUtf8("replacement");
        const auto string_roots = vm.interpreter.ProtectReferences(std::array{retained, replacement});
        slot(a, "packageName", "Ljava/lang/String;") = {retained.Value(), SlotTag::ref};
        slot(source, "packageName", "Ljava/lang/String;") = {replacement.Value(), SlotTag::ref};
        const auto token = vm.interpreter.NewIntrinsicInstance("Landroid/os/Binder;");
        const auto token_root = vm.interpreter.ProtectReferences(std::array{token});
        slot(source, "token", "Landroid/os/IBinder;") = {token.Value(), SlotTag::ref};
        slot(source, "screenBrightness", "F") = {std::bit_cast<std::uint32_t>(0.25F), SlotTag::cat1};
        slot(source, "buttonBrightness", "F") = {std::bit_cast<std::uint32_t>(0.5F), SlotTag::cat1};
        slot(source, "width", "I") = {321, SlotTag::cat1};
        slot(source, "flags", "I") = {0x400, SlotTag::cat1};
        slot(source, "memoryType", "I") = {9, SlotTag::cat1};
        vm.CallOn(first, "setAttributes", "(Landroid/view/WindowManager$LayoutParams;)V", {VmValue::Ref(source)});
        CHECK(attributes(first) == a);
        CHECK(slot(a, "screenBrightness", "F").bits == std::bit_cast<std::uint32_t>(0.25F));
        CHECK(slot(a, "buttonBrightness", "F").bits == std::bit_cast<std::uint32_t>(0.5F));
        CHECK(slot(a, "width", "I").bits == 321);
        CHECK(slot(a, "flags", "I").bits == 0x400);
        CHECK(slot(a, "packageName", "Ljava/lang/String;").bits == retained.Value());
        CHECK(slot(a, "token", "Landroid/os/IBinder;").bits == token.Value());
        CHECK(slot(a, "memoryType", "I").bits == 0); // AOSP copyFrom excludes this deprecated field.
        CHECK(slot(b, "screenBrightness", "F").bits == std::bit_cast<std::uint32_t>(-1.0F));
        slot(source, "screenBrightness", "F") = {0, SlotTag::cat1};
        CHECK(slot(a, "screenBrightness", "F").bits == std::bit_cast<std::uint32_t>(0.25F));
    }
}
