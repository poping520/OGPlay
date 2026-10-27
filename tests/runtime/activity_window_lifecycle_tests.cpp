#include "../dexvm/container_traversal_fixture.h"
#include "ogplay/runtime/integration/bitmap_pixels.h"
#include "runtime/boundary/core/boundary_symbols.h"
#include "ogplay/runtime/dexvm/big_int_runtime.h"
#include <set>
#include "ogplay/runtime/integration/native_activity_runtime.h"
#include "ogplay/runtime/dexvm/nio_runtime.h"
#include "runtime/integration/dexvm_android/shared.h"
#include "../fixtures/bitmap_decode_data.h"
#include <chrono>
#include <atomic>
#include "ogplay/runtime/vfs/sandbox_store.h"
#include "../dexvm/boot_dex.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <future>
#include "ogplay/runtime/dexvm/vm_monitors.h"
#include <thread>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include <doctest/doctest.h>
#include "ogplay/runtime/dexvm/reflection.h"

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/encoding.h"
#include "ogplay/core/logger.h"
#include "ogplay/loader/elf.h"
#include "ogplay/runtime/dexvm/access_flags.h"
#include "ogplay/runtime/dexvm/class_loader_facade.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/integration/dexvm_bridge.h"
#include "ogplay/runtime/integration/native_library_loader.h"
#include "ogplay/runtime/integration/apk_assets.h"
#include "ogplay/runtime/jni/jni.h"
#include "ogplay/runtime/jni/jni_java_vm.h"
#include "ogplay/runtime/jni/jni_field_store.h"
#include "ogplay/runtime/jni/jni_native_registry.h"
#include "ogplay/runtime/jni/jni_object_array.h"
#include "ogplay/runtime/jni_guest/jni_guest_abi.h"
#include "ogplay/runtime/jni_guest/jni_guest_static_calls.h"
#include "ogplay/runtime/jni_guest/jni_guest_bindings.h"
#include "ogplay/cpu/interpreter.h"
#include "ogplay/memory/bus.h"
#include "ogplay/session/android_app_process.h"
#include "ogplay/session/dex_activity_lifecycle.h"
#include "ogplay/video/fake_video_player.h"

#include "native_library_fixture.h"

using namespace ogplay::test::native_library;


TEST_CASE("DVM-89 initial traversal waits until an existing worker parks") {
    using namespace ogplay;
    OrchestratedApp fixture("fixture.LateParkActivity", true, false);
    fixture.app->StartApplication();
    const auto started = fixture.app->StartLauncherActivity();
    CHECK(started.state == session::LifecycleRunState::running);
    CHECK(fixture.CallStaticInt("Lfixture/HandshakeView;",
                                "getTraversalCount") == 1);
    CHECK(fixture.CallStaticInt("Lfixture/HandshakeView;",
                                "getWorkerPhaseAtTraversal") == 1);
    const auto threads = fixture.app->DexVm().Threads().Snapshot();
    CHECK(std::ranges::any_of(threads, [](const auto& thread) {
        return thread.wait_state ==
               runtime::dexvm::VmThreadWaitState::sleeping;
    }));
    CHECK(fixture.logger.Snapshot(
              core::LogLevel::warn, "session.dex_lifecycle").empty());
    static_cast<void>(fixture.app->Stop());
}

TEST_CASE("DVM-89 initial traversal fails open when a worker never parks") {
    using namespace ogplay;
    OrchestratedApp fixture("fixture.NeverParkActivity", true, false);
    fixture.app->StartApplication();
    const auto started = fixture.app->StartLauncherActivity();
    CHECK(started.state == session::LifecycleRunState::running);
    CHECK(fixture.CallStaticInt("Lfixture/HandshakeView;",
                                "getTraversalCount") == 1);
    const auto warnings = fixture.logger.Snapshot(
        core::LogLevel::warn, "session.dex_lifecycle");
    REQUIRE(warnings.size() == 1U);
    CHECK(warnings.front().message ==
          "initial Java thread quiescence handshake reached yield limit");
    static_cast<void>(fixture.app->Stop());
}

TEST_CASE("DVM-89 initial traversal without workers keeps focus deferred") {
    using namespace ogplay;
    OrchestratedApp fixture("fixture.HandshakeActivity", true, false);
    fixture.app->StartApplication();
    const auto started = fixture.app->StartLauncherActivity();
    CHECK(started.state == session::LifecycleRunState::running);
    CHECK(fixture.CallStaticInt("Lfixture/HandshakeView;",
                                "getTraversalCount") == 1);
    CHECK(fixture.CallStaticInt("Lfixture/HandshakeView;",
                                "getFocusEvents") == 0);
    CHECK(fixture.logger.Snapshot(
              core::LogLevel::warn, "session.dex_lifecycle").empty());

    static_cast<void>(fixture.app->ActivityLifecycle().StepFrame());
    CHECK(fixture.CallStaticInt("Lfixture/HandshakeView;",
                                "getFocusEvents") == 1);
    static_cast<void>(fixture.app->Stop());
}

TEST_CASE("DVM-218 lifecycle preserves onPause failure and attempts later cleanup") {
    using namespace ogplay;
    using runtime::dexvm::InterpreterBackend;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        OrchestratedApp fixture("fixture.TeardownFailureActivity", true, false,
                                {}, {}, true, backend);
        fixture.app->StartApplication();
        REQUIRE(fixture.app->StartLauncherActivity().state == session::LifecycleRunState::running);
        try {
            static_cast<void>(fixture.app->Stop());
            FAIL("onPause failure was swallowed");
        } catch (const std::exception& error) {
            const std::string message = error.what();
            CHECK(message.find("pause-primary") != std::string::npos);
            CHECK(message.find("destroy-secondary") == std::string::npos);
        }
        // Healthy Stop still attempts the foreground onDestroy; its secondary failure stays hidden.
        CHECK(fixture.CallStaticInt("Lfixture/TeardownFailureActivity;", "getStage") == 3);
        CHECK_FALSE(fixture.app->NativeProcess().Running());
        CHECK_FALSE(fixture.app->NativeProcess().Environment().IsThreadAttached(1));
        CHECK_NOTHROW(static_cast<void>(fixture.app->Stop()));
    }
}

