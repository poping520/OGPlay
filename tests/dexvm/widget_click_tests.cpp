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

using namespace ogplay::test::widget;
using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

TEST_CASE("visible button with a listener receives the click") {
    ClickVm vm;
    const auto listener = vm.NewListener();
    vm.CallOn(vm.skip_button, "setOnClickListener",
              "(Landroid/view/View$OnClickListener;)V",
              {VmValue::Ref(listener)});
    // Both buttons visible: row centered, skip is the second (x 50..70).
    CHECK(vm.Click(60.0F, 95.0F).has_value());
    CHECK(vm.CallStaticInt("getClicks") == 1);
    // The sibling without a listener consumes nothing.
    CHECK_FALSE(vm.Click(40.0F, 95.0F).has_value());
    CHECK(vm.CallStaticInt("getClicks") == 1);
}

TEST_CASE("Button inherits View enabled state and disabled hit filtering") {
    ClickVm vm;
    vm.CallOn(vm.skip_button, "setOnClickListener",
              "(Landroid/view/View$OnClickListener;)V",
              {VmValue::Ref(vm.NewListener())});

    CHECK(vm.CallOn(vm.skip_button, "isEnabled", "()Z").AsInt() == 1);
    vm.CallOn(vm.skip_button, "setEnabled", "(Z)V", {VmValue::Int(0)});
    CHECK(vm.CallOn(vm.skip_button, "isEnabled", "()Z").AsInt() == 0);
    CHECK_FALSE(vm.Click(60.0F, 95.0F).has_value());
    CHECK(vm.CallStaticInt("getClicks") == 0);

    vm.CallOn(vm.skip_button, "setEnabled", "(Z)V", {VmValue::Int(1)});
    CHECK(vm.CallOn(vm.skip_button, "isEnabled", "()Z").AsInt() == 1);
    CHECK(vm.Click(60.0F, 95.0F).has_value());
    CHECK(vm.CallStaticInt("getClicks") == 1);
}

TEST_CASE("DVM-179 clickable gates touch clicks and still consumes without a listener") {
    ClickVm vm;
    const auto listener = vm.NewListener();
    vm.CallOn(vm.skip_button, "setOnClickListener",
              "(Landroid/view/View$OnClickListener;)V",
              {VmValue::Ref(listener)});
    CHECK(vm.CallOn(vm.skip_button, "isClickable", "()Z").AsInt() == 1);
    CHECK(vm.Click(60.0F, 95.0F).has_value());
    CHECK(vm.CallStaticInt("getClicks") == 1);

    vm.CallOn(vm.skip_button, "setClickable", "(Z)V", {VmValue::Int(0)});
    CHECK(vm.CallOn(vm.skip_button, "isClickable", "()Z").AsInt() == 0);
    const auto node = FindViewUiNode(*vm.context, vm.skip_button.Value());
    REQUIRE(node.has_value());
    REQUIRE(vm.context->ui_click_listeners.contains(*node));
    CHECK_FALSE(vm.Click(60.0F, 95.0F).has_value());
    CHECK(vm.CallStaticInt("getClicks") == 1);
    CHECK_FALSE(InvokeViewOnClick(vm.interpreter, *vm.context,
                                  vm.skip_button.Value())
                    .has_value());
    CHECK(vm.CallStaticInt("getClicks") == 2);

    vm.CallOn(vm.skip_button, "setClickable", "(Z)V", {VmValue::Int(1)});
    CHECK(vm.Click(60.0F, 95.0F).has_value());
    CHECK(vm.CallStaticInt("getClicks") == 3);

    vm.context->ui_touch_listeners[*node] = listener;
    vm.CallOn(vm.skip_button, "setClickable", "(Z)V", {VmValue::Int(0)});
    vm.SetTouchResult(true);
    auto gesture = DispatchViewGestureEvent(
        vm.interpreter, *vm.context, vm.skip_button.Value(), 0, 60.0F, 95.0F,
        false, false);
    CHECK(gesture.handled);
    CHECK_FALSE(gesture.click_eligible);
    gesture = DispatchViewGestureEvent(
        vm.interpreter, *vm.context, vm.skip_button.Value(), 1, 60.0F, 95.0F,
        gesture.click_eligible, gesture.touch_consumed);
    CHECK(vm.CallStaticInt("getTouches") == 2);
    CHECK(vm.CallStaticInt("getClicks") == 3);

    const auto plain = vm.interpreter.NewIntrinsicInstance("Landroid/view/View;");
    vm.CallDirect(plain, "Landroid/view/View;", "<init>",
                  "(Landroid/content/Context;)V",
                  {VmValue::Ref(vm.activity)});
    vm.CallOn(vm.activity, "setContentView", "(Landroid/view/View;)V",
              {VmValue::Ref(plain)});
    vm.CallOn(plain, "setClickable", "(Z)V", {VmValue::Int(1)});
    auto deep = ogplay::session::DispatchDeepTouchEvent(
        vm.interpreter, *vm.context, 0, 50.0F, 50.0F, 0U);
    CHECK_FALSE(deep.error.has_value());
    CHECK(deep.handled);
    CHECK(deep.captured_view == plain.Value());
    CHECK(vm.CallStaticInt("getClicks") == 3);

    vm.CallOn(plain, "setClickable", "(Z)V", {VmValue::Int(0)});
    const auto custom = vm.NewTouchView();
    vm.CallOn(custom, "setClickable", "(Z)V", {VmValue::Int(0)});
    vm.CallOn(vm.activity, "setContentView", "(Landroid/view/View;)V",
              {VmValue::Ref(custom)});
    vm.ResetTouchView(true);
    deep = ogplay::session::DispatchDeepTouchEvent(
        vm.interpreter, *vm.context, 0, 12.0F, 34.0F, 0U);
    CHECK(deep.handled);
    CHECK(deep.captured_view == custom.Value());
    CHECK(vm.TouchViewValue("getEvents") == 1);
}

