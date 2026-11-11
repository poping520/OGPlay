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
#include "resources_test_support.h"

using namespace ogplay::test::native_library;

TEST_CASE("DexVM bridge publishes the guest VFS working directory") {
    ApplicationProcess application{ogplay::runtime::dexvm::InterpreterBackend::switch_dispatch, "/data/game"};

    CHECK(application.bridge->Vm().GetSystemProperty("user.dir") == "/data/game");
}

TEST_CASE("Android app process installs the default encoded audio loader") {
    OrchestratedApp fixture{"fixture.MainActivity", true, false};
    CHECK(fixture.app->NativeProcess().SoundPoolMixer().Enabled());
}

TEST_CASE("ROM Application startup preserves order identity and native loads") {
    using namespace ogplay;
    SUBCASE("framework default is a stable process root") {
        OrchestratedApp fixture("fixture.LauncherActivity", true, false);
        fixture.app->StartApplication();
        const auto first = fixture.context->application;
        REQUIRE(first.IsValid());
        CHECK_THROWS(fixture.app->StartApplication());
        CHECK(fixture.context->application == first);
        CHECK(fixture.context->application_base_context.IsValid());
    }
    SUBCASE("custom startup observes clinit construct attach onCreate order") {
        OrchestratedApp fixture("fixture.LauncherActivity", true, false, {}, {}, true,
                                runtime::dexvm::InterpreterBackend::switch_dispatch, "fixture.CustomApplication");
        fixture.app->StartApplication();
        REQUIRE(fixture.context->application.IsValid());
        CHECK(fixture.CallStaticInt("Lfixture/CustomApplication;", "getStage") == 4);
        const auto application = fixture.context->application;
        CHECK_THROWS(fixture.app->StartApplication());
        CHECK(fixture.context->application == application);
    }
    SUBCASE("Application callbacks load the first native library") {
        for (const auto application : {"fixture.ClinitLoadingApplication", "fixture.OnCreateLoadingApplication"}) {
            CAPTURE(application);
            OrchestratedApp fixture("fixture.LauncherActivity", true, true, {}, {}, true,
                                    runtime::dexvm::InterpreterBackend::switch_dispatch, application);
            fixture.app->StartApplication();
            const auto records = fixture.context->native_libraries->Records();
            const auto record = std::ranges::find(records, "liba.so", &runtime::NativeLibraryRecord::soname);
            REQUIRE(record != records.end());
            CHECK(record->jni_on_load_calls == 1U);
        }
    }
}

TEST_CASE("Application failure prevents launcher construction and surface effects") {
    using namespace ogplay;
    OrchestratedApp fixture("fixture.LauncherActivity", true, false, {}, {}, true,
                            runtime::dexvm::InterpreterBackend::switch_dispatch, "fixture.ThrowingApplication");
    CHECK_THROWS(fixture.app->StartApplication());
    CHECK_FALSE(fixture.context->application.IsValid());
    CHECK_FALSE(fixture.context->foreground_activity.IsValid());
    CHECK_FALSE(fixture.app->NativeProcess().ManagedSurfaceIsOpen());
    CHECK(fixture.ReadStaticInt("Lfixture/LauncherActivity;", "instances") == 0);
}

