#include "ogplay/runtime/integration/bitmap_pixels.h"
#include "runtime/boundary/core/boundary_symbols.h"
#include "ogplay/runtime/dexvm/big_int_runtime.h"
#include <set>
#include "ogplay/runtime/integration/native_activity_runtime.h"
#include "ogplay/runtime/dexvm/nio_runtime.h"
#include "runtime/integration/dexvm_android/shared.h"
#include "../fixtures/bitmap_decode_data.h"
#include <chrono>
#include <cstdlib>
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

TEST_CASE("native library path lookup is side effect free and returns loadable APK and system paths") {
    using namespace ogplay;
    FixtureProcess fixture;
    auto other_abi = Library("liba.so", AppElf({"liba.so", "libwrongabi.so", std::nullopt}));
    other_abi.abi = loader::AndroidArmAbi::armeabi;
    loader::ApkNativeLibraryInventory inventory{{
        Library("liba.so", AppElf({"liba.so", "", std::nullopt})),
        std::move(other_abi),
    }};
    const auto selected = loader::SelectApkNativeLibraries(inventory, loader::AndroidArmAbi::armeabi_v7a);
    auto system_image = AppElf({"liba.so", "", std::nullopt});
    auto bundled_image = AppElf({"libbundled.so", "", std::nullopt});
    const runtime::BionicModuleSource system[]{{"liba.so", system_image}, {"libbundled.so", bundled_image}};
    runtime::NativeLibraryLoader libraries(*fixture.process, selected, runtime::SelectBionicProfile(19), system);
    const auto count = fixture.process->ApplicationModuleCount();
    const auto apk_path = libraries.FindLibraryPath("a");
    const auto system_path = libraries.FindLibraryPath("bundled");
    REQUIRE(apk_path == "/data/app-lib/liba.so");
    REQUIRE(system_path == "/system/lib/libbundled.so");
    CHECK_FALSE(libraries.FindLibraryPath("missing"));
    for (const auto name : {"", "../a", "a/b", "a\\b", "/system/lib/liba.so"}) {
        CHECK_FALSE(libraries.FindLibraryPath(name));
    }
    CHECK_FALSE(libraries.FindLibraryPath(std::string_view{"a\0suffix", 8}));
    CHECK(libraries.Records().empty());
    CHECK(fixture.process->ApplicationModuleCount() == count);

    const auto access = fixture.process->GuestMemoryAccess();
    const auto symbols = runtime::detail::BuildAndroidBoundarySymbols();
    const runtime::BionicHleSymbolProvider provider(symbols);
    for (const auto& path : {*apk_path, *system_path}) {
        const auto records_before = libraries.Records().size();
        const auto scratch = access.allocate(static_cast<std::uint32_t>(path.size() + 1U));
        std::vector<std::byte> bytes(path.size() + 1U);
        for (std::size_t i = 0; i < path.size(); ++i)
            bytes[i] = static_cast<std::byte>(path[i]);
        access.write(scratch, bytes);
        runtime::A32GuestCallFrame frame{*provider.Lookup("libdl.so", "dlopen"), {scratch.Value(), 1U, 0U, 0U}, {}};
        CHECK(fixture.process->Invoke(frame).return_value != 0U);
        access.release(scratch, static_cast<std::uint32_t>(bytes.size()));
        CHECK(libraries.Records().size() == records_before);
        CHECK(libraries.LoadPath(path, 7U).canonical_path == path);
    }
}