TEST_CASE("UI hit test chooses the topmost resolved clickable node") {
    ClickVm vm;
    vm.CallOn(vm.skip_button, "setOnClickListener",
              "(Landroid/view/View$OnClickListener;)V",
              {VmValue::Ref(vm.NewListener())});
    const auto top_view = vm.interpreter.NewIntrinsicInstance(
        "Landroid/widget/ImageButton;");
    const auto top_node = vm.context->ui_tree.CreateNode(ui::UiClass::ImageButton);
    BindViewToUiNode(*vm.context, top_view, top_node);
    vm.context->ui_tree.Get(top_node)->layout.width = {ui::SizeMode::Fixed, 20};
    vm.context->ui_tree.Get(top_node)->layout.height = {ui::SizeMode::Fixed, 10};
    vm.context->ui_tree.Get(top_node)->layout.layout_gravity = 0x51;
    vm.context->ui_tree.Attach(vm.context->ui_tree.Root(), top_node);
    vm.CallOn(top_view, "setOnClickListener",
              "(Landroid/view/View$OnClickListener;)V",
              {VmValue::Ref(vm.NewListener())});
    CHECK(FindClickableViewAt(*vm.context, 50.0F, 95.0F) == top_view.Value());
    vm.context->ui_tree.SetVisibility(top_node, ui::Visibility::Invisible);
    CHECK(FindClickableViewAt(*vm.context, 50.0F, 95.0F) ==
          vm.skip_button.Value());
    CHECK_FALSE(ViewContainsPoint(*vm.context, top_view.Value(), 50.0F, 95.0F));
}

TEST_CASE("hidden buttons do not receive clicks and GONE reflows the row") {
    ClickVm vm;
    const auto listener = vm.NewListener();
    vm.CallOn(vm.skip_button, "setOnClickListener",
              "(Landroid/view/View$OnClickListener;)V",
              {VmValue::Ref(listener)});
    // INVISIBLE keeps the slot but never consumes the touch.
    vm.CallOn(vm.skip_button, "setVisibility", "(I)V", {VmValue::Int(4)});
    CHECK(vm.CallOn(vm.skip_button, "getVisibility", "()I").AsInt() == 4);
    CHECK_FALSE(vm.Click(60.0F, 95.0F).has_value());

    // GONE removes the sibling: skip alone re-centers to x 40..60.
    vm.CallOn(vm.skip_button, "setVisibility", "(I)V", {VmValue::Int(0)});
    vm.CallOn(vm.other_button, "setVisibility", "(I)V", {VmValue::Int(8)});
    CHECK(vm.Click(50.0F, 95.0F).has_value());
    CHECK(vm.CallStaticInt("getClicks") == 1);
    CHECK_FALSE(vm.Click(65.0F, 95.0F).has_value());
}

