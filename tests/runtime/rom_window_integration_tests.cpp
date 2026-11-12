#include "native_library_fixture.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>

#include <doctest/doctest.h>

#include "ogplay/loader/apk.h"
#include "ogplay/runtime/bionic/bionic_profile.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/video/ffmpeg_video_player.h"

namespace {
using namespace ogplay;
using namespace ogplay::runtime;
using test::native_library::ReadPayloadBytes;

const auto fixture_root = std::filesystem::path(OGPLAY_SOURCE_DIR) / "tests/fixtures/ui";

struct RomWindowApp {
    core::CapabilityLedger ledger;
    core::Logger logger;
    VirtualFileSystem filesystem;
    std::shared_ptr<DexVmAndroidContext> context;
    std::unique_ptr<session::AndroidAppProcess> app;
    std::optional<AndroidBoundaryFrame> image;

    explicit RomWindowApp(std::string_view activity, bool realtime = false) {
        const auto android = std::filesystem::path(OGPLAY_SOURCE_DIR) / "data/android/19";
        context = AssembleAndroidContext(ReadBootClasspath(android / "framework"));
        context->framework_resource_apk = android / "framework/framework-res.apk";
        context->system_font_source = [android](std::string_view name) {
            std::ifstream stream(android / "fonts" / name, std::ios::binary);
            const std::vector<char> bytes{std::istreambuf_iterator<char>(stream), {}};
            const auto data = std::as_bytes(std::span{bytes});
            return std::vector<std::byte>{data.begin(), data.end()};
        };
        if (video::FfmpegAvailable())
            context->video_source_player_factory = video::MakeFfmpegVideoSourcePlayerFactory();

        std::ifstream apk(fixture_root / "rom_window.apk", std::ios::binary);
        REQUIRE(apk);
        const std::vector<char> bytes{std::istreambuf_iterator<char>(apk), {}};
        const auto data = std::as_bytes(std::span{bytes});
        context->apk_bytes.assign(data.begin(), data.end());
        context->archive = loader::ParseApkArchive(context->apk_bytes);
        auto manifest = loader::ParseAndroidBinaryManifest(
            loader::ReadApkEntry(context->apk_bytes, context->archive, "AndroidManifest.xml"));
        const auto name = manifest.package + "." + std::string(activity);
        const auto launch_filters = manifest.activity_components.front().intent_filters;
        for (auto& component : manifest.activity_components) {
            component.intent_filters.clear();
            if (component.name == name) component.intent_filters = launch_filters;
        }
        manifest.launcher_activity = name;
        const auto dex = loader::ReadApkEntry(context->apk_bytes, context->archive, "classes.dex");
        std::vector<std::vector<std::byte>> module_bytes;
        std::vector<BionicModuleSource> modules;
        for (const auto soname : SelectBionicProfile(19).guest_libraries) {
            module_bytes.push_back(ReadPayloadBytes("lib/" + std::string(soname)));
            modules.push_back({std::string(soname), module_bytes.back()});
        }
        session::AndroidAppProcessRequest request;
        request.context = context;
        request.boundary_options.async_present_readback = realtime;
        request.manifest = std::move(manifest);
        request.system_libraries = modules;
        for (const auto byte : dex)
            request.dex_bytes.push_back(std::to_integer<std::uint8_t>(byte));
        request.boot_dex_files = BootClasspathDexFiles(context->boot_classpath);
        request.icu_data = ReadPayloadBytes("icu/icudt51l.dat");
        request.tzdata = ReadPayloadBytes("zoneinfo/tzdata");
        request.filesystem = &filesystem;
        request.surface_width = 320;
        request.surface_height = 400;
        request.platform.android_id = "0123456789abcdef";
        request.ledger = &ledger;
        request.logger = &logger;
#if defined(_WIN32)
        request.backend = {gles::AngleRenderer::d3d11, gles::AngleDevice::hardware};
#elif defined(__APPLE__)
        request.backend = {gles::AngleRenderer::metal, gles::AngleDevice::hardware};
#else
        request.backend = {gles::AngleRenderer::vulkan, gles::AngleDevice::hardware};
#endif
        app = session::AndroidAppProcess::Create(std::move(request));
        app->ActivityLifecycle().SetRealtimeVideoClock(realtime);
        app->StartApplication();
        REQUIRE(app->StartLauncherActivity().state == session::LifecycleRunState::running);
    }