TEST_CASE("DVM-235 live Surface window is hidden and restored without losing registrations") {
    using namespace ogplay;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        OrchestratedApp f("fixture.ResultCaller", true, false, {}, {}, true, backend);
        f.app->StartApplication();
        REQUIRE(f.app->StartLauncherActivity().state == session::LifecycleRunState::running);
        auto& vm = f.app->DexVm().Vm();
        auto& linker = vm.Linker();
        auto& lifecycle = f.app->ActivityLifecycle();
        const auto caller = f.context->activity;
        const auto call = [&](VmObjectRef object, const char* name, const char* signature,
                              std::vector<VmValue> args = {}) {
            const auto type = vm.Model().ObjectClass(object);
            const auto slot = linker.FindVtableIndex(type, name, signature);
            REQUIRE(slot.has_value());
            args.insert(args.begin(), VmValue::Ref(object));
            const auto result = vm.Call(linker.Class(type).vtable[*slot], args);
            REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
            return result.value;
        };
        const auto surface = vm.NewIntrinsicInstance("Landroid/view/SurfaceView;");
        const auto init_surface = linker.FindDirectMethod(vm.Model().ObjectClass(surface),
            "<init>", "(Landroid/content/Context;)V");
        REQUIRE(init_surface.has_value());
        REQUIRE_FALSE(vm.Call(*init_surface, std::array{
            VmValue::Ref(surface), VmValue::Ref(caller)}).exception.IsValid());
        int source_focus_gains{};
        const auto focus_slot = linker.FindVtableIndex(vm.Model().ObjectClass(surface), "onWindowFocusChanged", "(Z)V");
        REQUIRE(focus_slot.has_value());
        auto& focus_method = linker.MutableMethod(linker.Class(vm.Model().ObjectClass(surface)).vtable[*focus_slot]);
        focus_method.kind = MethodKind::intrinsic;
        focus_method.implementation = [surface, &source_focus_gains](IntrinsicContext& call) {
            if (call.receiver == surface && call.arguments[0].AsInt() != 0) ++source_focus_gains;
            return VmValue::Void();
        };
        const auto holder = call(surface, "getHolder", "()Landroid/view/SurfaceHolder;").ref;
        REQUIRE(holder.IsValid());
        call(holder, "setFixedSize", "(II)V", {VmValue::Int(64), VmValue::Int(36)});
        const auto fixed_frame = call(holder, "getSurfaceFrame", "()Landroid/graphics/Rect;").ref;
        CHECK(call(fixed_frame, "width", "()I").AsInt() == 64);
        CHECK(call(fixed_frame, "height", "()I").AsInt() == 36);
        call(caller, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(surface)});
        static_cast<void>(lifecycle.StepFrame());
        REQUIRE_FALSE(f.context->active_surface_holders.empty());
        const auto before = lifecycle.State();
        CHECK(source_focus_gains == 1);
        const auto original_surface = call(holder, "getSurface", "()Landroid/view/Surface;").ref;
        const auto overlay = vm.NewIntrinsicInstance("Landroid/view/View;");
        const auto overlay_root = vm.ProtectReferences(std::array{overlay});
        const auto overlay_init = linker.FindDirectMethod(vm.Model().ObjectClass(overlay), "<init>", "(Landroid/content/Context;)V");
        REQUIRE(overlay_init.has_value());
        REQUIRE_FALSE(vm.Call(*overlay_init, std::array{VmValue::Ref(overlay), VmValue::Ref(caller)}).exception.IsValid());
        const auto overlay_params = vm.NewIntrinsicInstance("Landroid/view/ViewGroup$LayoutParams;");
        const auto params_root = vm.ProtectReferences(std::array{overlay_params});
        const auto params_init = linker.FindDirectMethod(vm.Model().ObjectClass(overlay_params), "<init>", "(II)V");
        REQUIRE(params_init.has_value());
        REQUIRE_FALSE(vm.Call(*params_init, std::array{VmValue::Ref(overlay_params), VmValue::Int(-1), VmValue::Int(-1)}).exception.IsValid());
        call(caller, "addContentView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V", {VmValue::Ref(overlay), VmValue::Ref(overlay_params)});
        static_cast<void>(lifecycle.StepFrame());
        CHECK(f.context->holder_surfaces.at(holder.Value()) == original_surface);
        CHECK(source_focus_gains == 1);
        const auto container = call(caller, "findViewById", "(I)Landroid/view/View;", {VmValue::Int(0x01020002)}).ref;
        CHECK(call(surface, "getParent", "()Landroid/view/ViewParent;").ref == container);
        CHECK(call(overlay, "getParent", "()Landroid/view/ViewParent;").ref == container);
        const auto intent = vm.NewIntrinsicInstance("Landroid/content/Intent;");
        const auto init_intent = linker.FindDirectMethod(vm.Model().ObjectClass(intent), "<init>", "()V");
        REQUIRE(init_intent.has_value());
        REQUIRE_FALSE(vm.Call(*init_intent, std::array{VmValue::Ref(intent)}).exception.IsValid());
        call(intent, "setClassName", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
             {VmValue::Ref(vm.NewStringUtf8("fixture")), VmValue::Ref(vm.NewStringUtf8("fixture.ResultChild"))});
        call(intent, "addFlags", "(I)Landroid/content/Intent;", {VmValue::Int(0x10000)});
        call(caller, "startActivity", "(Landroid/content/Intent;)V", {VmValue::Ref(intent)});
        REQUIRE(f.context->activity_commands.size() == 1);
        static_cast<void>(lifecycle.StepFrame());
        CHECK(lifecycle.State().state == session::LifecycleRunState::running);
        CHECK(lifecycle.State().frame == before.frame + 2);
        CHECK(f.context->activity != caller);
        const auto node = runtime::FindViewUiNode(*f.context, surface.Value());
        REQUIRE(node.has_value());
        CHECK(f.context->ui_tree.IsAttached(*node));
        CHECK_FALSE(f.context->ui_tree.IsVisible(*node));
        CHECK_FALSE(f.context->ui_tree.IsVisible(*runtime::FindViewUiNode(*f.context, overlay.Value())));
        CHECK(f.context->surface_holders.contains(surface.Value()));
        CHECK(f.context->activity_stack.size() == 2);
        CHECK(source_focus_gains == 1);
        CHECK_FALSE(runtime::AttachSurfaceViewSubtree(vm, *f.context, *node).has_value());
        CHECK_FALSE(f.context->active_surface_holders.contains(holder.Value()));
        CHECK(f.ledger.Unimplemented().empty());
        call(f.context->activity, "finish", "()V");
        static_cast<void>(lifecycle.StepFrame());
        CHECK(f.context->activity == caller);
        CHECK(f.context->content_view == surface);
        CHECK(f.context->ui_tree.IsVisible(*node));
        CHECK(f.context->ui_tree.IsVisible(*runtime::FindViewUiNode(*f.context, overlay.Value())));
        CHECK(call(overlay, "getParent", "()Landroid/view/ViewParent;").ref == container);
        CHECK_FALSE(f.context->active_surface_holders.empty());
        CHECK(source_focus_gains == 2);
        static_cast<void>(f.app->Stop());
        CHECK_FALSE(f.app->NativeProcess().Running());
        CHECK(f.context->activity_stack.empty());
        CHECK(f.context->activity_commands.empty());
        CHECK(f.app->State() == session::AndroidAppProcessState::stopped);
        static_cast<void>(f.app->Stop());
    }
}

