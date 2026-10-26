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

TEST_CASE("APS-14 Activity finish and destroy state follows each Java instance") {
    using namespace ogplay;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        CAPTURE(backend == InterpreterBackend::threaded ? "threaded" : "switch");
        { // Finish remains visible through pause, stop, destroy and retirement.
            OrchestratedApp f("fixture.ResultCaller", true, false, {}, {}, true, backend);
            loader::AndroidManifestActivityComponent component;
            component.name = "fixture.ActivityState";
            f.context->activity_components.push_back(std::move(component));
            f.app->StartApplication();
            REQUIRE(f.app->StartLauncherActivity().state == session::LifecycleRunState::running);
            auto& vm = f.app->DexVm().Vm();
            const auto on = [&](VmObjectRef object, const char* name, const char* signature,
                                std::vector<VmValue> args = {}) {
                return runtime::android_intrinsics::CallAndroidMethod(vm, object, name, signature, std::move(args));
            };
            const auto parent = f.context->activity;
            const auto intent = vm.NewIntrinsicInstance("Landroid/content/Intent;");
            const auto intent_root = vm.ProtectReferences(std::array{intent});
            const auto ctor = vm.Linker().FindDirectMethod(vm.Linker().ResolveDescriptor("Landroid/content/Intent;"), "<init>", "()V");
            REQUIRE(ctor);
            REQUIRE_FALSE(vm.Call(*ctor, std::array{VmValue::Ref(intent)}).exception.IsValid());
            on(intent, "setClassName", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
                {VmValue::Ref(vm.NewStringUtf8("fixture")), VmValue::Ref(vm.NewStringUtf8("fixture.ActivityState"))});
            on(parent, "startActivity", "(Landroid/content/Intent;)V", {VmValue::Ref(intent)});
            static_cast<void>(f.app->ActivityLifecycle().StepFrame());
            const auto child = f.context->activity;
            REQUIRE(child != parent);
            const auto child_root = vm.ProtectReferences(std::array{child});
            CHECK(on(child, "state", "()I").AsInt() == 0);
            CHECK(on(child, "destroyState", "()I").AsInt() == -1);
            on(child, "finish", "()V");
            CHECK(on(child, "state", "()I").AsInt() == 1);
            CHECK(on(parent, "isFinishing", "()Z").AsInt() == 0);
            CHECK(on(parent, "isDestroyed", "()Z").AsInt() == 0);
            static_cast<void>(f.app->ActivityLifecycle().StepFrame());
            CHECK(f.context->activity == parent);
            CHECK(f.context->finishing_activity.load() == 0);
            CHECK(on(child, "pauseState", "()I").AsInt() == 1);
            CHECK(on(child, "stopState", "()I").AsInt() == 1);
            CHECK(on(child, "destroyState", "()I").AsInt() == 3);
            CHECK(on(child, "state", "()I").AsInt() == 3);
            CHECK(on(parent, "isFinishing", "()Z").AsInt() == 0);
            static_cast<void>(f.app->Stop());
            CHECK(on(child, "state", "()I").AsInt() == 3);
        }
        { // Process shutdown and rejected finish do not mark the instance.
            OrchestratedApp f("fixture.ActivityState", true, false, {}, {}, true, backend);
            f.app->StartApplication();
            REQUIRE(f.app->StartLauncherActivity().state == session::LifecycleRunState::running);
            auto& vm = f.app->DexVm().Vm();
            const auto activity = f.context->activity;
            const auto root = vm.ProtectReferences(std::array{activity});
            const auto state = [&](const char* name) {
                return runtime::android_intrinsics::CallAndroidMethod(vm, activity, name, "()I").AsInt();
            };
            CHECK(state("state") == 0);
            f.context->process_stopping = true;
            const auto type = vm.Model().ObjectClass(activity);
            const auto slot = vm.Linker().FindVtableIndex(type, "finish", "()V");
            REQUIRE(slot);
            const auto rejected = vm.Call(vm.Linker().Class(type).vtable[*slot], std::array{VmValue::Ref(activity)});
            REQUIRE(rejected.exception.IsValid());
            CHECK(vm.Linker().Class(rejected.exception_class).descriptor == "Ljava/lang/IllegalStateException;");
            CHECK(state("state") == 0);
            CHECK(f.context->activity_commands.empty());
            f.context->process_stopping = false;
            static_cast<void>(f.app->Stop());
            CHECK(state("pauseState") == 0);
            CHECK(state("stopState") == 0);
            // Cooperative host shutdown calls onDestroy without instance finish.
            CHECK(state("destroyState") == 0);
            CHECK(state("state") == 0);
        }
    }
}