TEST_CASE("touches outside every clickable view fall through") {
    ClickVm vm;
    const auto listener = vm.NewListener();
    vm.CallOn(vm.skip_button, "setOnClickListener",
              "(Landroid/view/View$OnClickListener;)V",
              {VmValue::Ref(listener)});
    // The fullscreen VideoView has no listener, so the middle of the
    // screen belongs to Activity.onTouchEvent.
    CHECK_FALSE(vm.Click(50.0F, 50.0F).has_value());
    // Inside the bar but outside both buttons.
    CHECK_FALSE(vm.Click(10.0F, 95.0F).has_value());
}

TEST_CASE("touch consumption and click eligibility stay independent") {
    struct Case { bool touch; bool click; bool fallback; int clicks; int cancel; };
    for (const auto test : {
             Case{false, false, true, 0, 0}, Case{true, false, false, 0, 0},
             Case{false, true, false, 1, 0}, Case{true, true, false, 0, 0},
             Case{false, true, false, 0, 1}, Case{false, true, false, 0, 2},
             Case{false, true, false, 0, 3}}) {
        ClickVm vm;
        const auto listener = vm.NewListener();
        const auto node =
            FindViewUiNode(*vm.context, vm.skip_button.Value());
        REQUIRE(node.has_value());
        vm.context->ui_touch_listeners[*node] = listener;
        if (test.click) {
            vm.context->ui_click_listeners[*node] = listener;
            vm.context->ui_tree.SetClickable(*node, true);
        }
        vm.SetTouchResult(test.touch);
        auto result = DispatchViewGestureEvent(vm.interpreter, *vm.context,
            vm.skip_button.Value(), 0, 60.0F, 95.0F, false, false);
        CHECK_FALSE(result.error.has_value());
        CHECK((!result.handled) == test.fallback);
        if (result.keep_capture) {
            float up_x = 60.0F;
            if (test.cancel == 1) up_x = 80.0F;
            if (test.cancel == 2) vm.context->ui_tree.SetVisibility(
                *node, ui::Visibility::Invisible);
            if (test.cancel == 3) vm.context->ui_tree.Detach(*node);
            result = DispatchViewGestureEvent(vm.interpreter, *vm.context,
                vm.skip_button.Value(), 1, up_x, 95.0F,
                result.click_eligible, result.touch_consumed);
            CHECK_FALSE(result.error.has_value());
        }
        CHECK(vm.CallStaticInt("getTouches") == (test.fallback ? 1 : 2));
        CHECK(vm.CallStaticInt("getClicks") == test.clicks);
    }
}

TEST_CASE("Scroll interception cancels a child click after touch slop") {
    ClickVm vm;
    const auto listener = vm.NewListener();
    const auto node = FindViewUiNode(*vm.context, vm.skip_button.Value());
    REQUIRE(node.has_value());
    vm.context->ui_touch_listeners[*node] = listener;
    vm.context->ui_click_listeners[*node] = listener;
    vm.context->ui_tree.SetClickable(*node, true);
    vm.SetTouchResult(false);

    auto result = DispatchViewGestureEvent(
        vm.interpreter, *vm.context, vm.skip_button.Value(), 0,
        60.0F, 95.0F, false, false);
    REQUIRE(result.keep_capture);
    CHECK_FALSE(ogplay::session::ShouldInterceptScrollGesture(
        100, 95.0F, 88.0F, 1.0F));
    CHECK_FALSE(ogplay::session::ShouldInterceptScrollGesture(
        0, 95.0F, 70.0F, 1.0F));
    CHECK(ogplay::session::ShouldInterceptScrollGesture(
        100, 95.0F, 86.0F, 1.0F));

    result = DispatchViewGestureEvent(
        vm.interpreter, *vm.context, vm.skip_button.Value(), 3,
        60.0F, 86.0F, result.click_eligible, result.touch_consumed);
    CHECK_FALSE(result.keep_capture);
    CHECK_FALSE(result.click_eligible);
    CHECK(vm.CallStaticInt("getTouches") == 2);
    CHECK(vm.CallStaticInt("getClicks") == 0);

    // A separate tap remains one DOWN/UP pair and clicks once.
    result = DispatchViewGestureEvent(
        vm.interpreter, *vm.context, vm.skip_button.Value(), 0,
        60.0F, 95.0F, false, false);
    result = DispatchViewGestureEvent(
        vm.interpreter, *vm.context, vm.skip_button.Value(), 1,
        60.0F, 95.0F, result.click_eligible, result.touch_consumed);
    CHECK(vm.CallStaticInt("getClicks") == 1);
}