TEST_CASE("native library loader appends dependency constructors and one explicit JNI_OnLoad") {
    using namespace ogplay;
    FixtureProcess fixture;
    loader::ApkNativeLibraryInventory inventory{{
        Library("liba.so", AppElf({"liba.so", "libb.so", 0x00010006U})),
        Library("libb.so", AppElf({"libb.so", "", 0x00010004U})),
        Library("libplain.so", AppElf({"libplain.so", "", std::nullopt})),
    }};
    const auto selected = loader::SelectApkNativeLibraries(inventory, loader::AndroidArmAbi::armeabi_v7a);
    runtime::NativeLibraryLoader libraries(*fixture.process, selected);

    const auto first = libraries.LoadLibrary("a", 7U);
    CHECK(first.initialized_modules == std::vector<std::string>{"libb.so", "liba.so"});
    CHECK(first.jni_version == 0x00010006U);
    CHECK(fixture.process->ApplicationModuleCount() == 2U);
    CHECK(fixture.process->HasLoadedModule("liba.so"));
    CHECK(fixture.process->HasLoadedModule("libb.so"));

    const auto repeated = libraries.LoadPath(runtime::NativeLibraryLoader::SyntheticGuestPath("liba.so"), 7U);
    CHECK(repeated.handle == first.handle);
    CHECK(repeated.already_loaded);
    CHECK_FALSE(repeated.recursive);
    CHECK(repeated.initialized_modules.empty());
    const auto registry_snapshot = libraries.TrySnapshot();
    REQUIRE(registry_snapshot);
    CHECK(registry_snapshot->total == libraries.Records().size());
    auto records = libraries.Records();
    REQUIRE(records.size() == 1U);
    CHECK(records[0].jni_on_load_calls == 1U);

    const auto dependency = libraries.LoadLibrary("b", 7U);
    CHECK(dependency.initialized_modules.empty());
    CHECK(dependency.jni_version == 0x00010004U);
    records = libraries.Records();
    REQUIRE(records.size() == 2U);
    CHECK(records[0].jni_on_load_calls == 1U);
    CHECK(records[1].jni_on_load_calls == 1U);
    const auto plain = libraries.LoadLibrary("plain", 7U);
    CHECK_FALSE(plain.jni_version.has_value());
    records = libraries.Records();
    REQUIRE(records.size() == 3U);
    CHECK(records[2].jni_on_load_calls == 0U);
    CHECK_THROWS_WITH_AS(static_cast<void>(libraries.LoadLibrary("a", 8U)),
                         "native library is already associated with another ClassLoader: "
                         "/data/app-lib/liba.so",
                         runtime::NativeLibraryLoadError);

    fixture.process->Stop();
    CHECK_FALSE(fixture.process->Running());
    CHECK(fixture.process->AttachedJniThreadCount() == 0U);
}

TEST_CASE("native library loader resolves Bionic dependencies after rootless startup") {
    using namespace ogplay;
    FixtureProcess fixture;
    auto application = AppElf({"libusesystem.so", "libstdc++.so", runtime::kJniVersion1_6});
    auto libstdcpp = AppElf({"libstdc++.so", "", std::nullopt});
    loader::ApkNativeLibraryInventory inventory{{
        Library("libusesystem.so", application),
    }};
    const auto selected = loader::SelectApkNativeLibraries(inventory, loader::AndroidArmAbi::armeabi_v7a);
    const runtime::BionicModuleSource system[]{{"libstdc++.so", libstdcpp}};
    runtime::NativeLibraryLoader libraries(*fixture.process, selected, runtime::SelectBionicProfile(19), system);

    const auto loaded = libraries.LoadLibrary("usesystem", 7U);
    CHECK(loaded.initialized_modules == std::vector<std::string>{"libstdc++.so", "libusesystem.so"});
    CHECK(fixture.process->HasLoadedModule("libstdc++.so"));
    CHECK(fixture.process->HasLoadedModule("libusesystem.so"));
    CHECK(loaded.jni_version == runtime::kJniVersion1_6);
}

TEST_CASE("native library loader resolves APK modules by inventory or ELF SONAME") {
    using namespace ogplay;
    FixtureProcess fixture;
    loader::ApkNativeLibraryInventory inventory{{
        Library("libconsumer-alias.so", AppElf({"libconsumer-alias.so", "Tales.final.so", std::nullopt})),
        Library("libconsumer-inventory.so", AppElf({"libconsumer-inventory.so", "libTales.final.so", std::nullopt})),
        Library("libTales.final.so", AppElf({"Tales.final.so", "", std::nullopt})),
    }};
    const auto selected = loader::SelectApkNativeLibraries(inventory, loader::AndroidArmAbi::armeabi_v7a);
    runtime::NativeLibraryLoader libraries(*fixture.process, selected);

    SUBCASE("explicit load keeps the inventory identity") {
        const auto loaded = libraries.LoadLibrary("Tales.final", 3U);
        CHECK(loaded.initialized_modules == std::vector<std::string>{"libTales.final.so"});
        REQUIRE(libraries.Records().size() == 1U);
        CHECK(libraries.Records()[0].soname == "libTales.final.so");
        CHECK(fixture.process->HasLoadedModule("libTales.final.so"));
        CHECK(fixture.process->HasLoadedModule("Tales.final.so"));
    }

    SUBCASE("DT_NEEDED resolves the ELF SONAME alias") {
        const auto loaded = libraries.LoadLibrary("consumer-alias", 3U);
        CHECK(loaded.initialized_modules == std::vector<std::string>{"libTales.final.so", "libconsumer-alias.so"});
        CHECK(fixture.process->ApplicationModuleCount() == 2U);
    }

    SUBCASE("DT_NEEDED resolves the inventory basename") {
        const auto loaded = libraries.LoadLibrary("consumer-inventory", 3U);
        CHECK(loaded.initialized_modules == std::vector<std::string>{"libTales.final.so", "libconsumer-inventory.so"});
        CHECK(fixture.process->ApplicationModuleCount() == 2U);
    }
}