TEST_CASE("DVM-246 ARM JNI Dialog preserves Surface owner focus modal pixels and window return") {
    using namespace ogplay;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        OrchestratedApp f("fixture.ResultCaller", true, false, {}, {}, true, backend,
                          "android.app.Application", {}, {800, 480});
        f.app->StartApplication();
        REQUIRE(f.app->StartLauncherActivity().state == session::LifecycleRunState::running);
        auto& bridge = f.app->DexVm(); auto& vm = bridge.Vm(); auto& linker = vm.Linker();
        auto& lifecycle = f.app->ActivityLifecycle();
        const auto owner = f.context->activity;
        const auto call = [&](VmObjectRef object, const char* name, const char* signature, std::vector<VmValue> args = {}) {
            const auto type = vm.Model().ObjectClass(object);
            const auto index = linker.FindVtableIndex(type, name, signature);
            REQUIRE(index.has_value()); args.insert(args.begin(), VmValue::Ref(object));
            const auto result = vm.Call(linker.Class(type).vtable[*index], args);
            REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message); return result.value;
        };
        const auto construct = [&](VmObjectRef object, const char* signature, std::vector<VmValue> args) {
            const auto init = linker.FindDirectMethod(vm.Model().ObjectClass(object), "<init>", signature);
            REQUIRE(init.has_value()); args.insert(args.begin(), VmValue::Ref(object));
            REQUIRE_FALSE(vm.Call(*init, args).exception.IsValid());
        };
        const auto surface = vm.NewIntrinsicInstance("Landroid/view/SurfaceView;");
        const auto surface_root = vm.ProtectReferences(std::array{surface});
        construct(surface, "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        call(owner, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(surface)});
        static_cast<void>(lifecycle.StepFrame());
        const auto holder = call(surface, "getHolder", "()Landroid/view/SurfaceHolder;").ref;
        const auto backing = call(holder, "getSurface", "()Landroid/view/Surface;").ref;
        const auto builder = vm.NewIntrinsicInstance("Landroid/app/AlertDialog$Builder;");
        const auto builder_root = vm.ProtectReferences(std::array{builder});
        construct(builder, "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        call(builder, "setTitle", "(Ljava/lang/CharSequence;)Landroid/app/AlertDialog$Builder;", {VmValue::Ref(vm.NewStringUtf8("Confirm"))});
        call(builder, "setMessage", "(Ljava/lang/CharSequence;)Landroid/app/AlertDialog$Builder;", {VmValue::Ref(vm.NewStringUtf8("Visible modal"))});
        const auto observer_type = linker.ResolveDescriptor("Lfixture/DialogClickObserver;");
        REQUIRE_FALSE(vm.EnsureClassInitialized(observer_type).exception.IsValid());
        const auto observer = vm.Model().NewInstance(observer_type, linker.Class(observer_type).instance_slots);
        const auto observer_root = vm.ProtectReferences(std::array{observer});
        construct(observer, "()V", {});
        call(builder, "setNeutralButton", "(Ljava/lang/CharSequence;Landroid/content/DialogInterface$OnClickListener;)Landroid/app/AlertDialog$Builder;",
             {VmValue::Ref(vm.NewStringUtf8("OK")), VmValue::Ref(observer)});
        const auto dialog = call(builder, "create", "()Landroid/app/AlertDialog;").ref;
        call(dialog, "setOnDismissListener", "(Landroid/content/DialogInterface$OnDismissListener;)V", {VmValue::Ref(observer)});
        auto& guest = bridge.Session(); auto& process = guest.Process(); auto& env = guest.Environment();
        const auto memory = process.GuestMemoryAccess(); const auto scratch = memory.allocate(256);
        env.PushLocalFrame(1, 32);
        const auto ref = bridge.PublishLocal(dialog);
        const auto klass = bridge.PublishLocal(vm.Model().ClassObject(vm.Model().ObjectClass(dialog)));
        const auto invoke = [&](const char* name, std::array<std::uint32_t, 4> args) {
            return process.Invoke({memory::GuestAddress{EnvironmentThunk(name)}, args, {}}).return_value;
        };
        const auto method = [&](const char* name, const char* signature) {
            std::array<std::byte, 256> data{};
            std::memcpy(data.data(), name, std::strlen(name) + 1);
            std::memcpy(data.data() + 64, signature, std::strlen(signature) + 1); memory.write(scratch, data);
            const auto id = invoke("GetMethodID", {process.GuestEnvironment().Value(), klass.Value(), scratch.Value(), scratch.Add(64).Value()});
            REQUIRE(id != 0); REQUIRE_FALSE(env.ExceptionCheck(1)); return id;
        };
        const auto show = method("show", "()V");
        const auto showing = method("isShowing", "()Z");
        const auto motion_class = bridge.PublishLocal(vm.Model().ClassObject(linker.ResolveDescriptor("Landroid/view/MotionEvent;")));
        // API19 public action fields: native callers need metadata even when Java
        // bytecode has inlined these constants. Include deprecated pointer aliases.
        for (const auto& [name, expected] : std::array{
            std::pair{"ACTION_MASK", 0xffU},
            std::pair{"ACTION_DOWN", 0U}, std::pair{"ACTION_UP", 1U},
            std::pair{"ACTION_MOVE", 2U}, std::pair{"ACTION_CANCEL", 3U},
            std::pair{"ACTION_OUTSIDE", 4U},
            std::pair{"ACTION_POINTER_DOWN", 5U}, std::pair{"ACTION_POINTER_UP", 6U},
            std::pair{"ACTION_HOVER_MOVE", 7U}, std::pair{"ACTION_SCROLL", 8U},
            std::pair{"ACTION_HOVER_ENTER", 9U}, std::pair{"ACTION_HOVER_EXIT", 10U},
            std::pair{"ACTION_POINTER_INDEX_MASK", 0xff00U},
            std::pair{"ACTION_POINTER_INDEX_SHIFT", 8U},
            std::pair{"ACTION_POINTER_1_DOWN", 5U}, std::pair{"ACTION_POINTER_2_DOWN", 0x105U},
            std::pair{"ACTION_POINTER_3_DOWN", 0x205U},
            std::pair{"ACTION_POINTER_1_UP", 6U}, std::pair{"ACTION_POINTER_2_UP", 0x106U},
            std::pair{"ACTION_POINTER_3_UP", 0x206U},
            std::pair{"ACTION_POINTER_ID_MASK", 0xff00U}, std::pair{"ACTION_POINTER_ID_SHIFT", 8U},
            std::pair{"AXIS_X", 0U}, std::pair{"AXIS_Y", 1U}, std::pair{"AXIS_GENERIC_16", 47U}}) {
            std::array<std::byte, 256> data{};
            std::memcpy(data.data(), name, std::strlen(name) + 1); data[64] = std::byte{'I'}; memory.write(scratch, data);
            const auto field = invoke("GetStaticFieldID", {process.GuestEnvironment().Value(), motion_class.Value(), scratch.Value(), scratch.Add(64).Value()});
            REQUIRE(field != 0); REQUIRE_FALSE(env.ExceptionCheck(1));
            CHECK(invoke("GetStaticIntField", {process.GuestEnvironment().Value(), motion_class.Value(), field, 0}) == expected);
            REQUIRE_FALSE(env.ExceptionCheck(1));
        }
        static_cast<void>(invoke("CallVoidMethodA", {process.GuestEnvironment().Value(), ref.Value(), show, 0}));
        REQUIRE_FALSE(env.ExceptionCheck(1));
        CHECK(invoke("CallBooleanMethodA", {process.GuestEnvironment().Value(), ref.Value(), showing, 0}) == 1);
        static_cast<void>(lifecycle.StepFrame());
        CHECK_FALSE(f.context->window_has_focus.load());
        CHECK(call(holder, "getSurface", "()Landroid/view/Surface;").ref == backing);
        runtime::AndroidBoundaryFrame frame{.width=800, .height=480, .rgba8=std::vector<std::uint8_t>(800 * 480 * 4)};
        for (std::size_t i = 0; i < frame.rgba8.size(); i += 4) { frame.rgba8[i + 2] = 255; frame.rgba8[i + 3] = 255; }
        const auto composed = lifecycle.ComposePresentedFrame(frame);
        CHECK(composed.rgba8[2] == 119); // Exactly one modal scrim, above the base producer.
        REQUIRE(f.context->dialog_presentations.size() == 1);
        const auto modal_root = f.context->dialog_presentations.front().root;
        const auto modal_tree = [&]() -> const runtime::ui::UiTree& {
            REQUIRE_FALSE(f.context->dialog_presentations.empty());
            REQUIRE(f.context->dialog_presentations.front().host_tree);
            return *f.context->dialog_presentations.front().host_tree;
        };
        const auto intent = vm.NewIntrinsicInstance("Landroid/content/Intent;");
        const auto intent_root = vm.ProtectReferences(std::array{intent}); construct(intent, "()V", {});
        call(intent, "setClassName", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
             {VmValue::Ref(vm.NewStringUtf8("fixture")), VmValue::Ref(vm.NewStringUtf8("fixture.ResultChild"))});
        call(owner, "startActivity", "(Landroid/content/Intent;)V", {VmValue::Ref(intent)});
        static_cast<void>(lifecycle.StepFrame());
        CHECK_FALSE(modal_tree().IsVisible(modal_root));
        CHECK_FALSE(runtime::HasAndroidModalDialog(*f.context));
        call(f.context->activity, "finish", "()V"); static_cast<void>(lifecycle.StepFrame());
        CHECK(f.context->activity == owner);
        CHECK(modal_tree().IsVisible(modal_root));
        CHECK_FALSE(f.context->window_has_focus.load());
        const auto counter = [&](const char* method_name) {
            return f.CallStaticInt("Lfixture/DialogClickObserver;", method_name);
        };
        const auto pointer = [&](int action) {
            const auto button = *f.context->dialog_presentations.front().buttons[2];
            const auto* state = modal_tree().Get(button);
            REQUIRE(state);
            const auto rect = state->screen_frame;
            lifecycle.QueueInput({.type=runtime::AndroidBoundaryInputType::pointer_button,
                .x=static_cast<float>(rect.left + 2), .y=static_cast<float>(rect.top + 2),
                .pressed=action==0});
        };
        const auto key = [&](int action) {
            lifecycle.QueueInput({.type=runtime::AndroidBoundaryInputType::key,
                .code=66, .pressed=action==0, .action=action});
        };
        // A real mouse/MCP click spans frames. Unchanged modal focus must not cancel it.
        pointer(0); static_cast<void>(lifecycle.StepFrame());
        CHECK(f.context->dialog_pressed_button.has_value());
        static_cast<void>(lifecycle.StepFrame());
        CHECK(f.context->dialog_pressed_button.has_value()); CHECK(counter("getClicks") == 0);
        pointer(1); static_cast<void>(lifecycle.StepFrame());
        CHECK(counter("getClicks") == 1); CHECK(counter("getWhich") == -3);
        CHECK(counter("getDismisses") == 1); REQUIRE(f.context->dialog_presentations.empty());
        CHECK(f.context->window_has_focus.load());
        CHECK(call(holder, "getSurface", "()Landroid/view/Surface;").ref == backing);

        static_cast<void>(invoke("CallVoidMethodA", {process.GuestEnvironment().Value(), ref.Value(), show, 0}));
        static_cast<void>(lifecycle.StepFrame());
        key(0); static_cast<void>(lifecycle.StepFrame());
        static_cast<void>(lifecycle.StepFrame()); CHECK(f.context->dialog_keys_down.contains(66));
        key(1); static_cast<void>(lifecycle.StepFrame());
        CHECK(counter("getClicks") == 2); CHECK(counter("getDismisses") == 2);
        REQUIRE(f.context->dialog_presentations.empty());

        // Actual pause and host input reset still cancel, even while Activity focus is false.
        static_cast<void>(invoke("CallVoidMethodA", {process.GuestEnvironment().Value(), ref.Value(), show, 0}));
        static_cast<void>(lifecycle.StepFrame());
        pointer(0); static_cast<void>(lifecycle.StepFrame()); REQUIRE(f.context->dialog_pressed_button);
        static_cast<void>(lifecycle.Suspend()); CHECK_FALSE(f.context->dialog_pressed_button);
        CHECK_FALSE(f.context->dialog_pointer_active);
        static_cast<void>(lifecycle.Resume());
        pointer(1); static_cast<void>(lifecycle.StepFrame());
        CHECK(counter("getClicks") == 2); CHECK(counter("getDismisses") == 2);
        REQUIRE(f.context->dialog_presentations.size() == 1U);
        key(0); static_cast<void>(lifecycle.StepFrame()); REQUIRE(f.context->dialog_keys_down.contains(66));
        static_cast<void>(lifecycle.Suspend()); CHECK(f.context->dialog_keys_down.empty());
        static_cast<void>(lifecycle.Resume());
        key(1); static_cast<void>(lifecycle.StepFrame()); CHECK(counter("getClicks") == 2);
        pointer(0); static_cast<void>(lifecycle.StepFrame()); REQUIRE(f.context->dialog_pressed_button);
        lifecycle.CancelInput(); CHECK_FALSE(f.context->dialog_pressed_button);
        pointer(1); static_cast<void>(lifecycle.StepFrame()); CHECK(counter("getClicks") == 2);

        const auto dismiss = method("dismiss", "()V");
        static_cast<void>(invoke("CallVoidMethodA", {process.GuestEnvironment().Value(), ref.Value(), dismiss, 0}));
        REQUIRE_FALSE(env.ExceptionCheck(1));
        static_cast<void>(lifecycle.StepFrame());
        CHECK(f.context->window_has_focus.load()); CHECK(f.context->dialog_presentations.empty());
        CHECK(call(holder, "getSurface", "()Landroid/view/Surface;").ref.IsValid());
        static_cast<void>(vm.CollectGarbage("dialog-jni-owner"));
        static_cast<void>(env.PopLocalFrame(1)); memory.release(scratch, 256);
        static_cast<void>(f.app->Stop()); CHECK(f.context->dialog_presentations.empty());
    }
}