    void Step(unsigned frames = 1) {
        for (unsigned i = 0; i < frames; ++i) {
            REQUIRE(app->ActivityLifecycle().StepFrame().state == session::LifecycleRunState::running);
            if (auto frame = app->NativeProcess().TakeLatestFrame()) image = std::move(frame);
        }
    }

    void Click(float x, float y) {
        for (bool down : {true, false}) {
            app->ActivityLifecycle().QueueInput(
                {.type = AndroidBoundaryInputType::pointer_button, .x = x, .y = y, .pressed = down});
            Step();
        }
        Step();
    }

    void Key(int code) {
        for (bool down : {true, false}) {
            app->ActivityLifecycle().QueueInput({.type = AndroidBoundaryInputType::key, .code = code, .pressed = down});
            Step();
        }
        Step();
    }

    std::array<int, 4> Pixel(unsigned x, unsigned y) const {
        REQUIRE(image);
        const auto pixels = image->Pixels();
        const auto at = (static_cast<std::size_t>(y) * image->width + x) * 4;
        REQUIRE(at + 4 <= pixels.size());
        return {pixels[at], pixels[at + 1], pixels[at + 2], pixels[at + 3]};
    }

    int Read(std::string_view activity, const char* name) {
        auto& vm = app->DexVm().Vm();
        const dexvm::VmExecutionLockScope lock(vm.ExecutionLock());
        auto& linker = vm.Linker();
        const auto type = linker.ResolveDescriptor("Lorg/ogplay/fixture/window/" + std::string(activity) + ";");
        const auto field = linker.FindFieldRecursive(type, name, "I");
        REQUIRE(field);
        return static_cast<int>(linker.Class(type).static_storage[linker.Field(*field).slot]);
    }

    std::uint64_t Windows() {
        const auto windows = TrySnapshotAndroidUi(app->DexVm().Vm(), *context);
        REQUIRE(windows);
        return windows->windows;
    }
};
}  // namespace

TEST_CASE("P3 ROM ViewRootImpl posts a software frame and preserves pixels outside invalidate") {
    RomWindowApp fixture("WindowActivity");
    CHECK(fixture.Read("WindowActivity", "entryOrientation") == 1);
    CHECK(fixture.context->requested_activity_orientation.load() == 9);
    for (int i = 0; i < 3 && !fixture.image; ++i)
        fixture.Step();
    REQUIRE(fixture.image);
    CHECK(fixture.Pixel(12, 12) == std::array{248, 162, 42, 255});
    CHECK(fixture.Pixel(30, 30) == std::array{27, 57, 93, 255});
    fixture.Click(12, 12);
    CHECK(fixture.context->requested_activity_orientation.load() == 9);
    CHECK(fixture.Pixel(12, 12) == std::array{112, 220, 114, 255});
    CHECK(fixture.Pixel(30, 30) == std::array{27, 57, 93, 255});
    CHECK(fixture.app->Stop().state == ogplay::session::LifecycleRunState::stopped);
}