TEST_CASE("native library loader keeps malformed unresolved and bad JNI failures stable") {
    using namespace ogplay;
    FixtureProcess fixture;
    std::vector<std::byte> malformed(4, std::byte{});
    loader::ApkNativeLibraryInventory inventory{{
        Library("libbad.so", std::move(malformed)),
        Library("libmissing.so", AppElf({"libmissing.so", "libabsent.so", std::nullopt})),
        Library("libversion.so", AppElf({"libversion.so", "", 0x00090009U})),
        Library("libalias.so", AppElf({"libother.so", "", std::nullopt})),
    }};
    const auto selected = loader::SelectApkNativeLibraries(inventory, loader::AndroidArmAbi::armeabi_v7a);
    runtime::NativeLibraryLoader libraries(*fixture.process, selected);

    try {
        static_cast<void>(libraries.LoadLibrary("bad", 3U));
        FAIL("malformed ELF load unexpectedly succeeded");
    } catch (const runtime::NativeLibraryLoadError& error) {
        CHECK(error.Reason() == runtime::NativeLibraryLoadErrorReason::malformed_elf);
    }
    try {
        static_cast<void>(libraries.LoadLibrary("missing", 3U));
        FAIL("unresolved dependency load unexpectedly succeeded");
    } catch (const runtime::NativeLibraryLoadError& error) {
        CHECK(error.Reason() == runtime::NativeLibraryLoadErrorReason::unresolved_dependency);
        const auto first_message = std::string(error.what());
        try {
            static_cast<void>(libraries.LoadLibrary("missing", 3U));
            FAIL("failed library retry unexpectedly succeeded");
        } catch (const runtime::NativeLibraryLoadError& repeated) {
            CHECK(repeated.Reason() == error.Reason());
            CHECK(repeated.what() == first_message);
        }
    }
    try {
        static_cast<void>(libraries.LoadLibrary("version", 3U));
        FAIL("invalid JNI version unexpectedly succeeded");
    } catch (const runtime::NativeLibraryLoadError& error) {
        CHECK(error.Reason() == runtime::NativeLibraryLoadErrorReason::invalid_jni_version);
    }
    const auto alias = libraries.LoadLibrary("alias", 3U);
    CHECK(alias.initialized_modules == std::vector<std::string>{"libalias.so"});
    CHECK(fixture.process->HasLoadedModule("libalias.so"));
    CHECK(fixture.process->HasLoadedModule("libother.so"));
    CHECK_THROWS_AS(static_cast<void>(libraries.LoadPath("C:/host/libbad.so", 3U)), runtime::NativeLibraryLoadError);
    CHECK(libraries.Records().size() == 4U);
}

