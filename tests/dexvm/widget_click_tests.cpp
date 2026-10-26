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

TEST_CASE("DVM-331 ROM setVisibility masks the supplied visibility bits") {
    ClickVm vm;
    const auto receiver_class = vm.model.ObjectClass(vm.skip_button);
    const auto index = vm.linker.FindVtableIndex(receiver_class,
                                                 "setVisibility", "(I)V");
    REQUIRE(index.has_value());
    const auto outcome = vm.interpreter.Call(
        vm.linker.Class(receiver_class).vtable[*index],
        std::vector<VmValue>{VmValue::Ref(vm.skip_button), VmValue::Int(3)});
    CHECK_FALSE(outcome.exception.IsValid());
    CHECK(vm.CallOn(vm.skip_button, "getVisibility", "()I").AsInt() == 0);
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
        vm.context->activity = override;
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
    vm.CallOn(vm.activity, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(view)});
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
