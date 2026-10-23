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
    CHECK(FindClickableViewAt(vm.interpreter, *vm.context, 50.0F, 95.0F) == top_view.Value());
    vm.context->ui_tree.SetVisibility(top_node, ui::Visibility::Invisible);
    CHECK(FindClickableViewAt(vm.interpreter, *vm.context, 50.0F, 95.0F) == vm.skip_button.Value());
    CHECK_FALSE(ViewContainsPoint(vm.interpreter, *vm.context, top_view.Value(), 50.0F, 95.0F));
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
    REQUIRE(ogplay::session::DispatchDeepTouchEvent(vm.interpreter, *vm.context, 0, 12, 34, 0).handled);
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

TEST_CASE("LUI18 decor dispatch shares host capture and virtual Activity fallback") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ClickVm vm(backend);
        vm.context->activity = vm.activity;
        const auto window = vm.CallOn(vm.activity, "getWindow", "()Landroid/view/Window;").ref;
        const auto decor = vm.CallOn(window, "getDecorView", "()Landroid/view/View;").ref;
        const auto view = vm.NewTouchView();
        vm.CallOn(vm.activity, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(view)});
        vm.ResetTouchView(true);
        const auto send = [&](int action, float x, float y) {
            const auto event = MakeMotionEvent(vm.interpreter, action, x, y, 0);
            return vm.CallOn(decor, "dispatchTouchEvent", "(Landroid/view/MotionEvent;)Z", {VmValue::Ref(event)}).AsInt();
        };
        CHECK(send(0, 12, 34) == 1);
        CHECK(vm.context->ui_touch_capture == view.Value());
        CHECK(DispatchDeepTouchEvent(vm.interpreter, *vm.context, 2, 130, 135, 0).handled);
        CHECK(send(1, 130, 135) == 1);
        CHECK(vm.TouchViewValue("getEvents") == 3);
        CHECK(vm.context->ui_touch_capture == 0);
        // Host DOWN, guest multi-pointer phases, guest UP share the owner.
        CHECK(DispatchDeepTouchEvent(vm.interpreter, *vm.context, 0, 12, 34, 0).handled);
        AndroidBoundaryInput input;
        input.type = AndroidBoundaryInputType::pointer_motion;
        input.action = 5 | (1 << 8);
        AndroidInputPointer first, second;
        first.axes[0] = 130; first.axes[1] = 135; second = first; second.id = 4;
        input.pointers = {first, second};
        auto event = MakeMotionEvent(vm.interpreter, input);
        CHECK(vm.CallOn(decor, "dispatchTouchEvent", "(Landroid/view/MotionEvent;)Z", {VmValue::Ref(event)}).AsInt() == 1);
        CHECK(vm.TouchViewValue("getLastAction") == (5 | (1 << 8)));
        input.action = 6 | (1 << 8);
        event = MakeMotionEvent(vm.interpreter, input);
        CHECK(vm.CallOn(decor, "dispatchTouchEvent", "(Landroid/view/MotionEvent;)Z", {VmValue::Ref(event)}).AsInt() == 1);
        CHECK(vm.context->ui_touch_capture == view.Value());
        CHECK(send(1, 130, 135) == 1);

        const auto fallback = vm.interpreter.NewIntrinsicInstance("LTouchFallbackProbe;");
        vm.context->activity = fallback;
        const auto w2 = vm.CallOn(fallback, "getWindow", "()Landroid/view/Window;").ref;
        const auto d2 = vm.CallOn(w2, "getDecorView", "()Landroid/view/View;").ref;
        vm.ResetTouchView(false);
        const auto down = MakeMotionEvent(vm.interpreter, 0, 12, 34, 0);
        CHECK(vm.CallOn(d2, "dispatchTouchEvent", "(Landroid/view/MotionEvent;)Z", {VmValue::Ref(down)}).AsInt() == 1);
        CHECK(vm.user_interactions == 1);
        CHECK(vm.activity_touch_events == 1);
        const auto override = vm.interpreter.NewIntrinsicInstance("LTouchActivityProbe;");
        const auto w3 = vm.CallOn(override, "getWindow", "()Landroid/view/Window;").ref;
        const auto d3 = vm.CallOn(w3, "getDecorView", "()Landroid/view/View;").ref;
        CHECK(vm.CallOn(d3, "dispatchTouchEvent", "(Landroid/view/MotionEvent;)Z", {VmValue::Ref(down)}).AsInt() == 1);
        CHECK(vm.touch_dispatch_events == 1);
    }
}