TEST_CASE("deep View override captures a listener-free touch gesture") {
    ClickVm vm;
    const auto view = vm.NewTouchView();
    vm.CallOn(vm.activity, "setContentView", "(Landroid/view/View;)V",
              {VmValue::Ref(view)});

    vm.ResetTouchView(true);
    auto result = ogplay::session::DispatchDeepTouchEvent(
        vm.interpreter, *vm.context, 0, 12.0F, 34.0F, 0U);
    CHECK_FALSE(result.error.has_value());
    CHECK(result.handled);
    CHECK(result.captured_view == view.Value());
    CHECK(vm.TouchViewValue("getEvents") == 1);
    CHECK(vm.TouchViewValue("getLastAction") == 0);

    vm.ResetTouchView(false);
    result = ogplay::session::DispatchDeepTouchEvent(
        vm.interpreter, *vm.context, 2, 130.0F, 135.0F,
        result.captured_view);
    CHECK_FALSE(result.error.has_value());
    CHECK_FALSE(result.handled);
    CHECK(result.captured_view == view.Value());
    CHECK(vm.TouchViewValue("getEvents") == 1);
    CHECK(vm.TouchViewValue("getLastAction") == 2);
    result = ogplay::session::DispatchDeepTouchEvent(
        vm.interpreter, *vm.context, 1, 130.0F, 135.0F,
        result.captured_view);
    CHECK_FALSE(result.error.has_value());
    CHECK_FALSE(result.handled);
    CHECK(result.captured_view == 0U);
    CHECK(vm.TouchViewValue("getEvents") == 2);
    CHECK(vm.TouchViewValue("getLastAction") == 1);

    result = ogplay::session::DispatchDeepTouchEvent(
        vm.interpreter, *vm.context, 2, 14.0F, 36.0F, 0U);
    CHECK_FALSE(result.error.has_value());
    CHECK_FALSE(result.handled);
    CHECK(result.captured_view == 0U);
    CHECK(vm.TouchViewValue("getEvents") == 2);
    result = ogplay::session::DispatchDeepTouchEvent(
        vm.interpreter, *vm.context, 0, 14.0F, 36.0F, 0U);
    CHECK_FALSE(result.error.has_value());
    CHECK_FALSE(result.handled);
    CHECK(result.captured_view == 0U);
    CHECK(vm.TouchViewValue("getEvents") == 3);
}

TEST_CASE("deep touch traversal skips defaults and follows reverse Z order") {
    ClickVm vm;
    const auto root = vm.interpreter.NewIntrinsicInstance(
        "Landroid/widget/FrameLayout;");
    const auto lower = vm.interpreter.NewIntrinsicInstance(
        "LTestContentView;");
    vm.CallDirect(lower, "Landroid/view/View;", "<init>",
                  "(Landroid/content/Context;)V",
                  {VmValue::Ref(vm.activity)});
    const auto upper = vm.NewTouchView();
    vm.CallOn(root, "addView", "(Landroid/view/View;)V",
              {VmValue::Ref(lower)});
    vm.CallOn(root, "addView", "(Landroid/view/View;)V",
              {VmValue::Ref(upper)});
    vm.CallOn(vm.activity, "setContentView", "(Landroid/view/View;)V",
              {VmValue::Ref(root)});

    vm.ResetTouchView(false);
    vm.content_view_handled = true;
    const auto result = ogplay::session::DispatchDeepTouchEvent(
        vm.interpreter, *vm.context, 0, 50.0F, 50.0F, 0U);
    CHECK_FALSE(result.error.has_value());
    CHECK(result.handled);
    CHECK(result.captured_view == lower.Value());
    CHECK(vm.TouchViewValue("getEvents") == 1);
    CHECK(vm.content_view_events == 1);

    const auto empty = vm.interpreter.NewIntrinsicInstance(
        "Landroid/widget/FrameLayout;");
    vm.CallOn(vm.activity, "setContentView", "(Landroid/view/View;)V",
              {VmValue::Ref(empty)});
    const auto rejected = ogplay::session::DispatchDeepTouchEvent(
        vm.interpreter, *vm.context, 0, 50.0F, 50.0F, 0U);
    CHECK_FALSE(rejected.error.has_value());
    CHECK_FALSE(rejected.handled);
    CHECK(rejected.captured_view == 0U);
}