TEST_CASE("DexVM System load APIs support nested JNI OnLoad Java reentry") {
    using namespace ogplay;
    auto backend = runtime::dexvm::InterpreterBackend::switch_dispatch;
    SUBCASE("switch interpreter") {
        backend = runtime::dexvm::InterpreterBackend::switch_dispatch;
    }
    SUBCASE("threaded interpreter") {
        backend = runtime::dexvm::InterpreterBackend::threaded;
    }
    auto libc = LibcElf();
    auto a = AppElf({"liba.so", "libc.so", runtime::kJniVersion1_6, true, true});
    auto b = AppElf({"libb.so", "", runtime::kJniVersion1_4});
    auto path = AppElf({"libPath.so", "", runtime::kJniVersion1_4});
    auto vfs = AppElf({"libvfs.so", "", runtime::kJniVersion1_6});
    auto bundled = AppElf({"libbundled.so", "", std::nullopt});
    std::array module_inputs{
        loader::Elf32ModuleInput{"liba.so", a, memory::GuestAddress{0x20000000U}},
        loader::Elf32ModuleInput{"libc.so", libc, memory::GuestAddress{0x10000000U}},
    };
    runtime::VirtualFileSystem filesystem;
    filesystem.PutFile("/data/data/fixture/files/libvfs.so", vfs, false);
    filesystem.AddPathAlias("/data/data/fixture/lib", "/data/app-lib");
    auto session = runtime::AndroidGuestCallSession::Start(
        {19, "liba.so", module_inputs, {}, 64, 36, UINT64_C(200000), 1, &filesystem, {}});
    loader::ApkNativeLibraryInventory inventory{{
        Library("liba.so", a),
        Library("libb.so", b),
        Library("libPath.so", path),
    }};
    const auto selected = loader::SelectApkNativeLibraries(inventory, loader::AndroidArmAbi::armeabi_v7a);
    const runtime::BionicModuleSource system[]{{"libbundled.so", bundled}};
    runtime::NativeLibraryLoader libraries(session->Process(), selected, runtime::SelectBionicProfile(19), system);
    auto context = std::make_shared<runtime::DexVmAndroidContext>();
    test::BindBootDexArchive(*context);
    context->session = session.get();
    context->native_libraries = &libraries;
    core::CapabilityLedger ledger;
    auto catalog = runtime::AndroidIntrinsicCatalog(context);
    auto bridge = std::make_unique<runtime::DexVmGuestBridge>(
        *session, ReadDexFixture("aps5.dex"), catalog, context, ledger, nullptr,
        runtime::DexVmBridgeConfig{.interpreter = {.backend = backend}}, test::ReadBootDex());
    context->threads = &bridge->Threads();

    const auto aps5_class = bridge->Linker().FindClass("Lfixture/Aps5;");
    REQUIRE(aps5_class.has_value());
    const auto method = [&](const std::string_view name, const std::string_view descriptor) {
        const auto found = bridge->Linker().FindDirectMethod(*aps5_class, std::string(name), std::string(descriptor));
        REQUIRE(found.has_value());
        return *found;
    };
    const auto expect_link_error = [&](const runtime::dexvm::VmCallOutcome& outcome) {
        REQUIRE(outcome.exception.IsValid());
        CHECK(bridge->Linker().Class(outcome.exception_class).descriptor == "Ljava/lang/UnsatisfiedLinkError;");
    };

    const auto nested = bridge->Vm().Call(method("start", "()V"), {});
    REQUIRE_MESSAGE(!nested.exception.IsValid(), nested.exception_message);
    auto records = libraries.Records();
    REQUIRE(records.size() == 2U);
    CHECK(records[0].soname == "liba.so");
    CHECK(records[0].jni_on_load_calls == 1U);
    CHECK(records[1].soname == "libb.so");
    CHECK(records[1].jni_on_load_calls == 1U);

    const auto load_path = method("loadPath", "(Ljava/lang/String;)V");
    const auto call_with_string = [&](const runtime::dexvm::VmMethodId target, const std::string& value) {
        const std::array arguments{runtime::dexvm::VmValue::Ref(bridge->Vm().NewStringUtf8(value))};
        return bridge->Vm().Call(target, arguments);
    };
    const auto alias_result = call_with_string(load_path, "/data/data/fixture/lib/libPath.so");
    REQUIRE_MESSAGE(!alias_result.exception.IsValid(), alias_result.exception_message);
    const auto mixed_case = call_with_string(load_path, "/DATA/DATA/FIXTURE/LIB/LIBPATH.SO");
    REQUIRE_MESSAGE(!mixed_case.exception.IsValid(), mixed_case.exception_message);
    const auto logical_result = call_with_string(method("loadLibrary", "(Ljava/lang/String;)V"), "Path");
    REQUIRE_MESSAGE(!logical_result.exception.IsValid(), logical_result.exception_message);
    records = libraries.Records();
    const auto alias_found = std::ranges::find(records, "libPath.so", &runtime::NativeLibraryRecord::soname);
    REQUIRE(alias_found != records.end());
    const auto alias_record = *alias_found;
    CHECK(alias_record.canonical_path == "/data/app-lib/libPath.so");
    CHECK(libraries.LoadLibrary("Path", alias_record.class_loader).handle == alias_record.handle);
    const auto resolved_path = libraries.FindLibraryPath("Path");
    REQUIRE(resolved_path.has_value());
    const auto path_result = call_with_string(load_path, *resolved_path);
    REQUIRE_MESSAGE(!path_result.exception.IsValid(), path_result.exception_message);

    const auto vfs_result = call_with_string(load_path, "/data/data/fixture/files/./LIBVFS.so");
    REQUIRE_MESSAGE(!vfs_result.exception.IsValid(), vfs_result.exception_message);
    const auto vfs_repeated = call_with_string(load_path, "/data/data/fixture/files/libvfs.so");
    REQUIRE_MESSAGE(!vfs_repeated.exception.IsValid(), vfs_repeated.exception_message);

    const auto load_library = method("loadLibrary", "(Ljava/lang/String;)V");
    expect_link_error(call_with_string(load_library, "missing"));
    expect_link_error(call_with_string(load_path, "/data/data/fixture/lib/libmissing.so"));
    expect_link_error(call_with_string(load_path, "/data/data/fixture/lib/../lib/libPath.so"));
    try {
        static_cast<void>(libraries.LoadPath("/data/data/fixture/lib/libmissing.so", 0U));
        FAIL("missing alias library must fail");
    } catch (const runtime::NativeLibraryLoadError& error) {
        CHECK(error.Reason() == runtime::NativeLibraryLoadErrorReason::library_not_found);
    }
    try {
        static_cast<void>(libraries.LoadPath("/data/data/fixture/lib/../lib/libPath.so", 0U));
        FAIL("alias traversal must fail");
    } catch (const runtime::NativeLibraryLoadError& error) {
        CHECK(error.Reason() == runtime::NativeLibraryLoadErrorReason::invalid_request);
    }
    expect_link_error(call_with_string(load_path, "/data/local/tmp/libmissing.so"));

    records = libraries.Records();
    REQUIRE(records.size() == 4U);
    for (const auto& record : records)
        CHECK(record.jni_on_load_calls == 1U);
    CHECK(std::ranges::count(records, "libPath.so", &runtime::NativeLibraryRecord::soname) == 1);
    CHECK(records[3].canonical_path == "/data/data/fixture/files/libvfs.so");
    CHECK(records[3].soname == "libvfs.so");
    CHECK(records[3].jni_on_load_calls == 1U);

    const auto system_path = libraries.FindLibraryPath("bundled");
    REQUIRE(system_path.has_value());
    const auto system_result = call_with_string(load_path, *system_path);
    REQUIRE_MESSAGE(!system_result.exception.IsValid(), system_result.exception_message);
    CHECK(session->Process().HasLoadedModule("libbundled.so"));

    bridge.reset();
    session->Stop();
    CHECK_FALSE(session->Running());
}

