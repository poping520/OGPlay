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

[[nodiscard]] ogplay::video::VideoPlayerFactory FakeAudioFactory(
    const std::uint32_t sample_rate, const std::uint8_t channels) {
    return [sample_rate, channels](const std::filesystem::path&) {
        ogplay::video::VideoMetadata metadata;
        metadata.width = 8U;
        metadata.height = 4U;
        metadata.duration_ms = 1000;
        metadata.audio_sample_rate = sample_rate;
        metadata.audio_channels = channels;
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
                  linker.RegisterIntrinsics(AndroidIntrinsicCatalog(context));
                  linker.RegisterDex(ReadFixture("videoview.dex"));
                  linker.Link();
                  return linker;
              }(),
              model, nullptr, ledger, {}) {
        context->surface_width = 64U;
        context->surface_height = 32U;
        context->vfs = &vfs;
        context->video_player_factory = std::move(factory);
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

TEST_CASE("videoview media controls follow prepared player state") {
    VideoVm vm(FakeFactory());
    const auto view = vm.NewVideoView();
    CHECK_FALSE(vm.CallOn(view, "canSeekForward", "()Z").AsInt());
    CHECK_FALSE(vm.CallOn(view, "canSeekBackward", "()Z").AsInt());
    CHECK_FALSE(vm.CallOn(view, "canPause", "()Z").AsInt());

    vm.CallOn(view, "setVideoPath", "(Ljava/lang/String;)V",
              {VmValue::Ref(vm.interpreter.NewStringUtf8(kGuestVideoPath))});
    CHECK(vm.CallOn(view, "canSeekForward", "()Z").AsInt() == 1);
    CHECK(vm.CallOn(view, "canSeekBackward", "()Z").AsInt() == 1);
    CHECK(vm.CallOn(view, "canPause", "()Z").AsInt() == 1);

    vm.CallOn(view, "stopPlayback", "()V");
    CHECK_FALSE(vm.CallOn(view, "canSeekForward", "()Z").AsInt());
    CHECK_FALSE(vm.CallOn(view, "canSeekBackward", "()Z").AsInt());
    CHECK_FALSE(vm.CallOn(view, "canPause", "()Z").AsInt());
}

TEST_CASE("videoview fallback completion is deferred to the video pump") {
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
    CHECK(vm.Completions() == 0);
    CHECK(vm.Pump() == 0U);
    CHECK(vm.Completions() == 1);
    CHECK(vm.Pump() == 0U);
    CHECK(vm.Completions() == 1);
}

TEST_CASE("videoview error listener registration can be replaced and cleared") {
    VideoVm vm(FakeFactory());
    const auto view = vm.NewVideoView();
    const auto first = vm.NewListener();
    const auto second = vm.NewListener();
    constexpr auto descriptor =
        "(Landroid/media/MediaPlayer$OnErrorListener;)V";
    vm.CallOn(view, "setOnErrorListener", descriptor, {VmValue::Ref(first)});
    CHECK(vm.context->video_errors.at(view.Value()) == first);
    vm.CallOn(view, "setOnErrorListener", descriptor, {VmValue::Ref(second)});
    CHECK(vm.context->video_errors.at(view.Value()) == second);
    vm.CallOn(view, "setOnErrorListener", descriptor,
              {VmValue::Ref(VmObjectRef{})});
    CHECK_FALSE(vm.context->video_errors.contains(view.Value()));
}

TEST_CASE("MediaPlayer accepts error and prepared listeners and resets") {
    VideoVm vm(FakeFactory());
    const auto player =
        vm.interpreter.NewIntrinsicInstance("Landroid/media/MediaPlayer;");
    // Resolution itself is the contract: pvz's audioPlay registers an error
    // listener before any playback, and AOSP accepts null to clear.
    vm.CallOn(player, "setOnErrorListener",
              "(Landroid/media/MediaPlayer$OnErrorListener;)V",
              {VmValue::Ref(VmObjectRef{})});
    vm.CallOn(player, "setOnPreparedListener",
              "(Landroid/media/MediaPlayer$OnPreparedListener;)V",
              {VmValue::Ref(VmObjectRef{})});
    vm.CallOn(player, "reset", "()V");
    CHECK(vm.CallOn(player, "isPlaying", "()Z").AsInt() == 0);
}

TEST_CASE("WifiInfo without a connection does not invent a MAC address") {
    VideoVm vm(FakeFactory());
    const auto info =
        vm.interpreter.NewIntrinsicInstance("Landroid/net/wifi/WifiInfo;");
    CHECK_FALSE(vm.CallOn(info, "getMacAddress", "()Ljava/lang/String;")
                    .ref.IsValid());
}

TEST_CASE("application Context identity survives Activity replacement") {
    VideoVm vm(FakeFactory());
    vm.context->package_name = "org.ogplay.inheritance";
    const auto base =
        vm.interpreter.NewIntrinsicInstance("Landroid/content/Context;");
    const auto application =
        vm.interpreter.NewIntrinsicInstance("Landroid/app/Application;");
    const auto first =
        vm.interpreter.NewIntrinsicInstance("Landroid/app/Activity;");
    const auto second =
        vm.interpreter.NewIntrinsicInstance("Landroid/app/Activity;");
    vm.CallOn(first, "attachBaseContext", "(Landroid/content/Context;)V",
              {VmValue::Ref(base)});
    vm.CallOn(application, "attachBaseContext",
              "(Landroid/content/Context;)V", {VmValue::Ref(base)});
    vm.CallOn(second, "attachBaseContext", "(Landroid/content/Context;)V",
              {VmValue::Ref(base)});
    const auto first_context = vm.CallOn(
        first, "getApplicationContext", "()Landroid/content/Context;").ref;
    const auto second_context = vm.CallOn(
        second, "getApplicationContext", "()Landroid/content/Context;").ref;
    CHECK(first_context.IsValid());
    CHECK(first_context == second_context);
    CHECK(first_context != first);
    const auto package = vm.CallOn(
        application, "getPackageName", "()Ljava/lang/String;").ref;
    CHECK(vm.interpreter.StringUtf8(package) == "org.ogplay.inheritance");

    const auto application_class =
        vm.linker.ResolveDescriptor("Landroid/app/Application;");
    const auto declared = vm.linker.MethodsOf(application_class);
    REQUIRE(declared.size() == 2);
    CHECK(vm.linker.Method(declared[0]).name == "<init>");
    CHECK(vm.linker.Method(declared[1]).name == "onCreate");
}

TEST_CASE("Android 4.4 override callbacks preserve framework visibility") {
    VideoVm vm(FakeFactory());
    struct ExpectedMethod final {
        const char* owner;
        const char* name;
        const char* descriptor;
    };
    constexpr ExpectedMethod protected_methods[] = {
        {"Landroid/app/Activity;", "onCreate", "(Landroid/os/Bundle;)V"},
        {"Landroid/app/Activity;", "onStart", "()V"},
        {"Landroid/app/Activity;", "onRestart", "()V"},
        {"Landroid/app/Activity;", "onResume", "()V"},
        {"Landroid/app/Activity;", "onPause", "()V"},
        {"Landroid/app/Activity;", "onStop", "()V"},
        {"Landroid/app/Activity;", "onDestroy", "()V"},
        {"Landroid/content/ContextWrapper;", "attachBaseContext",
         "(Landroid/content/Context;)V"},
        {"Landroid/app/IntentService;", "onHandleIntent",
         "(Landroid/content/Intent;)V"},
        {"Landroid/view/View;", "onSizeChanged", "(IIII)V"},
        {"Landroid/os/AsyncTask;", "onPreExecute", "()V"},
        {"Landroid/os/AsyncTask;", "onPostExecute",
         "(Ljava/lang/Object;)V"},
        {"Landroid/os/AsyncTask;", "onProgressUpdate",
         "([Ljava/lang/Object;)V"},
        {"Landroid/os/AsyncTask;", "onCancelled",
         "(Ljava/lang/Object;)V"},
        {"Landroid/os/AsyncTask;", "doInBackground",
         "([Ljava/lang/Object;)Ljava/lang/Object;"},
        {"Landroid/os/ResultReceiver;", "onReceiveResult",
         "(ILandroid/os/Bundle;)V"},
        {"Landroid/os/HandlerThread;", "onLooperPrepared", "()V"},
    };
    for (const auto& expected : protected_methods) {
        CAPTURE(expected.owner);
        CAPTURE(expected.name);
        CAPTURE(expected.descriptor);
        const auto owner = vm.linker.ResolveDescriptor(expected.owner);
        const auto slot = vm.linker.FindVtableIndex(
            owner, expected.name, expected.descriptor);
        REQUIRE(slot.has_value());
        const auto& method =
            vm.linker.Method(vm.linker.Class(owner).vtable[*slot]);
        CHECK(method.owner == owner);
        CHECK((method.access_flags & 0x0007U) == 0x0004U);
    }

    const auto activity =
        vm.linker.ResolveDescriptor("Landroid/app/Activity;");
    const auto public_slot = vm.linker.FindVtableIndex(
        activity, "onConfigurationChanged",
        "(Landroid/content/res/Configuration;)V");
    REQUIRE(public_slot.has_value());
    CHECK((vm.linker.Method(vm.linker.Class(activity).vtable[*public_slot])
               .access_flags &
           0x0007U) == 0x0001U);

    const auto async_task =
        vm.linker.ResolveDescriptor("Landroid/os/AsyncTask;");
    CHECK((vm.linker.Class(async_task).access_flags & 0x0400U) != 0U);
}

TEST_CASE("AnyVideoPlaying reports only actively playing views") {
    VideoVm vm(FakeFactory());
    CHECK_FALSE(AnyVideoPlaying(*vm.context));
    const auto view = vm.NewVideoView();
    vm.CallOn(view, "setVideoPath", "(Ljava/lang/String;)V",
              {VmValue::Ref(vm.interpreter.NewStringUtf8(kGuestVideoPath))});
    CHECK_FALSE(AnyVideoPlaying(*vm.context));
    vm.CallOn(view, "start", "()V");
    CHECK(AnyVideoPlaying(*vm.context));
    vm.CallOn(view, "pause", "()V");
    CHECK_FALSE(AnyVideoPlaying(*vm.context));
    vm.CallOn(view, "start", "()V");
    vm.CallOn(view, "stopPlayback", "()V");
    CHECK_FALSE(AnyVideoPlaying(*vm.context));
}

TEST_CASE("video audio mixes into the stereo output with resampling") {
    // Mono 8 kHz ramp into 16 kHz stereo: every source sample lands twice
    // on both channels, on top of the existing buffer content.
    VideoVm vm(FakeAudioFactory(8000U, 1U));
    const auto view = vm.NewVideoView();
    vm.CallOn(view, "setVideoPath", "(Ljava/lang/String;)V",
              {VmValue::Ref(vm.interpreter.NewStringUtf8(kGuestVideoPath))});
    vm.CallOn(view, "start", "()V");

    std::vector<std::int16_t> buffer(16U, 100);
    CHECK(MixVideoPcmIntoStereo(*vm.context, buffer, 16000U) == 1U);
    const std::vector<std::int16_t> expected{
        100, 100, 100, 100, 101, 101, 101, 101,
        102, 102, 102, 102, 103, 103, 103, 103};
    CHECK(buffer == expected);
}

TEST_CASE("video audio resampling stays continuous across pump batches") {
    // 8 kHz into 12 kHz: source index sequence 0,0,1,2,2,3 must not repeat
    // or skip when the six frames split into two batches of three.
    VideoVm vm(FakeAudioFactory(8000U, 2U));
    const auto view = vm.NewVideoView();
    vm.CallOn(view, "setVideoPath", "(Ljava/lang/String;)V",
              {VmValue::Ref(vm.interpreter.NewStringUtf8(kGuestVideoPath))});
    vm.CallOn(view, "start", "()V");

    std::vector<std::int16_t> first(6U, 0);
    std::vector<std::int16_t> second(6U, 0);
    CHECK(MixVideoPcmIntoStereo(*vm.context, first, 12000U) == 1U);
    CHECK(MixVideoPcmIntoStereo(*vm.context, second, 12000U) == 1U);
    CHECK(first == std::vector<std::int16_t>{0, 0, 0, 0, 1, 1});
    CHECK(second == std::vector<std::int16_t>{2, 2, 2, 2, 3, 3});
}

TEST_CASE("paused, stopped and audioless videos contribute silence") {
    VideoVm vm(FakeAudioFactory(8000U, 1U));
    const auto view = vm.NewVideoView();
    vm.CallOn(view, "setVideoPath", "(Ljava/lang/String;)V",
              {VmValue::Ref(vm.interpreter.NewStringUtf8(kGuestVideoPath))});
    std::vector<std::int16_t> buffer(8U, 0);
    // Not started yet.
    CHECK(MixVideoPcmIntoStereo(*vm.context, buffer, 16000U) == 0U);
    vm.CallOn(view, "start", "()V");
    vm.CallOn(view, "pause", "()V");
    CHECK(MixVideoPcmIntoStereo(*vm.context, buffer, 16000U) == 0U);
    vm.CallOn(view, "stopPlayback", "()V");
    CHECK(MixVideoPcmIntoStereo(*vm.context, buffer, 16000U) == 0U);
    CHECK(buffer == std::vector<std::int16_t>(8U, 0));

    // A playing stream without an audio track also stays silent.
    VideoVm silent(FakeFactory());
    const auto silent_view = silent.NewVideoView();
    silent.CallOn(silent_view, "setVideoPath", "(Ljava/lang/String;)V",
                  {VmValue::Ref(
                      silent.interpreter.NewStringUtf8(kGuestVideoPath))});
    silent.CallOn(silent_view, "start", "()V");
    CHECK(MixVideoPcmIntoStereo(*silent.context, buffer, 16000U) == 0U);
}

TEST_CASE("videoview missing file completion is deferred to the video pump") {
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
    CHECK(vm.Completions() == 0);
    CHECK(vm.Pump() == 0U);
    CHECK(vm.Completions() == 1);
    CHECK(vm.Pump() == 0U);
}