TEST_CASE("AndroidAppProcess starts a manifest launcher without preloading app ELF") {
    using namespace ogplay;
    OrchestratedApp fixture("fixture.LauncherActivity");
    CHECK(fixture.app->State() == session::AndroidAppProcessState::dex_vm_ready);
    CHECK_FALSE(fixture.app->NativeProcess().HasLoadedModule("liba.so"));
    const auto direct_library = "/data/app-lib/liba.so";
    const auto alias_library = "/data/data/fixture/lib/liba.so";
    CHECK(fixture.filesystem.CanonicalPath(alias_library) == direct_library);
    CHECK(fixture.filesystem.Stat(alias_library).size == fixture.native_a.size());
    CHECK_FALSE(fixture.filesystem.Stat(alias_library).writable);
    for (const auto path : {direct_library, alias_library}) {
        const auto descriptor = fixture.filesystem.Open(path, {.read = true});
        std::vector<std::byte> contents(fixture.native_a.size());
        CHECK(fixture.filesystem.Read(descriptor, contents) == contents.size());
        CHECK(contents == fixture.native_a);
        CHECK_THROWS_AS(fixture.filesystem.Truncate(descriptor, 0), runtime::VfsError);
        fixture.filesystem.Close(descriptor);
        CHECK_THROWS_AS(static_cast<void>(fixture.filesystem.Open(path, {.write = true})), runtime::VfsError);
        CHECK_THROWS_AS(static_cast<void>(fixture.filesystem.Open(path, {.write = true, .truncate = true})),
                        runtime::VfsError);
        CHECK_THROWS_AS(fixture.filesystem.RemoveFile(path), runtime::VfsError);
    }
    // Existing VFS limits: prefix aliases are absent from parent enumeration,
    // and without a persistent sandbox the native library root permits create.
    const auto children = fixture.filesystem.ListDirectory("/data/data/fixture");
    CHECK(std::ranges::none_of(children, [](const auto& entry) { return entry.name == "lib"; }));
    const auto created = fixture.filesystem.Open("/data/data/fixture/lib/new.bin", {.write = true, .create = true});
    fixture.filesystem.Close(created);
    CHECK(fixture.filesystem.Stat("/data/app-lib/new.bin").size == 0U);
    CHECK(fixture.app->Context()->package_resource_path == "/data/app/fixture-1.apk");
    CHECK(fixture.app->Context()->secure_settings.at("android_id") == "0123456789abcdef");
    const auto apk = fixture.filesystem.Open(fixture.app->Context()->package_resource_path, {.read = true});
    std::array<std::byte, 4> apk_magic{};
    CHECK(fixture.filesystem.Read(apk, apk_magic) == apk_magic.size());
    CHECK(apk_magic == std::array{std::byte{0x50}, std::byte{0x4b}, std::byte{0x03}, std::byte{0x04}});
    CHECK_THROWS_AS(
        static_cast<void>(fixture.filesystem.Open(fixture.app->Context()->package_resource_path, {.write = true})),
        runtime::VfsError);
    CHECK_THROWS_AS(static_cast<void>(fixture.app->StartLauncherActivity()), session::AndroidAppProcessError);

    fixture.app->StartApplication();
    CHECK(fixture.app->State() == session::AndroidAppProcessState::application_started);
    CHECK_FALSE(fixture.app->NativeProcess().HasLoadedModule("liba.so"));
    const auto started = fixture.app->StartLauncherActivity();
    CHECK(started.state == session::LifecycleRunState::running);
    CHECK(fixture.CallStaticInt("Lfixture/LauncherActivity;", "getStage") == 3);
    CHECK(fixture.app->Context()->application.IsValid());
    CHECK(fixture.app->Context()->foreground_activity.IsValid());

    const auto stopped = fixture.app->Stop();
    CHECK(stopped.state == session::LifecycleRunState::stopped);
    CHECK_FALSE(fixture.app->NativeProcess().Running());
    CHECK(fixture.app->State() == session::AndroidAppProcessState::stopped);
}

TEST_CASE("Android app process registers the last Activity theme and preserves alias snapshots") {
    using namespace ogplay;
    runtime::VirtualFileSystem filesystem;
    core::CapabilityLedger ledger;
    core::Logger logger;
    const auto libc = LibcElf();
    const runtime::BionicModuleSource system{"libc.so", libc};
    session::AndroidAppProcessRequest request;
    request.manifest = AppManifest("fixture.DelayedContentActivity");
    request.manifest.application_theme = 0x01030007U;
    auto& first = request.manifest.activity_components.front();
    first.theme = 0x7f030001U;
    auto alias = first;
    alias.kind = loader::AndroidManifestComponentKind::activity_alias;
    alias.name = "fixture.Alias";
    alias.target_activity = first.name;
    alias.intent_filters.clear();
    auto last = first;
    last.theme = 0x7f030002U;
    last.intent_filters.clear();
    request.manifest.activity_components.push_back(alias);
    request.manifest.activity_components.push_back(last);
    last.theme.reset();
    last.name = "fixture.DefaultTheme";
    request.manifest.activity_components.push_back(last);
    request.system_libraries = std::span{&system, 1};
    request.dex_bytes = ReadDexFixture("application.dex");
    request.boot_dex_files = test::ReadBootDex();
    request.context = std::make_shared<runtime::DexVmAndroidContext>();
    test::BindBootDexArchive(*request.context);
    request.platform.android_id = "0123456789abcdef";
    request.filesystem = &filesystem;
    request.ledger = &ledger;
    request.logger = &logger;
    auto app = session::AndroidAppProcess::Create(std::move(request));
    CHECK(app->Context()->activity_themes.at("fixture.DelayedContentActivity") == 0x7f030002U);
    CHECK(app->Context()->activity_themes.at("fixture.Alias") == 0x7f030001U);
    CHECK(app->Context()->activity_themes.at("fixture.DefaultTheme") == 0x01030007U);
    static_cast<void>(app->Stop());
}

