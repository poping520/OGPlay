#include "dexvm/resource_fixture.h"
#include "boot_dex.h"
// Widget state, resolved UiTree geometry and listener dispatch integration.

#include <doctest/doctest.h>

#include <array>
#include <algorithm>
#include <iterator>
#include <filesystem>
#include <fstream>
#include <memory>
#include <limits>
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

TEST_CASE("TextView Java state controls deterministic measure and draw state") {
    ClickVm vm;
    const auto text = vm.interpreter.NewIntrinsicInstance("Landroid/widget/TextView;");
    vm.CallOn(text, "setText", "(Ljava/lang/CharSequence;)V", {VmValue::Ref(vm.interpreter.NewStringUtf8("Hi"))});
    vm.CallOn(text, "setTextColor", "(I)V", {VmValue::Int(static_cast<std::int32_t>(0xffff0000U))});
    vm.CallOn(text, "setTextSize", "(F)V", {VmValue::Float(8.0F)});
    // setContentView(View) uses MATCH_PARENT; request intrinsic measurement explicitly.
    const auto params = vm.interpreter.NewIntrinsicInstance("Landroid/widget/FrameLayout$LayoutParams;");
    vm.CallDirect(params, "Landroid/widget/FrameLayout$LayoutParams;", "<init>", "(II)V",
                  {VmValue::Int(-2), VmValue::Int(-2)});
    vm.CallOn(vm.activity, "setContentView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
              {VmValue::Ref(text), VmValue::Ref(params)});
    const auto returned = vm.CallOn(text, "getText", "()Ljava/lang/CharSequence;").ref;
    CHECK(vm.model.StringValue(returned) == u"Hi");
    CHECK(vm.CallOn(text, "getWidth", "()I").AsInt() == 11);
    CHECK(vm.CallOn(text, "getHeight", "()I").AsInt() == 7);
    const auto node = FindViewUiNode(*vm.context, text.Value());
    REQUIRE(node.has_value());
    CHECK(vm.context->ui_tree.Get(*node)->text_color == 0xff0000ffU);

    vm.CallOn(text, "setText", "(Ljava/lang/CharSequence;)V", {VmValue::Ref(vm.interpreter.NewStringUtf8("H"))});
    CHECK(vm.CallOn(text, "getWidth", "()I").AsInt() == 5);

    const auto receiver_class = vm.model.ObjectClass(text);
    const auto method = vm.linker.FindVtableIndex(receiver_class, "setText", "(Ljava/lang/CharSequence;)V");
    REQUIRE(method.has_value());
    const auto multiline = vm.interpreter.Call(
        vm.linker.Class(receiver_class).vtable[*method],
        std::vector<VmValue>{VmValue::Ref(text), VmValue::Ref(vm.interpreter.NewStringUtf8("two\nlines"))});
    CHECK_FALSE(multiline.exception.IsValid());
    CHECK(vm.context->ui_tree.Get(*node)->text == u"two\nlines");
}

TEST_CASE("EditText Editable preserves identity text and watcher registration") {
    ClickVm vm;
    const auto edit = vm.interpreter.NewIntrinsicInstance("Landroid/widget/EditText;");
    vm.CallOn(edit, "setText", "(Ljava/lang/CharSequence;)V", {VmValue::Ref(vm.interpreter.NewStringUtf8("13"))});
    const auto first = vm.CallOn(edit, "getText", "()Landroid/text/Editable;").ref;
    const auto second = vm.CallOn(edit, "getText", "()Landroid/text/Editable;").ref;
    CHECK(first == second);
    const auto value = vm.CallOn(first, "toString", "()Ljava/lang/String;").ref;
    CHECK(vm.model.StringValue(value) == u"13");

    const auto watcher = vm.interpreter.NewIntrinsicInstance("LRecordingTextWatcher;");
    vm.CallOn(edit, "addTextChangedListener", "(Landroid/text/TextWatcher;)V", {VmValue::Ref(watcher)});
    REQUIRE(vm.context->text_watchers.contains(edit.Value()));
    CHECK(vm.context->text_watchers.at(edit.Value()).front() == watcher);
    vm.CallOn(vm.activity, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(edit)});
    static_cast<void>(vm.interpreter.CollectGarbage("text-watcher-owner"));

    const auto node = FindViewUiNode(*vm.context, edit.Value());
    REQUIRE(node.has_value());
    vm.context->ui_tree.Get(*node)->numeric_input = true;
    vm.context->ui_tree.Get(*node)->max_length = 3;
    vm.CallOn(first, "replace", "(IILjava/lang/CharSequence;)Landroid/text/Editable;",
              {VmValue::Int(1), VmValue::Int(2), VmValue::Ref(vm.interpreter.NewStringUtf8("x45"))});
    CHECK(vm.context->ui_tree.Get(*node)->text == u"145");
    CHECK(vm.text_events == std::vector<std::string>{"before", "on", "after"});
    CHECK(vm.last_text_range == std::array<std::int32_t, 3>{1, 1, 2});

    vm.text_events.clear();
    vm.CallOn(first, "clear", "()V");
    CHECK(vm.context->ui_tree.Get(*node)->text.empty());
    CHECK(vm.text_events == std::vector<std::string>{"before", "on", "after"});

    vm.text_events.clear();
    vm.CallOn(edit, "setText", "(Ljava/lang/CharSequence;)V", {VmValue::Ref(vm.interpreter.NewStringUtf8("9z876"))});
    CHECK(vm.context->ui_tree.Get(*node)->text == u"987");
    CHECK(vm.text_events == std::vector<std::string>{"before", "on", "after"});
}