// The observer runs inside a registered native while the root call mutex is
// held. The worker holds the VM lock before entering the real System handler;
// root JNI clinit must progress while that handler waits for the call mutex.
TEST_CASE("System loadLibrary releases VM lock for concurrent native JNI class initialization") {
    using namespace ogplay;
    using namespace runtime;
    using namespace runtime::dexvm;
    auto libc = LibcElf();
    auto root = AppElf({"libroot.so", "libc.so", kJniVersion1_6});
    const std::array native_code{0xe92d4080U, 0xe3a070e0U, 0xef000000U, 0xe8bd8080U};
    for (std::size_t i = 0; i < native_code.size(); ++i)
        Put32(root, 0x1080U + i * 4U, native_code[i]);
    const std::array inputs{
        loader::Elf32ModuleInput{"libroot.so", root, memory::GuestAddress{0x20000000U}},
        loader::Elf32ModuleInput{"libc.so", libc, memory::GuestAddress{0x10000000U}},
    };
    VirtualFileSystem filesystem;
    std::promise<void> native_entered, worker_entered, finished;
    auto native_ready = native_entered.get_future();
    auto worker_ready = worker_entered.get_future();
    auto done = finished.get_future();
    std::atomic<bool> armed{false};
    bool initialized = false;
    DexVmGuestBridge* active_bridge{};
    AndroidGuestCallSession* active_session{};
    JniObjectIdentity owner_identity{};
    AndroidGuestCallSessionRequest request{19, "libroot.so", inputs, {}, 64, 36, UINT64_C(200000), 1, &filesystem, {}};
    request.guest_call_slice_observer_thread = std::this_thread::get_id();
    request.guest_call_slice_observer = [&](std::uint64_t) {
        if (!armed.exchange(false)) return;
        native_entered.set_value();
        worker_ready.wait();
        initialized = active_session->Fields().EnsureClassInitialized(owner_identity, 1U);
        CHECK(active_session->Classes().GetFieldId(owner_identity, "exitCode", "I", true));
        CHECK_FALSE(active_bridge->Vm().ExecutionLock().HeldByCurrentThread());
    };
    auto session = AndroidGuestCallSession::Start(request);
    active_session = session.get();
    loader::ApkNativeLibraryInventory inventory{{
        Library("libb.so", AppElf({"libb.so", "", kJniVersion1_6})),
    }};
    const auto selected = loader::SelectApkNativeLibraries(inventory, loader::AndroidArmAbi::armeabi_v7a);
    NativeLibraryLoader libraries(session->Process(), selected);
    auto context = std::make_shared<DexVmAndroidContext>();
    test::BindBootDexArchive(*context);
    context->session = session.get();
    context->native_libraries = &libraries;
    core::CapabilityLedger ledger;
    auto bridge =
        std::make_unique<DexVmGuestBridge>(*session, ReadDexFixture("aps5.dex"), AndroidIntrinsicCatalog(context),
                                           context, ledger, nullptr, DexVmBridgeConfig{}, test::ReadBootDex());
    active_bridge = bridge.get();
    context->threads = &bridge->Threads();
    auto& vm = bridge->Vm();
    const auto owner = bridge->Linker().ResolveDescriptor("Lfixture/Aps5;");
    owner_identity = *bridge->RegisteredClassIdentity(owner);
    const auto fail = bridge->Linker().FindDirectMethod(owner, "fail", "()V");
    REQUIRE(fail);
    const auto module = session->Process().LoadApplicationModules("libroot.so", {});
    const auto target = session->Process().FindModuleExport(module.root_module_index, "JNI_OnLoad").Add(0x80U);
    const std::array methods{JniNativeMethod{"fail", "()V", target}};
    session->Natives().RegisterNatives(owner_identity, methods);
    const auto system = session->Classes().FindClass("java/lang/System");
    REQUIRE(system);
    const auto load = session->Classes().GetMethodId(*system, "loadLibrary", "(Ljava/lang/String;)V", true);
    REQUIRE(load);
    JniJavaVm java_vm(session->Environment());
    REQUIRE(java_vm.AttachCurrentThread(5U, kJniVersion1_6).status == JniStatus::ok);
    const auto name = vm.NewStringUtf8("b");
    const auto name_ref = session->Environment().PublishLocalObject(5U, bridge->Model().ToIdentity(name));
    // A broken lock order must fail within a bound instead of hanging the suite.
    std::thread watchdog([&] {
        if (done.wait_for(std::chrono::seconds(10)) != std::future_status::ready) std::abort();
    });
    auto worker = std::async(std::launch::async, [&] {
        native_ready.wait();
        VmExecutionLockScope lock(vm.ExecutionLock());
        worker_entered.set_value();
        const std::array<JniValue, 1> arguments{name_ref};
        static_cast<void>(
            session->Invocations().InvokeStatic(5U, *system, *load, arguments, JniArgumentSource::value_array));
        CHECK(vm.ExecutionLock().HeldByCurrentThread());
        CHECK_FALSE(session->Environment().ExceptionCheck(5U));
    });
    armed = true;
    const auto outcome = vm.Call(*fail, {});
    worker.get();
    finished.set_value();
    watchdog.join();
    REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
    CHECK(initialized);
    REQUIRE(libraries.Records().size() == 1U);
    CHECK(libraries.Records()[0].jni_on_load_calls == 1U);
    CHECK(java_vm.DetachCurrentThread(5U) == JniStatus::ok);
}