TEST_CASE("AndroidAppProcess supports a pure Java APK without a Profile or ABI") {
    using namespace ogplay;
    OrchestratedApp fixture("fixture.LauncherActivity", true, false);
    CHECK_FALSE(fixture.app->SelectedAbi().has_value());
    CHECK(fixture.filesystem.CanonicalPath("/data/data/fixture/lib/libmissing.so") == "/data/app-lib/libmissing.so");
    REQUIRE(fixture.app->NativeLibraries() != nullptr);
    const auto records = fixture.app->NativeLibraries()->Records();
    CHECK(std::ranges::all_of(records, [](const auto& record) { return record.soname == "libogplay_jni.so"; }));
    fixture.app->StartApplication();
    const auto started = fixture.app->StartLauncherActivity();
    CHECK(started.state == session::LifecycleRunState::running);
    CHECK_FALSE(fixture.app->NativeProcess().HasLoadedModule("liba.so"));
    static_cast<void>(fixture.app->Stop());
}

TEST_CASE("AndroidAppProcess keeps Activity native loads Java-driven") {
    using namespace ogplay;

    SUBCASE("Activity clinit loads after Application") {
        OrchestratedApp fixture("fixture.ClinitLoadingActivity");
        fixture.app->StartApplication();
        CHECK_FALSE(fixture.app->NativeProcess().HasLoadedModule("liba.so"));
        static_cast<void>(fixture.app->StartLauncherActivity());
        REQUIRE(fixture.app->NativeLibraries() != nullptr);
        auto records = fixture.app->NativeLibraries()->Records();
        std::erase_if(records, [](const auto& record) { return record.soname == "libogplay_jni.so"; });
        REQUIRE(records.size() == 1U);
        CHECK(records[0].soname == "liba.so");
        CHECK(records[0].jni_on_load_calls == 1U);
        CHECK(fixture.app->NativeProcess().HasLoadedModule("liba.so"));
    }

    SUBCASE("Activity onCreate loads after construction") {
        OrchestratedApp fixture("fixture.OnCreateLoadingActivity");
        fixture.app->StartApplication();
        CHECK_FALSE(fixture.app->NativeProcess().HasLoadedModule("liba.so"));
        static_cast<void>(fixture.app->StartLauncherActivity());
        REQUIRE(fixture.app->NativeLibraries() != nullptr);
        auto records = fixture.app->NativeLibraries()->Records();
        std::erase_if(records, [](const auto& record) { return record.soname == "libogplay_jni.so"; });
        REQUIRE(records.size() == 1U);
        CHECK(records[0].soname == "liba.so");
        CHECK(fixture.app->NativeProcess().HasLoadedModule("liba.so"));
    }
}

TEST_CASE("AndroidAppProcess rejects a manifest without a launcher") {
    CHECK_THROWS_AS(static_cast<void>(OrchestratedApp("fixture.LauncherActivity", false)),
                    ogplay::session::AndroidAppProcessError);
}

