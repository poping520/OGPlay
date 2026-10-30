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

TEST_CASE("DVM-150 Java exit unwinds Activity startup and stops the guest process") {
    using namespace ogplay;
    OrchestratedApp fixture("fixture.ExitActivity");
    fixture.app->StartApplication();
    const auto stopped = fixture.app->StartLauncherActivity();
    CHECK(stopped.state == session::LifecycleRunState::stopped);
    CHECK(fixture.app->State() == session::AndroidAppProcessState::stopped);
    CHECK(fixture.app->DexVm().Vm().ExitCode() == 29);
    CHECK(runtime::SessionExitRequested(*fixture.app->Context()));
    CHECK_FALSE(fixture.app->NativeProcess().Running());
    CHECK(fixture.app->Stop().state == session::LifecycleRunState::stopped);
}

TEST_CASE("DVM-150 Java exit cannot be caught by Application onCreate") {
    using namespace ogplay;
    ApplicationProcess fixture;
    session::DexActivityLifecycleBindings bindings;
    bindings.bridge = fixture.bridge.get();
    bindings.context = fixture.context;
    bindings.application_descriptor = "Lfixture/ExitApplication;";
    bindings.launcher_descriptor = "Lfixture/LauncherActivity;";
    bool opened = false;
    bindings.open_surface = [&] { opened = true; };
    session::DexActivityLifecycle lifecycle(std::move(bindings));
    const auto state = lifecycle.Start();
    CHECK(state.state == session::LifecycleRunState::stopped);
    CHECK(fixture.bridge->Vm().ExitCode() == 31);
    // Read the value captured by the hook before nativeExit; no Java calls after exit.
    auto& vm = fixture.bridge->Vm();
    const auto hook = vm.Linker().ResolveDescriptor("Lfixture/ExitHook;");
    const auto field = vm.Linker().FindFieldRecursive(hook, "observed", "Ljava/lang/String;");
    REQUIRE(field);
    const runtime::dexvm::VmObjectRef observed(vm.Linker().Class(hook).static_storage[vm.Linker().Field(*field).slot]);
    CHECK(vm.StringUtf8(observed) == "done");
    CHECK(fixture.context->uptime_millis.load() >= 10);
    CHECK_FALSE(opened);
}

TEST_CASE("DVM-150 AndroidAppProcess does not launch after Application exits") {
    using namespace ogplay;
    OrchestratedApp fixture("fixture.LauncherActivity", true, false, "", {}, true,
                            runtime::dexvm::InterpreterBackend::switch_dispatch, "fixture.ExitApplication");
    fixture.app->StartApplication();
    CHECK(fixture.app->State() == session::AndroidAppProcessState::stopped);
    CHECK(fixture.app->StartLauncherActivity().state == session::LifecycleRunState::stopped);
    CHECK(fixture.app->DexVm().Vm().ExitCode() == 31);
    CHECK_FALSE(fixture.app->NativeProcess().Running());
    CHECK_FALSE(fixture.app->Context()->activity.IsValid());
}

