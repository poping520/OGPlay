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

TEST_CASE("DVM-195 original NativeActivity runs in the existing Java native process") {
    using namespace ogplay;
    namespace dx = runtime::dexvm;
    for (const auto backend : {dx::InterpreterBackend::switch_dispatch,
                               dx::InterpreterBackend::threaded}) {
        OrchestratedApp fixture("android.app.NativeActivity", true, true, {}, {}, true, backend);
        fixture.app->StartApplication();
        const auto started = fixture.app->StartLauncherActivity();
        REQUIRE(started.state == session::LifecycleRunState::running);
        auto& bridge = fixture.app->DexVm();
        auto& vm = bridge.Vm();
        auto& process = bridge.Session().Process();
        const auto memory = process.GuestMemoryAccess();
        const auto activity = fixture.context->activity;
        const auto pointer = fixture.context->native_activity->InputQueuePointer(activity);
        REQUIRE_FALSE(pointer.IsNull());
        const auto native = memory::GuestAddress{pointer.Value() - 148U};
        const auto read = [&](memory::GuestAddress address) {
            std::array<std::byte, 4> bytes{}; memory.read(address, bytes);
            std::uint32_t value{};
            for (std::size_t i=0; i<4; ++i) value |= std::to_integer<std::uint32_t>(bytes[i]) << (8*i);
            return value;
        };
        const auto write = [&](memory::GuestAddress address, std::uint32_t value) {
            std::array<std::byte, 4> bytes{};
            for (std::size_t i=0; i<4; ++i) bytes[i] = static_cast<std::byte>(value >> (8*i));
            memory.write(address, bytes);
        };
        CHECK(read(native.Add(4)) == bridge.Session().GuestJavaVm().Value());
        CHECK(read(native.Add(8)) == bridge.Session().GuestEnvironment().Value());
        CHECK(read(native.Add(24)) == 19);
        const runtime::JniReference actual_activity(read(native.Add(12)));
        CHECK(bridge.FromReference(actual_activity) == activity);
        const auto asset_manager = runtime::android_intrinsics::CallAndroidMethod(vm, activity, "getAssets", "()Landroid/content/res/AssetManager;").ref;
        const auto asset_reference = bridge.PublishLocal(asset_manager);
        const auto native_symbols = runtime::detail::BuildAndroidBoundarySymbols();
        const runtime::BionicHleSymbolProvider asset_provider(native_symbols);
        const auto convert = asset_provider.Lookup("libandroid.so", "AAssetManager_fromJava");
        REQUIRE(convert.has_value());
        const auto native_manager = process.Invoke({*convert,
            {process.GuestEnvironment().Value(), asset_reference.Value(), 0, 0}, {}}).return_value;
        CHECK(read(native.Add(32)) == native_manager);
        process.Environment().DeleteLocalRef(1, asset_reference);
        CHECK(read(native.Add(160+8)) == 64); CHECK(read(native.Add(160+12)) == 36);
        CHECK(read(native.Add(108)) == 1); // actual guest JNI invocation
        CHECK(fixture.context->window_input_queue.IsValid());
        CHECK(fixture.context->active_surface_holders.contains(fixture.context->window_surface_holder.Value()));
        const auto trace = memory.allocate(20);
        write(trace, 0); write(trace.Add(4), 0); write(trace.Add(8), 0);
        write(native.Add(28), trace.Value());
        const auto call = [&](const char* name, const char* signature, std::vector<dx::VmValue> args = {}) {
            return runtime::android_intrinsics::CallAndroidMethod(vm, activity, name, signature, std::move(args));
        };
        const auto bundle = runtime::android_intrinsics::NewAndroidBundle(vm);
        static_cast<void>(call("onSaveInstanceState", "(Landroid/os/Bundle;)V", {dx::VmValue::Ref(bundle)}));
        CHECK(read(trace) == 0); // unregistered save callback
        const auto state = memory.allocate(4);
        constexpr std::array state_bytes{std::byte{'a'}, std::byte{'b'}, std::byte{'c'}, std::byte{0}};
        memory.write(state, state_bytes);
        write(trace.Add(12), state.Value()); write(trace.Add(16), 3);
        const auto entry = process.FindModuleExport(fixture.app->NativeLibraries()->Records().front().module_index, "ANativeActivity_onCreate");
        write(memory::GuestAddress{read(native)}.Add(8), entry.Add(256).Value());
        static_cast<void>(call("onSaveInstanceState", "(Landroid/os/Bundle;)V", {dx::VmValue::Ref(bundle)}));
        const auto saved = runtime::android_intrinsics::CallAndroidMethod(vm, bundle, "getByteArray", "(Ljava/lang/String;)[B",
            {dx::VmValue::Ref(vm.NewStringUtf8("android:native_state"))}).ref;
        CHECK(vm.Model().ReadByteRegion(saved, 0, 3) == std::vector<std::byte>(state_bytes.begin(), state_bytes.begin()+3));
        CHECK(read(state) == 0); // fixture guest free ran after the copy
        write(trace.Add(16), 1024U*1024U+1U);
        try {
            static_cast<void>(call("onSaveInstanceState", "(Landroid/os/Bundle;)V", {dx::VmValue::Ref(bundle)}));
            FAIL("oversized NativeActivity state unexpectedly succeeded");
        } catch (const dx::VmJavaThrow& error) {
            CHECK(error.descriptor == "Ljava/lang/IllegalStateException;");
            CHECK(error.message == "NativeActivity saved state exceeds budget");
        }
        write(memory::GuestAddress{read(native)}.Add(8), 0);
        memory.release(state, 4);
        static_cast<void>(fixture.app->ActivityLifecycle().Suspend());
        CHECK(read(trace.Add(4)) == 3);
        static_cast<void>(fixture.app->ActivityLifecycle().Resume());
        CHECK(read(trace.Add(4)) == 6); // resume then focus
        static_cast<void>(call("onLowMemory", "()V"));
        CHECK(read(trace.Add(4)) == 15);
        const auto configuration = vm.NewIntrinsicInstance("Landroid/content/res/Configuration;");
        static_cast<void>(call("onConfigurationChanged", "(Landroid/content/res/Configuration;)V", {dx::VmValue::Ref(configuration)}));
        CHECK(read(trace.Add(4)) == 14);
        static_cast<void>(call("onWindowFocusChanged", "(Z)V", {dx::VmValue::Int(1)}));
        CHECK(read(trace.Add(4)) == 6); CHECK(read(trace.Add(8)) == 1);
        // Releasing and reacquiring the Surface destroys the old ownership once.
        runtime::SetWindowSurfaceCallback(vm, *fixture.context, dx::VmObjectRef{});
        CHECK(read(trace.Add(4)) == 10);
        const auto old_window = read(trace.Add(8));
        const auto count = read(trace);
        runtime::SetWindowSurfaceCallback(vm, *fixture.context, dx::VmObjectRef{});
        CHECK(read(trace) == count);
        runtime::SetWindowSurfaceCallback(vm, *fixture.context, activity);
        CHECK(read(trace) == count + 3); CHECK(read(trace.Add(4)) == 9);
        CHECK(read(trace.Add(8)) != old_window);
        runtime::SetWindowInputCallback(vm, *fixture.context, dx::VmObjectRef{});
        CHECK(read(trace.Add(4)) == 12);
        CHECK_FALSE(fixture.context->window_input_queue.IsValid());
        runtime::SetWindowInputCallback(vm, *fixture.context, activity);
        CHECK(read(trace.Add(4)) == 11);
        static_cast<void>(fixture.app->Stop());
        CHECK(read(trace.Add(4)) == 4); // native onStop; process close does not call Java onDestroy
        CHECK(fixture.context->native_activity->InputQueuePointer(activity).IsNull());
        CHECK_FALSE(memory.validate(native, 4));
        CHECK_THROWS(static_cast<void>(bridge.FromReference(actual_activity)));
        memory.release(trace, 20);
    }
}