TEST_CASE("run-apk delegates application startup and never selects an ELF root") {
    const auto read_source = [](const std::string_view relative) {
        const auto path = std::string(OGPLAY_SOURCE_DIR) + std::string(relative);
        std::ifstream input(path, std::ios::binary);
        if (!input.good()) throw std::runtime_error("missing frontend source");
        return std::string{std::istreambuf_iterator<char>(input), {}};
    };
    const auto cli = read_source("/src/frontend/game/run_game.cpp");
    CHECK(cli.find("AndroidAppProcess::Create") != std::string::npos);
    CHECK(cli.find("SelectApkCompatibilityProfile") != std::string::npos);
    CHECK(cli.find("AndroidGuestCallSession::Start") == std::string::npos);
    CHECK(cli.find("PrepareApkProfileLaunch") == std::string::npos);
    CHECK(cli.find("MatchApkTitleProfile") == std::string::npos);
    CHECK(cli.find("InitializeJniLibrary") == std::string::npos);
    CHECK(cli.find("so_sha256") == std::string::npos);
    CHECK(cli.find("APK guest execution failed") == std::string::npos);

    const auto gui = read_source("/src/frontend/library/import.cpp");
    CHECK(gui.find("SelectApkCompatibilityProfile") != std::string::npos);
    CHECK(gui.find("MatchApkTitleProfile") == std::string::npos);
}

TEST_CASE("DVM-107 manifest launcher alias retains its component identity") {
    using namespace ogplay;
    using namespace runtime::dexvm;
    OrchestratedApp fixture("fixture.LauncherActivity", true, false, "fixture.StartAlias");
    fixture.app->StartApplication();
    REQUIRE(fixture.app->StartLauncherActivity().state == session::LifecycleRunState::running);
    auto& bridge = fixture.app->DexVm();
    const auto activity = fixture.context->foreground_activity;
    CHECK(bridge.Linker().Class(bridge.Model().ObjectClass(activity)).descriptor == "Lfixture/LauncherActivity;");
    fixture.RunOnMainThread([&] {
        const auto invoke = [&](VmObjectRef receiver, const char* name, const char* signature) {
            const auto type = bridge.Model().ObjectClass(receiver);
            const auto slot = bridge.Linker().FindVtableIndex(type, name, signature);
            REQUIRE(slot.has_value());
            const auto result =
                bridge.Vm().Call(bridge.Linker().Class(type).vtable[*slot], std::array{VmValue::Ref(receiver)});
            REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
            return result.value.ref;
        };
        CHECK(bridge.Vm().StringUtf8(invoke(activity, "getLocalClassName", "()Ljava/lang/String;")) == "StartAlias");
        const auto component = invoke(activity, "getComponentName", "()Landroid/content/ComponentName;");
        CHECK(bridge.Vm().StringUtf8(invoke(component, "getClassName", "()Ljava/lang/String;")) ==
              "fixture.StartAlias");
        const auto intent = invoke(activity, "getIntent", "()Landroid/content/Intent;");
        CHECK(invoke(intent, "getComponent", "()Landroid/content/ComponentName;") == component);
    });
    CHECK(fixture.app->Stop().state == session::LifecycleRunState::stopped);
}

TEST_CASE("DVM-112 AndroidAppProcess installs sealed service discovery facts") {
    for (const bool enabled : {false, true}) {
        OrchestratedApp f("fixture.LauncherActivity", true, false, {},
                          {{"fixture.LocalService", false, {{{"fixture.SERVICE"}, {"fixture.CATEGORY"}, true}}}},
                          enabled);
        const auto context = f.app->Context();
        CHECK(context->service_inventory_known);
        CHECK(context->application_enabled == enabled);
        REQUIRE(context->service_components.size() == 1);
        const auto& service = context->service_components[0];
        CHECK(service.name == "fixture.LocalService");
        CHECK_FALSE(service.enabled);
        REQUIRE(service.intent_filters.size() == 1);
        CHECK(service.intent_filters[0].actions == std::vector<std::string>{"fixture.SERVICE"});
        CHECK(service.intent_filters[0].categories == std::vector<std::string>{"fixture.CATEGORY"});
        CHECK(service.intent_filters[0].has_data);
    }
}