TEST_CASE("LUI18 ViewGroup interception cancels child capture") {
    ClickVm vm;
    const auto group = vm.interpreter.NewIntrinsicInstance("LTouchGroupProbe;");
    const auto child = vm.NewTouchView();
    vm.CallOn(group, "addView", "(Landroid/view/View;)V", {VmValue::Ref(child)});
    vm.CallOn(vm.activity, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(group)});
    vm.ResetTouchView(true);
    REQUIRE(DispatchDeepTouchEvent(vm.interpreter, *vm.context, 0, 12, 34, 0).handled);
    CHECK(vm.intercept_events == 1);
    vm.intercept_touches = true;
    CHECK(DispatchDeepTouchEvent(vm.interpreter, *vm.context, 2, 13, 35, 0).handled);
    CHECK(vm.TouchViewValue("getLastAction") == 3);
    CHECK(vm.context->ui_touch_capture == 0);
    CHECK(vm.TouchViewValue("getEvents") == 2);
}

TEST_CASE("LUI18 guest View calls enabled listener before virtual onTouchEvent") {
    ClickVm vm;
    const auto view = vm.NewTouchView();
    const auto listener = vm.NewListener();
    vm.CallOn(view, "setOnTouchListener", "(Landroid/view/View$OnTouchListener;)V", {VmValue::Ref(listener)});
    vm.ResetTouchView(true);
    const auto event = MakeMotionEvent(vm.interpreter, 0, 12, 34, 0);
    const auto root = vm.interpreter.ProtectReferences(std::array{event});
    vm.SetTouchResult(true);
    CHECK(vm.CallOn(view, "dispatchTouchEvent", "(Landroid/view/MotionEvent;)Z", {VmValue::Ref(event)}).AsInt() == 1);
    CHECK(vm.CallStaticInt("getTouches") == 1);
    CHECK(vm.TouchViewValue("getEvents") == 0);
    vm.SetTouchResult(false);
    CHECK(vm.CallOn(view, "dispatchTouchEvent", "(Landroid/view/MotionEvent;)Z", {VmValue::Ref(event)}).AsInt() == 1);
    CHECK(vm.CallStaticInt("getTouches") == 2);
    CHECK(vm.TouchViewValue("getEvents") == 1);
    vm.CallOn(view, "setEnabled", "(Z)V", {VmValue::Int(0)});
    CHECK(vm.CallOn(view, "dispatchTouchEvent", "(Landroid/view/MotionEvent;)Z", {VmValue::Ref(event)}).AsInt() == 1);
    CHECK(vm.CallStaticInt("getTouches") == 2);
    CHECK(vm.TouchViewValue("getEvents") == 2);
}

