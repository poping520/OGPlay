// VideoView real-playback semantics against the deterministic Fake backend:
// frames publish letterboxed to the surface, position follows the shared
// uptime clock, onCompletion fires exactly once per playback, and the
// no-decoder fallback still completes immediately.

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/vfs/vfs.h"
#include "ogplay/video/fake_video_player.h"

namespace {

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

constexpr const char* kGuestVideoPath = "/sdcard/short-mp4v-aac.mp4";

[[nodiscard]] std::vector<std::uint8_t> ReadFixture(const std::string& name) {
    const std::string path =
        std::string(OGPLAY_DEXVM_FIXTURE_DIR) + "/" + name;
    std::ifstream stream(path, std::ios::binary);
    REQUIRE_MESSAGE(stream.good(), "missing fixture: ", path);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream),
                                     std::istreambuf_iterator<char>());
}

[[nodiscard]] ogplay::video::VideoPlayerFactory FakeFactory() {
    return [](const std::filesystem::path&) {
        ogplay::video::VideoMetadata metadata;
        metadata.width = 8U;
        metadata.height = 4U;
        metadata.duration_ms = 1000;
        return std::unique_ptr<ogplay::video::VideoPlayer>(
            std::make_unique<ogplay::video::FakeVideoPlayer>(metadata, 10U));
    };
}

struct VideoVm final {
    JniStringStore strings;
    JniPrimitiveArrayStore arrays;
    JavaObjectModel model;
    DexClassLinker linker;
    ogplay::core::CapabilityLedger ledger;
    ogplay::core::Logger logger;
    std::shared_ptr<DexVmAndroidContext> context;
    VirtualFileSystem vfs;
    Interpreter interpreter;

    explicit VideoVm(ogplay::video::VideoPlayerFactory factory)
        : model(strings, arrays),
          context(std::make_shared<DexVmAndroidContext>()),
          interpreter(
              [this]() -> DexClassLinker& {
                  linker.RegisterIntrinsics(CoreIntrinsicCatalog());
                  linker.RegisterIntrinsics(AndroidIntrinsicCatalog());
                  linker.RegisterDex(ReadFixture("videoview.dex"));
                  linker.Link();
                  return linker;
              }(),
              model,
              [this, &factory]() {
                  context->surface_width = 64U;
                  context->surface_height = 32U;
                  context->vfs = &vfs;
                  context->video_player_factory = std::move(factory);
                  IntrinsicRegistry registry;
                  RegisterAndroidBuiltins(registry, context);
                  return registry;
              }(),
              nullptr, ledger, {}) {
        interpreter.RegisterCoreBuiltins();
        interpreter.SetLogger(&logger);
        vfs.MountHostDirectory(
            "/sdcard",
            std::filesystem::path{OGPLAY_SOURCE_DIR} / "tests/fixtures/video");
    }

    [[nodiscard]] VmObjectRef NewVideoView() {
        return interpreter.NewIntrinsicInstance("Landroid/widget/VideoView;");
    }

    VmValue CallOn(const VmObjectRef receiver, const std::string& name,
                   const std::string& descriptor,
                   std::vector<VmValue> arguments = {}) {
        const auto receiver_class = model.ObjectClass(receiver);
        const auto index =
            linker.FindVtableIndex(receiver_class, name, descriptor);
        REQUIRE_MESSAGE(index.has_value(), name);
        arguments.insert(arguments.begin(), VmValue::Ref(receiver));
        const auto outcome = interpreter.Call(
            linker.Class(receiver_class).vtable[*index], arguments);
        REQUIRE_MESSAGE(!outcome.exception.IsValid(),
                        outcome.exception_message);
        return outcome.value;
    }

    [[nodiscard]] VmObjectRef NewListener() {
        const auto java_class = linker.FindClass("LVideoListener;");
        REQUIRE(java_class.has_value());
        const auto clinit = interpreter.EnsureClassInitialized(*java_class);
        REQUIRE_MESSAGE(!clinit.exception.IsValid(),
                        clinit.exception_message);
        const auto init =
            linker.FindDirectMethod(*java_class, "<init>", "()V");
        REQUIRE(init.has_value());
        const auto listener = model.NewInstance(
            *java_class, linker.Class(*java_class).instance_slots);
        const auto outcome = interpreter.Call(
            *init, std::vector<VmValue>{VmValue::Ref(listener)});
        REQUIRE_MESSAGE(!outcome.exception.IsValid(),
                        outcome.exception_message);
        return listener;
    }