TEST_CASE("P3 ROM EditText accepts SDL input context commits and Android backspace") {
    RomWindowApp fixture("WidgetsActivity");
    fixture.Step(2);
    fixture.Click(40, 24);
    CHECK(fixture.Read("WidgetsActivity", "imeAccepted") == 1);
    REQUIRE(fixture.context->host_ime_visible.load());
    QueueAndroidTextEvent(*fixture.context, "a", true);
    fixture.Step();
    CHECK(fixture.Read("WidgetsActivity", "textLength") == 1);
    QueueAndroidTextEvent(*fixture.context, "b", false);
    fixture.Step();
    CHECK(fixture.Read("WidgetsActivity", "lastCharacter") == 'b');
    fixture.Key(67);
    CHECK(fixture.Read("WidgetsActivity", "activityKeys") == 12);
    CHECK(fixture.Read("WidgetsActivity", "textLength") == 0);
}

TEST_CASE("P3 ROM Dialog and AlertDialog own the foreground until dismissed") {
    RomWindowApp fixture("WidgetsActivity");
    fixture.Step(2);
    REQUIRE(fixture.Windows() == 1);
    const auto activity_center = fixture.Pixel(160, 200);
    fixture.Click(40, 72);
    REQUIRE(fixture.Windows() == 2);
    CHECK(fixture.Pixel(160, 200) != activity_center);
    REQUIRE(fixture.Read("WidgetsActivity", "dialogY") > 0);
    fixture.Click(static_cast<float>(fixture.Read("WidgetsActivity", "dialogX")),
                  static_cast<float>(fixture.Read("WidgetsActivity", "dialogY")));
    CHECK(fixture.Read("WidgetsActivity", "dialogClicks") == 1);
    CHECK(fixture.Windows() == 1);
    fixture.Click(40, 72);
    REQUIRE(fixture.Windows() == 2);
    fixture.Key(4);
    CHECK(fixture.Read("WidgetsActivity", "dialogKeys") == 12);
    CHECK(fixture.Read("WidgetsActivity", "cancelled") == 1);
    CHECK(fixture.Windows() == 1);
    fixture.Click(40, 120);
    REQUIRE(fixture.Windows() == 2);
    REQUIRE(fixture.Read("WidgetsActivity", "alertY") > 0);
    fixture.Click(static_cast<float>(fixture.Read("WidgetsActivity", "alertX")),
                  static_cast<float>(fixture.Read("WidgetsActivity", "alertY")));
    CHECK(fixture.Read("WidgetsActivity", "alertClicks") == 1);
    CHECK(fixture.Windows() == 1);
    fixture.Click(40, 120);
    REQUIRE(fixture.Windows() == 2);
    fixture.Key(4);
    CHECK(fixture.Read("WidgetsActivity", "cancelled") == 2);
    CHECK(fixture.Windows() == 1);
}

TEST_CASE("P3 ROM PopupWindow Spinner and Toast use WindowManager sessions") {
    RomWindowApp fixture("WidgetsActivity");
    fixture.Step(2);
    fixture.Click(40, 168);
    REQUIRE(fixture.Windows() == 2);
    fixture.Click(40, 210);
    CHECK(fixture.Read("WidgetsActivity", "popupClicks") == 1);
    fixture.Click(40, 216);
    REQUIRE(fixture.Windows() == 2);
    fixture.Key(4);
    CHECK(fixture.Windows() == 1);
    fixture.Click(40, 264);
    REQUIRE(fixture.Windows() == 2);
    fixture.Step(126);
    CHECK(fixture.Windows() == 1);
    fixture.Click(40, 264);
    REQUIRE(fixture.Windows() == 2);
    fixture.Step(126);
    REQUIRE(fixture.Windows() == 2);
    fixture.Step(94);
    CHECK(fixture.Windows() == 1);
}

TEST_CASE("P3 ROM ActivityThread delivers results and orders suspend and restart") {
    RomWindowApp fixture("WidgetsActivity");
    fixture.Step(2);
    CHECK(fixture.Read("WidgetsActivity", "lifecycle") == 12);
    static_cast<void>(fixture.app->ActivityLifecycle().Suspend());
    CHECK(fixture.Read("WidgetsActivity", "lifecycle") == 1234);
    static_cast<void>(fixture.app->ActivityLifecycle().Resume());
    fixture.Step(2);
    CHECK(fixture.Read("WidgetsActivity", "lifecycle") == 1234512);
    fixture.Click(40, 312);
    fixture.Step(2);
    fixture.Click(100, 150);
    fixture.Step(2);
    CHECK(fixture.Read("WidgetsActivity", "result") == 41);
}

