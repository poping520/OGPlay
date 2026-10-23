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

TEST_CASE("direct View window focus callback does not dispatch descendants") {
    ClickVm vm;
    const auto root = vm.interpreter.NewIntrinsicInstance(
        "Landroid/widget/FrameLayout;");
    const auto focus_class = vm.linker.FindClass("LFocusView;");
    REQUIRE(focus_class.has_value());
    const auto initialized = vm.interpreter.EnsureClassInitialized(*focus_class);
    REQUIRE_FALSE(initialized.exception.IsValid());
    const auto child = vm.model.NewInstance(
        *focus_class, vm.linker.Class(*focus_class).instance_slots);
    vm.CallDirect(child, "LFocusView;", "<init>",
                  "(Landroid/content/Context;)V",
                  {VmValue::Ref(vm.activity)});
    const auto focus_value = [&](const char* method_name) {
        const auto method = vm.linker.FindDirectMethod(
            *focus_class, method_name, "()I");
        REQUIRE(method.has_value());
        const auto outcome = vm.interpreter.Call(*method, {});
        REQUIRE_FALSE(outcome.exception.IsValid());
        return outcome.value.AsInt();
    };
    vm.CallOn(root, "addView", "(Landroid/view/View;I)V",
              {VmValue::Ref(child), VmValue::Int(0)});

    vm.CallOn(root, "onWindowFocusChanged", "(Z)V", {VmValue::Int(1)});
    CHECK(focus_value("getFocusEvents") == 0);
    vm.CallOn(child, "onWindowFocusChanged", "(Z)V", {VmValue::Int(1)});
    CHECK(focus_value("getFocusEvents") == 1);
    CHECK(focus_value("getLastFocus") == 1);
    vm.CallOn(child, "onWindowFocusChanged", "(Z)V", {VmValue::Int(0)});
    CHECK(focus_value("getFocusEvents") == 2);
    CHECK(focus_value("getLastFocus") == 0);
}

TEST_CASE("View OnFocusChangeListener links and dispatches through its API 19 interface") {
    ClickVm vm;
    const auto listener_interface = vm.linker.FindClass(
        "Landroid/view/View$OnFocusChangeListener;");
    const auto listener_class = vm.linker.FindClass("LFocusListener;");
    REQUIRE(listener_interface.has_value());
    REQUIRE(listener_class.has_value());

    const auto initialized =
        vm.interpreter.EnsureClassInitialized(*listener_class);
    REQUIRE_FALSE(initialized.exception.IsValid());
    CHECK(vm.linker.IsAssignable(*listener_interface, *listener_class));
    const auto listener = vm.model.NewInstance(
        *listener_class, vm.linker.Class(*listener_class).instance_slots);
    vm.CallDirect(listener, "LFocusListener;", "<init>", "()V");

    const auto dispatch = vm.linker.FindDirectMethod(
        *listener_class, "dispatch",
        "(Landroid/view/View$OnFocusChangeListener;Landroid/view/View;Z)V");
    REQUIRE(dispatch.has_value());
    const auto dispatch_focus = [&](const std::int32_t focused) {
        const auto outcome = vm.interpreter.Call(
            *dispatch,
            std::vector<VmValue>{VmValue::Ref(listener),
                                 VmValue::Ref(vm.skip_button),
                                 VmValue::Int(focused)});
        REQUIRE_FALSE(outcome.exception.IsValid());
    };
    const auto read_value = [&](const char* name) {
        const auto method = vm.linker.FindDirectMethod(
            *listener_class, name, "()I");
        REQUIRE(method.has_value());
        const auto outcome = vm.interpreter.Call(*method, {});
        REQUIRE_FALSE(outcome.exception.IsValid());
        return outcome.value.AsInt();
    };

    dispatch_focus(1);
    CHECK(read_value("getFocusChanges") == 1);
    CHECK(read_value("getLastFocusChange") == 1);
    dispatch_focus(0);
    CHECK(read_value("getFocusChanges") == 2);
    CHECK(read_value("getLastFocusChange") == 0);
}