TEST_CASE("DVM-123 Button.Small style attributes resolve from framework resources") {
    ClickVm vm(InterpreterBackend::switch_dispatch, false);
    vm.CallOn(vm.activity, "attachBaseContext", "(Landroid/content/Context;)V",
              {VmValue::Ref(ogplay::test::MakeRomAppContext(vm.interpreter, vm.context))});
    vm.CallOn(vm.activity, "setTheme", "(I)V", {VmValue::Int(0x0103006b)});
    const auto theme = vm.CallOn(vm.activity, "getTheme", "()Landroid/content/res/Resources$Theme;").ref;
    const auto value = vm.interpreter.NewIntrinsicInstance("Landroid/util/TypedValue;");
    const auto root = vm.interpreter.ProtectReferences(std::array{theme, value});
    CHECK(vm.CallOn(theme, "resolveAttribute", "(ILandroid/util/TypedValue;Z)Z",
                    {VmValue::Int(0x01010049), VmValue::Ref(value), VmValue::Int(1)})
              .AsInt());
    const auto type = vm.linker.FindFieldRecursive(vm.model.ObjectClass(value), "type", "I");
    REQUIRE(type);
    CHECK(vm.model.InstanceSlots(value)[vm.linker.Field(*type).slot].bits == 1);
}