TEST_CASE("DVM-249 lifecycle prepares MediaPlayer video without any AudioTrack") {
    using namespace ogplay;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        OrchestratedApp f("fixture.DelayedContentActivity", true, false, {}, {}, true, backend);
        f.filesystem.MountHostDirectory("/movies", std::filesystem::path{OGPLAY_SOURCE_DIR} / "tests/fixtures/video");
        f.context->video_source_player_factory = [](auto) {
            video::VideoMetadata metadata;
            metadata.width = 8; metadata.height = 4; metadata.duration_ms = 1000;
            return std::make_unique<video::FakeVideoPlayer>(metadata, 10);
        };
        f.app->StartApplication();
        REQUIRE(f.app->StartLauncherActivity().state == session::LifecycleRunState::running);
        auto& vm = f.app->DexVm().Vm(); auto& linker = vm.Linker();
        const auto player = vm.NewIntrinsicInstance("Landroid/media/MediaPlayer;");
        const auto root = vm.ProtectReferences(std::array{player});
        const auto owner = vm.Model().ObjectClass(player);
        const auto ctor = linker.FindDirectMethod(owner, "<init>", "()V");
        REQUIRE(ctor);
        REQUIRE_FALSE(vm.Call(*ctor, std::array{VmValue::Ref(player)}).exception.IsValid());
        const auto call = [&](const char* name, const char* signature, std::vector<VmValue> args = {}) {
            const auto slot = linker.FindVtableIndex(owner, name, signature);
            REQUIRE(slot); args.insert(args.begin(), VmValue::Ref(player));
            const auto result = vm.Call(linker.Class(owner).vtable[*slot], args);
            REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
        };
        call("setDataSource", "(Ljava/lang/String;)V", {VmValue::Ref(vm.NewStringUtf8("/movies/short-mp4v-aac.mp4"))});
        call("prepareAsync", "()V");
        REQUIRE(f.context->audio_tracks.empty());
        using Phase = runtime::DexVmAndroidContext::MediaPlayerState::Phase;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (f.context->media_players.at(player.Value()).phase == Phase::preparing && std::chrono::steady_clock::now() < deadline) {
            static_cast<void>(f.app->ActivityLifecycle().StepFrame()); std::this_thread::yield();
        }
        REQUIRE(f.context->media_players.at(player.Value()).phase == Phase::prepared);
        static_cast<void>(f.app->ActivityLifecycle().StepFrame());
        CHECK(f.context->video_views.at(player.Value()).pending_event == 0);
        CHECK(f.context->audio_tracks.empty());
        static_cast<void>(f.app->Stop());
        CHECK(f.context->video_views.empty()); CHECK(f.context->media_players.empty());
    }
}

TEST_CASE("DVM-245 lifecycle traverses direct content children without a primary View projection") {
    using namespace ogplay;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        OrchestratedApp f("fixture.DelayedContentActivity", true, false, {}, {}, true, backend);
        f.app->StartApplication();
        REQUIRE(f.app->StartLauncherActivity().state == session::LifecycleRunState::running);
        auto& vm = f.app->DexVm().Vm(); auto& linker = vm.Linker();
        const auto call = [&](VmObjectRef object, const char* name, const char* signature, std::vector<VmValue> args = {}) {
            const auto type = vm.Model().ObjectClass(object);
            const auto index = linker.FindVtableIndex(type, name, signature);
            REQUIRE(index.has_value()); args.insert(args.begin(), VmValue::Ref(object));
            const auto result = vm.Call(linker.Class(type).vtable[*index], args);
            REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message); return result.value;
        };
        REQUIRE_FALSE(f.context->content_view.IsValid());
        const auto container = call(f.context->activity, "findViewById", "(I)Landroid/view/View;", {VmValue::Int(0x01020002)}).ref;
        const auto surface = vm.NewIntrinsicInstance("Landroid/view/SurfaceView;");
        const auto root = vm.ProtectReferences(std::array{surface});
        const auto constructor = linker.FindDirectMethod(vm.Model().ObjectClass(surface), "<init>", "(Landroid/content/Context;)V");
        REQUIRE(constructor.has_value());
        REQUIRE_FALSE(vm.Call(*constructor, std::array{VmValue::Ref(surface), VmValue::Ref(f.context->activity)}).exception.IsValid());
        const auto holder = call(surface, "getHolder", "()Landroid/view/SurfaceHolder;").ref;
        call(container, "addView", "(Landroid/view/View;II)V", {VmValue::Ref(surface), VmValue::Int(64), VmValue::Int(36)});
        REQUIRE_FALSE(f.context->content_view.IsValid());
        const auto backing = call(holder, "getSurface", "()Landroid/view/Surface;").ref;
        static_cast<void>(f.app->ActivityLifecycle().StepFrame());
        CHECK(f.context->active_surface_holders.contains(holder.Value()));
        CHECK(call(holder, "getSurface", "()Landroid/view/Surface;").ref == backing);
        CHECK(call(surface, "getParent", "()Landroid/view/ViewParent;").ref == container);
        call(container, "removeView", "(Landroid/view/View;)V", {VmValue::Ref(surface)});
        CHECK_FALSE(f.context->active_surface_holders.contains(holder.Value()));
        CHECK_FALSE(f.context->content_view.IsValid());
        static_cast<void>(f.app->Stop());
        CHECK(f.app->State() == session::AndroidAppProcessState::stopped);
    }
}