TEST_CASE("BND54 CPU texture declarations resolve vendor family without a Profile") {
    using namespace ogplay;
    using Family = gles::CompressedTextureFamily;
    loader::AndroidManifestFacts facts;
    CHECK(session::ResolveManifestCompressedTextureFamily(facts) == Family::all);
    facts.supported_gl_textures = {"GL_AMD_compressed_ATC_texture"};
    CHECK(session::ResolveManifestCompressedTextureFamily(facts) == Family::atc);
    facts.supported_gl_textures.push_back("GL_OES_compressed_ETC1_RGB8_texture");
    CHECK(session::ResolveManifestCompressedTextureFamily(facts) == Family::atc);
    facts.supported_gl_textures.push_back("GL_IMG_texture_compression_pvrtc");
    CHECK(session::ResolveManifestCompressedTextureFamily(facts) == Family::all);
    facts.supported_gl_textures = {"GL_IMG_texture_compression_pvrtc"};
    CHECK(session::ResolveManifestCompressedTextureFamily(facts) == Family::pvrtc);
    facts.supported_gl_textures.push_back("GL_UNKNOWN_texture_compression");
    CHECK(session::ResolveManifestCompressedTextureFamily(facts) == Family::all);
    facts.supported_gl_textures = {"GL_OES_compressed_ETC1_RGB8_texture"};
    CHECK(session::ResolveManifestCompressedTextureFamily(facts) == Family::all);
}

TEST_CASE("BND54 APK texture declarations choose a vendor family without a Profile") {
    using namespace ogplay;
    OrchestratedApp fixture{"fixture.MainActivity",
                            true,
                            false,
                            {},
                            {},
                            true,
                            runtime::dexvm::InterpreterBackend::switch_dispatch,
                            "android.app.Application",
                            {},
                            {64, 36},
                            {"GL_AMD_compressed_ATC_texture"}};
    REQUIRE(fixture.context->session != nullptr);
    fixture.context->session->OpenManagedSurface();
    const auto extensions = fixture.context->session->ManagedGlString(0x1F03);
    CHECK(extensions.find("GL_AMD_compressed_ATC_texture") != extensions.npos);
    CHECK(extensions.find("GL_IMG_texture_compression_pvrtc") == extensions.npos);
    const auto records = fixture.logger.Snapshot(core::LogLevel::info, "session.app_process");
    REQUIRE(records.size() == 1U);
    CHECK(fixture.logger.RenderText(records.front()).find("source=manifest") != std::string::npos);
    fixture.context->session->CloseManagedSurface();
    static_cast<void>(fixture.app->Stop());
}

TEST_CASE("VFS-09 session and native process share the finite guest call watchdog default") {
    const ogplay::session::AndroidAppProcessRequest app;
    const ogplay::runtime::AndroidGuestProcessRequest process;
    const ogplay::runtime::AndroidGuestCallSessionRequest call;
    CHECK(app.maximum_ticks_per_call == process.maximum_ticks_per_call);
    CHECK(call.maximum_ticks_per_call == process.maximum_ticks_per_call);
    CHECK(app.maximum_ticks_per_call == UINT64_C(10000000000));
}

