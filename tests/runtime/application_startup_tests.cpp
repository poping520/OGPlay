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

TEST_CASE("DexVM bridge publishes the guest VFS working directory") {
    ApplicationProcess application{
        ogplay::runtime::dexvm::InterpreterBackend::switch_dispatch,
        "/data/game"};

    CHECK(application.bridge->Vm().GetSystemProperty("user.dir") ==
          "/data/game");
}

TEST_CASE("Android app process installs the default encoded audio loader") {
    OrchestratedApp fixture{"fixture.MainActivity", true, false};
    CHECK(fixture.app->NativeProcess().SoundPoolMixer().Enabled());
}

TEST_CASE("minimal Application startup preserves order identity and native loads") {
    using namespace ogplay;

    SUBCASE("framework default is a stable process root") {
        ApplicationProcess fixture;
        CHECK(fixture.session->Environment().GlobalReferenceCount() ==
              fixture.globals_before_bridge);
        const auto first = session::StartDexApplication(
            *fixture.bridge, fixture.context, "Landroid/app/Application;");
        const auto repeated = session::StartDexApplication(
            *fixture.bridge, fixture.context, "Landroid/app/Application;");
        CHECK(first == repeated);
        CHECK(fixture.context->application == first);
        CHECK(fixture.context->application_base_context.IsValid());
    }

    SUBCASE("custom startup observes clinit construct attach onCreate order") {
        ApplicationProcess fixture;
        const auto application = session::StartDexApplication(
            *fixture.bridge, fixture.context,
            "Lfixture/CustomApplication;");
        CHECK(application.IsValid());
        CHECK(fixture.CallStaticInt("Lfixture/CustomApplication;",
                                    "getStage") == 4);
        CHECK(session::StartDexApplication(
                  *fixture.bridge, fixture.context,
                  "Lfixture/CustomApplication;") == application);
        CHECK_THROWS_AS(
            static_cast<void>(session::StartDexApplication(
                *fixture.bridge, fixture.context,
                "Landroid/app/Application;")),
            session::DexActivityLifecycleError);
    }

    SUBCASE("clinit may load the first native library") {
        ApplicationProcess fixture;
        static_cast<void>(session::StartDexApplication(
            *fixture.bridge, fixture.context,
            "Lfixture/ClinitLoadingApplication;"));
        const auto records = fixture.libraries->Records();
        REQUIRE(records.size() == 1U);
        CHECK(records[0].soname == "liba.so");
        CHECK(records[0].jni_on_load_calls == 1U);
    }

    SUBCASE("onCreate may load the first native library") {
        ApplicationProcess fixture;
        static_cast<void>(session::StartDexApplication(
            *fixture.bridge, fixture.context,
            "Lfixture/OnCreateLoadingApplication;"));
        const auto records = fixture.libraries->Records();
        REQUIRE(records.size() == 1U);
        CHECK(records[0].soname == "liba.so");
        CHECK(records[0].jni_on_load_calls == 1U);
    }
}

TEST_CASE("Application failure prevents launcher construction and surface effects") {
    using namespace ogplay;
    ApplicationProcess fixture;
    bool opened{};
    session::DexActivityLifecycleBindings bindings;
    bindings.bridge = fixture.bridge.get();
    bindings.context = fixture.context;
    bindings.launcher_descriptor = "Lfixture/LauncherActivity;";
    bindings.application_descriptor = "Lfixture/ThrowingApplication;";
    bindings.open_surface = [&] { opened = true; };
    session::DexActivityLifecycle lifecycle(std::move(bindings));

    CHECK_THROWS_WITH_AS(
        static_cast<void>(lifecycle.Start()),
        doctest::Contains(
            "Application onCreate failed: uncaught Java exception\n"
            "  exception: Ljava/lang/RuntimeException;\n"
            "  message: application failed\n"
            "  stack trace:\n"
            "    at Lfixture/ThrowingApplication;.onCreate (pc 7)"),
        session::DexActivityLifecycleError);
    CHECK_FALSE(opened);
    CHECK_FALSE(fixture.context->application.IsValid());
    CHECK_FALSE(fixture.context->activity.IsValid());
    CHECK(fixture.CallStaticInt("Lfixture/LauncherActivity;",
                                "getInstances") == 0);
}