TEST_CASE("View system UI visibility exposes API 19 state and listener shape") {
    ClickVm vm;
    const auto listener_interface = vm.linker.FindClass(
        "Landroid/view/View$OnSystemUiVisibilityChangeListener;");
    const auto listener_class = vm.linker.FindClass(
        "LSystemUiVisibilityListener;");
    REQUIRE(listener_interface.has_value());
    REQUIRE(listener_class.has_value());
    CHECK(vm.linker.IsAssignable(*listener_interface, *listener_class));
    REQUIRE(vm.linker.Class(*listener_interface).own_virtual_methods.size() == 1U);
    const auto& callback = vm.linker.Method(
        vm.linker.Class(*listener_interface).own_virtual_methods.front());
    CHECK(callback.name == "onSystemUiVisibilityChange");
    CHECK(callback.descriptor == "(I)V");
    CHECK(callback.access_flags == 0x0401U);

    const auto window = vm.CallOn(
        vm.activity, "getWindow", "()Landroid/view/Window;").ref;
    REQUIRE(window.IsValid());
    const auto decor = vm.CallOn(
        window, "getDecorView", "()Landroid/view/View;").ref;
    REQUIRE(decor.IsValid());
    CHECK(vm.CallOn(window, "getDecorView",
                    "()Landroid/view/View;").ref == decor);
    CHECK(vm.CallOn(decor, "getSystemUiVisibility", "()I").AsInt() == 0);
    vm.CallOn(decor, "setSystemUiVisibility", "(I)V",
              {VmValue::Int(0x1f06)});
    CHECK(vm.CallOn(decor, "getSystemUiVisibility", "()I").AsInt() ==
          0x1f06);
    CHECK(vm.CallOn(vm.skip_button, "getSystemUiVisibility", "()I").AsInt() ==
          0);

    const auto listener = vm.interpreter.NewIntrinsicInstance(
        "LSystemUiVisibilityListener;");
    vm.CallOn(
        decor, "setOnSystemUiVisibilityChangeListener",
        "(Landroid/view/View$OnSystemUiVisibilityChangeListener;)V",
        {VmValue::Ref(listener)});
    CHECK(vm.system_ui_visibility_events == 0);
    CHECK_FALSE(vm.linker
                    .FindFieldRecursive(vm.model.ObjectClass(decor), "mOgplaySystemUiVisibilityListener",
                                        "Landroid/view/View$OnSystemUiVisibilityChangeListener;")
                    .has_value());
    CHECK(vm.context->view_adapter_references.at(decor.Value()).system_ui_listener == listener);
    vm.CallOn(
        decor, "setOnSystemUiVisibilityChangeListener",
        "(Landroid/view/View$OnSystemUiVisibilityChangeListener;)V",
        {VmValue::Ref(VmObjectRef{})});
    CHECK_FALSE(vm.context->view_adapter_references.at(decor.Value()).system_ui_listener.IsValid());
}

TEST_CASE("View system UI constants are public and reflect their API 19 values") {
    ClickVm vm;
    const auto view_class = vm.linker.ResolveDescriptor("Landroid/view/View;");
    const auto class_object = vm.model.ClassObject(view_class);
    const auto read_public_int = [&](const char* name) {
        const auto field = vm.CallOn(
            class_object, "getField",
            "(Ljava/lang/String;)Ljava/lang/reflect/Field;",
            {VmValue::Ref(vm.interpreter.NewStringUtf8(name))}).ref;
        REQUIRE(field.IsValid());
        return vm.CallOn(field, "getInt", "(Ljava/lang/Object;)I",
                         {VmValue::Ref(VmObjectRef{})}).AsInt();
    };
    CHECK(read_public_int("SYSTEM_UI_FLAG_VISIBLE") == 0x0000);
    CHECK(read_public_int("SYSTEM_UI_FLAG_LOW_PROFILE") == 0x0001);
    CHECK(read_public_int("SYSTEM_UI_FLAG_HIDE_NAVIGATION") == 0x0002);
    CHECK(read_public_int("SYSTEM_UI_FLAG_FULLSCREEN") == 0x0004);
    CHECK(read_public_int("SYSTEM_UI_FLAG_LAYOUT_STABLE") == 0x0100);
    CHECK(read_public_int("SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION") == 0x0200);
    CHECK(read_public_int("SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN") == 0x0400);
    CHECK(read_public_int("SYSTEM_UI_FLAG_IMMERSIVE") == 0x0800);
    CHECK(read_public_int("SYSTEM_UI_FLAG_IMMERSIVE_STICKY") == 0x1000);
}