TEST_CASE("DVM-219 empty Activity pumps delayed content handoff and finish") {
    using namespace ogplay;
    using runtime::dexvm::InterpreterBackend;
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        SUBCASE("late SurfaceView is sized before one callback generation") {
            OrchestratedApp fixture("fixture.DelayedContentActivity", true, false,
                                    {}, {}, true, backend);
            fixture.app->StartApplication();
            CHECK(fixture.app->StartLauncherActivity().state == session::LifecycleRunState::running);
            CHECK_FALSE(fixture.context->content_view.IsValid());
            CHECK(fixture.CallStaticInt("Lfixture/DelayedContentActivity;", "getResumed") == 1);
            auto& lifecycle = fixture.app->ActivityLifecycle();
            static_cast<void>(lifecycle.StepFrame());
            static_cast<void>(lifecycle.StepFrame());
            CHECK_FALSE(fixture.context->content_view.IsValid());
            static_cast<void>(lifecycle.StepFrame());
            CHECK(fixture.context->content_view.IsValid());
            CHECK(fixture.CallStaticInt("Lfixture/LateSurfaceView;", "getWidth") == 64);
            CHECK(fixture.CallStaticInt("Lfixture/LateSurfaceView;", "getEvents") == 123);
            static_cast<void>(lifecycle.StepFrame());
            CHECK(fixture.CallStaticInt("Lfixture/LateSurfaceView;", "getEvents") == 123);
            // Removing/reinstalling the same View retains its geometry and
            // Java identity; an unchanged size does not repeat onSizeChanged.
            auto& vm = fixture.app->DexVm().Vm();
            const auto original = fixture.context->content_view;
            const auto roots = vm.ProtectReferences(std::array{original});
            const auto temporary = vm.NewIntrinsicInstance("Landroid/view/View;");
            const auto temporary_roots = vm.ProtectReferences(std::array{temporary});
            const auto constructor = vm.Linker().FindDirectMethod(vm.Model().ObjectClass(temporary),
                "<init>", "(Landroid/content/Context;)V");
            REQUIRE(constructor.has_value());
            REQUIRE_FALSE(vm.Call(*constructor, std::array{
                runtime::dexvm::VmValue::Ref(temporary),
                runtime::dexvm::VmValue::Ref(fixture.context->activity)}).exception.IsValid());
            const auto type = vm.Model().ObjectClass(fixture.context->activity);
            const auto index = vm.Linker().FindVtableIndex(type, "setContentView", "(Landroid/view/View;)V");
            REQUIRE(index.has_value());
            for (const auto view : {temporary, original}) {
                const auto result = vm.Call(vm.Linker().Class(type).vtable[*index],
                    std::array{runtime::dexvm::VmValue::Ref(fixture.context->activity),
                               runtime::dexvm::VmValue::Ref(view)});
                REQUIRE_FALSE(result.exception.IsValid());
            }
            CHECK(fixture.CallStaticInt("Lfixture/LateSurfaceView;", "getEvents") == 1234);
            static_cast<void>(lifecycle.StepFrame());
            CHECK(fixture.CallStaticInt("Lfixture/LateSurfaceView;", "getEvents") == 123423);
            static_cast<void>(fixture.app->Stop());
            CHECK(fixture.CallStaticInt("Lfixture/LateSurfaceView;", "getEvents") == 1234234);
        }
        SUBCASE("delayed handoff may enter another empty Activity") {
            OrchestratedApp fixture("fixture.DelayedSwitchActivity", true, false,
                                    {}, {}, true, backend);
            fixture.app->StartApplication();
            static_cast<void>(fixture.app->StartLauncherActivity());
            const auto old = fixture.context->activity;
            auto& lifecycle = fixture.app->ActivityLifecycle();
            for (int i = 0; i < 3; ++i) static_cast<void>(lifecycle.StepFrame());
            CHECK(fixture.context->activity != old);
            CHECK_FALSE(fixture.context->content_view.IsValid());
            CHECK(fixture.CallStaticInt("Lfixture/DelayedContentActivity;", "getInstances") == 2);
            CHECK_FALSE(runtime::SessionExitRequested(*fixture.context));
            for (int i = 0; i < 3; ++i) static_cast<void>(lifecycle.StepFrame());
            CHECK(runtime::SessionExitRequested(*fixture.context));
            static_cast<void>(fixture.app->Stop());
        }
        SUBCASE("finish in onCreate needs no content or onResume") {
            OrchestratedApp fixture("fixture.FinishingEmptyActivity", true, false,
                                    {}, {}, true, backend);
            fixture.app->StartApplication();
            static_cast<void>(fixture.app->StartLauncherActivity());
            CHECK_FALSE(fixture.context->content_view.IsValid());
            CHECK(runtime::SessionExitRequested(*fixture.context));
            CHECK(fixture.CallStaticInt("Lfixture/DelayedContentActivity;", "getResumed") == 0);
            static_cast<void>(fixture.app->Stop());
            // DVM-236: process Stop does not synthesize Activity.onDestroy.
            CHECK(fixture.CallStaticInt("Lfixture/DelayedContentActivity;", "getResumed") == 0);
        }
    }
}

TEST_CASE("DVM-125 lifecycle owns coherent Activity and View window focus") {
    using namespace ogplay;
    using runtime::dexvm::InterpreterBackend;
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        CAPTURE(backend == InterpreterBackend::threaded ? "threaded" :
                                                         "switch");
        OrchestratedApp fixture("fixture.WindowFocusActivity", true, false,
                                {}, {}, true, backend);
        fixture.app->StartApplication();
        const auto started = fixture.app->StartLauncherActivity();
        CHECK(started.state == session::LifecycleRunState::running);
        const auto old_activity = fixture.context->activity;
        const auto call_activity = [&](const runtime::dexvm::VmObjectRef receiver,
                                       const char* name,
                                       const char* descriptor) {
            auto& bridge = fixture.app->DexVm();
            const auto java_class = bridge.Vm().Model().ObjectClass(receiver);
            const auto index = bridge.Linker().FindVtableIndex(
                java_class, name, descriptor);
            REQUIRE(index.has_value());
            const auto outcome = bridge.Vm().Call(
                bridge.Linker().Class(java_class).vtable[*index],
                std::vector{runtime::dexvm::VmValue::Ref(receiver)});
            REQUIRE_FALSE(outcome.exception.IsValid());
            return outcome.value;
        };
        const auto call_static_void = [&](const char* owner,
                                          const char* name) {
            auto& bridge = fixture.app->DexVm();
            const auto java_class = bridge.Linker().FindClass(owner);
            REQUIRE(java_class.has_value());
            const auto method = bridge.Linker().FindDirectMethod(
                *java_class, name, "()V");
            REQUIRE(method.has_value());
            const auto outcome = bridge.Vm().Call(*method, {});
            REQUIRE_FALSE(outcome.exception.IsValid());
        };

        // onResume runs before the first post-traversal focus message.
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusActivity;",
                                    "getResumeFocus") == 0);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusActivity;",
                                    "hasCurrentFocus") == 0);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusActivity;",
                                    "attachedHasFocus") == 0);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusActivity;",
                                    "detachedHasFocus") == 0);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusActivity;",
                                    "decorHasFocus") == 0);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusActivity;",
                                    "getEvents") == 0);

        static_cast<void>(fixture.app->ActivityLifecycle().StepFrame());
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusActivity;",
                                    "hasCurrentFocus") == 1);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusActivity;",
                                    "attachedHasFocus") == 1);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusActivity;",
                                    "detachedHasFocus") == 0);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusActivity;",
                                    "decorHasFocus") == 1);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusActivity;",
                                    "getEvents") == 1);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusActivity;",
                                    "getQueried") == 1);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusView;",
                                    "getEvents") == 1);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusView;",
                                    "getQueried") == 1);
        call_static_void("Lfixture/WindowFocusActivity;", "detachAttached");
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusActivity;",
                                    "attachedHasFocus") == 0);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusView;",
                                    "getEvents") == 1);
        call_static_void("Lfixture/WindowFocusActivity;", "attachAttached");
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusActivity;",
                                    "attachedHasFocus") == 1);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusView;",
                                    "getEvents") == 1);

        static_cast<void>(fixture.app->ActivityLifecycle().Suspend());
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusActivity;",
                                    "getEvents") == 2);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusActivity;",
                                    "getLast") == 0);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusActivity;",
                                    "getQueried") == 0);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusView;",
                                    "getEvents") == 2);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusView;",
                                    "getQueried") == 0);

        static_cast<void>(fixture.app->ActivityLifecycle().Resume());
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusActivity;",
                                    "getResumeFocus") == 0);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusActivity;",
                                    "getEvents") == 3);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusView;",
                                    "getEvents") == 3);

        static_cast<void>(call_activity(old_activity, "switchActivity", "()V"));
        static_cast<void>(fixture.app->ActivityLifecycle().StepFrame());
        CHECK(fixture.context->activity != old_activity);
        CHECK(call_activity(old_activity, "hasWindowFocus", "()Z").AsInt() == 0);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusActivity;",
                                    "getEvents") == 4);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusSecondActivity;",
                                    "getResumeFocus") == 0);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusSecondActivity;",
                                    "getEvents") == 1);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusSecondActivity;",
                                    "getQueried") == 1);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusView;",
                                    "getEvents") == 5);

        const auto stopped = fixture.app->Stop();
        CHECK(stopped.state == session::LifecycleRunState::stopped);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusSecondActivity;",
                                    "getEvents") == 2);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusSecondActivity;",
                                    "getQueried") == 0);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusView;",
                                    "getEvents") == 6);
    }
}