TEST_CASE("DVM-340 subtitle TextView shadow and printable text allow both Views to detach") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ClickVm vm(backend);
        const auto text = vm.interpreter.NewIntrinsicInstance("Landroid/widget/TextView;");
        const auto surface = vm.interpreter.NewIntrinsicInstance("Landroid/view/SurfaceView;");
        vm.CallOn(vm.activity, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(surface)});
        const auto callback = vm.interpreter.NewIntrinsicInstance("LContentSurfaceCallback;");
        const auto holder = vm.CallOn(surface, "getHolder", "()Landroid/view/SurfaceHolder;").ref;
        const auto roots = vm.interpreter.ProtectReferences(std::array{text, surface, callback, holder});
        vm.CallOn(holder, "addCallback", "(Landroid/view/SurfaceHolder$Callback;)V", {VmValue::Ref(callback)});
        REQUIRE_FALSE(DispatchSurfaceHolderCallbacks(vm.interpreter, *vm.context, SurfaceHolderPhase::created));
        REQUIRE_FALSE(DispatchSurfaceHolderCallbacks(vm.interpreter, *vm.context, SurfaceHolderPhase::changed));
        CHECK(vm.content_surface_creates == 1);
        vm.CallOn(text, "setGravity", "(I)V", {VmValue::Int(17)});
        vm.CallOn(text, "setTextColor", "(I)V", {VmValue::Int(-1)});
        vm.CallOn(text, "setTypeface", "(Landroid/graphics/Typeface;I)V",
                  {VmValue::Ref(VmObjectRef{}), VmValue::Int(1)});
        vm.CallOn(text, "setTextSize", "(F)V", {VmValue::Float(18)});
        vm.CallOn(text, "setShadowLayer", "(FFFI)V",
                  {VmValue::Float(2), VmValue::Float(0), VmValue::Float(0), VmValue::Int(-16777216)});
        const auto params = vm.interpreter.NewIntrinsicInstance("Landroid/widget/FrameLayout$LayoutParams;");
        vm.CallDirect(params, "Landroid/widget/FrameLayout$LayoutParams;", "<init>", "(III)V",
                      {VmValue::Int(-1), VmValue::Int(-2), VmValue::Int(80)});
        vm.CallOn(vm.activity, "addContentView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
                  {VmValue::Ref(text), VmValue::Ref(params)});
        vm.CallOn(text, "setText", "(Ljava/lang/CharSequence;)V",
                  {VmValue::Ref(vm.interpreter.NewStringUtf8("I'm on the island."))});
        CHECK(vm.CallOn(text, "getShadowRadius", "()F").AsFloat() == 2);
        CHECK(vm.CallOn(text, "getShadowDx", "()F").AsFloat() == 0);
        CHECK(vm.CallOn(text, "getShadowDy", "()F").AsFloat() == 0);
        CHECK(vm.CallOn(text, "getShadowColor", "()I").AsInt() == -16777216);
        const auto id = *FindViewUiNode(*vm.context, text.Value());
        CHECK(vm.context->ui_tree.Get(id)->text_shadow.radius == 2);
        vm.CallOn(text, "setShadowLayer", "(FFFI)V",
                  {VmValue::Float(40), VmValue::Float(3.5F), VmValue::Float(-2.5F), VmValue::Int(0x40123456)});
        CHECK(vm.CallOn(text, "getShadowRadius", "()F").AsFloat() == 40);
        CHECK(vm.CallOn(text, "getShadowDx", "()F").AsFloat() == 3.5F);
        CHECK(vm.CallOn(text, "getShadowDy", "()F").AsFloat() == -2.5F);
        CHECK(vm.CallOn(text, "getShadowColor", "()I").AsInt() == 0x40123456);
        REQUIRE(EnsureAndroidUiTraversal(vm.interpreter, *vm.context, AndroidUiTraversalReason::OwnerTick,
                                         AndroidUiTraversalPhase::ResolveOnly));
        const auto projection = vm.context->ui_tree.PublishedProjection();
        REQUIRE(projection);
        CHECK(projection->Get(id)->text_shadow.radius == 40);
        CHECK(projection->Get(id)->text_shadow.rgba == 0x12345640U);
        for (const auto view : {text, surface}) {
            const auto parent = vm.CallOn(view, "getParent", "()Landroid/view/ViewParent;").ref;
            REQUIRE(parent.IsValid());
            vm.CallOn(parent, "removeView", "(Landroid/view/View;)V", {VmValue::Ref(view)});
            CHECK_FALSE(vm.CallOn(view, "getParent", "()Landroid/view/ViewParent;").ref.IsValid());
        }
        // Managed Surface retirement is delivered at the normal owner safe point.
        REQUIRE_FALSE(PumpAndroidSurfaceEvents(vm.interpreter, *vm.context));
        CHECK(vm.content_surface_destroys == 1);
        REQUIRE_FALSE(PumpAndroidSurfaceEvents(vm.interpreter, *vm.context));
        CHECK(vm.content_surface_destroys == 1);
        CHECK(vm.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-340 unsupported shadow geometry is ignored atomically and recorded once") {
    ClickVm vm;
    const auto text = vm.interpreter.NewIntrinsicInstance("Landroid/widget/TextView;");
    vm.CallOn(text, "setShadowLayer", "(FFFI)V",
              {VmValue::Float(-2), VmValue::Float(1), VmValue::Float(-1), VmValue::Int(-1)});
    CHECK(vm.CallOn(text, "getShadowRadius", "()F").AsFloat() == -2);
    const auto type = vm.model.ObjectClass(text);
    const auto method = vm.linker.FindVtableIndex(type, "setShadowLayer", "(FFFI)V");
    REQUIRE(method);
    for (const auto args :
         {std::array{std::numeric_limits<float>::quiet_NaN(), 0.F, 0.F},
          std::array{2.F, std::numeric_limits<float>::infinity(), 0.F}, std::array{2.F, 0.F, -4097.F}}) {
        const auto result =
            vm.interpreter.Call(vm.linker.Class(type).vtable[*method],
                                std::vector{VmValue::Ref(text), VmValue::Float(args[0]), VmValue::Float(args[1]),
                                            VmValue::Float(args[2]), VmValue::Int(0)});
        CHECK_FALSE(result.exception.IsValid());
        CHECK(vm.CallOn(text, "getShadowRadius", "()F").AsFloat() == -2);
        CHECK(vm.CallOn(text, "getShadowDx", "()F").AsFloat() == 1);
    }
    CHECK(vm.ledger.Unimplemented().empty());
    const auto warnings = vm.ledger.Warnings();
    REQUIRE(warnings.size() == 1U);
    CHECK(warnings[0].id == "ui.degraded.TextView.shadowGeometry");
    CHECK(warnings[0].count == 3U);
}