TEST_CASE("NativeActivity failed lifecycle releases backing before JNI detach") {
    using namespace ogplay;
    namespace dx = runtime::dexvm;
    for (const auto backend : {dx::InterpreterBackend::switch_dispatch,
                               dx::InterpreterBackend::threaded}) {
        OrchestratedApp fixture("android.app.NativeActivity", true, true, {}, {}, true, backend);
        fixture.app->StartApplication();
        auto& linker = fixture.app->DexVm().Linker();
        const auto type = linker.ResolveDescriptor("Landroid/app/NativeActivity;");
        const auto slot = linker.FindVtableIndex(type, "onStart", "()V");
        REQUIRE(slot.has_value());
        auto& on_start = linker.MutableMethod(linker.Class(type).vtable[*slot]);
        on_start.kind = dx::MethodKind::intrinsic;
        on_start.implementation = [](dx::IntrinsicContext&) -> dx::VmValue {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "original startup failure"};
        };
        try {
            static_cast<void>(fixture.app->StartLauncherActivity());
            FAIL("injected startup failure did not propagate");
        } catch (const session::DexActivityLifecycleError& error) {
            CHECK(std::string(error.what()).find("original startup failure") != std::string::npos);
        }
        const auto activity = fixture.context->activity;
        auto* native_runtime = fixture.context->native_activity.get();
        const auto pointer = native_runtime->InputQueuePointer(activity);
        REQUIRE_FALSE(pointer.IsNull());
        const auto memory = fixture.app->NativeProcess().GuestMemoryAccess();
        const auto native = memory::GuestAddress{pointer.Value() - 148U};
        REQUIRE(memory.validate(native, 4));
        // Failed startup skips Java onDestroy with a live native handle.
        CHECK_NOTHROW(static_cast<void>(fixture.app->Stop()));
        CHECK(native_runtime->InputQueuePointer(activity).IsNull());
        CHECK_FALSE(memory.validate(native, 4));
        CHECK(fixture.app->NativeProcess().AttachedJniThreadCount() == 0U);
        CHECK_NOTHROW(native_runtime->Release());
        CHECK_NOTHROW(static_cast<void>(fixture.app->Stop()));
        // Unwinding must preserve the startup error instead of terminating.
        try {
            auto app = std::move(fixture.app);
            throw session::DexActivityLifecycleError("original startup failure");
        } catch (const session::DexActivityLifecycleError& error) {
            CHECK(std::string(error.what()) == "original startup failure");
            CHECK_FALSE(fixture.context->native_activity);
        }
    }
}