TEST_CASE("DVM-197 cold renderer waits for its registered holder and first Surface traversal") {
    using namespace ogplay;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        OrchestratedApp f("fixture.ColdSurfaceActivity", true, false, {}, {}, true, backend);
        f.app->StartApplication();
        REQUIRE(f.app->StartLauncherActivity().state == session::LifecycleRunState::running);
        auto& c = *f.context;
        auto& lifecycle = f.app->ActivityLifecycle();
        const auto view = c.gl_surface_renderer_view;
        REQUIRE(view.IsValid());
        REQUIRE(c.surface_holders.contains(view.Value()));
        const auto holder = c.surface_holders.at(view.Value());
        CHECK(c.active_surface_holders.contains(holder.Value()));
        CHECK(c.surface_traversal_pending.empty());
        CHECK(c.gl_surface_runtimes.at(view.Value())->surface_available);
        CHECK(f.CallStaticInt("Lfixture/ColdSurfaceActivity;", "getCreated") == 1);
        CHECK(f.CallStaticInt("Lfixture/ColdSurfaceActivity;", "getChanged") == 1);
        CHECK(f.CallStaticInt("Lfixture/ColdSurfaceActivity;", "getWidth") == 64);
        CHECK(f.CallStaticInt("Lfixture/ColdSurfaceActivity;", "getHeight") == 36);
        REQUIRE(lifecycle.StepFrame().state == session::LifecycleRunState::running);
        CHECK(f.CallStaticInt("Lfixture/ColdSurfaceActivity;", "getDraws") == 1);
        static_cast<void>(lifecycle.Suspend());
        CHECK_FALSE(c.gl_surface_runtimes.at(view.Value())->renderer_surface.IsValid());
        CHECK(c.surface_holders.at(view.Value()) == holder);
        static_cast<void>(lifecycle.Resume());
        REQUIRE(lifecycle.StepFrame().state == session::LifecycleRunState::running);
        CHECK(f.CallStaticInt("Lfixture/ColdSurfaceActivity;", "getCreated") == 2);
        CHECK(f.CallStaticInt("Lfixture/ColdSurfaceActivity;", "getChanged") == 2);
        CHECK(f.CallStaticInt("Lfixture/ColdSurfaceActivity;", "getDraws") == 2);
        CHECK(c.surface_holders.at(view.Value()) == holder);
        static_cast<void>(f.app->Stop());
        CHECK(f.app->State() == session::AndroidAppProcessState::stopped);
    }
}