TEST_CASE("AndroidAppProcess starts a manifest launcher without preloading app ELF") {
    using namespace ogplay;
    OrchestratedApp fixture("fixture.LauncherActivity");
    CHECK(fixture.app->State() ==
          session::AndroidAppProcessState::dex_vm_ready);
    CHECK(fixture.app->NativeProcess().ApplicationModuleCount() == 0U);
    CHECK(fixture.app->Context()->package_resource_path ==
          "/data/app/fixture-1.apk");
    CHECK(fixture.app->Context()->secure_settings.at("android_id") ==
          "0123456789abcdef");
    const auto apk = fixture.filesystem.Open(
        fixture.app->Context()->package_resource_path, {.read = true});
    std::array<std::byte, 4> apk_magic{};
    CHECK(fixture.filesystem.Read(apk, apk_magic) == apk_magic.size());
    CHECK(apk_magic == std::array{
                           std::byte{0x50}, std::byte{0x4b},
                           std::byte{0x03}, std::byte{0x04}});
    CHECK_THROWS_AS(
        static_cast<void>(fixture.filesystem.Open(
            fixture.app->Context()->package_resource_path, {.write = true})),
        runtime::VfsError);
    CHECK_THROWS_AS(
        static_cast<void>(fixture.app->StartLauncherActivity()),
        session::AndroidAppProcessError);

    fixture.app->StartApplication();
    CHECK(fixture.app->State() ==
          session::AndroidAppProcessState::application_started);
    CHECK(fixture.app->NativeProcess().ApplicationModuleCount() == 0U);
    const auto started = fixture.app->StartLauncherActivity();
    CHECK(started.state == session::LifecycleRunState::running);
    CHECK(fixture.CallStaticInt("Lfixture/LauncherActivity;", "getStage") == 3);
    CHECK(fixture.app->Context()->application.IsValid());
    CHECK(fixture.app->Context()->activity.IsValid());

    const auto stopped = fixture.app->Stop();
    CHECK(stopped.state == session::LifecycleRunState::stopped);
    CHECK_FALSE(fixture.app->NativeProcess().Running());
    CHECK(fixture.app->State() ==
          session::AndroidAppProcessState::stopped);
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
    request.boot_dex_bytes = test::ReadBootDex();
    request.context = std::make_shared<runtime::DexVmAndroidContext>();
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
    REQUIRE(fixture.app->NativeLibraries() != nullptr);
    CHECK(fixture.app->NativeLibraries()->Records().empty());
    fixture.app->StartApplication();
    const auto started = fixture.app->StartLauncherActivity();
    CHECK(started.state == session::LifecycleRunState::running);
    CHECK(fixture.app->NativeProcess().ApplicationModuleCount() == 0U);
    static_cast<void>(fixture.app->Stop());
}

TEST_CASE("AndroidAppProcess keeps Activity native loads Java-driven") {
    using namespace ogplay;

    SUBCASE("Activity clinit loads after Application") {
        OrchestratedApp fixture("fixture.ClinitLoadingActivity");
        fixture.app->StartApplication();
        CHECK(fixture.app->NativeProcess().ApplicationModuleCount() == 0U);
        static_cast<void>(fixture.app->StartLauncherActivity());
        REQUIRE(fixture.app->NativeLibraries() != nullptr);
        const auto records = fixture.app->NativeLibraries()->Records();
        REQUIRE(records.size() == 1U);
        CHECK(records[0].soname == "liba.so");
        CHECK(records[0].jni_on_load_calls == 1U);
        CHECK(fixture.app->NativeProcess().ApplicationModuleCount() == 1U);
    }

    SUBCASE("Activity onCreate loads after construction") {
        OrchestratedApp fixture("fixture.OnCreateLoadingActivity");
        fixture.app->StartApplication();
        CHECK(fixture.app->NativeProcess().ApplicationModuleCount() == 0U);
        static_cast<void>(fixture.app->StartLauncherActivity());
        REQUIRE(fixture.app->NativeLibraries() != nullptr);
        CHECK(fixture.app->NativeLibraries()->Records().size() == 1U);
        CHECK(fixture.app->NativeProcess().ApplicationModuleCount() == 1U);
    }
}