TEST_CASE("DVM-150 Runtime exit propagates through guest JNI OnLoad reentry") {
    using namespace ogplay;
    auto libc = LibcElf();
    auto a = AppElf({"liba.so", "libc.so", runtime::kJniVersion1_6, true, true});
    std::array inputs{
        loader::Elf32ModuleInput{"liba.so", a, memory::GuestAddress{0x20000000U}},
        loader::Elf32ModuleInput{"libc.so", libc, memory::GuestAddress{0x10000000U}},
    };
    runtime::VirtualFileSystem filesystem;
    auto session = runtime::AndroidGuestCallSession::Start(
        {19, "liba.so", inputs, {}, 64, 36, UINT64_C(200000), 1, &filesystem, {}});
    loader::ApkNativeLibraryInventory inventory{{Library("liba.so", a)}};
    auto selected = loader::SelectApkNativeLibraries(inventory, loader::AndroidArmAbi::armeabi_v7a);
    runtime::NativeLibraryLoader libraries(session->Process(), selected);
    auto context = std::make_shared<runtime::DexVmAndroidContext>();
    context->session = session.get();
    context->native_libraries = &libraries;
    ogplay::test::BindBootDexArchive(*context);
    core::CapabilityLedger ledger;
    auto catalog = runtime::AndroidIntrinsicCatalog(context);
    auto bridge =
        std::make_unique<runtime::DexVmGuestBridge>(*session, ReadDexFixture("aps5.dex"), catalog, context, ledger,
                                                    nullptr, runtime::DexVmBridgeConfig{}, ogplay::test::ReadBootDex());
    const auto type = bridge->Linker().ResolveDescriptor("Lfixture/Aps5;");
    const auto initialized = bridge->Vm().EnsureClassInitialized(type);
    REQUIRE_FALSE(initialized.exception.IsValid());
    bridge->Vm().SetStaticFieldBits("Lfixture/Aps5;", "exitCode", "I", 37);
    const auto start = bridge->Linker().FindDirectMethod(type, "start", "()V");
    REQUIRE(start.has_value());
    bool returned = false;
    try {
        static_cast<void>(bridge->Vm().Call(*start, {}));
        returned = true;
    } catch (const runtime::dexvm::DexVmError& error) {
        CHECK(error.Reason() == runtime::dexvm::DexVmErrorReason::thread_stopped);
    }
    CHECK_FALSE(returned);
    CHECK(bridge->Vm().ExitCode() == 37);
    CHECK(runtime::SessionExitRequested(*context));
    bridge.reset();
    session->Stop();
    CHECK_FALSE(session->Running());
}

TEST_CASE("DVM-202 teardown isolates pending JNI errors and still stops after cleanup failure") {
    using namespace ogplay;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        OrchestratedApp f("fixture.LauncherActivity", true, false, {}, {}, true, backend);
        f.app->StartApplication();
        static_cast<void>(f.app->StartLauncherActivity());
        auto& bridge = f.app->DexVm();
        auto& vm = bridge.Vm();
        auto& env = bridge.Session().Environment();
        const auto original = vm.MakeThrowable("Ljava/lang/UnsupportedOperationException;", "original failure");
        env.Throw(1, bridge.PublishLocal(original));
        const auto type = vm.Linker().ResolveDescriptor("Lfixture/FailingNativeCleanup;");
        const auto cleanup = vm.Linker().FindDirectMethod(type, "release", "(J)V");
        REQUIRE(cleanup.has_value());
        vm.TrackGuestNativeResource(original, *cleanup, 1);
        CHECK_THROWS_WITH_AS(static_cast<void>(f.app->Stop()),
                             doctest::Contains("native cleanup failed: Ljava/lang/UnsatisfiedLinkError;"),
                             runtime::AndroidGuestProcessError);
        CHECK(f.app->NativeProcess().AttachedJniThreadCount() == 0);
        CHECK_FALSE(env.ExceptionDiagnostics().empty());
        CHECK_NOTHROW(static_cast<void>(f.app->Stop()));
    }
}