TEST_CASE("DVM-197 renderer EGL policies establish current context before events and callbacks") {
    using namespace ogplay;
    namespace dx = runtime::dexvm;
    using runtime::android_intrinsics::CallAndroidMethod;
    for (const auto backend : {dx::InterpreterBackend::switch_dispatch, dx::InterpreterBackend::threaded}) {
      for (const int mode : {0, 1, 2, 3, 4}) {
        INFO("renderer mode=" << mode << " backend=" << static_cast<int>(backend));
        const bool custom = mode != 0;
        const bool reject_context = mode == 2;
        const bool reject_draw = mode == 3;
        const bool exit_draw = mode == 4;
        const auto owner_host = std::this_thread::get_id();
        dx::Interpreter* pump_vm = nullptr;
        std::promise<void> host_pumped;
        auto host_ready = host_pumped.get_future();
        std::atomic_bool waiting_for_host{false};
        bool notified_host = false;
        OrchestratedApp fixture("fixture.LauncherActivity", true, false, {}, {}, true, backend,
            "android.app.Application", [&] {
                CHECK(std::this_thread::get_id() == owner_host);
                if (pump_vm) CHECK_FALSE(pump_vm->ExecutionLock().HeldByCurrentThread());
                if (waiting_for_host.load() && !notified_host) {
                    notified_host = true;
                    host_pumped.set_value();
                }
            });
        fixture.app->StartApplication();
        REQUIRE(fixture.app->StartLauncherActivity().state == session::LifecycleRunState::running);
        auto& vm = fixture.app->DexVm().Vm();
        pump_vm = &vm;
        auto& linker = vm.Linker();
        auto& c = *fixture.context;
        const auto ref = dx::VmValue::Ref;
        const auto integer = dx::VmValue::Int;
        const auto call = [&](dx::VmObjectRef object, const char* name, const char* sig,
                              std::vector<dx::VmValue> args = {}) {
            return CallAndroidMethod(vm, object, name, sig, std::move(args));
        };
        const auto ints = [&](std::vector<std::int32_t> values) {
            const auto a = vm.Model().NewPrimitiveArray(linker.ResolveDescriptor("[I"), runtime::JniPrimitiveKind::integer,
                static_cast<runtime::JniSize>(values.size()));
            for (std::size_t i=0; i<values.size(); ++i) vm.Model().SetPrimitiveElement(a, static_cast<runtime::JniSize>(i), static_cast<std::uint32_t>(values[i]));
            return a;
        };
        const auto policy_type = linker.ResolveDescriptor("Lfixture/RendererPolicy;");
        const auto policy = vm.Model().NewInstance(policy_type, linker.Class(policy_type).instance_slots);
        const auto view = vm.NewIntrinsicInstance("Landroid/opengl/GLSurfaceView;");
        const auto ui_runnable = vm.Model().NewInstance(policy_type, linker.Class(policy_type).instance_slots);
        const auto roots = vm.ProtectReferences(std::array{policy, view, ui_runnable});
        const auto ctor = linker.FindDirectMethod(vm.Model().ObjectClass(view), "<init>", "(Landroid/content/Context;)V");
        REQUIRE(ctor.has_value());
        REQUIRE_FALSE(vm.Call(*ctor, std::array{ref(view), ref(c.activity)}).exception.IsValid());
        const auto hook = [&](const char* name, const char* sig, dx::IntrinsicHandler handler) {
            const auto slot = linker.FindVtableIndex(policy_type, name, sig);
            REQUIRE(slot.has_value());
            auto& method = linker.MutableMethod(linker.Class(policy_type).vtable[*slot]);
            method.kind = dx::MethodKind::intrinsic;
            method.implementation = std::move(handler);
        };
        const auto ui_host = std::this_thread::get_id();
        const auto thread_type = linker.ResolveDescriptor("Ljava/lang/Thread;");
        const auto current_thread_method = linker.FindDirectMethod(thread_type, "currentThread", "()Ljava/lang/Thread;");
        REQUIRE(current_thread_method.has_value());
        const auto ui_thread = vm.Call(*current_thread_method, {}).value.ref;
        REQUIRE(ui_thread.IsValid());
        std::thread::id gl_host;
        dx::VmObjectRef gl_thread;
        std::uint64_t gl_token{};
        const auto thread_identity = [&] {
            CHECK(std::this_thread::get_id() != ui_host);
            CHECK(vm.CurrentContextToken() != 1);
            const auto current_thread = vm.Threads().CurrentThreadObject();
            CHECK(current_thread.IsValid());
            CHECK(current_thread != ui_thread);
            if (!gl_thread.IsValid()) {
                gl_thread = current_thread;
                gl_host = std::this_thread::get_id();
                gl_token = vm.CurrentContextToken();
            }
            CHECK(current_thread == gl_thread);
            CHECK(std::this_thread::get_id() == gl_host);
            CHECK(vm.CurrentContextToken() == gl_token);
        };
        std::promise<void> ui_done;
        auto ui_completed = ui_done.get_future();
        bool ui_round_trip = false;
        std::promise<void>* event_done = nullptr;
        std::vector<std::string> order;
        const auto current = [&] {
            thread_identity();
            REQUIRE(c.renderer_context.IsValid());
            CHECK(call(c.renderer_egl, "eglGetCurrentContext", "()Ljavax/microedition/khronos/egl/EGLContext;").ref == c.renderer_context);
            const auto query = ints({0});
            const auto root = vm.ProtectReferences(std::array{query});
            CHECK(call(c.renderer_egl, "eglQueryContext", "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLContext;I[I)Z",
                       {ref(c.renderer_display),ref(c.renderer_context),integer(0x3098),ref(query)}).AsInt() == 1);
            CHECK(vm.Model().GetPrimitiveElement(query,0) == 2);
            const auto native_tid = c.native_thread_for_context(vm.CurrentContextToken());
            CHECK(native_tid != 1);
            // Java EGL and JNI/native calls must use one process TID.
            CHECK(c.session->InvokeManagedEgl("eglGetCurrentContext", {}, native_tid) ==
                  c.egl.contexts.at(c.renderer_context.Value()));
        };
        hook("run", "()V", [&](dx::IntrinsicContext& args) {
            if (args.receiver == ui_runnable) {
                CHECK(vm.CurrentContextToken() == 1);
                CHECK(std::this_thread::get_id() == ui_host);
                ui_done.set_value();
            } else {
                current(); order.push_back("event");
                if (event_done) event_done->set_value();
            }
            return dx::VmValue::Void();
        });
        hook("onSurfaceCreated", "(Ljavax/microedition/khronos/opengles/GL10;Ljavax/microedition/khronos/egl/EGLConfig;)V",
             [&](dx::IntrinsicContext& args) { current(); CHECK(args.arguments[0].ref == c.renderer_gl); CHECK(args.arguments[1].ref == c.renderer_config); order.push_back("created"); return dx::VmValue::Void(); });
        hook("onSurfaceChanged", "(Ljavax/microedition/khronos/opengles/GL10;II)V",
             [&](dx::IntrinsicContext& args) { current(); CHECK(args.arguments[1].AsInt() == 64); CHECK(args.arguments[2].AsInt() == 36); order.push_back("changed"); return dx::VmValue::Void(); });
        hook("onDrawFrame", "(Ljavax/microedition/khronos/opengles/GL10;)V",
             [&](dx::IntrinsicContext& args) { current(); CHECK(args.arguments[0].ref == c.renderer_gl); order.push_back("draw");
                 if (reject_draw) throw std::runtime_error("renderer draw failure");
                 if (exit_draw) vm.Exit(19);
                 if (!ui_round_trip) {
                     waiting_for_host.store(true);
                     const auto host_depth = vm.ExecutionLock().ReleaseForBlocking();
                     const auto host_status = host_ready.wait_for(std::chrono::seconds(2));
                     vm.ExecutionLock().ReacquireAfterBlocking(host_depth);
                     CHECK(host_status == std::future_status::ready);
                     call(c.activity, "runOnUiThread", "(Ljava/lang/Runnable;)V", {ref(ui_runnable)});
                     const auto depth = vm.ExecutionLock().ReleaseForBlocking();
                     const auto status = ui_completed.wait_for(std::chrono::seconds(2));
                     vm.ExecutionLock().ReacquireAfterBlocking(depth);
                     CHECK(status == std::future_status::ready);
                     ui_round_trip = true;
                 }
                 static_cast<void>(c.session->InvokeManagedGles(gles::GlesApi::gles2, "glClearColor", std::array{0x3f800000U,0U,0U,0x3f800000U}, c.native_thread_for_context(vm.CurrentContextToken())));
                 static_cast<void>(c.session->InvokeManagedGles(gles::GlesApi::gles2, "glClear", std::array{0x4000U}, c.native_thread_for_context(vm.CurrentContextToken())));
                 return dx::VmValue::Void(); });
        if (custom) {
            hook("chooseConfig", "(Ljavax/microedition/khronos/egl/EGL10;Ljavax/microedition/khronos/egl/EGLDisplay;)Ljavax/microedition/khronos/egl/EGLConfig;",
                 [&](dx::IntrinsicContext& args) {
                    thread_identity();
                    order.push_back("choose");
                    const auto attrs=ints({0x3040,4,0x3038}), count=ints({0});
                    const auto configs=vm.Model().NewObjectArray(linker.ResolveDescriptor("[Ljavax/microedition/khronos/egl/EGLConfig;"),linker.ResolveDescriptor("Ljavax/microedition/khronos/egl/EGLConfig;"),1);
                    const auto roots=vm.ProtectReferences(std::array{attrs,count,configs});
                    REQUIRE(call(args.arguments[0].ref,"eglChooseConfig","(Ljavax/microedition/khronos/egl/EGLDisplay;[I[Ljavax/microedition/khronos/egl/EGLConfig;I[I)Z",
                        {args.arguments[1],ref(attrs),ref(configs),integer(1),ref(count)}).AsInt()==1);
                    return ref(vm.Model().GetObjectElement(configs,0));
                 });
            hook("createContext", "(Ljavax/microedition/khronos/egl/EGL10;Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLConfig;)Ljavax/microedition/khronos/egl/EGLContext;",
                 [&](dx::IntrinsicContext& args) {
                    thread_identity();
                    order.push_back("create");
                    if (reject_context) return ref(c.egl.no_context);
                    const auto attrs=ints({0x3098,2,0x3038}); const auto root=vm.ProtectReferences(std::array{attrs});
                    return call(args.arguments[0].ref,"eglCreateContext","(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLConfig;Ljavax/microedition/khronos/egl/EGLContext;[I)Ljavax/microedition/khronos/egl/EGLContext;",
                        {args.arguments[1],args.arguments[2],ref(c.egl.no_context),ref(attrs)});
                 });
            hook("destroyContext", "(Ljavax/microedition/khronos/egl/EGL10;Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLContext;)V",
                 [&](dx::IntrinsicContext& args) {
                    thread_identity();
                    order.push_back("destroy");
                    CHECK(call(args.arguments[0].ref,"eglDestroyContext","(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLContext;)Z",{args.arguments[1],args.arguments[2]}).AsInt()==1);
                    return dx::VmValue::Void();
                 });
            call(view,"setEGLConfigChooser","(Landroid/opengl/GLSurfaceView$EGLConfigChooser;)V",{ref(policy)});
            call(view,"setEGLContextFactory","(Landroid/opengl/GLSurfaceView$EGLContextFactory;)V",{ref(policy)});
        } else {
            call(view,"setEGLContextClientVersion","(I)V",{integer(2)});
        }
        call(view,"setRenderer","(Landroid/opengl/GLSurfaceView$Renderer;)V",{ref(policy)});
        call(c.activity,"setContentView","(Landroid/view/View;)V",{ref(view)});
        call(view,"queueEvent","(Ljava/lang/Runnable;)V",{ref(policy)});
        if (reject_context) {
            CHECK_THROWS_WITH_AS(static_cast<void>(fixture.app->ActivityLifecycle().StepFrame()), "renderer EGL context creation failed", session::DexActivityLifecycleError);
            CHECK(order == std::vector<std::string>{"choose", "create"});
            CHECK_THROWS_WITH_AS(static_cast<void>(fixture.app->Stop()), "renderer EGL context creation failed", session::DexActivityLifecycleError);
            CHECK_FALSE(c.renderer_context.IsValid());
            CHECK_FALSE(c.renderer_surface.IsValid());
            continue;
        }
        if (exit_draw) {
            CHECK(fixture.app->ActivityLifecycle().StepFrame().state == session::LifecycleRunState::stopped);
            CHECK(vm.ExitCode() == 19);
            CHECK_FALSE(vm.Threads().IsAlive(gl_thread));
            CHECK(fixture.app->NativeProcess().AttachedJniThreadCount() == 0);
            CHECK_NOTHROW(static_cast<void>(fixture.app->Stop()));
            continue;
        }
        if (reject_draw) {
            CHECK_THROWS_WITH_AS(static_cast<void>(fixture.app->ActivityLifecycle().StepFrame()),
                "renderer draw failure", std::runtime_error);
            CHECK_THROWS_WITH_AS(static_cast<void>(fixture.app->Stop()), "renderer draw failure", std::runtime_error);
            CHECK_FALSE(vm.Threads().IsAlive(gl_thread));
            CHECK_FALSE(c.renderer_context.IsValid());
            CHECK_FALSE(c.renderer_surface.IsValid());
            continue;
        }
        static_cast<void>(fixture.app->ActivityLifecycle().StepFrame());
        auto frame = c.session->TakeLatestFrame();
        REQUIRE(frame.has_value());
        CHECK(frame->width == 64);
        CHECK(frame->height == 36);
        REQUIRE(frame->rgba8.size() == 64 * 36 * 4);
        CHECK(frame->rgba8[0] == 255);
        CHECK(frame->rgba8[1] == 0);
        CHECK(frame->rgba8[2] == 0);
        CHECK(frame->rgba8[3] == 255);
        static_cast<void>(vm.CollectGarbage("renderer-egl-roots"));
        std::vector<std::string> expected = custom ? std::vector<std::string>{"choose","create","event","created","changed","draw"} : std::vector<std::string>{"event","created","changed","draw"};
        CHECK(order == expected);
        static_cast<void>(fixture.app->ActivityLifecycle().StepFrame());
        expected.push_back("draw");
        CHECK(order == expected);
        // queueEvent must run while the lifecycle is idle, without StepFrame.
        CHECK(fixture.app->ActivityLifecycle().Suspend().state == session::LifecycleRunState::running);
        std::promise<void> idle_event;
        auto idle_done = idle_event.get_future();
        event_done = &idle_event;
        call(view,"queueEvent","(Ljava/lang/Runnable;)V",{ref(policy)});
        REQUIRE(idle_done.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
        event_done = nullptr;
        expected.push_back("event");
        CHECK(order == expected);
        CHECK(fixture.app->ActivityLifecycle().Resume().state == session::LifecycleRunState::running);
        CHECK(vm.CurrentContextToken() == 1);
        CHECK(vm.Threads().CurrentThreadObject() == ui_thread);
        CHECK_FALSE(call(c.renderer_egl, "eglGetCurrentContext", "()Ljavax/microedition/khronos/egl/EGLContext;").ref == c.renderer_context);
        for (const auto preserve : {false, true}) {
            call(view, "setPreserveEGLContextOnPause", "(Z)V", {integer(preserve ? 1 : 0)});
            const auto old_context = c.renderer_context;
            call(view, "onPause", "()V");
            CHECK(c.gl_surface_runtimes.at(view.Value())->paused);
            CHECK_FALSE(c.renderer_surface.IsValid());
            CHECK(c.renderer_context.IsValid() == preserve);
            call(view, "onResume", "()V");
            static_cast<void>(fixture.app->ActivityLifecycle().StepFrame());
            CHECK_FALSE(c.gl_surface_runtimes.at(view.Value())->paused);
            CHECK(c.renderer_surface.IsValid());
            CHECK((c.renderer_context == old_context) == preserve);
            CHECK(vm.Threads().IsAlive(gl_thread));
            if (!preserve) {
                if (custom) {
                    expected.push_back("destroy"); expected.push_back("choose"); expected.push_back("create");
                }
                expected.push_back("created");
            }
            expected.push_back("changed"); expected.push_back("draw");
            CHECK(order == expected);
        }
        const auto source_activity = c.activity;
        const auto source_content = c.content_view;
        const auto source_runtime = c.gl_surface_runtimes.at(view.Value());
        c.activity_components.push_back({loader::AndroidManifestComponentKind::activity,
            "fixture.ResultChild", std::nullopt, true, {}});
        const auto intent = vm.NewIntrinsicInstance("Landroid/content/Intent;");
        const auto intent_ctor = linker.FindDirectMethod(vm.Model().ObjectClass(intent), "<init>", "()V");
        REQUIRE(intent_ctor.has_value());
        REQUIRE_FALSE(vm.Call(*intent_ctor, std::array{ref(intent)}).exception.IsValid());
        call(intent, "setClassName", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
             {ref(vm.NewStringUtf8("fixture")), ref(vm.NewStringUtf8("fixture.ResultChild"))});
        call(source_activity, "startActivity", "(Landroid/content/Intent;)V", {ref(intent)});
        static_cast<void>(fixture.app->ActivityLifecycle().StepFrame());
        CHECK(c.activity != source_activity);
        CHECK_FALSE(c.renderer.IsValid());
        CHECK_FALSE(source_runtime->surface_available);
        CHECK(source_runtime->renderer_context.IsValid());
        CHECK(vm.Threads().IsAlive(gl_thread));
        call(c.activity, "finish", "()V");
        static_cast<void>(fixture.app->ActivityLifecycle().StepFrame());
        CHECK(c.activity == source_activity);
        CHECK(c.content_view == source_content);
        CHECK(c.gl_surface_runtimes.at(view.Value()) == source_runtime);
        CHECK(source_runtime->surface_available);
        CHECK(vm.Threads().IsAlive(gl_thread));
        expected.push_back("changed"); expected.push_back("draw");
        CHECK(order == expected);
        CHECK(fixture.app->Stop().state == session::LifecycleRunState::stopped);
        CHECK_FALSE(vm.Threads().IsAlive(gl_thread));
        CHECK_FALSE(c.run_gl_surface_thread);
        CHECK_FALSE(c.wake_gl_surface_thread);
        CHECK_THROWS(call(view,"queueEvent","(Ljava/lang/Runnable;)V",{ref(policy)}));
        if(custom) expected.push_back("destroy");
        CHECK(order == expected);
        CHECK_FALSE(c.renderer_context.IsValid());
        CHECK_FALSE(c.renderer_surface.IsValid());
        CHECK_FALSE(c.egl.window_surface.IsValid());
      }
    }
}

TEST_CASE("API19 Window background follows its unique lazy content root") {
    using namespace ogplay::runtime::dexvm;
    using ogplay::test::container_traversal::ContainerVm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        f.context->ui_test_accessibility_disabled = true;
        const auto owner = f.New("Landroid/app/Activity;");
        f.context->activity = owner;
        ogplay::test::AttachViewTestContext(f.vm, owner);
        const auto window = f.On(owner, "getWindow", "()Landroid/view/Window;").ref;
        const auto roots = f.vm.ProtectReferences(std::array{owner, window});
        const auto read_ref = [&](const char* name, const char* descriptor) {
            const auto field = f.linker.FindFieldRecursive(f.model.ObjectClass(window), name, descriptor);
            REQUIRE(field);
            return VmObjectRef{f.model.InstanceSlots(window)[f.linker.Field(*field).slot].bits};
        };
        const auto background = [&] { return read_ref("mBackgroundDrawable", "Landroid/graphics/drawable/Drawable;"); };
        CHECK_FALSE(read_ref("mContentParent", "Landroid/widget/FrameLayout;").IsValid());
        f.On(window, "setBackgroundDrawableResource", "(I)V", {VmValue::Int(0x0106000c)});
        const auto black = background();
        REQUIRE(black.IsValid());
        CHECK_FALSE(read_ref("mContentParent", "Landroid/widget/FrameLayout;").IsValid());
        // The saved Drawable must survive solely through its Java Window field.
        static_cast<void>(f.vm.CollectGarbage("window-background-rooted"));
        const auto root = f.On(window, "getDecorView", "()Landroid/view/View;").ref;
        CHECK(f.On(root, "getBackground", "()Landroid/graphics/drawable/Drawable;").ref == black);
        const auto next = f.New("Landroid/graphics/drawable/ColorDrawable;", "(I)V", {VmValue::Int(0x12345678)});
        const auto next_root = f.vm.ProtectReferences(std::array{next});
        const auto applications = [&] {
            int count = 0;
            for (const auto& event : f.vm.Trace("", 10000))
                if (event.class_descriptor == "Landroid/view/View;" && event.method_name == "setBackgroundDrawable") ++count;
            return count;
        };
        const auto initial_applications = applications();
        const auto set = [&](VmObjectRef drawable) {
            f.On(window, "setBackgroundDrawable", "(Landroid/graphics/drawable/Drawable;)V", {VmValue::Ref(drawable)});
        };
        set(black);
        CHECK(applications() - initial_applications == 0);
        set(next);
        CHECK(applications() - initial_applications == 1);
        CHECK(background() == next);
        CHECK(f.On(root, "getBackground", "()Landroid/graphics/drawable/Drawable;").ref == next);
        set(next);
        CHECK(applications() - initial_applications == 1);
        set(VmObjectRef{});
        CHECK(applications() - initial_applications == 2);
        CHECK_FALSE(background().IsValid());
        CHECK_FALSE(f.On(root, "getBackground", "()Landroid/graphics/drawable/Drawable;").ref.IsValid());
        set(VmObjectRef{});
        CHECK(applications() - initial_applications == 2);
        const auto failed = f.OnOutcome(window, "setBackgroundDrawableResource", "(I)V", {VmValue::Int(0x0106ffff)});
        REQUIRE(failed.exception.IsValid());
        CHECK(f.linker.Class(failed.exception_class).descriptor == "Landroid/content/res/Resources$NotFoundException;");
        CHECK_FALSE(background().IsValid());
        CHECK(applications() - initial_applications == 2);
        CHECK(f.On(window, "getDecorView", "()Landroid/view/View;").ref == root);
        // A virtual Resources override must preserve the original Throwable identity.
        const auto resources = f.On(owner, "getResources", "()Landroid/content/res/Resources;").ref;
        const auto resources_slot = f.linker.FindVtableIndex(f.model.ObjectClass(resources), "getDrawable",
                                                             "(I)Landroid/graphics/drawable/Drawable;");
        REQUIRE(resources_slot);
        auto& resource_method = f.linker.MutableMethod(f.linker.Class(f.model.ObjectClass(resources)).vtable[*resources_slot]);
        const auto resource_original = resource_method.implementation;
        const auto exception_root = f.vm.ProtectReferences(std::array{failed.exception});
        resource_method.implementation = [exception = failed.exception](IntrinsicContext&) -> VmValue {
            throw VmJavaThrow{"Landroid/content/res/Resources$NotFoundException;", "sentinel", exception};
        };
        const auto propagated = f.OnOutcome(window, "setBackgroundDrawableResource", "(I)V", {VmValue::Int(0x0106000c)});
        CHECK(propagated.exception == failed.exception);
        CHECK_FALSE(background().IsValid());
        resource_method.implementation = resource_original;
    }
}