TEST_CASE("P3 ROM Activity exit takes focus from the dialog before pause and destroy") {
    for (const bool finish : {false, true}) {
        INFO("finish=" << finish);
        constexpr auto activity = "ExitFocusActivity";
        RomWindowApp fixture(activity);
        fixture.Step(2);
        const auto decor_gained = fixture.Read(activity, "decorGained");
        REQUIRE(decor_gained > 0);
        fixture.Click(160, 200);
        REQUIRE(fixture.Windows() == 2);
        const auto dialog_gained = fixture.Read(activity, "dialogGained");
        REQUIRE(dialog_gained > decor_gained);
        if (finish) {
            fixture.Key(4);
            fixture.Step(2);
            CHECK(SessionExitRequested(*fixture.context));
        }
        CHECK(fixture.app->Stop().state == session::LifecycleRunState::stopped);
        const auto lost = fixture.Read(activity, "dialogLost");
        const auto paused = fixture.Read(activity, "paused");
        CHECK(lost > dialog_gained);
        CHECK(lost < paused);
        CHECK(paused < fixture.Read(activity, "destroyed"));
        CHECK(fixture.Read(activity, "lateFocus") == 0);
        CHECK(fixture.Windows() == 0);
    }
}

TEST_CASE("P3 ROM GLSurfaceView publishes beneath software overlays and retains EGL context") {
    RomWindowApp fixture("GlActivity");
    for (unsigned i = 0; i < 30; ++i) {
        fixture.Step();
        if (fixture.image && fixture.Pixel(60, 60)[1] >= 150) break;
        hal::SleepPreciseNs(1'000'000);
    }
    CHECK(fixture.Pixel(12, 12) == std::array{255, 0, 0, 255});
    CHECK(fixture.Pixel(60, 60)[1] >= 150);
    REQUIRE(fixture.Read("GlActivity", "contexts") == 1);
    CHECK(fixture.Read("GlActivity", "surfaceCreates") == 1);
    static_cast<void>(fixture.app->ActivityLifecycle().Suspend());
    CHECK(fixture.Read("GlActivity", "surfaceDestroys") == 1);
    CHECK(fixture.Read("GlActivity", "validOnDestroy") == 1);
    static_cast<void>(fixture.app->ActivityLifecycle().Resume());
    fixture.Step(10);
    CHECK(fixture.Read("GlActivity", "contexts") == 1);
    CHECK(fixture.Read("GlActivity", "surfaceCreates") == 2);
}

TEST_CASE("P3 ROM VideoView delivers a decoded frame to its ROM Surface") {
    if (!ogplay::video::FfmpegAvailable()) return;
    RomWindowApp fixture("VideoActivity");
    bool video_frame{};
    for (unsigned i = 0; i < 90; ++i) {
        fixture.Step();
        if (fixture.image && fixture.Pixel(160, 80) != std::array{0, 0, 0, 255}) {
            video_frame = true;
            break;
        }
        hal::SleepPreciseNs(1'000'000);
    }
    if (!video_frame) {
        const auto info = TrySnapshotAndroidVideo(*fixture.context);
        MESSAGE("video windows=" << fixture.Windows() << " players=" << (info ? info->size() : 0)
                                 << " prepared=" << fixture.Read("VideoActivity", "prepared")
                                 << " error=" << fixture.Read("VideoActivity", "error"));
        for (const auto& record : fixture.logger.Snapshot(core::LogLevel::warn))
            MESSAGE(record.category << " " << record.message);
    }
    CHECK(video_frame);
}