TEST_CASE("cooperative onPause can start Java workers before VM shutdown and join is bounded") {
    using namespace ogplay;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        for (const bool timeout : {false, true}) {
            INFO("timeout=" << timeout << " backend=" << static_cast<int>(backend));
            OrchestratedApp f("fixture.ResultCaller", true, false, {}, {}, true, backend);
            f.app->StartApplication();
            REQUIRE(f.app->StartLauncherActivity().state == session::LifecycleRunState::running);
            auto& vm = f.app->DexVm().Vm();
            auto& linker = vm.Linker();
            const auto worker_type = linker.ResolveDescriptor("Lfixture/NeverParkWorker;");
            const auto ctor = linker.FindDirectMethod(worker_type, "<init>", "()V");
            REQUIRE(ctor);
            const auto run = linker.FindVtableIndex(worker_type, "run", "()V");
            REQUIRE(run);
            std::atomic<int> completed{};
            if (!timeout) {
                auto& method = linker.MutableMethod(linker.Class(worker_type).vtable[*run]);
                method.kind = MethodKind::intrinsic;
                method.implementation = [&](IntrinsicContext&) {
                    ++completed;
                    return VmValue::Void();
                };
            }
            const auto activity_type = vm.Model().ObjectClass(f.context->activity);
            for (const auto callback : {"onPause", "onStop", "onDestroy"}) {
                const auto pause = linker.FindVtableIndex(activity_type, callback, "()V");
                REQUIRE(pause);
                auto& method = linker.MutableMethod(linker.Class(activity_type).vtable[*pause]);
                method.kind = MethodKind::intrinsic;
                method.implementation = [&](IntrinsicContext&) {
                    CHECK_FALSE(f.app->DexVm().Threads().ShuttingDown());
                    // The first worker needs native TLS/JNI preparation. With early
                    // admission closure it fails and the second start sees VM shutdown.
                    for (int i = 0; i < 2; ++i) {
                        const auto worker = vm.NewIntrinsicInstance("Lfixture/NeverParkWorker;");
                        const auto roots = vm.ProtectReferences(std::array{worker});
                        REQUIRE_FALSE(vm.Call(*ctor, std::array{VmValue::Ref(worker)}).exception.IsValid());
                        static_cast<void>(
                            runtime::android_intrinsics::CallAndroidMethod(vm, worker, "start", "()V", {}));
                        static_cast<void>(
                            runtime::android_intrinsics::CallAndroidMethod(vm, worker, "join", "()V", {}));
                    }
                    return VmValue::Void();
                };
            }
            if (timeout) {
                CHECK_THROWS_WITH(static_cast<void>(f.app->Stop()),
                                  doctest::Contains("lifecycle execution wall-time budget exhausted"));
                const auto hits = f.ledger.Unimplemented();
                CHECK(std::any_of(hits.begin(), hits.end(),
                                  [](const auto& hit) { return hit.id == "session.lifecycle.cleanup_timeout"; }));
            } else {
                CHECK(f.app->Stop().state == session::LifecycleRunState::stopped);
                CHECK(completed == 6);
            }
            CHECK(f.app->DexVm().Threads().ShuttingDown());
            CHECK_FALSE(f.app->NativeProcess().Running());
            CHECK_NOTHROW(static_cast<void>(f.app->Stop()));
        }
    }
}