TEST_CASE("DVM-223 Activity result stack restores caller identity and content") {
    using namespace ogplay;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        OrchestratedApp f("fixture.ResultCaller", true, false, {}, {}, true, backend);
        f.app->StartApplication();
        REQUIRE(f.app->StartLauncherActivity().state == session::LifecycleRunState::running);
        auto& vm = f.app->DexVm().Vm();
        auto& linker = vm.Linker();
        auto& lifecycle = f.app->ActivityLifecycle();
        const auto on = [&](VmObjectRef object, const char* name, const char* signature,
                            std::vector<VmValue> args = {}) {
            const auto type = vm.Model().ObjectClass(object);
            const auto index = linker.FindVtableIndex(type, name, signature);
            REQUIRE(index.has_value());
            args.insert(args.begin(), VmValue::Ref(object));
            auto result = vm.Call(linker.Class(type).vtable[*index], args);
            REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
            return result.value;
        };
        const auto intent = [&] {
            const auto type = linker.ResolveDescriptor("Landroid/content/Intent;");
            const auto object = vm.NewIntrinsicInstance("Landroid/content/Intent;");
            const auto init = linker.FindDirectMethod(type, "<init>", "()V");
            REQUIRE(init.has_value());
            REQUIRE_FALSE(vm.Call(*init, std::array{VmValue::Ref(object)}).exception.IsValid());
            on(object, "setClassName", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
               {VmValue::Ref(vm.NewStringUtf8("fixture")), VmValue::Ref(vm.NewStringUtf8("fixture.ResultChild"))});
            return object;
        };
        const auto launch = [&](std::int32_t code) {
            on(f.context->activity, "startActivityForResult", "(Landroid/content/Intent;I)V",
                {VmValue::Ref(intent()), VmValue::Int(code)});
            static_cast<void>(lifecycle.StepFrame());
        };
        const auto reset_events = [&] {
            const auto method = linker.FindDirectMethod(linker.ResolveDescriptor("Lfixture/ResultCaller;"), "resetEvents", "()V");
            REQUIRE(method.has_value()); REQUIRE_FALSE(vm.Call(*method, {}).exception.IsValid());
        };
        const auto parent = f.context->activity;
        const auto content = f.context->content_view;
        const auto node = runtime::FindViewUiNode(*f.context, content.Value());
        const auto parent_window = on(parent, "getWindow", "()Landroid/view/Window;").ref;
        const auto parent_attributes = on(parent_window, "getAttributes", "()Landroid/view/WindowManager$LayoutParams;").ref;
        const auto parent_decor = on(parent_window, "getDecorView", "()Landroid/view/View;").ref;
        on(content, "setId", "(I)V", {VmValue::Int(11)});
        f.context->activity_components.back().launch_mode = 1; // singleTop creates when target is not the top.
        launch(100);
        f.context->activity_components.back().launch_mode = 0;

        const auto child = f.context->activity;
        const auto child_content = f.context->content_view;
        CHECK(child != parent);
        CHECK(f.context->activity_stack.size() == 2);
        CHECK(on(child, "getWindow", "()Landroid/view/Window;").ref != parent_window);
        const auto child_window = on(child, "getWindow", "()Landroid/view/Window;").ref;
        CHECK(on(child_window, "getAttributes", "()Landroid/view/WindowManager$LayoutParams;").ref != parent_attributes);
        CHECK(on(child_window, "getDecorView", "()Landroid/view/View;").ref != parent_decor);
        on(child_content, "setId", "(I)V", {VmValue::Int(11)});
        CHECK(on(parent, "findViewById", "(I)Landroid/view/View;", {VmValue::Int(11)}).ref == content);
        CHECK(on(child, "findViewById", "(I)Landroid/view/View;", {VmValue::Int(11)}).ref == child_content);
        // JavaTraversal publishes only the foreground window's projection;
        // the parent's Java content survives in its retained window state.
        const auto foreground = f.context->ui_tree.PublishedProjection();
        REQUIRE(foreground);
        CHECK_FALSE(foreground->IsAttached(*node));
        CHECK_FALSE(foreground->IsVisible(*node));
        CHECK_FALSE(f.context->ui_tree.IsVisible(*node));
        on(child, "setResult", "(I)V", {VmValue::Int(1)});
        static_cast<void>(vm.CollectGarbage("activity-return-roots"));
        reset_events();
        on(child, "finish", "()V");
        on(child, "finish", "()V");
        CHECK_FALSE(runtime::SessionExitRequested(*f.context));
        static_cast<void>(lifecycle.StepFrame());
        CHECK(f.context->activity == parent);
        CHECK(f.context->content_view == content);
        CHECK(f.context->ui_tree.IsAttached(*node));
        const auto restored = f.context->ui_tree.PublishedProjection();
        REQUIRE(restored);
        CHECK(restored->IsAttached(*node));
        CHECK_FALSE(runtime::FindViewUiNode(*f.context, child_content.Value()).has_value());
        CHECK(on(parent, "getWindow", "()Landroid/view/Window;").ref == parent_window);
        CHECK(on(parent_window, "getDecorView", "()Landroid/view/View;").ref == parent_decor);
        CHECK(f.CallStaticInt("Lfixture/ResultCaller;", "getInstances") == 1);
        CHECK(f.CallStaticInt("Lfixture/ResultCaller;", "getEvents") == 3124);
        CHECK(f.CallStaticInt("Lfixture/ResultCaller;", "getCallbacks") == 1);
        CHECK(f.CallStaticInt("Lfixture/ResultCaller;", "getRequest") == 100);
        CHECK(f.CallStaticInt("Lfixture/ResultCaller;", "getResult") == 1);
        // Invalid requests fail synchronously and never enqueue a transition.
        const auto reject = [&](VmObjectRef requested, VmObjectRef options) {
            const auto type = vm.Model().ObjectClass(parent);
            const auto index = linker.FindVtableIndex(type, "startActivityForResult", "(Landroid/content/Intent;ILandroid/os/Bundle;)V");
            REQUIRE(index.has_value());
            const auto outcome = vm.Call(linker.Class(type).vtable[*index], std::array{
                VmValue::Ref(parent), VmValue::Ref(requested), VmValue::Int(77), VmValue::Ref(options)});
            REQUIRE(outcome.exception.IsValid());
            CHECK(linker.Class(outcome.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
            CHECK(f.context->activity_commands.empty());
        };
        const auto options = vm.NewIntrinsicInstance("Landroid/os/Bundle;");
        reject(intent(), options);
        const auto flagged = intent();
        on(flagged, "setFlags", "(I)Landroid/content/Intent;", {VmValue::Int(0x10000000)});
        reject(flagged, VmObjectRef{});
        for (const auto mode : {2U, 3U}) {
            f.context->activity_components.back().launch_mode = mode;
            reject(intent(), VmObjectRef{});
        }
        f.context->activity_components.back().launch_mode = 0;
        // No setResult means cancellation; a negative requestCode has no callback.
        for (const auto code : {101, -1}) {
            launch(code);
            on(f.context->activity, "finish", "()V");
            static_cast<void>(lifecycle.StepFrame());
            CHECK(f.context->activity == parent);
        }
        CHECK(f.CallStaticInt("Lfixture/ResultCaller;", "getCallbacks") == 2);
        CHECK(f.CallStaticInt("Lfixture/ResultCaller;", "getRequest") == 101);
        CHECK(f.CallStaticInt("Lfixture/ResultCaller;", "getResult") == 0);
        // Nested calls return to the immediate live instance.
        launch(102);
        const auto middle = f.context->activity;
        launch(103);
        CHECK(f.context->activity_stack.size() == 3);
        on(f.context->activity, "finish", "()V");
        static_cast<void>(lifecycle.StepFrame());
        CHECK(f.context->activity == middle);
        const auto data = intent();
        on(middle, "setResult", "(ILandroid/content/Intent;)V", {VmValue::Int(2), VmValue::Ref(data)});
        on(middle, "finish", "()V");
        static_cast<void>(vm.CollectGarbage("queued-activity-result"));
        static_cast<void>(lifecycle.StepFrame());
        CHECK(f.context->activity == parent);
        CHECK(f.CallStaticInt("Lfixture/ResultCaller;", "getCallbacks") == 3);
        CHECK(f.CallStaticInt("Lfixture/ResultCaller;", "getResult") == 2);
        const auto getter = linker.FindDirectMethod(linker.ResolveDescriptor("Lfixture/ResultCaller;"), "getData", "()Landroid/content/Intent;");
        REQUIRE(getter.has_value());
        const auto delivered = vm.Call(*getter, {}).value.ref;
        REQUIRE(delivered.IsValid());
        CHECK(delivered != data);
        CHECK(on(delivered, "getComponent", "()Landroid/content/ComponentName;").ref.IsValid());
        // Starting another Activity then finishing the middle one keeps the new top.
        launch(104);
        const auto departing = f.context->activity;
        on(departing, "startActivity", "(Landroid/content/Intent;)V", {VmValue::Ref(intent())});
        on(departing, "setResult", "(I)V", {VmValue::Int(1)});
        on(departing, "finish", "()V");
        static_cast<void>(lifecycle.StepFrame());
        CHECK(f.context->activity != parent);
        CHECK(f.context->activity_stack.size() == 2);
        CHECK(f.CallStaticInt("Lfixture/ResultCaller;", "getCallbacks") == 3);
        on(f.context->activity, "finish", "()V");
        static_cast<void>(lifecycle.StepFrame());
        CHECK(f.context->activity == parent);
        CHECK(f.CallStaticInt("Lfixture/ResultCaller;", "getCallbacks") == 4);
        // A last-instance finish followed by a launch in the same guest turn
        // does not lose either command or exit before the new Activity exists.
        on(parent, "finish", "()V");
        on(parent, "startActivity", "(Landroid/content/Intent;)V", {VmValue::Ref(intent())});
        static_cast<void>(lifecycle.StepFrame());
        CHECK(f.context->activity != parent);
        CHECK(f.context->activity_stack.size() == 1);
        CHECK_FALSE(runtime::SessionExitRequested(*f.context));
        on(f.context->activity, "finish", "()V");
        static_cast<void>(lifecycle.StepFrame());
        CHECK(runtime::SessionExitRequested(*f.context));
        static_cast<void>(f.app->Stop());
        CHECK(f.context->activity_stack.empty());
        CHECK(f.context->activity_commands.empty());
    }
}

TEST_CASE("DVM-250 task reuse preserves instance Intent and independent singleInstance roots") {
    using namespace ogplay;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        OrchestratedApp f("fixture.TaskActivity", true, false, {}, {}, true, backend);
        f.context->activity_components.front().launch_mode = 2;
        for (const auto& [name, mode] : std::array{std::pair{"fixture.TaskChild", 0U}, std::pair{"fixture.TaskIsolated", 3U}}) {
            loader::AndroidManifestActivityComponent component;
            component.name = name; component.launch_mode = mode;
            f.context->activity_components.push_back(std::move(component));
        }
        f.app->StartApplication();
        REQUIRE(f.app->StartLauncherActivity().state == session::LifecycleRunState::running);
        auto& vm = f.app->DexVm().Vm();
        auto& lifecycle = f.app->ActivityLifecycle();
        const auto on = [&](VmObjectRef object, const char* name, const char* signature, std::vector<VmValue> args = {}) {
            return runtime::android_intrinsics::CallAndroidMethod(vm, object, name, signature, std::move(args));
        };
        const auto intent = [&](const char* target) {
            const auto object = vm.NewIntrinsicInstance("Landroid/content/Intent;");
            const auto root = vm.ProtectReferences(std::array{object});
            const auto ctor = vm.Linker().FindDirectMethod(vm.Linker().ResolveDescriptor("Landroid/content/Intent;"), "<init>", "()V");
            REQUIRE(ctor); REQUIRE_FALSE(vm.Call(*ctor, std::array{VmValue::Ref(object)}).exception.IsValid());
            on(object, "setClassName", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
                {VmValue::Ref(vm.NewStringUtf8("fixture")), VmValue::Ref(vm.NewStringUtf8(target))});
            return object;
        };
        const auto request = [&](const char* target) {
            const auto requested = intent(target);
            on(f.context->activity, "startActivity", "(Landroid/content/Intent;)V", {VmValue::Ref(requested)});
            return requested;
        };
        const auto step = [&] { REQUIRE(lifecycle.StepFrame().state == session::LifecycleRunState::running); };
        const auto count = [&](const char* method) { return f.CallStaticInt("Lfixture/TaskActivity;", method); };
        const auto main = f.context->activity;
        const auto content = f.context->content_view;
        const auto initial = on(main, "getIntent", "()Landroid/content/Intent;").ref;
        const auto main_task = on(main, "getTaskId", "()I").AsInt();
        CHECK(on(main, "isTaskRoot", "()Z").AsInt() == 1);
        vm.SetStaticFieldBits("Lfixture/TaskActivity;", "trace", "I", 0);
        const auto supplied = request("fixture.TaskActivity");
        const auto supplied_root = vm.ProtectReferences(std::array{supplied});
        static_cast<void>(vm.CollectGarbage("task-queued-intent"));
        step();
        CHECK(f.context->activity == main);
        CHECK(f.context->activity_stack.size() == 1);
        CHECK(count("getNewIntents") == 1);
        CHECK(count("getTrace") == 123); // temporary pause -> new Intent -> resume
        CHECK(on(main, "getOriginal", "()Landroid/content/Intent;").ref == initial);
        CHECK(on(main, "getIntent", "()Landroid/content/Intent;").ref == initial);
        CHECK(on(main, "getReceived", "()Landroid/content/Intent;").ref != supplied);
        vm.SetStaticFieldBits("Lfixture/TaskActivity;", "replaceIntent", "I", 1);
        request("fixture.TaskActivity"); step();
        CHECK(on(main, "getIntent", "()Landroid/content/Intent;").ref == on(main, "getReceived", "()Landroid/content/Intent;").ref);
        vm.SetStaticFieldBits("Lfixture/TaskActivity;", "replaceIntent", "I", 0);

        request("fixture.TaskChild"); step();
        const auto first_child = f.context->activity;
        request("fixture.TaskChild"); step();
        CHECK(f.context->activity != first_child); // standard creates again
        CHECK(on(f.context->activity, "getTaskId", "()I").AsInt() == main_task);
        CHECK(on(f.context->activity, "isTaskRoot", "()Z").AsInt() == 0);
        request("fixture.TaskActivity"); step();
        CHECK(f.context->activity == main);
        CHECK(f.context->activity_stack.size() == 1); // clear above only this task
        CHECK(count("getDestroys") == 2);
        CHECK(f.context->content_view == content);
        CHECK(count("getNewIntents") == 3);

        request("fixture.TaskIsolated"); step();
        const auto isolated = f.context->activity;
        const auto isolated_root = vm.ProtectReferences(std::array{isolated});
        const auto isolated_task = on(isolated, "getTaskId", "()I").AsInt();
        CHECK(isolated_task != main_task);
        CHECK(on(isolated, "isTaskRoot", "()Z").AsInt() == 1);
        request("fixture.TaskChild"); step(); // leaves the isolated task
        const auto normal = f.context->activity;
        CHECK(on(normal, "getTaskId", "()I").AsInt() == main_task);
        CHECK(on(normal, "isTaskRoot", "()Z").AsInt() == 0);
        CHECK(f.context->activity_stack.size() == 3);
        request("fixture.TaskIsolated"); step();
        CHECK(f.context->activity == isolated);
        CHECK(f.context->activity_stack.size() == 3);
        CHECK(on(isolated, "getTaskId", "()I").AsInt() == isolated_task);
        CHECK(count("getNewIntents") == 4);
        on(isolated, "finish", "()V"); step();
        CHECK(f.context->activity == normal);
        CHECK(on(isolated, "getTaskId", "()I").AsInt() == -1);
        CHECK(on(isolated, "isTaskRoot", "()Z").AsInt() == 0);
        on(normal, "finish", "()V"); step();
        CHECK(f.context->activity == main);

        // Video-style return: launch the retained singleTask, then finish owner.
        request("fixture.TaskIsolated"); step();
        const auto video = f.context->activity;
        CHECK(on(video, "getTaskId", "()I").AsInt() != isolated_task);
        request("fixture.TaskActivity");
        on(video, "finish", "()V");
        static_cast<void>(vm.CollectGarbage("task-return"));
        step();
        CHECK(f.context->activity == main);
        CHECK(f.context->activity_stack.size() == 1);
        CHECK(f.context->content_view == content);
        CHECK(f.CallStaticInt("Lfixture/ResultCaller;", "getInstances") == 6);
        const auto custom = std::find_if(f.context->activity_components.begin(), f.context->activity_components.end(),
            [](const auto& component) { return component.name == "fixture.TaskChild"; });
        REQUIRE(custom != f.context->activity_components.end());
        custom->task_affinity = "fixture.custom";
        custom->launch_mode = 2;
        const auto unsupported_intent = intent("fixture.TaskChild");
        const auto slot = vm.Linker().FindVtableIndex(vm.Model().ObjectClass(main), "startActivity", "(Landroid/content/Intent;)V");
        REQUIRE(slot);
        const auto rejected = vm.Call(vm.Linker().Class(vm.Model().ObjectClass(main)).vtable[*slot],
            std::array{VmValue::Ref(main), VmValue::Ref(unsupported_intent)});
        REQUIRE(rejected.exception.IsValid());
        CHECK(vm.Linker().Class(rejected.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        CHECK(f.context->activity_commands.empty());
        custom->launch_mode = 0;
        request("fixture.TaskChild"); step(); // ordinary starts do not consult affinity
        CHECK(on(f.context->activity, "getTaskId", "()I").AsInt() == main_task);
        static_cast<void>(f.app->Stop());
    }
}

TEST_CASE("DVM-250 singleTop and singleInstance reuse their foreground instance") {
    using namespace ogplay;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
      for (const auto mode : {1U, 3U}) {
        OrchestratedApp f("fixture.TaskActivity", true, false, {}, {}, true, backend);
        f.context->activity_components.front().launch_mode = mode;
        f.app->StartApplication();
        REQUIRE(f.app->StartLauncherActivity().state == session::LifecycleRunState::running);
        auto& vm = f.app->DexVm().Vm();
        const auto object = f.context->activity;
        const auto command_intent = runtime::android_intrinsics::CallAndroidMethod(vm, object, "getIntent", "()Landroid/content/Intent;").ref;
        static_cast<void>(runtime::android_intrinsics::CallAndroidMethod(vm, object, "startActivity", "(Landroid/content/Intent;)V", {VmValue::Ref(command_intent)}));
        REQUIRE(f.app->ActivityLifecycle().StepFrame().state == session::LifecycleRunState::running);
        CHECK(f.context->activity == object);
        CHECK(f.context->activity_stack.size() == 1);
        CHECK(f.CallStaticInt("Lfixture/ResultCaller;", "getInstances") == 1);
        CHECK(f.CallStaticInt("Lfixture/TaskActivity;", "getNewIntents") == 1);
        if (mode == 1) {
            loader::AndroidManifestActivityComponent child_component;
            child_component.name = "fixture.TaskChild";
            f.context->activity_components.push_back(std::move(child_component));
            const auto child_intent = vm.NewIntrinsicInstance("Landroid/content/Intent;");
            const auto roots = vm.ProtectReferences(std::array{child_intent});
            const auto ctor = vm.Linker().FindDirectMethod(vm.Linker().ResolveDescriptor("Landroid/content/Intent;"), "<init>", "()V");
            REQUIRE(ctor); REQUIRE_FALSE(vm.Call(*ctor, std::array{VmValue::Ref(child_intent)}).exception.IsValid());
            const auto on = [&](VmObjectRef receiver, const char* name, const char* signature, std::vector<VmValue> args = {}) {
                return runtime::android_intrinsics::CallAndroidMethod(vm, receiver, name, signature, std::move(args));
            };
            on(child_intent, "setClassName", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
                {VmValue::Ref(vm.NewStringUtf8("fixture")), VmValue::Ref(vm.NewStringUtf8("fixture.TaskChild"))});
            on(object, "startActivity", "(Landroid/content/Intent;)V", {VmValue::Ref(child_intent)});
            static_cast<void>(f.app->ActivityLifecycle().StepFrame());
            on(f.context->activity, "startActivity", "(Landroid/content/Intent;)V", {VmValue::Ref(command_intent)});
            static_cast<void>(f.app->ActivityLifecycle().StepFrame());
            CHECK(f.context->activity != object);
            CHECK(f.context->activity_stack.size() == 3); // non-top singleTop creates
            CHECK(f.CallStaticInt("Lfixture/ResultCaller;", "getInstances") == 3);
            CHECK(f.CallStaticInt("Lfixture/TaskActivity;", "getNewIntents") == 1);
        } else {
            static_cast<void>(runtime::android_intrinsics::CallAndroidMethod(vm, object, "startActivity", "(Landroid/content/Intent;)V", {VmValue::Ref(command_intent)}));
            static_cast<void>(runtime::android_intrinsics::CallAndroidMethod(vm, object, "finish", "()V", {}));
            REQUIRE(f.app->ActivityLifecycle().StepFrame().state == session::LifecycleRunState::running);
            CHECK(f.context->activity != object); // finishing instances cannot be reused
            CHECK(f.context->activity_stack.size() == 1);
            CHECK(f.CallStaticInt("Lfixture/ResultCaller;", "getInstances") == 2);
        }
        static_cast<void>(f.app->Stop());
      }
    }
}

TEST_CASE("DVM-223 onStart launch and onCreate finish return before caller resume") {
    using namespace ogplay;
    using runtime::dexvm::InterpreterBackend;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        OrchestratedApp f("fixture.ResultOnStartCaller", true, false, {}, {}, true, backend);
        f.app->StartApplication();
        REQUIRE(f.app->StartLauncherActivity().state == session::LifecycleRunState::running);
        CHECK(f.context->activity_stack.size() == 1);
        CHECK(f.app->DexVm().Vm().Linker().Class(f.app->DexVm().Vm().Model().ObjectClass(f.context->activity)).descriptor == "Lfixture/ResultOnStartCaller;");
        CHECK(f.CallStaticInt("Lfixture/ResultCaller;", "getInstances") == 1);
        CHECK(f.CallStaticInt("Lfixture/ResultCaller;", "getCallbacks") == 1);
        CHECK(f.CallStaticInt("Lfixture/ResultCaller;", "getEvents") == 234);
        CHECK(f.CallStaticInt("Lfixture/ResultCaller;", "getResult") == 1);
        CHECK_FALSE(runtime::SessionExitRequested(*f.context));
        static_cast<void>(f.app->Stop());
    }
}

TEST_CASE("Activity.isTaskRoot is true for the launcher and false after a handoff") {
    using namespace ogplay;
    OrchestratedApp fixture("fixture.TaskRootLauncherActivity");
    fixture.app->StartApplication();
    const auto started = fixture.app->StartLauncherActivity();
    CHECK(started.state == session::LifecycleRunState::running);
    // The launcher recorded its own answer before requesting the handoff.
    CHECK(fixture.CallStaticInt("Lfixture/TaskRootLauncherActivity;",
                                "getRootSeen") == 1);
    // The handoff really ran, and the arriving activity answered false.
    CHECK(fixture.CallStaticInt("Lfixture/TaskRootChildActivity;",
                                "getInstances") == 1);
    CHECK(fixture.CallStaticInt("Lfixture/TaskRootChildActivity;",
                                "getRootSeen") == 0);
    static_cast<void>(fixture.app->Stop());
}