    [[nodiscard]] std::int32_t Completions() {
        const auto java_class = linker.FindClass("LVideoListener;");
        REQUIRE(java_class.has_value());
        const auto method = linker.FindDirectMethod(
            *java_class, "getCompletions", "()I");
        REQUIRE(method.has_value());
        const auto outcome = interpreter.Call(*method, {});
        REQUIRE_MESSAGE(!outcome.exception.IsValid(),
                        outcome.exception_message);
        return outcome.value.AsInt();
    }

    [[nodiscard]] std::size_t Pump(std::vector<std::vector<std::uint8_t>>*
                                       frames = nullptr) {
        std::size_t published = 0;
        const auto error = PumpVideoViews(
            interpreter, *context,
            [&](std::vector<std::uint8_t> rgba8) {
                ++published;
                if (frames != nullptr) frames->push_back(std::move(rgba8));
            });
        REQUIRE_MESSAGE(!error.has_value(), error.value_or(""));
        return published;
    }
};

}  // namespace

TEST_CASE("videoview plays through the fake backend and completes once") {
    VideoVm vm(FakeFactory());
    const auto view = vm.NewVideoView();
    const auto listener = vm.NewListener();
    vm.CallOn(view, "setOnCompletionListener",
              "(Landroid/media/MediaPlayer$OnCompletionListener;)V",
              {VmValue::Ref(listener)});
    vm.CallOn(view, "setVideoPath", "(Ljava/lang/String;)V",
              {VmValue::Ref(vm.interpreter.NewStringUtf8(kGuestVideoPath))});
    CHECK(vm.CallOn(view, "getDuration", "()I").AsInt() == 1000);

    vm.CallOn(view, "start", "()V");
    CHECK(vm.CallOn(view, "getCurrentPosition", "()I").AsInt() == 0);

    std::vector<std::vector<std::uint8_t>> frames;
    CHECK(vm.Pump(&frames) == 1U);  // frame 0 is due at position 0
    REQUIRE(frames.size() == 1U);
    CHECK(frames[0].size() == 64U * 32U * 4U);
    CHECK(vm.Completions() == 0);

    vm.context->uptime_millis = 500;
    CHECK(vm.CallOn(view, "getCurrentPosition", "()I").AsInt() == 500);
    CHECK(vm.Pump() == 1U);  // newest due frame (index 5) exactly once
    CHECK(vm.Pump() == 0U);  // same position -> no new frame

    vm.context->uptime_millis = 1000;
    static_cast<void>(vm.Pump());
    CHECK(vm.Completions() == 1);
    CHECK(vm.CallOn(view, "getCurrentPosition", "()I").AsInt() == 1000);

    // Past the end nothing replays and completion stays single-shot.
    vm.context->uptime_millis = 1200;
    CHECK(vm.Pump() == 0U);
    CHECK(vm.Completions() == 1);

    // start() after completion restarts from zero.
    vm.CallOn(view, "start", "()V");
    CHECK(vm.CallOn(view, "getCurrentPosition", "()I").AsInt() == 0);
    CHECK(vm.Pump() == 1U);
}

TEST_CASE("videoview letterboxes frames onto the surface canvas") {
    VideoVm vm(FakeFactory());
    const auto view = vm.NewVideoView();
    vm.CallOn(view, "setVideoPath", "(Ljava/lang/String;)V",
              {VmValue::Ref(vm.interpreter.NewStringUtf8(kGuestVideoPath))});
    vm.CallOn(view, "start", "()V");
    std::vector<std::vector<std::uint8_t>> frames;
    REQUIRE(vm.Pump(&frames) == 1U);
    const auto& canvas = frames[0];
    // 8x4 source on a 64x32 canvas scales to the full canvas (same aspect):
    // every pixel carries the frame-0 colour.
    const auto expected = ogplay::video::FakeVideoPlayer::FrameColorRgba(0);
    CHECK(canvas[0] == static_cast<std::uint8_t>(expected >> 24U));
    CHECK(canvas[1] == static_cast<std::uint8_t>(expected >> 16U));
    CHECK(canvas[2] == static_cast<std::uint8_t>(expected >> 8U));
    CHECK(canvas[3] == 0xFFU);
}