TEST_CASE("process close destroys the foreground before shutdown and finish stays per instance") {
    using namespace ogplay;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        for (const int mode : {0, 1, 2}) {
            INFO("mode=" << mode << " backend=" << static_cast<int>(backend));
            OrchestratedApp f("fixture.ResultCaller", true, false, {}, {}, true, backend);
            f.app->StartApplication();
            REQUIRE(f.app->StartLauncherActivity().state == session::LifecycleRunState::running);
            auto& vm = f.app->DexVm().Vm();
            auto& linker = vm.Linker();
            const auto caller = f.context->activity;
            const auto type = vm.Model().ObjectClass(caller);
            const auto slot = linker.FindVtableIndex(type, "onDestroy", "()V");
            REQUIRE(slot.has_value());
            int destroyed{};
            auto& method = linker.MutableMethod(linker.Class(type).vtable[*slot]);
            method.kind = MethodKind::intrinsic;
            method.implementation = [&](IntrinsicContext&) {
                ++destroyed;
                if (mode == 2) throw VmJavaThrow{"Ljava/lang/IllegalStateException;", "shared owner already destroyed"};
                return VmValue::Void();
            };
            const auto intent = vm.NewIntrinsicInstance("Landroid/content/Intent;");
            const auto ctor = linker.FindDirectMethod(vm.Model().ObjectClass(intent), "<init>", "()V");
            REQUIRE(ctor.has_value());
            REQUIRE_FALSE(vm.Call(*ctor, std::array{VmValue::Ref(intent)}).exception.IsValid());
            const auto on = [&](VmObjectRef object, const char* name, const char* signature,
                                std::vector<VmValue> args = {}) {
                return runtime::android_intrinsics::CallAndroidMethod(vm, object, name, signature, args);
            };
            on(intent, "setClassName", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
               {VmValue::Ref(vm.NewStringUtf8("fixture")), VmValue::Ref(vm.NewStringUtf8("fixture.ResultCaller"))});
            on(caller, "startActivity", "(Landroid/content/Intent;)V", {VmValue::Ref(intent)});
            static_cast<void>(f.app->ActivityLifecycle().StepFrame());
            REQUIRE(f.context->activity_stack.size() == 2);
            REQUIRE(f.context->activity != caller);
            if (mode != 0) {
                on(f.context->activity, "finish", "()V");
                if (mode == 2) {
                    CHECK_THROWS_WITH(static_cast<void>(f.app->ActivityLifecycle().StepFrame()),
                                      doctest::Contains("shared owner already destroyed"));
                } else {
                    static_cast<void>(f.app->ActivityLifecycle().StepFrame());
                    CHECK(f.context->activity == caller);
                }
                CHECK(destroyed == 1);
            }
            f.context->video_views[999].guest_path = "retained source";
            f.context->encoded_audio_leases[999] = {};
            REQUIRE(f.context->pcm_playback != nullptr);
            const auto player = f.context->pcm_playback->CreatePlayer({48000, 2, 16}, 2);
            f.context->audio_tracks[999].player = player;
            REQUIRE(f.context->pcm_playback->HasPlayer(player));
            CHECK_NOTHROW(static_cast<void>(f.app->Stop()));
            CHECK(destroyed == (mode == 0 ? 1 : mode == 1 ? 2 : 1));
            CHECK(f.context->video_views.empty());
            CHECK(f.context->encoded_audio_leases.empty());
            CHECK_FALSE(f.context->pcm_playback->HasPlayer(player));
            CHECK(f.context->activity_stack.empty());
            CHECK(f.context->process_stopping);
            CHECK_FALSE(f.app->NativeProcess().Running());
            CHECK(f.app->NativeProcess().AttachedJniThreadCount() == 0);
            CHECK_NOTHROW(static_cast<void>(f.app->Stop()));
            CHECK(destroyed == (mode == 0 ? 1 : mode == 1 ? 2 : 1));
        }
    }
}

TEST_CASE("DVM-236 guest process exit and runtime fault forbid Activity callbacks") {
    using namespace ogplay;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        for (const bool guest_exit : {false, true}) {
            OrchestratedApp f("fixture.ResultCaller", true, false, {}, {}, true, backend);
            f.app->StartApplication();
            REQUIRE(f.app->StartLauncherActivity().state == session::LifecycleRunState::running);
            auto& vm = f.app->DexVm().Vm();
            auto& linker = vm.Linker();
            const auto type = vm.Model().ObjectClass(f.context->activity);
            int callbacks{};
            for (const auto name : {"onPause", "onStop", "onDestroy"}) {
                const auto slot = linker.FindVtableIndex(type, name, "()V");
                REQUIRE(slot.has_value());
                auto& method = linker.MutableMethod(linker.Class(type).vtable[*slot]);
                method.kind = MethodKind::intrinsic;
                method.implementation = [&](IntrinsicContext&) {
                    ++callbacks;
                    return VmValue::Void();
                };
            }
            if (guest_exit) {
                const auto release = linker.FindDirectMethod(linker.ResolveDescriptor("Lfixture/FailingNativeCleanup;"),
                                                             "release", "(J)V");
                REQUIRE(release.has_value());
                vm.TrackGuestNativeResource(f.context->activity, *release, 1);
                const auto process = linker.ResolveDescriptor("Landroid/os/Process;");
                const auto kill = linker.FindDirectMethod(process, "killProcess", "(I)V");
                REQUIRE(kill.has_value());
                CHECK_THROWS_AS(static_cast<void>(vm.Call(*kill, std::array{VmValue::Int(1)})), DexVmError);
                CHECK(vm.ExitCode() == 0);
            }
            CHECK_NOTHROW(static_cast<void>(f.app->Stop(guest_exit ? session::DexProcessStopReason::host_shutdown
                                                                   : session::DexProcessStopReason::runtime_failure)));
            CHECK(callbacks == 0);
            CHECK_FALSE(f.app->NativeProcess().Running());
            CHECK(f.app->NativeProcess().AttachedJniThreadCount() == 0);
        }
    }
}