TEST_CASE("setVisibility rejects values outside VISIBLE/INVISIBLE/GONE") {
    ClickVm vm;
    const auto receiver_class = vm.model.ObjectClass(vm.skip_button);
    const auto index = vm.linker.FindVtableIndex(receiver_class,
                                                 "setVisibility", "(I)V");
    REQUIRE(index.has_value());
    const auto outcome = vm.interpreter.Call(
        vm.linker.Class(receiver_class).vtable[*index],
        std::vector<VmValue>{VmValue::Ref(vm.skip_button), VmValue::Int(3)});
    CHECK(outcome.exception.IsValid());
}

TEST_CASE("skip click stops the playing fake video end to end") {
    ClickVm vm;
    vm.CallOn(vm.video_view, "setVideoPath", "(Ljava/lang/String;)V",
              {VmValue::Ref(vm.interpreter.NewStringUtf8(
                  "/sdcard/short-mp4v-aac.mp4"))});
    vm.CallOn(vm.video_view, "start", "()V");
    CHECK_FALSE(vm.context->video_views.empty());

    const auto listener = vm.NewListener();
    vm.BindTarget(vm.video_view);
    vm.CallOn(vm.skip_button, "setOnClickListener",
              "(Landroid/view/View$OnClickListener;)V",
              {VmValue::Ref(listener)});
    // The trailer flow: the button starts hidden, a touch elsewhere would
    // reveal it (activity logic), then the click stops playback.
    vm.CallOn(vm.skip_button, "setVisibility", "(I)V", {VmValue::Int(8)});
    CHECK_FALSE(vm.Click(60.0F, 95.0F).has_value());
    vm.CallOn(vm.skip_button, "setVisibility", "(I)V", {VmValue::Int(0)});
    CHECK(vm.Click(60.0F, 95.0F).has_value());
    CHECK(vm.CallStaticInt("getClicks") == 1);
    CHECK(vm.context->video_views.empty());  // stopPlayback ran
}

TEST_CASE("BND46 deep touch dispatch preserves encoded action and cancels capture") {
    ClickVm vm;
    const auto view = vm.NewTouchView();
    vm.CallOn(vm.activity, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(view)});
    vm.ResetTouchView(true);
    AndroidBoundaryInput snapshot;
    snapshot.type = AndroidBoundaryInputType::pointer_motion;
    snapshot.action = 5 | (1 << 8);
    AndroidInputPointer first, second;
    first.axes[0] = 12; first.axes[1] = 34;
    second = first; second.id = 3;
    snapshot.pointers = {first, second};
    auto result = ogplay::session::DispatchDeepTouchEvent(
        vm.interpreter, *vm.context, 5, 12, 34, view.Value(), &snapshot);
    CHECK_FALSE(result.error.has_value());
    CHECK(vm.TouchViewValue("getLastAction") == (5 | 1 << 8));
    CHECK(result.captured_view == view.Value());
    result = ogplay::session::DispatchDeepTouchEvent(
        vm.interpreter, *vm.context, 3, 12, 34, view.Value(), &snapshot);
    CHECK_FALSE(result.error.has_value());
    CHECK(vm.TouchViewValue("getLastAction") == 3);
    CHECK(result.captured_view == 0);
}