TEST_CASE("AndroidAppProcess rejects a manifest without a launcher") {
    CHECK_THROWS_AS(
        static_cast<void>(OrchestratedApp("fixture.LauncherActivity", false)),
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
    OrchestratedApp fixture("fixture.LauncherActivity", true, false,
                            "fixture.StartAlias");
    fixture.app->StartApplication();
    REQUIRE(fixture.app->StartLauncherActivity().state ==
            session::LifecycleRunState::running);
    auto& bridge = fixture.app->DexVm();
    const auto activity = fixture.context->activity;
    CHECK(bridge.Linker().Class(bridge.Model().ObjectClass(activity)).descriptor ==
          "Lfixture/LauncherActivity;");
    const auto invoke = [&](VmObjectRef receiver, const char* name,
                            const char* signature) {
        const auto type = bridge.Model().ObjectClass(receiver);
        const auto slot = bridge.Linker().FindVtableIndex(type, name, signature);
        REQUIRE(slot.has_value());
        const auto result = bridge.Vm().Call(bridge.Linker().Class(type).vtable[*slot],
                                             std::array{VmValue::Ref(receiver)});
        REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
        return result.value.ref;
    };
    CHECK(bridge.Vm().StringUtf8(invoke(activity, "getLocalClassName",
                                        "()Ljava/lang/String;")) == "StartAlias");
    const auto component =
        invoke(activity, "getComponentName", "()Landroid/content/ComponentName;");
    CHECK(bridge.Vm().StringUtf8(
              invoke(component, "getClassName", "()Ljava/lang/String;")) ==
          "fixture.StartAlias");
    const auto intent = invoke(activity, "getIntent", "()Landroid/content/Intent;");
    CHECK(invoke(intent, "getComponent", "()Landroid/content/ComponentName;") ==
          component);
    CHECK(fixture.app->Stop().state == session::LifecycleRunState::stopped);
}

TEST_CASE("DVM-112 AndroidAppProcess installs sealed service discovery facts") {
    for (const bool enabled : {false, true}) {
        OrchestratedApp f("fixture.LauncherActivity", true, false, {},
            {{"fixture.LocalService", false, {{{"fixture.SERVICE"}, {"fixture.CATEGORY"}, true}}}}, enabled);
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

TEST_CASE("BND54 APK texture declarations choose a vendor family without a Profile") {
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

    OrchestratedApp fixture{"fixture.MainActivity", true, false, {}, {}, true,
        runtime::dexvm::InterpreterBackend::switch_dispatch, "android.app.Application", {}, {64, 36},
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

TEST_CASE("DVM-264 application binding publishes tmpdir before Application class initialization") {
    using namespace ogplay;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ApplicationProcess fixture(backend, "/data/game", "platform_tables.dex");
        fixture.context->package_name = "fixture.platform";
        auto& vm = fixture.bridge->Vm();
        REQUIRE(vm.GetSystemProperty("java.io.tmpdir") == "/tmp");
        static_cast<void>(
            session::StartDexApplication(*fixture.bridge, fixture.context, "Lfixture/PlatformApplication;"));
        const auto type = vm.Linker().ResolveDescriptor("Lfixture/PlatformApplication;");
        const auto method = vm.Linker().FindDirectMethod(type, "initial", "()Ljava/lang/String;");
        REQUIRE(method.has_value());
        const auto outcome = vm.Call(*method, {});
        REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
        CHECK(vm.StringUtf8(outcome.value.ref) == "/data/data/fixture.platform/cache");
        CHECK(vm.GetSystemProperty("java.io.tmpdir") == "/data/data/fixture.platform/cache");
        CHECK(vm.GetSystemProperty("user.dir") == "/data/game");
    }
}