TEST_CASE("View dispatchKeyEvent routes bounded KeyEvent actions to overrides") {
    ClickVm vm;
    const auto view = vm.interpreter.NewIntrinsicInstance("LTestKeyView;");
    vm.CallDirect(view, "Landroid/view/View;", "<init>",
                  "(Landroid/content/Context;)V",
                  {VmValue::Ref(vm.activity)});
    const auto event = vm.interpreter.NewIntrinsicInstance(
        "Landroid/view/KeyEvent;");
    vm.CallDirect(event, "Landroid/view/KeyEvent;", "<init>", "(II)V",
                  {VmValue::Int(0), VmValue::Int(42)});

    CHECK(vm.CallOn(view, "dispatchKeyEvent",
                    "(Landroid/view/KeyEvent;)Z",
                    {VmValue::Ref(event)}).AsInt() == 1);
    CHECK(vm.key_down_events == 1);
    CHECK(vm.key_up_events == 0);
    CHECK(vm.last_key_code == 42);

    const auto up = vm.interpreter.NewIntrinsicInstance(
        "Landroid/view/KeyEvent;");
    vm.CallDirect(up, "Landroid/view/KeyEvent;", "<init>", "(II)V",
                  {VmValue::Int(1), VmValue::Int(42)});
    CHECK(vm.CallOn(view, "dispatchKeyEvent",
                    "(Landroid/view/KeyEvent;)Z",
                    {VmValue::Ref(up)}).AsInt() == 1);
    CHECK(vm.key_down_events == 1);
    CHECK(vm.key_up_events == 1);

    const auto multiple = vm.interpreter.NewIntrinsicInstance(
        "Landroid/view/KeyEvent;");
    vm.CallDirect(multiple, "Landroid/view/KeyEvent;", "<init>", "(II)V",
                  {VmValue::Int(2), VmValue::Int(7)});
    CHECK(vm.CallOn(view, "dispatchKeyEvent",
                    "(Landroid/view/KeyEvent;)Z",
                    {VmValue::Ref(multiple)}).AsInt() == 1);
    CHECK(vm.key_multiple_events == 1);
    CHECK(vm.last_key_code == 7);
}

TEST_CASE("KeyEvent exposes API19 unicode meta and repeat semantics") {
    ClickVm vm;
    const auto event = vm.interpreter.NewIntrinsicInstance(
        "Landroid/view/KeyEvent;");
    vm.CallDirect(event, "Landroid/view/KeyEvent;", "<init>", "(JJIIIIII)V",
                  {VmValue::Long(10), VmValue::Long(11), VmValue::Int(0),
                   VmValue::Int(29), VmValue::Int(1), VmValue::Int(0x41),
                   VmValue::Int(7), VmValue::Int(4)});

    CHECK(vm.CallOn(event, "getKeyCode", "()I").AsInt() == 29);
    CHECK(vm.CallOn(event, "getRepeatCount", "()I").AsInt() == 1);
    CHECK(vm.CallOn(event, "getMetaState", "()I").AsInt() == 0x41);
    CHECK(vm.CallOn(event, "getDeviceId", "()I").AsInt() == 7);
    CHECK(vm.CallOn(event, "getScanCode", "()I").AsInt() == 4);
    CHECK(vm.CallOn(event, "getUnicodeChar", "()I").AsInt() == 'A');
    CHECK(vm.CallOn(event, "getUnicodeChar", "(I)I",
                    {VmValue::Int(0)}).AsInt() == 'a');
    SetAndroidKeyEventUnicode(vm.interpreter, event, 0x00e9);
    CHECK(vm.CallOn(event, "getUnicodeChar", "()I").AsInt() == 0x00e9);
    CHECK(vm.CallOn(event, "getUnicodeChar", "(I)I",
                    {VmValue::Int(0)}).AsInt() == 'a');

    const auto arrow = vm.interpreter.NewIntrinsicInstance(
        "Landroid/view/KeyEvent;");
    vm.CallDirect(arrow, "Landroid/view/KeyEvent;", "<init>", "(II)V",
                  {VmValue::Int(0), VmValue::Int(19)});
    CHECK(vm.CallOn(arrow, "getUnicodeChar", "()I").AsInt() == 0);
}