TEST_CASE("DVM-264 ROM application binding publishes tmpdir before Application class initialization") {
    using namespace ogplay;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        runtime::VirtualFileSystem filesystem;
        filesystem.CreateDirectory("/data");
        filesystem.CreateDirectory("/data/game");
        filesystem.SetWorkingDirectory("/data/game");
        core::CapabilityLedger ledger;
        core::Logger logger;
        std::vector<std::vector<std::byte>> library_bytes;
        std::vector<runtime::BionicModuleSource> system;
        for (const auto name : runtime::SelectBionicProfile(19).guest_libraries) {
            library_bytes.push_back(ReadPayloadBytes("lib/" + std::string(name)));
            system.push_back({std::string(name), library_bytes.back()});
        }
        session::AndroidAppProcessRequest request;
        request.manifest = AppManifest("fixture.LauncherActivity", true);
        request.manifest.package = "fixture.platform";
        request.manifest.application_class = "fixture.PlatformApplication";
        request.dex_bytes = ReadDexFixture("platform_tables.dex");
        request.boot_dex_files = test::ReadBootDex();
        request.system_libraries = system;
        request.icu_data = ReadPayloadBytes("icu/icudt51l.dat");
        request.tzdata = ReadPayloadBytes("zoneinfo/tzdata");
        request.context = std::make_shared<runtime::DexVmAndroidContext>();
        test::BindBootDexArchive(*request.context);
        const std::array entries{std::pair{std::string("fixture"), std::vector<std::byte>{}}};
        request.context->apk_bytes = test::resources::Zip(entries);
        request.platform.android_id = "0123456789abcdef";
        request.filesystem = &filesystem;
        request.ledger = &ledger;
        request.logger = &logger;
        request.dexvm.interpreter.backend = backend;
        auto app = session::AndroidAppProcess::Create(std::move(request));
        app->StartApplication();
        {
            auto& vm = app->DexVm().Vm();
            const VmExecutionLockScope lock(vm.ExecutionLock());
            const auto type = vm.Linker().ResolveDescriptor("Lfixture/PlatformApplication;");
            const auto field = vm.Linker().FindFieldRecursive(type, "initialTmp", "Ljava/lang/String;");
            REQUIRE(field);
            const auto value = VmObjectRef{vm.Linker().Class(type).static_storage[vm.Linker().Field(*field).slot]};
            CHECK(vm.StringUtf8(value) == "/data/data/fixture.platform/cache");
            const auto reader = vm.CreateExecutionContext();
            vm.RunInExecutionContext(reader, [&] {
                CHECK(vm.GetSystemProperty("java.io.tmpdir") == "/data/data/fixture.platform/cache");
                CHECK(vm.GetSystemProperty("user.dir") == "/data/game");
            });
            vm.DiscardExecutionContext(reader);
        }
        static_cast<void>(app->Stop());
    }
}

TEST_CASE("DVM-354 API19 heap classes and largeHeap are applied before Application construction") {
    using namespace ogplay;
    using namespace runtime::dexvm;
    constexpr std::int64_t mib = 1024 * 1024;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        for (const bool large : {false, true}) {
            OrchestratedApp fixture("fixture.LauncherActivity", true, false, "", {}, true, backend,
                                    "fixture.HeapApplication", {}, {64, 36}, {}, large);
            fixture.app->StartApplication();
            auto& vm = fixture.app->DexVm().Vm();
            auto& linker = vm.Linker();
            fixture.RunOnMainThread([&] {
                const auto call = [&](const char* owner, const char* name, const char* signature,
                                      std::vector<VmValue> args = {}) {
                    const auto type = linker.ResolveDescriptor(owner);
                    const auto method = linker.FindDirectMethod(type, name, signature);
                    REQUIRE(method);
                    const auto result = vm.Call(*method, args);
                    REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
                    return result.value;
                };
                const auto on = [&](VmObjectRef object, const char* name, const char* signature) {
                    const auto type = vm.Model().ObjectClass(object);
                    const auto slot = linker.FindVtableIndex(type, name, signature);
                    REQUIRE(slot);
                    const auto result = vm.Call(linker.Class(type).vtable[*slot], std::array{VmValue::Ref(object)});
                    REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
                    return result.value;
                };
                CHECK(call("Landroid/app/ActivityManager;", "staticGetMemoryClass", "()I").AsInt() == 96);
                CHECK(call("Landroid/app/ActivityManager;", "staticGetLargeMemoryClass", "()I").AsInt() == 256);
                const auto runtime = call("Ljava/lang/Runtime;", "getRuntime", "()Ljava/lang/Runtime;").ref;
                CHECK(on(runtime, "maxMemory", "()J").AsLong() == (large ? 256 : 96) * mib);
                const auto application = fixture.app->Context()->application;
                const auto base = on(application, "getBaseContext", "()Landroid/content/Context;").ref;
                REQUIRE(base.IsValid());
                CHECK(linker.Class(vm.Model().ObjectClass(base)).descriptor == "Landroid/app/ContextImpl;");
                const auto loader = on(base, "getClassLoader", "()Ljava/lang/ClassLoader;").ref;
                REQUIRE(loader.IsValid());
                CHECK(linker.Class(vm.Model().ObjectClass(loader)).is_boot_dex);
                const auto class_object = vm.Model().ClassObject(vm.Model().ObjectClass(application));
                CHECK(on(class_object, "getClassLoader", "()Ljava/lang/ClassLoader;").ref == loader);
                const auto files = on(base, "getFilesDir", "()Ljava/io/File;").ref;
                REQUIRE(files.IsValid());
                CHECK(vm.StringUtf8(on(files, "getPath", "()Ljava/lang/String;").ref) == "/data/data/fixture/files");
                const auto info = on(application, "getApplicationInfo", "()Landroid/content/pm/ApplicationInfo;").ref;
                const auto flags = linker.FindFieldRecursive(vm.Model().ObjectClass(info), "flags", "I");
                REQUIRE(flags);
                CHECK(((vm.Model().InstanceSlots(info)[linker.Field(*flags).slot].bits & 0x00100000U) != 0) == large);
                const auto app_type = linker.ResolveDescriptor("Lfixture/HeapApplication;");
                const auto measured = linker.FindFieldRecursive(app_type, "constructorMax", "J");
                REQUIRE(measured);
                const auto& storage = linker.Class(app_type).static_storage;
                const auto index = linker.Field(*measured).slot;
                const auto bytes = static_cast<std::uint64_t>(storage[index]) |
                                   (static_cast<std::uint64_t>(storage[index + 1]) << 32U);
                CHECK(bytes == static_cast<std::uint64_t>((large ? 256 : 96) * mib));
                const auto allocate = linker.FindDirectMethod(app_type, "allocateProbe", "()[B");
                REQUIRE(allocate);
                const auto allocation = vm.Call(*allocate, {});
                if (large) {
                    REQUIRE_FALSE(allocation.exception.IsValid());
                    CHECK(vm.Model().IsValidRef(allocation.value.ref));
                } else {
                    REQUIRE(allocation.exception.IsValid());
                    CHECK(linker.Class(allocation.exception_class).descriptor == "Ljava/lang/OutOfMemoryError;");
                }
            });
            static_cast<void>(fixture.app->Stop());
        }
    }
}

