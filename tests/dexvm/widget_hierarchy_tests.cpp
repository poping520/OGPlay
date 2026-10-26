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
    const auto receivers = FindTouchReceiversAt(vm.interpreter, *vm.context, 50.0F, 50.0F);
    REQUIRE_FALSE(receivers.empty());
    CHECK(receivers.front() == touch.Value());

    vm.ResetTouchView(true);
    const auto result = ogplay::session::DispatchDeepTouchEvent(
        vm.interpreter, *vm.context, 0, 50.0F, 50.0F, 0U);
    CHECK_FALSE(result.error.has_value());
    CHECK(result.handled);
    CHECK(result.captured_view == touch.Value());
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
    CHECK(vm.CallOn(panel, "getOrientation", "()I").AsInt() == 1);
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
            if (std::string_view(descriptor) != "Landroid/webkit/WebView;") {
                const auto type = f.linker.ResolveDescriptor(descriptor);
                const auto object = f.vm.NewIntrinsicInstance(descriptor);
                const auto constructor = f.linker.FindDirectMethod(type, "<init>", "(Landroid/content/Context;)V");
                REQUIRE(constructor);
                const auto result = f.vm.Call(*constructor, std::array{VmValue::Ref(object), VmValue::Ref(owner)});
                CHECK(result.exception.IsValid());  // The unmodified default requests unimplemented fading edges.
                CHECK(result.exception_message.find(std::string_view(descriptor) == "Landroid/widget/ScrollView;"
                                                        ? "initializeFadingEdge"
                                                        : "reviewed construction adapter") != std::string::npos);
            } else {
                const auto object = f.New(descriptor, "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
                CHECK(f.On(object, "getContext", "()Landroid/content/Context;").ref == owner);
            }
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
        CHECK(calls[2] == 0);  // ScrollView default construction failed before FrameLayout.
        CHECK(calls[3] == 1);
        CHECK(calls[5] == 0);  // Table kinds are outside the reviewed construction set.
        CHECK(calls[6] == 1);
    }
}