TEST_CASE("native load failures restore recursive VM execution ownership") {
    using namespace ogplay::runtime;
    using namespace ogplay::runtime::dexvm;
    ApplicationProcess fixture;
    auto& vm = fixture.bridge->Vm();
    VmExecutionLockScope first(vm.ExecutionLock());
    VmExecutionLockScope second(vm.ExecutionLock());
    const std::array arguments{
        VmValue::Ref(vm.NewStringUtf8("/data/app-lib/libmissing.so")),
        VmValue::Ref(vm.NewStringUtf8("ANativeActivity_onCreate")),
    };
    IntrinsicContext call{vm, VmObjectRef{}, arguments};
    bool released = false;
    vm.ExecutionLock().SetBlockingObserver(&released, [](void* state, std::thread::id, bool blocked) noexcept {
        if (blocked) *static_cast<bool*>(state) = true;
    });

    struct ClearObserver final {
        VmExecutionLock& lock;

        ~ClearObserver() { lock.SetBlockingObserver(nullptr, nullptr); }
    } clear{vm.ExecutionLock()};

    CHECK_THROWS_AS(android_intrinsics::PlatformSystemLoadHandler(fixture.context)(call), VmJavaThrow);
    CHECK(released);
    CHECK(vm.ExecutionLock().HeldByCurrentThread());
    released = false;
    CHECK_THROWS_AS(fixture.context->native_activity->Call(call, "loadNativeCode"), VmJavaThrow);
    CHECK(released);
    const auto depth = vm.ExecutionLock().ReleaseForBlocking();
    vm.ExecutionLock().ReacquireAfterBlocking(depth);
    CHECK(depth == 2U);
}