TEST_CASE("AndroidAppProcess runs host requests inside the ROM main Looper on one guest owner") {
    using namespace ogplay;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        OrchestratedApp f("fixture.LauncherActivity", true, false, {}, {}, true, backend);
        f.app->StartApplication();
        const auto host_thread = std::this_thread::get_id();
        std::thread::id main_thread;
        auto& vm = f.app->DexVm().Vm();
        const auto held = vm.NewStringUtf8("host-held-root");
        {
            const auto root = vm.ProtectReferences(std::array{held});
            for (int request = 0; request < 2; ++request) {
                f.RunOnMainThread([&] {
                    if (request == 0) main_thread = std::this_thread::get_id();
                    CHECK(std::this_thread::get_id() == main_thread);
                    CHECK(main_thread != host_thread);
                    CHECK(vm.CurrentContextToken() == 1U);
                    bool loop = false, handler = false;
                    for (const auto& stack : vm.StackSnapshot()) {
                        if (stack.context_token != 1U) continue;
                        for (const auto& frame : stack.frames) {
                            loop |= frame.class_descriptor == "Landroid/os/Looper;" && frame.method_name == "loop";
                            handler |= frame.class_descriptor == "Landroid/os/Handler;" &&
                                       frame.method_name == "dispatchMessage";
                        }
                    }
                    CHECK(loop);
                    CHECK(handler);
                    static_cast<void>(vm.CollectGarbage("main-channel-host-root"));
                    CHECK(vm.Model().IsValidRef(held));
                    CHECK(vm.StringUtf8(held) == "host-held-root");
                });
            }
        }
        f.RunOnMainThread([&] {
            static_cast<void>(vm.CollectGarbage("main-channel-root-release"));
            CHECK_FALSE(vm.Model().IsValidRef(held));
        });
        CHECK(f.app->Stop().state == session::LifecycleRunState::stopped);
    }
}