TEST_CASE("DVM-195 NativeActivity library load defers JNI OnLoad until Java requests it") {
    using namespace ogplay;
    FixtureProcess fixture;
    loader::ApkNativeLibraryInventory inventory(std::vector{Library("liba.so",
        AppElf({"liba.so", "libc.so", runtime::kJniVersion1_6}))});
    const auto selected = loader::SelectApkNativeLibraries(inventory, loader::AndroidArmAbi::armeabi_v7a);
    runtime::NativeLibraryLoader libraries(*fixture.process, selected);
    const auto native = libraries.LoadPath("/data/app-lib/liba.so", 1, runtime::NativeLibraryEntry::native_activity);
    CHECK_FALSE(native.jni_version.has_value());
    CHECK(libraries.Records().front().jni_on_load_calls == 0);
    const auto java = libraries.LoadLibrary("a", 1);
    CHECK(java.handle == native.handle);
    CHECK(java.module_index == native.module_index);
    CHECK(java.jni_version == runtime::kJniVersion1_6);
    CHECK(libraries.Records().front().jni_on_load_calls == 1);
    CHECK(libraries.LoadLibrary("a", 1).already_loaded);
}

TEST_CASE("DVM-195 NativeActivity missing libraries and entry points fail without native handles") {
    for (const bool missing_library : {false, true}) {
        OrchestratedApp fixture("android.app.NativeActivity");
        auto& metadata = fixture.context->activity_components.front().meta_data;
        metadata.push_back({missing_library ? "android.app.lib_name" : "android.app.func_name", std::string("missing")});
        fixture.app->StartApplication();
        CHECK_THROWS_WITH_AS(static_cast<void>(fixture.app->StartLauncherActivity()),
            doctest::Contains(missing_library ? "Unable to find native library: missing" : "missing"), std::exception);
        CHECK(fixture.context->native_activity->InputQueuePointer(fixture.context->activity).IsNull());
    }
}

TEST_CASE("DVM-195 takeSurface null before traversal suppresses native surface callbacks") {
    using namespace ogplay;
    OrchestratedApp fixture("fixture.NativeTakeoverActivity");
    fixture.app->StartApplication();
    REQUIRE(fixture.app->StartLauncherActivity().state == session::LifecycleRunState::running);
    CHECK_FALSE(fixture.context->window_surface_callback.IsValid());
    CHECK(fixture.context->active_surface_holders.empty());
    CHECK(fixture.context->content_view.IsValid());
    CHECK(fixture.context->window_input_queue.IsValid());
    CHECK(fixture.app->Stop().state == session::LifecycleRunState::stopped);
}