TEST_CASE("DVM-261 DEX framework overrides dispatch and super retains native behavior") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::android_value::AndroidValueVm fixture(backend, {}, false, "framework_override.dex", true);
        const auto owner = fixture.vm.NewIntrinsicInstance("Landroid/content/Context;");
        const auto hits = [&](const VmObjectRef object) {
            const auto field = fixture.linker.FindFieldRecursive(fixture.model.ObjectClass(object), "calls", "I");
            REQUIRE(field.has_value());
            return fixture.model.InstanceSlots(object)[fixture.linker.Field(*field).slot].bits;
        };
        const auto pause = fixture.New("Lfixture/PauseView;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        auto state = std::make_shared<DexVmAndroidContext::GlSurfaceRuntime>();
        state->view = pause;
        fixture.context->gl_surface_runtimes[pause.Value()] = state;
        fixture.Static("Lfixture/FrameworkOverrideProbe;", "pause", "(Landroid/opengl/GLSurfaceView;)V",
                       {VmValue::Ref(pause)});
        CHECK(hits(pause) == 1);
        CHECK(state->paused);

        const auto view = fixture.New("Lfixture/IdView;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        fixture.Static("Lfixture/FrameworkOverrideProbe;", "id", "(Landroid/view/View;I)V",
                       {VmValue::Ref(view), VmValue::Int(261)});
        CHECK(hits(view) == 1);
        CHECK(fixture.On(view, "getId", "()I").AsInt() == 261);

        const auto web = fixture.New("Lfixture/ChromeView;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto chrome = fixture.New("Landroid/webkit/WebChromeClient;");
        fixture.Static("Lfixture/FrameworkOverrideProbe;", "chrome",
                       "(Landroid/webkit/WebView;Landroid/webkit/WebChromeClient;)V",
                       {VmValue::Ref(web), VmValue::Ref(chrome)});
        CHECK(hits(web) == 1);
        const auto field = fixture.linker.FindFieldRecursive(fixture.model.ObjectClass(web), "mWebChromeClient",
                                                             "Landroid/webkit/WebChromeClient;");
        REQUIRE(field.has_value());
        CHECK(fixture.model.InstanceSlots(web)[fixture.linker.Field(*field).slot].bits == chrome.Value());

        const auto handler = fixture.New("Lfixture/AtTimeHandler;");
        const auto message = fixture.vm.NewIntrinsicInstance("Landroid/os/Message;");
        CHECK(fixture
                  .Static("Lfixture/FrameworkOverrideProbe;", "delayed", "(Landroid/os/Handler;Landroid/os/Message;J)Z",
                          {VmValue::Ref(handler), VmValue::Ref(message), VmValue::Long(10)})
                  .AsInt() == 1);
        CHECK(hits(handler) == 1);
        CHECK_FALSE(fixture.context->scheduled_work.empty());
    }
}