TEST_CASE("videoview pause freezes and seekTo moves the position") {
    VideoVm vm(FakeFactory());
    const auto view = vm.NewVideoView();
    vm.CallOn(view, "setVideoPath", "(Ljava/lang/String;)V",
              {VmValue::Ref(vm.interpreter.NewStringUtf8(kGuestVideoPath))});
    vm.CallOn(view, "start", "()V");
    vm.context->uptime_millis = 300;
    vm.CallOn(view, "pause", "()V");
    CHECK(vm.CallOn(view, "getCurrentPosition", "()I").AsInt() == 300);
    vm.context->uptime_millis = 600;
    CHECK(vm.CallOn(view, "getCurrentPosition", "()I").AsInt() == 300);

    // Resume-from-checkpoint path (MyVideoView-style seekTo + start).
    vm.CallOn(view, "seekTo", "(I)V", {VmValue::Int(100)});
    CHECK(vm.CallOn(view, "getCurrentPosition", "()I").AsInt() == 100);
    vm.CallOn(view, "start", "()V");
    vm.context->uptime_millis = 700;
    CHECK(vm.CallOn(view, "getCurrentPosition", "()I").AsInt() == 200);

    // Out-of-range seeks clamp instead of failing.
    vm.CallOn(view, "seekTo", "(I)V", {VmValue::Int(5000)});
    CHECK(vm.CallOn(view, "getCurrentPosition", "()I").AsInt() == 1000);
}

TEST_CASE("videoview stopPlayback releases the player") {
    VideoVm vm(FakeFactory());
    const auto view = vm.NewVideoView();
    vm.CallOn(view, "setVideoPath", "(Ljava/lang/String;)V",
              {VmValue::Ref(vm.interpreter.NewStringUtf8(kGuestVideoPath))});
    CHECK(vm.CallOn(view, "getDuration", "()I").AsInt() == 1000);
    vm.CallOn(view, "stopPlayback", "()V");
    CHECK(vm.CallOn(view, "getDuration", "()I").AsInt() == 0);
    CHECK(vm.context->video_views.empty());
}

TEST_CASE("videoview without a decoder falls back to immediate completion") {
    VideoVm vm(ogplay::video::VideoPlayerFactory{});
    const auto view = vm.NewVideoView();
    const auto listener = vm.NewListener();
    vm.CallOn(view, "setOnCompletionListener",
              "(Landroid/media/MediaPlayer$OnCompletionListener;)V",
              {VmValue::Ref(listener)});
    vm.CallOn(view, "setVideoPath", "(Ljava/lang/String;)V",
              {VmValue::Ref(vm.interpreter.NewStringUtf8(kGuestVideoPath))});
    CHECK(vm.CallOn(view, "getDuration", "()I").AsInt() == 0);
    vm.CallOn(view, "start", "()V");
    CHECK(vm.Completions() == 1);
}

TEST_CASE("videoview with a missing file falls back to immediate completion") {
    VideoVm vm(FakeFactory());
    const auto view = vm.NewVideoView();
    const auto listener = vm.NewListener();
    vm.CallOn(view, "setOnCompletionListener",
              "(Landroid/media/MediaPlayer$OnCompletionListener;)V",
              {VmValue::Ref(listener)});
    vm.CallOn(view, "setVideoPath", "(Ljava/lang/String;)V",
              {VmValue::Ref(
                  vm.interpreter.NewStringUtf8("/sdcard/missing.mp4"))});
    vm.CallOn(view, "start", "()V");
    CHECK(vm.Completions() == 1);
    CHECK(vm.Pump() == 0U);
}