TEST_CASE("DexVM preserves failure from a resolved RegisterNatives target") {
    using namespace ogplay;
    auto libc = LibcElf();
    auto root = AppElf({"libroot.so", "libc.so", runtime::kJniVersion1_6});
    std::array module_inputs{
        loader::Elf32ModuleInput{"libroot.so", root, memory::GuestAddress{0x20000000U}},
        loader::Elf32ModuleInput{"libc.so", libc, memory::GuestAddress{0x10000000U}},
    };
    runtime::VirtualFileSystem filesystem;
    auto session = runtime::AndroidGuestCallSession::Start(
        {19, "libroot.so", module_inputs, {}, 64, 36, UINT64_C(200000), 1, &filesystem, {}});
    loader::ApkNativeLibraryInventory inventory{{
        Library("libroot.so", root),
    }};
    const auto selected = loader::SelectApkNativeLibraries(inventory, loader::AndroidArmAbi::armeabi_v7a);
    runtime::NativeLibraryLoader libraries(session->Process(), selected);
    auto context = std::make_shared<runtime::DexVmAndroidContext>();
    context->session = session.get();
    context->native_libraries = &libraries;
    core::CapabilityLedger ledger;
    auto catalog = runtime::AndroidIntrinsicCatalog(context);
    auto bridge =
        std::make_unique<runtime::DexVmGuestBridge>(*session, ReadDexFixture("aps5.dex"), catalog, context, ledger,
                                                    nullptr, runtime::DexVmBridgeConfig{}, test::ReadBootDex());
    context->threads = &bridge->Threads();

    const auto owner = bridge->Linker().FindClass("Lfixture/Aps5;");
    REQUIRE(owner.has_value());
    const auto method = bridge->Linker().FindDirectMethod(*owner, "fail", "()V");
    REQUIRE(method.has_value());
    const auto identity = bridge->RegisteredClassIdentity(*owner);
    REQUIRE(identity.has_value());
    const std::array methods{runtime::JniNativeMethod{"fail", "()V", memory::GuestAddress{0xdeadbee0U}}};
    session->Natives().RegisterNatives(*identity, methods);
    REQUIRE(session->Natives().Resolve(*identity, "fail", "()V").has_value());

    try {
        static_cast<void>(bridge->Vm().Call(*method, {}));
        FAIL("faulting registered native unexpectedly returned");
    } catch (const runtime::AndroidGuestProcessError& error) {
        const std::string message = error.what();
        CHECK(message.find("registered JNI native invocation failed") != std::string::npos);
        CHECK(message.find("class=fixture/Aps5") != std::string::npos);
        CHECK(message.find("method=fail") != std::string::npos);
        CHECK(message.find("descriptor=()V") != std::string::npos);
        CHECK(message.find("guest_thread=1") != std::string::npos);
        CHECK(message.find("pc=0xdeadbee0") != std::string::npos);
        CHECK(message.find("reason=memory_fault(4)") != std::string::npos);
        CHECK(message.find("native method has no registered mapping or export") == std::string::npos);
    }

    bridge.reset();
    session->Stop();
}