TEST_CASE("DVM-261 native template methods preserve overrides and default state") {
    using ogplay::test::android_value::AndroidValueVm;
    const auto parent_call = [](IntrinsicContext& call, const char* parent, const char* name, const char* signature) {
        auto& linker = call.vm.Linker();
        const auto type = linker.ResolveDescriptor(parent);
        const auto method = std::string_view(name) == "<init>"
                                ? *linker.FindDirectMethod(type, name, signature)
                                : linker.Class(type).vtable[*linker.FindVtableIndex(type, name, signature)];
        std::vector<VmValue> arguments{VmValue::Ref(call.receiver)};
        arguments.insert(arguments.end(), call.arguments.begin(), call.arguments.end());
        const auto outcome = call.vm.Call(method, arguments);
        if (outcome.exception.IsValid())
            throw VmJavaThrow{linker.Class(outcome.exception_class).descriptor, outcome.exception_message,
                              outcome.exception};
        return outcome.value;
    };
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        int chooser_calls{}, text_calls{}, size_calls{}, single_calls{}, lines_calls{};
        int indexed_calls{}, params_calls{}, flags_calls{}, color_calls{}, looper_calls{};
        const auto add_override = [&](IntrinsicClassBuilder& builder, const char* parent, const char* name,
                                      const char* signature, int& count) {
            builder.OverrideMethod(name, signature,
                                   [parent_call, parent, name, signature, &count](IntrinsicContext& call) {
                                       ++count;
                                       return parent_call(call, parent, name, signature);
                                   });
        };
        auto gl = IntrinsicClassBuilder::Class("Ltest/TemplateGl;", "Landroid/opengl/GLSurfaceView;");
        add_override(gl, "Landroid/opengl/GLSurfaceView;", "setEGLConfigChooser",
                     "(Landroid/opengl/GLSurfaceView$EGLConfigChooser;)V", chooser_calls);
        auto text = IntrinsicClassBuilder::Class("Ltest/TemplateText;", "Landroid/widget/TextView;");
        add_override(text, "Landroid/widget/TextView;", "setText",
                     "(Ljava/lang/CharSequence;Landroid/widget/TextView$BufferType;)V", text_calls);
        add_override(text, "Landroid/widget/TextView;", "setTextSize", "(IF)V", size_calls);
        add_override(text, "Landroid/widget/TextView;", "setSingleLine", "(Z)V", single_calls);
        add_override(text, "Landroid/widget/TextView;", "setLines", "(I)V", lines_calls);
        auto group = IntrinsicClassBuilder::Class("Ltest/TemplateGroup;", "Landroid/view/ViewGroup;");
        add_override(group, "Landroid/view/ViewGroup;", "addView", "(Landroid/view/View;I)V", indexed_calls);
        add_override(group, "Landroid/view/ViewGroup;", "addView",
                     "(Landroid/view/View;ILandroid/view/ViewGroup$LayoutParams;)V", params_calls);
        auto window = IntrinsicClassBuilder::Class("Ltest/TemplateWindow;", "Landroid/view/Window;");
        add_override(window, "Landroid/view/Window;", "setFlags", "(II)V", flags_calls);
        auto color = IntrinsicClassBuilder::Class("Ltest/TemplateColor;", "Landroid/graphics/drawable/ColorDrawable;");
        color.Constructor("(I)V", [parent_call](IntrinsicContext& call) {
            return parent_call(call, "Landroid/graphics/drawable/ColorDrawable;", "<init>", "(I)V");
        });
        add_override(color, "Landroid/graphics/drawable/ColorDrawable;", "setColor", "(I)V", color_calls);
        auto thread = IntrinsicClassBuilder::Class("Ltest/TemplateThread;", "Landroid/os/HandlerThread;");
        thread.OverrideMethod("getLooper", "()Landroid/os/Looper;", [&looper_calls](IntrinsicContext&) {
            ++looper_calls;
            return VmValue::Ref(VmObjectRef{});
        });
        AndroidValueVm fixture(backend,
                               {std::move(gl).Build(), std::move(text).Build(), std::move(group).Build(),
                                std::move(window).Build(), std::move(color).Build(), std::move(thread).Build()},
                               false, nullptr, true);
        const auto view = fixture.vm.NewIntrinsicInstance("Ltest/TemplateGl;");
        fixture.On(view, "setEGLConfigChooser", "(Z)V", {VmValue::Int(1)});
        fixture.On(
            view, "setEGLConfigChooser", "(IIIIII)V",
            {VmValue::Int(5), VmValue::Int(6), VmValue::Int(5), VmValue::Int(0), VmValue::Int(24), VmValue::Int(8)});
        CHECK(chooser_calls == 2);
        CHECK(fixture.context->gl_surface_config_specs.at(view.Value()) ==
              std::vector<std::int32_t>{5, 6, 5, 0, 24, 8});
        CHECK_FALSE(fixture.context->gl_surface_runtimes.contains(view.Value()));
        const auto widget = fixture.vm.NewIntrinsicInstance("Ltest/TemplateText;");
        fixture.On(widget, "setText", "(Ljava/lang/CharSequence;)V",
                   {VmValue::Ref(fixture.vm.NewStringUtf8("override"))});
        fixture.On(widget, "setTextSize", "(F)V", {VmValue::Float(18.0F)});
        fixture.On(widget, "setSingleLine", "()V");
        CHECK(text_calls == 1);
        CHECK(size_calls == 1);
        CHECK(single_calls == 1);
        CHECK(lines_calls == 1);
        CHECK(fixture.vm.StringUtf8(fixture.On(widget, "getText", "()Ljava/lang/CharSequence;").ref) == "override");
        CHECK(fixture.On(widget, "getTextSize", "()F").AsFloat() == 18.0F);
        const auto parent = fixture.vm.NewIntrinsicInstance("Ltest/TemplateGroup;");
        const auto child = fixture.vm.NewIntrinsicInstance("Landroid/view/View;");
        fixture.On(parent, "addView", "(Landroid/view/View;)V", {VmValue::Ref(child)});
        CHECK(indexed_calls == 1);
        CHECK(params_calls == 1);
        CHECK(fixture.On(parent, "getChildCount", "()I").AsInt() == 1);
        fixture.On(parent, "removeView", "(Landroid/view/View;)V", {VmValue::Ref(child)});
        const auto params =
            fixture.New("Landroid/view/ViewGroup$LayoutParams;", "(II)V", {VmValue::Int(10), VmValue::Int(20)});
        fixture.On(parent, "addView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
                   {VmValue::Ref(child), VmValue::Ref(params)});
        CHECK(params_calls == 2);
        const auto win = fixture.vm.NewIntrinsicInstance("Ltest/TemplateWindow;");
        fixture.On(win, "addFlags", "(I)V", {VmValue::Int(3)});
        fixture.On(win, "clearFlags", "(I)V", {VmValue::Int(1)});
        CHECK(flags_calls == 2);
        const auto drawable = fixture.New("Ltest/TemplateColor;", "(I)V", {VmValue::Int(0x12345678)});
        CHECK(color_calls == 1);
        CHECK(fixture.On(drawable, "getColor", "()I").AsInt() == 0x12345678);
        const auto worker = fixture.vm.NewIntrinsicInstance("Ltest/TemplateThread;");
        CHECK(fixture.On(worker, "quit", "()Z").AsInt() == 0);
        CHECK(fixture.On(worker, "quitSafely", "()Z").AsInt() == 0);
        CHECK(looper_calls == 2);
    }
}