TEST_CASE("P3 WebView retains disabled load callback while attached to a ROM window") {
    RomWindowApp fixture("WebActivity");
    fixture.Step(2);
    CHECK(fixture.Read("WebActivity", "disabledLoads") == 1);
    CHECK(fixture.Windows() == 1);
    REQUIRE(fixture.image);
    const auto warnings = fixture.ledger.Warnings();
    CHECK(std::ranges::any_of(warnings, [](const auto& warning) {
        return warning.id == std::string(core::kUiDegradedWarningPrefix) + "TextureView.hardwareAcceleration";
    }));
}

TEST_CASE("P3 orientation relaunch saves state and replaces the window at the new size") {
    RomWindowApp fixture("ConfigActivity");
    fixture.Step(2);
    fixture.Click(100, 100);
    fixture.Step(3);
    CHECK(fixture.Read("ConfigActivity", "creates") == 2);
    CHECK(fixture.Read("ConfigActivity", "changes") == 0);
    CHECK(fixture.Read("ConfigActivity", "restored") == 73);
    CHECK(fixture.Read("ConfigActivity", "order") == 45671283);
    CHECK(fixture.Read("ConfigActivity", "width") == 400);
    CHECK(fixture.Read("ConfigActivity", "height") == 320);
    CHECK(fixture.Read("ConfigActivity", "metricWidth") == 400);
    CHECK(fixture.Read("ConfigActivity", "orientation") == 2);
    REQUIRE(fixture.image);
    CHECK(fixture.image->width == 400);
    CHECK(fixture.image->height == 320);
    CHECK(fixture.Windows() == 1);
    CHECK(fixture.Pixel(100, 100) == std::array{32, 160, 64, 255});
    CHECK(fixture.Pixel(399, 319) == std::array{32, 160, 64, 255});
}

TEST_CASE("P3 handled orientation dispatches configuration and relayout without recreation") {
    RomWindowApp fixture("HandledConfigActivity");
    fixture.Step(2);
    fixture.Click(100, 100);
    fixture.Step(3);
    CHECK(fixture.Read("ConfigActivity", "creates") == 1);
    CHECK(fixture.Read("ConfigActivity", "changes") == 1);
    CHECK(fixture.Read("ConfigActivity", "order") == 0);
    CHECK(fixture.Read("ConfigActivity", "width") == 400);
    CHECK(fixture.Read("ConfigActivity", "height") == 320);
    CHECK(fixture.Read("ConfigActivity", "metricWidth") == 400);
    CHECK(fixture.Read("ConfigActivity", "orientation") == 2);
    REQUIRE(fixture.image);
    CHECK(fixture.image->width == 400);
    CHECK(fixture.Pixel(399, 319) == std::array{32, 160, 64, 255});
    CHECK(fixture.Windows() == 1);
}

TEST_CASE("P3 continuous EGL producer completes one swap per Clock advance") {
    for (const bool realtime : {false, true}) {
        INFO("realtime=" << realtime);
        RomWindowApp fixture("GlActivity", realtime);
        for (unsigned i = 0; i < 30 && fixture.Read("GlActivity", "draws") < 3; ++i) {
            fixture.Step();
            hal::SleepPreciseNs(1'000'000);
        }
        REQUIRE(fixture.Read("GlActivity", "draws") >= 3);
        const auto settle = [&] {
            const auto deadline = hal::Clock::SteadyTimestampNs() + 2'000'000'000;
            while (!EglProducersSettled(*fixture.context))
                REQUIRE(hal::Clock::SteadyTimestampNs() < deadline);
        };
        settle();
        for (unsigned i = 0; i < 5; ++i) {
            const auto before = fixture.Read("GlActivity", "draws");
            const auto time = fixture.context->uptime_millis.load();
            if (realtime) CHECK(fixture.app->ActivityLifecycle().ReadyForFrame());
            fixture.Step();
            settle();
            CHECK(fixture.Read("GlActivity", "draws") == before + 1);
            CHECK(fixture.context->uptime_millis.load() == time + 16);
        }
    }
}

