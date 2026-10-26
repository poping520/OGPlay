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