TEST_CASE("DVM-262 Button constructor uses the ROM three-argument overload and style") {
    using ogplay::test::android_value::AndroidValueVm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend, {}, false, nullptr, true);
        const auto type = f.linker.ResolveDescriptor("Landroid/widget/Button;");
        const auto target =
            f.linker.FindDirectMethod(type, "<init>", "(Landroid/content/Context;Landroid/util/AttributeSet;I)V");
        REQUIRE(target.has_value());
        const auto original = f.linker.Method(*target).implementation;
        int calls = 0;
        f.linker.MutableMethod(*target).implementation = [original, &calls](IntrinsicContext& c) {
            ++calls;
            CHECK(c.arguments[2].AsInt() == 0x1010048);
            return original(c);
        };
        const auto context = f.vm.NewIntrinsicInstance("Landroid/content/Context;");
        const auto button = f.vm.NewIntrinsicInstance("Landroid/widget/Button;");
        const auto constructor =
            f.linker.FindDirectMethod(type, "<init>", "(Landroid/content/Context;Landroid/util/AttributeSet;)V");
        REQUIRE(constructor.has_value());
        const auto outcome = f.vm.Call(
            *constructor, std::array{VmValue::Ref(button), VmValue::Ref(context), VmValue::Ref(VmObjectRef{})});
        CHECK(calls == 1);
        // DIAG-9 X: theme/default-style resolution is unsupported. The real target
        // must receive the ROM style and propagate this failure, not fake success.
        REQUIRE(outcome.exception.IsValid());
        CHECK(f.linker.Class(outcome.exception_class).descriptor == "Ljava/lang/UnsatisfiedLinkError;");
        CHECK(outcome.exception_message.find("Landroid/content/Context;.getThemeResId()I") != std::string::npos);
    }
}

TEST_CASE("DVM-262 EditText constructor follows both this and TextView super overloads") {
    using ogplay::test::android_value::AndroidValueVm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend, {}, false, nullptr, true);
        std::array<int, 2> calls{};
        const std::array owners{"Landroid/widget/EditText;", "Landroid/widget/TextView;"};
        for (std::size_t i = 0; i < owners.size(); ++i) {
            const auto target = f.linker.FindDirectMethod(f.linker.ResolveDescriptor(owners[i]), "<init>",
                                                          "(Landroid/content/Context;Landroid/util/AttributeSet;I)V");
            REQUIRE(target.has_value());
            const auto original = f.linker.Method(*target).implementation;
            f.linker.MutableMethod(*target).implementation = [original, &calls, i](IntrinsicContext& c) {
                ++calls[i];
                CHECK(c.arguments[2].AsInt() == 0x101006e);
                return original(c);
            };
        }
        const auto context = f.vm.NewIntrinsicInstance("Landroid/content/Context;");
        const auto edit = f.vm.NewIntrinsicInstance("Landroid/widget/EditText;");
        const auto constructor = f.linker.FindDirectMethod(f.model.ObjectClass(edit), "<init>",
                                                           "(Landroid/content/Context;Landroid/util/AttributeSet;)V");
        REQUIRE(constructor.has_value());
        const auto outcome =
            f.vm.Call(*constructor, std::array{VmValue::Ref(edit), VmValue::Ref(context), VmValue::Ref(VmObjectRef{})});
        CHECK(calls == std::array<int, 2>{1, 1});
        // DIAG-9 X: the original TextView target rejects the existing defStyleAttr
        // projection. Only this/super forwarding is under test; keep the failure.
        REQUIRE(outcome.exception.IsValid());
        CHECK(f.linker.Class(outcome.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        CHECK(outcome.exception_message.find("defStyleAttr") != std::string::npos);
        CHECK(outcome.exception_message.find("registered default-style projection") != std::string::npos);
    }
}