TEST_CASE("P3 dirty EGL producer without requestRender does not hold the Clock") {
    for (const bool realtime : {false, true}) {
        INFO("realtime=" << realtime);
        RomWindowApp fixture("DirtyGlActivity", realtime);
        for (unsigned i = 0; i < 30; ++i) {
            fixture.Step();
            hal::SleepPreciseNs(1'000'000);
            if (fixture.image && fixture.Pixel(60, 60)[1] >= 150) break;
        }
        fixture.Step(5);
        const auto before = fixture.Read("GlActivity", "draws");
        const auto time = fixture.context->uptime_millis.load();
        if (realtime) CHECK(fixture.app->ActivityLifecycle().ReadyForFrame());
        fixture.Step(5);
        CHECK(fixture.Read("GlActivity", "draws") == before);
        CHECK(fixture.context->uptime_millis.load() == time + 80);
    }
}

TEST_CASE("P3 slow EGL producer releases the Clock until it swaps again") {
    for (const bool realtime : {false, true}) {
        INFO("realtime=" << realtime);
        RomWindowApp fixture("GlActivity", realtime);
        auto& lifecycle = fixture.app->ActivityLifecycle();
        const auto settle = [&] {
            auto& egl = fixture.context->egl;
            const auto swapped = [&] {
                std::scoped_lock lock(egl.pace_mutex);
                return std::ranges::any_of(egl.producers, [](const auto& item) { return item.second.waiting; });
            };
            const auto deadline = hal::Clock::SteadyTimestampNs() + 2'000'000'000;
            while (!swapped()) {
                REQUIRE(hal::Clock::SteadyTimestampNs() < deadline);
                hal::SleepPreciseNs(1'000'000);
            }
        };
        for (unsigned i = 0; i < 30 && fixture.Read("GlActivity", "draws") < 3; ++i) {
            fixture.Step();
            hal::SleepPreciseNs(1'000'000);
        }
        REQUIRE(fixture.Read("GlActivity", "draws") >= 3);
        settle();
        const auto hold_draw = [&](bool hold) {
            auto& vm = fixture.app->DexVm().Vm();
            const dexvm::VmExecutionLockScope lock(vm.ExecutionLock());
            vm.SetStaticFieldBits("Lorg/ogplay/fixture/window/GlActivity;", "holdDraw", "I", hold);
        };
        constexpr std::uint64_t budget_ns = 30'000'000;
        lifecycle.SetEglProducerWaitBudgetForTesting(budget_ns);
        hold_draw(true);
        if (realtime) hal::SleepPreciseNs(fixture.context->frame_period_ns);
        const auto ticks = fixture.context->uptime_millis.load();
        fixture.Step();
        const auto deadline = hal::Clock::SteadyTimestampNs() + 2'000'000'000;
        while (!fixture.Read("GlActivity", "slowDraws"))
            REQUIRE(hal::Clock::SteadyTimestampNs() < deadline);
        if (realtime) {
            CHECK_FALSE(lifecycle.ReadyForFrame());
            hal::SleepPreciseNs(budget_ns);
            CHECK(lifecycle.ReadyForFrame());
        }
        const auto draws = fixture.Read("GlActivity", "draws");
        fixture.Step(2);
        CHECK(fixture.context->uptime_millis.load() == ticks + 48);
        CHECK(fixture.Read("GlActivity", "draws") == draws);
        const auto warnings = fixture.logger.Snapshot(core::LogLevel::warn, "runtime.egl.pacing");
        CHECK(warnings.size() == 1);
        hold_draw(false);
        settle();
        if (realtime) hal::SleepPreciseNs(fixture.context->frame_period_ns);
        fixture.Step();
        settle();
        CHECK(fixture.Read("GlActivity", "draws") == draws + 2);
    }
}
