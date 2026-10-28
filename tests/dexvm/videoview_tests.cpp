#include "ogplay/runtime/jni/host_copy_payload_provider.h"
#include "boot_dex.h"
#include "container_traversal_fixture.h"
// VideoView real-playback semantics against the deterministic Fake backend:
// frames publish letterboxed to the surface, position follows the shared
// uptime clock, onCompletion fires exactly once per playback, and the
// failures deliver error without fabricating completion.

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "ogplay/audio/encoded_music.h"
#include "ogplay/loader/apk.h"
#include "ogplay/runtime/dexvm/io_runtime.h"
#include "ogplay/runtime/integration/dexvm_io_vfs.h"
#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/session/ui_compositor.h"
#include "ogplay/video/ffmpeg_video_player.h"
#include "ogplay/runtime/vfs/vfs.h"
#include "ogplay/video/fake_video_player.h"
#include "ogplay/video/rgba_canvas.h"

namespace {

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

constexpr const char* kGuestVideoPath = "/sdcard/short-mp4v-aac.mp4";

[[nodiscard]] std::vector<std::uint8_t> ReadFixture(const std::string& name) {
    const std::string path = std::string(OGPLAY_DEXVM_FIXTURE_DIR) + "/" + name;
    std::ifstream stream(path, std::ios::binary);
    REQUIRE_MESSAGE(stream.good(), "missing fixture: ", path);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
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

[[nodiscard]] ogplay::video::VideoSourcePlayerFactory FakeSourceFactory(bool& opened, std::size_t& bytes_read) {
    return [&opened, &bytes_read](std::shared_ptr<const ogplay::video::VideoDataSource> source) {
        opened = true;
        std::array<std::byte, 4> header{};
        bytes_read += source->ReadAt(0, header);
        ogplay::video::VideoMetadata metadata;
        metadata.width = 8U;
        metadata.height = 4U;
        metadata.duration_ms = 1000;
        return std::unique_ptr<ogplay::video::VideoPlayer>(
            std::make_unique<ogplay::video::FakeVideoPlayer>(metadata, 10U));
    };
}

[[nodiscard]] ogplay::video::VideoPlayerFactory FakeAudioFactory(const std::uint32_t sample_rate,
                                                                 const std::uint8_t channels) {
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

void Append16(std::vector<std::byte>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::byte>(value));
    bytes.push_back(static_cast<std::byte>(value >> 8U));
}

void Append32(std::vector<std::byte>& bytes, std::uint32_t value) {
    Append16(bytes, static_cast<std::uint16_t>(value));
    Append16(bytes, static_cast<std::uint16_t>(value >> 16U));
}

std::vector<std::byte> MakeStoredZip(const std::string_view name, const std::span<const std::byte> payload) {
    std::uint32_t crc = 0xffffffffU;
    for (const auto byte : payload) {
        crc ^= std::to_integer<std::uint8_t>(byte);
        for (unsigned bit = 0; bit < 8; ++bit) {
            const auto mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xedb88320U & mask);
        }
    }
    crc = ~crc;
    const auto append_name = [&](std::vector<std::byte>& bytes) {
        for (const auto value : name) {
            bytes.push_back(static_cast<std::byte>(value));
        }
    };
    std::vector<std::byte> bytes;
    Append32(bytes, 0x04034b50U);
    Append16(bytes, 20);
    Append16(bytes, 0);
    Append16(bytes, 0);
    Append16(bytes, 0);
    Append16(bytes, 0);
    Append32(bytes, crc);
    Append32(bytes, static_cast<std::uint32_t>(payload.size()));
    Append32(bytes, static_cast<std::uint32_t>(payload.size()));
    Append16(bytes, static_cast<std::uint16_t>(name.size()));
    Append16(bytes, 0);
    append_name(bytes);
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    const auto central_offset = static_cast<std::uint32_t>(bytes.size());
    Append32(bytes, 0x02014b50U);
    Append16(bytes, 20);
    Append16(bytes, 20);
    Append16(bytes, 0);
    Append16(bytes, 0);
    Append16(bytes, 0);
    Append16(bytes, 0);
    Append32(bytes, crc);
    Append32(bytes, static_cast<std::uint32_t>(payload.size()));
    Append32(bytes, static_cast<std::uint32_t>(payload.size()));
    Append16(bytes, static_cast<std::uint16_t>(name.size()));
    Append16(bytes, 0);
    Append16(bytes, 0);
    Append16(bytes, 0);
    Append16(bytes, 0);
    Append32(bytes, 0);
    Append32(bytes, 0);
    append_name(bytes);
    const auto central_size = static_cast<std::uint32_t>(bytes.size()) - central_offset;
    Append32(bytes, 0x06054b50U);
    Append16(bytes, 0);
    Append16(bytes, 0);
    Append16(bytes, 1);
    Append16(bytes, 1);
    Append32(bytes, central_size);
    Append32(bytes, central_offset);
    Append16(bytes, 0);
    return bytes;
}

struct VideoVm final {
    ogplay::core::CapabilityLedger ledger;
    ogplay::core::Logger logger;
    std::shared_ptr<DexVmAndroidContext> context;
    VirtualFileSystem vfs;
    DexVmIoVfsAdapter io_file_system{vfs};
    ogplay::audio::EncodedMusicMixer music;
    std::unique_ptr<ogplay::test::container_traversal::AndroidTestRuntime> runtime;
    JavaObjectModel& model;
    DexClassLinker& linker;
    Interpreter& interpreter;
    VmThreadRuntime* threads{};

    explicit VideoVm(ogplay::video::VideoPlayerFactory factory, const InterpreterConfig config = {},
                     std::vector<IntrinsicClassDecl> extra = {})
        : context(std::make_shared<DexVmAndroidContext>()), runtime([&] {
              context->surface_width = 64;
              context->surface_height = 32;
              ogplay::test::BindBootDexArchive(*context);
              auto catalog = AndroidIntrinsicCatalog(context);
              catalog.insert(catalog.end(), std::make_move_iterator(extra.begin()),
                             std::make_move_iterator(extra.end()));
              return std::make_unique<ogplay::test::container_traversal::AndroidTestRuntime>(
                  context, ledger, logger, catalog, ReadFixture("videoview.dex"), config, false, &vfs);
          }()),
          model(runtime->bridge->Model()), linker(runtime->bridge->Linker()), interpreter(runtime->bridge->Vm()) {
        interpreter.Monitors().SetTimeSource([state = context] { return state->uptime_millis.load(); });
        context->surface_width = 64U;
        context->surface_height = 32U;
        context->vfs = &vfs;
        interpreter.IO().SetFileSystem(&io_file_system);
        context->encoded_music = &music;
        threads = &runtime->bridge->Threads();
        context->threads = threads;
        const auto looper = linker.ResolveDescriptor("Landroid/os/Looper;");
        const auto prepare = linker.FindDirectMethod(looper, "prepareMainLooper", "()V");
        REQUIRE(prepare.has_value());
        const auto prepared = interpreter.Call(*prepare, {});
        REQUIRE_MESSAGE(!prepared.exception.IsValid(), prepared.exception_message);

        if (factory) {
            context->video_source_player_factory =
                [factory = std::move(factory)](std::shared_ptr<const ogplay::video::VideoDataSource>) {
                    return factory({});
                };
        }
        interpreter.SetLogger(&logger);
        vfs.MountHostDirectory("/sdcard", std::filesystem::path{OGPLAY_SOURCE_DIR} / "tests/fixtures/video");
    }

    VmObjectRef content_root;

    void AttachView(VmObjectRef view, VmObjectRef owner) {
        if (!content_root.IsValid()) {
            content_root = interpreter.NewIntrinsicInstance("Landroid/widget/FrameLayout;");
            const auto type = linker.ResolveDescriptor("Landroid/widget/FrameLayout;");
            const auto initialized = interpreter.EnsureClassInitialized(type);
            REQUIRE_MESSAGE(!initialized.exception.IsValid(), initialized.exception_message);
            const auto ctor = linker.FindDirectMethod(type, "<init>", "(Landroid/content/Context;)V");
            REQUIRE(ctor);
            const auto result = interpreter.Call(*ctor, std::array{VmValue::Ref(content_root), VmValue::Ref(owner)});
            REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
            context->content_view = content_root;
        }
        CallOn(content_root, "addView", "(Landroid/view/View;)V", {VmValue::Ref(view)});
        static_cast<void>(EnsureAndroidUiTraversal(interpreter, *context, AndroidUiTraversalReason::OwnerTick,
                                                   AndroidUiTraversalPhase::ResolveOnly));
    }

    [[nodiscard]] VmObjectRef NewVideoView() {
        const auto view = interpreter.NewIntrinsicInstance("Landroid/widget/VideoView;");
        const auto owner = linker.ResolveDescriptor("Landroid/widget/VideoView;");
        const auto ctor = linker.FindDirectMethod(owner, "<init>", "(Landroid/content/Context;)V");
        REQUIRE(ctor.has_value());
        const auto activity = interpreter.NewIntrinsicInstance("Landroid/content/Context;");
        const auto result = interpreter.Call(*ctor, std::array{VmValue::Ref(view), VmValue::Ref(activity)});
        REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
        const auto node = FindViewUiNode(*context, view.Value());
        REQUIRE(node.has_value());
        AttachView(view, activity);
        return view;
    }

    [[nodiscard]] VmObjectRef NewPlainPlayer() {
        const auto player = interpreter.NewIntrinsicInstance("Landroid/media/MediaPlayer;");
        const auto owner = linker.ResolveDescriptor("Landroid/media/MediaPlayer;");
        const auto ctor = linker.FindDirectMethod(owner, "<init>", "()V");
        REQUIRE(ctor);
        const auto result = interpreter.Call(*ctor, std::array{VmValue::Ref(player)});
        REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
        return player;
    }

    [[nodiscard]] VmObjectRef NewSurfaceView() {
        const auto view = interpreter.NewIntrinsicInstance("Landroid/view/SurfaceView;");
        const auto owner = linker.ResolveDescriptor("Landroid/view/SurfaceView;");
        const auto ctor = linker.FindDirectMethod(owner, "<init>", "(Landroid/content/Context;)V");
        REQUIRE(ctor);
        const auto activity = interpreter.NewIntrinsicInstance("Landroid/content/Context;");
        const auto result = interpreter.Call(*ctor, std::array{VmValue::Ref(view), VmValue::Ref(activity)});
        REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
        const auto node = *FindViewUiNode(*context, view.Value());
        AttachView(view, activity);
        context->managed_host_surface_open = true;
        const auto error = AttachSurfaceViewSubtree(interpreter, *context, node);
        REQUIRE_MESSAGE(!error, error.value_or(""));
        const auto pumped = PumpAndroidSurfaceEvents(interpreter, *context);
        REQUIRE_MESSAGE(!pumped, pumped.value_or(""));
        return view;
    }

    void PreparePlain(VmObjectRef player, VmObjectRef surface) {
        CallOn(player, "setDataSource", "(Ljava/lang/String;)V",
               {VmValue::Ref(interpreter.NewStringUtf8(kGuestVideoPath))});
        const auto holder = CallOn(surface, "getHolder", "()Landroid/view/SurfaceHolder;").ref;
        CallOn(player, "setDisplay", "(Landroid/view/SurfaceHolder;)V", {VmValue::Ref(holder)});
        CallOn(player, "prepare", "()V");
    }

    void PumpMediaEvents() {
        static_cast<void>(Pump());
        const auto error = PumpAndroidAudioTracks(interpreter, *context);
        REQUIRE_MESSAGE(!error, error.value_or(""));
    }

    VmValue CallOn(const VmObjectRef receiver, const std::string& name, const std::string& descriptor,
                   std::vector<VmValue> arguments = {}) {
        const auto receiver_class = model.ObjectClass(receiver);
        const auto index = linker.FindVtableIndex(receiver_class, name, descriptor);
        REQUIRE_MESSAGE(index.has_value(), name);
        arguments.insert(arguments.begin(), VmValue::Ref(receiver));
        const auto outcome = interpreter.Call(linker.Class(receiver_class).vtable[*index], arguments);
        INFO(name, descriptor);
        std::string exception_detail;
        if (outcome.exception.IsValid()) {
            exception_detail = linker.Class(model.ObjectClass(outcome.exception)).descriptor;
            for (const auto& frame : outcome.exception_stack) {
                exception_detail +=
                    "\n" + frame.class_descriptor + "->" + frame.method_name + " pc=" + std::to_string(frame.pc);
            }
        }
        REQUIRE_MESSAGE(!outcome.exception.IsValid(), exception_detail, outcome.exception_message);
        return outcome.value;
    }

    [[nodiscard]] VmObjectRef NewListener() {
        const auto java_class = linker.FindClass("LVideoListener;");
        REQUIRE(java_class.has_value());
        const auto clinit = interpreter.EnsureClassInitialized(*java_class);
        REQUIRE_MESSAGE(!clinit.exception.IsValid(), clinit.exception_message);
        const auto init = linker.FindDirectMethod(*java_class, "<init>", "()V");
        REQUIRE(init.has_value());
        const auto listener = model.NewInstance(*java_class, linker.Class(*java_class).instance_slots);
        const auto outcome = interpreter.Call(*init, std::vector<VmValue>{VmValue::Ref(listener)});
        REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
        return listener;
    }

    [[nodiscard]] std::int32_t Count(const char* name) {
        const auto java_class = linker.FindClass("LVideoListener;");
        REQUIRE(java_class.has_value());
        const auto method = linker.FindDirectMethod(*java_class, name, "()I");
        REQUIRE(method.has_value());
        const auto outcome = interpreter.Call(*method, {});
        REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
        return outcome.value.AsInt();
    }

    [[nodiscard]] std::int32_t Completions() { return Count("getCompletions"); }

    [[nodiscard]] std::int32_t Prepared() { return Count("getPrepared"); }

    [[nodiscard]] std::int32_t Errors() { return Count("getErrors"); }

    [[nodiscard]] VmObjectRef Player(const char* name = "getPlayer") {
        const auto owner = linker.ResolveDescriptor("LVideoListener;");
        const auto method = linker.FindDirectMethod(owner, name, "()Landroid/media/MediaPlayer;");
        REQUIRE(method.has_value());
        const auto result = interpreter.Call(*method, {});
        REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
        return result.value.ref;
    }

    [[nodiscard]] std::size_t Pump(std::vector<std::vector<std::uint8_t>>* frames = nullptr) {
        std::size_t published = 0;
        const auto error = PumpVideoViews(interpreter, *context, [&](std::vector<std::uint8_t> rgba8) {
            ++published;
            if (frames != nullptr) frames->push_back(std::move(rgba8));
        });
        std::string diagnostics;
        for (const auto& record : logger.Snapshot(ogplay::core::LogLevel::warn))
            diagnostics += record.message + "\n";
        INFO(diagnostics);
        REQUIRE_MESSAGE(!error.has_value(), error.value_or(""));
        if (context->content_view.IsValid())
            static_cast<void>(EnsureAndroidUiTraversal(interpreter, *context, AndroidUiTraversalReason::OwnerTick,
                                                       AndroidUiTraversalPhase::ResolveOnly));
        return published;
    }
};

}  // namespace

TEST_CASE("videoview plays through the fake backend and completes once") {
    VideoVm vm(FakeFactory());
    const auto view = vm.NewVideoView();
    const auto listener = vm.NewListener();
    vm.CallOn(view, "setOnCompletionListener", "(Landroid/media/MediaPlayer$OnCompletionListener;)V",
              {VmValue::Ref(listener)});
    vm.CallOn(view, "setVideoPath", "(Ljava/lang/String;)V",
              {VmValue::Ref(vm.interpreter.NewStringUtf8(kGuestVideoPath))});
    static_cast<void>(vm.Pump());
    CHECK(vm.CallOn(view, "getDuration", "()I").AsInt() == 1000);

    vm.CallOn(view, "start", "()V");
    CHECK(vm.CallOn(view, "getCurrentPosition", "()I").AsInt() == 0);
    const auto origin = vm.context->uptime_millis.load();

    std::vector<std::vector<std::uint8_t>> frames;
    CHECK(vm.Pump(&frames) == 1U);  // frame 0 is due at position 0
    REQUIRE(frames.size() == 1U);
    CHECK(frames[0].size() == 64U * 32U * 4U);
    CHECK(vm.Completions() == 0);

    vm.context->uptime_millis = origin + 500;
    CHECK(vm.CallOn(view, "getCurrentPosition", "()I").AsInt() == 500);
    CHECK(vm.Pump() == 1U);  // newest due frame (index 5) exactly once
    CHECK(vm.Pump() == 0U);  // same position -> no new frame

    vm.context->uptime_millis = origin + 1000;
    static_cast<void>(vm.Pump());
    CHECK(vm.Completions() == 1);
    CHECK(vm.CallOn(view, "getCurrentPosition", "()I").AsInt() == 1000);

    // Past the end nothing replays and completion stays single-shot.
    vm.context->uptime_millis = origin + 1200;
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
    static_cast<void>(vm.Pump());
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
    static_cast<void>(vm.Pump());
    vm.CallOn(view, "start", "()V");
    const auto origin = vm.context->uptime_millis.load();
    vm.context->uptime_millis = origin + 300;
    vm.CallOn(view, "pause", "()V");
    CHECK(vm.CallOn(view, "getCurrentPosition", "()I").AsInt() == 300);
    vm.context->uptime_millis = origin + 600;
    CHECK(vm.CallOn(view, "getCurrentPosition", "()I").AsInt() == 300);

    // Resume-from-checkpoint path (MyVideoView-style seekTo + start).
    vm.CallOn(view, "seekTo", "(I)V", {VmValue::Int(100)});
    CHECK(vm.CallOn(view, "getCurrentPosition", "()I").AsInt() == 100);
    CHECK(vm.Pump() == 1U);  // seeking while paused presents the selected frame
    CHECK_FALSE(vm.CallOn(view, "isPlaying", "()Z").AsInt());
    vm.CallOn(view, "start", "()V");
    const auto resume = vm.context->uptime_millis.load();
    vm.context->uptime_millis = resume + 100;
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
    static_cast<void>(vm.Pump());
    CHECK(vm.CallOn(view, "getDuration", "()I").AsInt() == 1000);
    vm.CallOn(view, "stopPlayback", "()V");
    CHECK(vm.CallOn(view, "getDuration", "()I").AsInt() == -1);
    CHECK(vm.context->video_views.empty());
}

TEST_CASE("videoview production source factory consumes a VFS lease") {
    VideoVm vm(ogplay::video::VideoPlayerFactory{});
    bool opened{};
    std::size_t bytes_read{};
    vm.context->video_source_player_factory = FakeSourceFactory(opened, bytes_read);
    const auto view = vm.NewVideoView();
    vm.CallOn(view, "setVideoPath", "(Ljava/lang/String;)V",
              {VmValue::Ref(vm.interpreter.NewStringUtf8(kGuestVideoPath))});
    static_cast<void>(vm.Pump());
    CHECK(opened);
    CHECK(bytes_read == 4U);
    CHECK(vm.CallOn(view, "getDuration", "()I").AsInt() == 1000);
}

TEST_CASE("videoview source factory consumes an APK-backed VFS lease") {
    VideoVm vm(ogplay::video::VideoPlayerFactory{});
    const std::array payload{std::byte{'v'}, std::byte{'i'}, std::byte{'d'}, std::byte{'0'}};
    unsigned full_reads{};
    const std::vector<ogplay::runtime::VfsLazyMountEntry> entries{{
        "movie.bin",
        payload.size(),
        [&] {
            ++full_reads;
            return std::vector<std::byte>(payload.begin(), payload.end());
        },
        [&](const std::uint64_t offset, const std::span<std::byte> destination) {
            std::copy_n(payload.begin() + static_cast<std::ptrdiff_t>(offset), destination.size(), destination.begin());
            return destination.size();
        },
    }};
    vm.vfs.MountLazyReadOnly(ogplay::runtime::VfsSource::apk, "/apk", entries);
    bool opened{};
    std::size_t bytes_read{};
    vm.context->video_source_player_factory = FakeSourceFactory(opened, bytes_read);
    const auto view = vm.NewVideoView();
    vm.CallOn(view, "setVideoPath", "(Ljava/lang/String;)V",
              {VmValue::Ref(vm.interpreter.NewStringUtf8("/apk/movie.bin"))});
    static_cast<void>(vm.Pump());
    CHECK(opened);
    CHECK(bytes_read == payload.size());
    CHECK(full_reads == 0U);
    CHECK(vm.vfs.IoStatistics().full_materialized_bytes == 0U);
}

TEST_CASE("videoview media controls follow prepared player state") {
    VideoVm vm(FakeFactory());
    const auto view = vm.NewVideoView();
    CHECK_FALSE(vm.CallOn(view, "canSeekForward", "()Z").AsInt());
    CHECK_FALSE(vm.CallOn(view, "canSeekBackward", "()Z").AsInt());
    CHECK_FALSE(vm.CallOn(view, "canPause", "()Z").AsInt());

    vm.CallOn(view, "setVideoPath", "(Ljava/lang/String;)V",
              {VmValue::Ref(vm.interpreter.NewStringUtf8(kGuestVideoPath))});
    static_cast<void>(vm.Pump());
    CHECK(vm.CallOn(view, "canSeekForward", "()Z").AsInt() == 1);
    CHECK(vm.CallOn(view, "canSeekBackward", "()Z").AsInt() == 1);
    CHECK(vm.CallOn(view, "canPause", "()Z").AsInt() == 1);

    vm.CallOn(view, "stopPlayback", "()V");
    CHECK_FALSE(vm.CallOn(view, "canSeekForward", "()Z").AsInt());
    CHECK_FALSE(vm.CallOn(view, "canSeekBackward", "()Z").AsInt());
    CHECK_FALSE(vm.CallOn(view, "canPause", "()Z").AsInt());
}

TEST_CASE("videoview missing decoder reports error without completion") {
    VideoVm vm(ogplay::video::VideoPlayerFactory{});
    const auto view = vm.NewVideoView();
    const auto listener = vm.NewListener();
    vm.CallOn(view, "setOnCompletionListener", "(Landroid/media/MediaPlayer$OnCompletionListener;)V",
              {VmValue::Ref(listener)});
    vm.CallOn(view, "setOnErrorListener", "(Landroid/media/MediaPlayer$OnErrorListener;)V", {VmValue::Ref(listener)});
    vm.CallOn(view, "setVideoPath", "(Ljava/lang/String;)V",
              {VmValue::Ref(vm.interpreter.NewStringUtf8(kGuestVideoPath))});
    vm.CallOn(view, "start", "()V");
    CHECK(vm.Errors() == 0);
    CHECK(vm.Pump() == 0U);
    CHECK(vm.Errors() == 1);
    CHECK(vm.Completions() == 0);
    CHECK(vm.Pump() == 0U);
    CHECK(vm.Errors() == 1);
}

TEST_CASE("videoview error listener replacement and unhandled error are observable") {
    VideoVm vm(FakeFactory());
    const auto view = vm.NewVideoView();
    const auto listener = vm.NewListener();
    vm.CallOn(view, "setOnErrorListener", "(Landroid/media/MediaPlayer$OnErrorListener;)V", {VmValue::Ref(listener)});
    vm.CallOn(view, "setVideoPath", "(Ljava/lang/String;)V",
              {VmValue::Ref(vm.interpreter.NewStringUtf8("/sdcard/missing.mp4"))});
    CHECK(vm.Pump() == 0U);
    CHECK(vm.Errors() == 1);
    vm.CallOn(view, "setOnErrorListener", "(Landroid/media/MediaPlayer$OnErrorListener;)V",
              {VmValue::Ref(VmObjectRef{})});
    vm.CallOn(view, "setVideoPath", "(Ljava/lang/String;)V",
              {VmValue::Ref(vm.interpreter.NewStringUtf8("/sdcard/missing.mp4"))});
    const auto error = PumpVideoViews(vm.interpreter, *vm.context, {});
    REQUIRE(error.has_value());
    CHECK(error->find("video error was not handled") != std::string::npos);
    CHECK(vm.Completions() == 0);
}

TEST_CASE("MediaPlayer accepts error and prepared listeners and resets") {
    VideoVm vm(FakeFactory());
    const auto player = vm.interpreter.NewIntrinsicInstance("Landroid/media/MediaPlayer;");
    const auto klass = vm.linker.ResolveDescriptor("Landroid/media/MediaPlayer;");
    const auto ctor = vm.linker.FindDirectMethod(klass, "<init>", "()V");
    REQUIRE(ctor.has_value());
    const auto constructed = vm.interpreter.Call(*ctor, std::vector<VmValue>{VmValue::Ref(player)});
    REQUIRE_MESSAGE(!constructed.exception.IsValid(), constructed.exception_message);
    vm.CallOn(player, "setOnErrorListener", "(Landroid/media/MediaPlayer$OnErrorListener;)V",
              {VmValue::Ref(VmObjectRef{})});
    vm.CallOn(player, "setOnPreparedListener", "(Landroid/media/MediaPlayer$OnPreparedListener;)V",
              {VmValue::Ref(VmObjectRef{})});
    vm.CallOn(player, "reset", "()V");
    CHECK(vm.CallOn(player, "isPlaying", "()Z").AsInt() == 0);
    ShutdownAndroidScheduler(*vm.context);
}

TEST_CASE("WifiInfo without a connection does not invent a MAC address") {
    VideoVm vm(FakeFactory());
    const auto info = vm.interpreter.NewIntrinsicInstance("Landroid/net/wifi/WifiInfo;");
    CHECK_FALSE(vm.CallOn(info, "getMacAddress", "()Ljava/lang/String;").ref.IsValid());
}

TEST_CASE("application Context identity survives Activity replacement") {
    VideoVm vm(FakeFactory());
    vm.context->package_name = "org.ogplay.inheritance";
    const auto base = vm.interpreter.NewIntrinsicInstance("Landroid/content/Context;");
    const auto application = vm.interpreter.NewIntrinsicInstance("Landroid/app/Application;");
    const auto first = vm.interpreter.NewIntrinsicInstance("Landroid/app/Activity;");
    const auto second = vm.interpreter.NewIntrinsicInstance("Landroid/app/Activity;");
    vm.CallOn(first, "attachBaseContext", "(Landroid/content/Context;)V", {VmValue::Ref(base)});
    vm.CallOn(application, "attachBaseContext", "(Landroid/content/Context;)V", {VmValue::Ref(base)});
    vm.CallOn(second, "attachBaseContext", "(Landroid/content/Context;)V", {VmValue::Ref(base)});
    const auto first_context = vm.CallOn(first, "getApplicationContext", "()Landroid/content/Context;").ref;
    const auto second_context = vm.CallOn(second, "getApplicationContext", "()Landroid/content/Context;").ref;
    CHECK(first_context.IsValid());
    CHECK(first_context == second_context);
    CHECK(first_context != first);
    const auto package = vm.CallOn(application, "getPackageName", "()Ljava/lang/String;").ref;
    CHECK(vm.interpreter.StringUtf8(package) == "org.ogplay.inheritance");

    const auto application_class = vm.linker.ResolveDescriptor("Landroid/app/Application;");
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
        {"Landroid/content/ContextWrapper;", "attachBaseContext", "(Landroid/content/Context;)V"},
        {"Landroid/app/IntentService;", "onHandleIntent", "(Landroid/content/Intent;)V"},
        {"Landroid/view/View;", "onSizeChanged", "(IIII)V"},
        {"Landroid/os/AsyncTask;", "onPreExecute", "()V"},
        {"Landroid/os/AsyncTask;", "onPostExecute", "(Ljava/lang/Object;)V"},
        {"Landroid/os/AsyncTask;", "onProgressUpdate", "([Ljava/lang/Object;)V"},
        {"Landroid/os/AsyncTask;", "onCancelled", "(Ljava/lang/Object;)V"},
        {"Landroid/os/AsyncTask;", "doInBackground", "([Ljava/lang/Object;)Ljava/lang/Object;"},
        {"Landroid/os/ResultReceiver;", "onReceiveResult", "(ILandroid/os/Bundle;)V"},
        {"Landroid/os/HandlerThread;", "onLooperPrepared", "()V"},
    };
    for (const auto& expected : protected_methods) {
        CAPTURE(expected.owner);
        CAPTURE(expected.name);
        CAPTURE(expected.descriptor);
        const auto owner = vm.linker.ResolveDescriptor(expected.owner);
        const auto slot = vm.linker.FindVtableIndex(owner, expected.name, expected.descriptor);
        REQUIRE(slot.has_value());
        const auto& method = vm.linker.Method(vm.linker.Class(owner).vtable[*slot]);
        CHECK(method.owner == owner);
        CHECK((method.access_flags & 0x0007U) == 0x0004U);
    }

    const auto activity = vm.linker.ResolveDescriptor("Landroid/app/Activity;");
    const auto public_slot =
        vm.linker.FindVtableIndex(activity, "onConfigurationChanged", "(Landroid/content/res/Configuration;)V");
    REQUIRE(public_slot.has_value());
    CHECK((vm.linker.Method(vm.linker.Class(activity).vtable[*public_slot]).access_flags & 0x0007U) == 0x0001U);

    const auto async_task = vm.linker.ResolveDescriptor("Landroid/os/AsyncTask;");
    CHECK((vm.linker.Class(async_task).access_flags & 0x0400U) != 0U);
}

TEST_CASE("AnyVideoPlaying reports only actively playing views") {
    VideoVm vm(FakeFactory());
    CHECK_FALSE(AnyVideoPlaying(*vm.context));
    const auto view = vm.NewVideoView();
    vm.CallOn(view, "setVideoPath", "(Ljava/lang/String;)V",
              {VmValue::Ref(vm.interpreter.NewStringUtf8(kGuestVideoPath))});
    static_cast<void>(vm.Pump());
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
    static_cast<void>(vm.Pump());
    vm.CallOn(view, "start", "()V");

    std::vector<std::int16_t> buffer(16U, 100);
    CHECK(MixVideoPcmIntoStereo(*vm.context, buffer, 16000U) == 1U);
    const std::vector<std::int16_t> expected{100, 100, 100, 100, 101, 101, 101, 101,
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
    static_cast<void>(vm.Pump());
    vm.CallOn(view, "start", "()V");

    std::vector<std::int16_t> first(6U, 0);
    std::vector<std::int16_t> second(6U, 0);
    CHECK(MixVideoPcmIntoStereo(*vm.context, first, 12000U) == 1U);
    CHECK(MixVideoPcmIntoStereo(*vm.context, second, 12000U) == 1U);
    CHECK(first == std::vector<std::int16_t>{0, 0, 0, 0, 1, 1});
    CHECK(second == std::vector<std::int16_t>{2, 2, 2, 2, 3, 3});
}

TEST_CASE("video audio downsampling skips unread source frames across 1-frame pumps") {
    VideoVm vm(FakeAudioFactory(192000U, 1U));
    const auto view = vm.NewVideoView();
    vm.CallOn(view, "setVideoPath", "(Ljava/lang/String;)V",
              {VmValue::Ref(vm.interpreter.NewStringUtf8(kGuestVideoPath))});
    static_cast<void>(vm.Pump());
    vm.CallOn(view, "start", "()V");

    std::vector<std::int16_t> first(2U, 0);
    std::vector<std::int16_t> second(2U, 0);
    CHECK(MixVideoPcmIntoStereo(*vm.context, first, 48000U) == 1U);
    CHECK(MixVideoPcmIntoStereo(*vm.context, second, 48000U) == 1U);
    CHECK(first == std::vector<std::int16_t>{0, 0});
    CHECK(second == std::vector<std::int16_t>{4, 4});
}

TEST_CASE("paused, stopped and audioless videos contribute silence") {
    VideoVm vm(FakeAudioFactory(8000U, 1U));
    const auto view = vm.NewVideoView();
    vm.CallOn(view, "setVideoPath", "(Ljava/lang/String;)V",
              {VmValue::Ref(vm.interpreter.NewStringUtf8(kGuestVideoPath))});
    static_cast<void>(vm.Pump());
    std::vector<std::int16_t> buffer(8U, 0);
    // Not started yet.
    CHECK(MixVideoPcmIntoStereo(*vm.context, buffer, 16000U) == 0U);
    vm.CallOn(view, "start", "()V");
    vm.CallOn(view, "pause", "()V");
    CHECK(MixVideoPcmIntoStereo(*vm.context, buffer, 16000U) == 0U);
    vm.CallOn(view, "stopPlayback", "()V");
    CHECK(MixVideoPcmIntoStereo(*vm.context, buffer, 16000U) == 0U);
    CHECK(buffer == std::vector<std::int16_t>(8U, 0));

    VideoVm silent(FakeFactory());
    const auto silent_view = silent.NewVideoView();
    silent.CallOn(silent_view, "setVideoPath", "(Ljava/lang/String;)V",
                  {VmValue::Ref(silent.interpreter.NewStringUtf8(kGuestVideoPath))});
    static_cast<void>(silent.Pump());
    silent.CallOn(silent_view, "start", "()V");
    CHECK(MixVideoPcmIntoStereo(*silent.context, buffer, 16000U) == 0U);
}

TEST_CASE("two playing VideoViews mix into one stereo buffer") {
    VideoVm vm(FakeAudioFactory(8000U, 1U));
    const auto first = vm.NewVideoView();
    const auto second = vm.NewVideoView();
    vm.CallOn(first, "setVideoPath", "(Ljava/lang/String;)V",
              {VmValue::Ref(vm.interpreter.NewStringUtf8(kGuestVideoPath))});
    static_cast<void>(vm.Pump());
    vm.CallOn(second, "setVideoPath", "(Ljava/lang/String;)V",
              {VmValue::Ref(vm.interpreter.NewStringUtf8(kGuestVideoPath))});
    static_cast<void>(vm.Pump());
    vm.CallOn(first, "start", "()V");
    vm.CallOn(second, "start", "()V");
    std::vector<std::int16_t> buffer(4U, 0);
    CHECK(MixVideoPcmIntoStereo(*vm.context, buffer, 8000U) == 2U);
}

TEST_CASE("videoview seekTo resets the PCM cursor") {
    VideoVm vm(FakeAudioFactory(8000U, 1U));
    const auto view = vm.NewVideoView();
    vm.CallOn(view, "setVideoPath", "(Ljava/lang/String;)V",
              {VmValue::Ref(vm.interpreter.NewStringUtf8(kGuestVideoPath))});
    static_cast<void>(vm.Pump());
    vm.CallOn(view, "start", "()V");
    std::vector<std::int16_t> first(2U, 0);
    CHECK(MixVideoPcmIntoStereo(*vm.context, first, 8000U) == 1U);
    vm.CallOn(view, "seekTo", "(I)V", {VmValue::Int(0)});
    std::vector<std::int16_t> again(2U, 0);
    CHECK(MixVideoPcmIntoStereo(*vm.context, again, 8000U) == 1U);
    CHECK(again == first);
}

TEST_CASE("videoview missing file dispatches error once") {
    VideoVm vm(FakeFactory());
    const auto view = vm.NewVideoView();
    const auto listener = vm.NewListener();
    vm.CallOn(view, "setOnErrorListener", "(Landroid/media/MediaPlayer$OnErrorListener;)V", {VmValue::Ref(listener)});
    vm.CallOn(view, "setVideoPath", "(Ljava/lang/String;)V",
              {VmValue::Ref(vm.interpreter.NewStringUtf8("/sdcard/missing.mp4"))});
    vm.CallOn(view, "start", "()V");
    CHECK(vm.Errors() == 0);
    CHECK(vm.Pump() == 0U);
    CHECK(vm.Errors() == 1);
    CHECK(vm.Pump() == 0U);
    CHECK(vm.Completions() == 0);
}

TEST_CASE("DVM216 VideoView prepares asynchronously with the same controllable player") {
    VideoVm vm(FakeAudioFactory(8000U, 2U));
    const auto view = vm.NewVideoView();
    const auto listener = vm.NewListener();
    vm.CallOn(view, "setOnPreparedListener", "(Landroid/media/MediaPlayer$OnPreparedListener;)V",
              {VmValue::Ref(listener)});
    vm.CallOn(view, "setOnCompletionListener", "(Landroid/media/MediaPlayer$OnCompletionListener;)V",
              {VmValue::Ref(listener)});
    vm.interpreter.SetStaticFieldBits("LVideoListener;", "volumeOnPrepared", "Z", 1);
    vm.CallOn(view, "setVideoPath", "(Ljava/lang/String;)V",
              {VmValue::Ref(vm.interpreter.NewStringUtf8(kGuestVideoPath))});
    CHECK(vm.Prepared() == 0);
    CHECK(vm.CallOn(view, "getDuration", "()I").AsInt() == -1);
    vm.CallOn(view, "seekTo", "(I)V", {VmValue::Int(100)});
    vm.CallOn(view, "start", "()V");
    CHECK_FALSE(AnyVideoPlaying(*vm.context));
    CHECK(vm.Pump() == 1U);
    CHECK(vm.Prepared() == 1);
    const auto player = vm.Player();
    REQUIRE(player.IsValid());
    CHECK(vm.CallOn(player, "getCurrentPosition", "()I").AsInt() == 100);
    CHECK(vm.CallOn(player, "getVideoWidth", "()I").AsInt() == 8);
    CHECK(vm.context->video_views.at(view.Value()).left_volume == 0.5F);
    CHECK(vm.context->video_views.at(view.Value()).right_volume == 0.25F);
    std::vector<std::int16_t> samples(4, 0);
    CHECK(MixVideoPcmIntoStereo(*vm.context, samples, 8000U) == 1U);
    // Fake audio cursor is 800 frames after seek; real per-instance gains apply.
    CHECK(samples == std::vector<std::int16_t>{400, 200, 401, 200});
    const auto origin = vm.context->uptime_millis.load();
    vm.context->uptime_millis = origin + 900;
    static_cast<void>(vm.Pump());
    CHECK(vm.Prepared() == 1);
    CHECK(vm.Completions() == 1);
    CHECK(vm.Player("getCompletedPlayer") == player);
    vm.CallOn(view, "stopPlayback", "()V");
    CHECK(vm.context->media_players.empty());
    CHECK(vm.context->video_views.empty());
}

TEST_CASE("DVM216 VideoView releases and replaces pending generations") {
    VideoVm vm(FakeFactory());
    const auto view = vm.NewVideoView();
    const auto listener = vm.NewListener();
    vm.CallOn(view, "setOnPreparedListener", "(Landroid/media/MediaPlayer$OnPreparedListener;)V",
              {VmValue::Ref(listener)});
    const auto path = vm.interpreter.NewStringUtf8(kGuestVideoPath);
    vm.CallOn(view, "setVideoPath", "(Ljava/lang/String;)V", {VmValue::Ref(path)});
    vm.CallOn(view, "setVideoPath", "(Ljava/lang/String;)V", {VmValue::Ref(path)});
    CHECK(vm.context->media_players.size() == 1U);
    static_cast<void>(vm.Pump());
    CHECK(vm.Prepared() == 1);
    vm.CallOn(view, "resume", "()V");
    vm.CallOn(view, "stopPlayback", "()V");
    CHECK(vm.Pump() == 0U);
    CHECK(vm.Prepared() == 1);
    CHECK(vm.context->media_players.empty());
    vm.CallOn(view, "resume", "()V");
    vm.interpreter.SetStaticFieldBits("LVideoListener;", "stopOnPrepared", "Z", 1);
    vm.CallOn(view, "start", "()V");
    CHECK(vm.Pump() == 0U);  // callback releases the player before deferred start
    CHECK(vm.Prepared() == 2);
    CHECK(vm.context->video_views.empty());
    CHECK(vm.context->media_players.empty());
}

TEST_CASE("DVM216 VideoView resource URIs retain a sealed APK range after AFD close") {
    VideoVm vm(FakeFactory());
    vm.context->package_name = "org.ogplay.video";
    const std::array payload{std::byte{'v'}, std::byte{'i'}, std::byte{'d'}, std::byte{'0'}};
    vm.context->apk_bytes = MakeStoredZip("res/raw/movie.mp4", payload);
    vm.context->archive = ogplay::loader::ParseApkArchive(vm.context->apk_bytes);
    vm.context->arsc.entries.push_back({.resource_id = 0x7f040001U,
                                        .type_name = "raw",
                                        .entry_name = "movie",
                                        .string_value = "res/raw/movie.mp4",
                                        .value_type = 3});
    std::shared_ptr<const ogplay::video::VideoDataSource> retained;
    vm.context->video_source_player_factory = [&](auto source) {
        retained = source;
        return FakeFactory()({});
    };
    const auto view = vm.NewVideoView();
    const auto listener = vm.NewListener();
    vm.CallOn(view, "setOnPreparedListener", "(Landroid/media/MediaPlayer$OnPreparedListener;)V",
              {VmValue::Ref(listener)});
    for (const auto text :
         {"android.resource://org.ogplay.video/raw/movie", "android.resource://org.ogplay.video/2130968577"}) {
        const auto owner = vm.linker.ResolveDescriptor("Landroid/net/Uri;");
        const auto parse = vm.linker.FindDirectMethod(owner, "parse", "(Ljava/lang/String;)Landroid/net/Uri;");
        REQUIRE(parse.has_value());
        const auto result = vm.interpreter.Call(*parse, std::array{VmValue::Ref(vm.interpreter.NewStringUtf8(text))});
        REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
        vm.CallOn(view, "setVideoURI", "(Landroid/net/Uri;)V", {result.value});
        static_cast<void>(vm.Pump());
        REQUIRE(retained != nullptr);
        CHECK(retained->Size() == payload.size());
        std::array<std::byte, 8> read{};
        CHECK(retained->ReadAt(0, read) == payload.size());
        CHECK(std::equal(payload.begin(), payload.end(), read.begin()));
        CHECK(retained->ReadAt(payload.size(), read) == 0U);
        CHECK(vm.CallOn(view, "getDuration", "()I").AsInt() == 1000);
    }
    CHECK(vm.Prepared() == 2);
    vm.CallOn(view, "stopPlayback", "()V");
    retained.reset();
    CHECK(vm.context->video_views.empty());
}

TEST_CASE("DVM216 local video event protocol runs in both interpreter backends") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        CAPTURE(backend);
        InterpreterConfig config;
        config.backend = backend;
        VideoVm vm(FakeFactory(), config);
        const auto view = vm.NewVideoView();
        const auto listener = vm.NewListener();
        vm.CallOn(view, "setOnPreparedListener", "(Landroid/media/MediaPlayer$OnPreparedListener;)V",
                  {VmValue::Ref(listener)});
        vm.CallOn(view, "setOnErrorListener", "(Landroid/media/MediaPlayer$OnErrorListener;)V",
                  {VmValue::Ref(listener)});
        vm.CallOn(view, "setVideoPath", "(Ljava/lang/String;)V",
                  {VmValue::Ref(vm.interpreter.NewStringUtf8("file:///sdcard/short-mp4v-aac.mp4"))});
        CHECK(vm.Prepared() == 0);
        vm.CallOn(view, "start", "()V");
        CHECK(vm.Pump() == 1U);
        CHECK(vm.Prepared() == 1);
        CHECK(vm.CallOn(vm.Player(), "isPlaying", "()Z").AsInt() == 1);
        vm.CallOn(view, "setVideoPath", "(Ljava/lang/String;)V",
                  {VmValue::Ref(vm.interpreter.NewStringUtf8("https://invalid.example/movie.mp4"))});
        CHECK(vm.Errors() == 0);
        CHECK(vm.Pump() == 0U);
        CHECK(vm.Errors() == 1);
        CHECK(vm.Completions() == 0);
        const auto gaps = vm.ledger.Unimplemented();
        CHECK(std::any_of(gaps.begin(), gaps.end(),
                          [](const auto& gap) { return gap.id == "android.video.uri_scheme"; }));
        vm.CallOn(view, "stopPlayback", "()V");
        CHECK(vm.context->video_views.empty());
    }
}

TEST_CASE("DVM216 decoder and audio failures dispatch error through the main video pump") {
    class FailingPlayer final : public ogplay::video::VideoPlayer {
    public:
        explicit FailingPlayer(bool fail_frames) : frames_(fail_frames), backing_({8, 4, 1000, 8000, 1}, 10) {}

        const ogplay::video::VideoMetadata& Metadata() const noexcept override { return backing_.Metadata(); }

        std::optional<ogplay::video::VideoFrame> TakeFrame(std::int64_t position) override {
            if (frames_) throw ogplay::video::VideoPlayerError("frame decode failed");
            return backing_.TakeFrame(position);
        }

        std::size_t ReadPcm(std::span<std::int16_t>) override {
            throw ogplay::video::VideoPlayerError("audio decode failed");
        }

        void SeekTo(std::int64_t position) override { backing_.SeekTo(position); }

    private:
        bool frames_;
        ogplay::video::FakeVideoPlayer backing_;
    };

    for (const bool frame_failure : {true, false}) {
        VideoVm vm(
            [frame_failure](const std::filesystem::path&) { return std::make_unique<FailingPlayer>(frame_failure); });
        const auto view = vm.NewVideoView();
        const auto listener = vm.NewListener();
        vm.CallOn(view, "setOnErrorListener", "(Landroid/media/MediaPlayer$OnErrorListener;)V",
                  {VmValue::Ref(listener)});
        vm.CallOn(view, "setVideoPath", "(Ljava/lang/String;)V",
                  {VmValue::Ref(vm.interpreter.NewStringUtf8(kGuestVideoPath))});
        vm.CallOn(view, "start", "()V");
        CHECK(vm.Pump() == (frame_failure ? 0U : 1U));
        if (!frame_failure) {
            std::vector<std::int16_t> samples(4, 0);
            CHECK(MixVideoPcmIntoStereo(*vm.context, samples, 8000U) == 0U);
            CHECK(vm.Errors() == 0);  // the audio worker never invokes Java
            CHECK(vm.Pump() == 0U);
        }
        CHECK(vm.Errors() == 1);
        CHECK(vm.Pump() == 0U);
        CHECK(vm.Errors() == 1);
        CHECK(vm.Completions() == 0);
        vm.CallOn(view, "stopPlayback", "()V");
    }
}

TEST_CASE("video APK media source validates once and retains exact stored range reads") {
    VideoVm vm(FakeFactory());
    const std::vector<std::byte> payload{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
                                         std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}};
    vm.context->apk_bytes = MakeStoredZip("res/raw/movie.mp4", payload);
    vm.context->archive = ogplay::loader::ParseApkArchive(vm.context->apk_bytes);
    ogplay::audio::EncodedAudioSource source;
    source.kind = ogplay::audio::EncodedAudioSource::Kind::apk_entry;
    source.name = "res/raw/movie.mp4";
    source.offset = 2;
    source.length = 4;
    auto data = LoadEncodedAudioSource(*vm.context, source);
    REQUIRE(data != nullptr);
    CHECK(data->Size() == 4U);
    std::array<std::byte, 3> read{};
    REQUIRE(data->ReadAt(1, read) == 3U);
    CHECK(read == std::array{std::byte{4}, std::byte{5}, std::byte{6}});
    REQUIRE(data->ReadAt(0, read) == 3U);
    CHECK(read == std::array{std::byte{3}, std::byte{4}, std::byte{5}});
    CHECK(data->ReadAt(4, read) == 0U);
    std::stop_source stop;
    stop.request_stop();
    CHECK(data->ReadAt(0, read, stop.get_token()) == 0U);
    data.reset();
    // Corrupt before capturing a new sealed source; the old view is retired.
    const auto offset =
        ogplay::loader::StoredApkEntryDataOffset(vm.context->apk_bytes, vm.context->archive, source.name);
    vm.context->apk_bytes[static_cast<std::size_t>(offset)] = std::byte{9};
    CHECK(LoadEncodedAudioSource(*vm.context, source) == nullptr);
}

TEST_CASE("DVM-249 ordinary MediaPlayer video events transport looping and Surface retirement") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        InterpreterConfig config;
        config.backend = backend;
        VideoVm vm(FakeAudioFactory(8000, 2), config);
        const auto surface = vm.NewSurfaceView(), player = vm.NewPlainPlayer(), listener = vm.NewListener();
        for (const auto name : {"setOnPreparedListener", "setOnCompletionListener", "setOnErrorListener",
                                "setOnVideoSizeChangedListener"}) {
            const std::string type = std::string(name).substr(3);
            vm.CallOn(player, name, "(Landroid/media/MediaPlayer$" + type + ";)V", {VmValue::Ref(listener)});
        }
        vm.interpreter.SetStaticFieldBits("LVideoListener;", "startOnPrepared", "Z", 1);
        vm.PreparePlain(player, surface);
        CHECK(vm.Prepared() == 0);
        CHECK(vm.CallOn(player, "getVideoWidth", "()I").AsInt() == 8);
        CHECK(vm.CallOn(player, "getVideoHeight", "()I").AsInt() == 4);
        vm.PumpMediaEvents();
        CHECK(vm.Prepared() == 1);
        CHECK(vm.Count("getWidth") == 8);
        CHECK(vm.Count("getHeight") == 4);
        CHECK(vm.Player() == player);
        CHECK(vm.CallOn(player, "isPlaying", "()Z").AsInt() == 1);
        static_cast<void>(vm.Pump());
        REQUIRE(vm.context->video_views.at(player.Value()).latest_frame);
        vm.CallOn(player, "setLooping", "(Z)V", {VmValue::Int(1)});
        const auto now = vm.context->uptime_millis.load();
        vm.context->uptime_millis = now + 1250;
        vm.PumpMediaEvents();
        CHECK(vm.Completions() == 0);
        CHECK(vm.CallOn(player, "getCurrentPosition", "()I").AsInt() == 250);
        vm.CallOn(player, "pause", "()V");
        vm.context->uptime_millis += 250;
        CHECK(vm.CallOn(player, "getCurrentPosition", "()I").AsInt() == 250);
        vm.CallOn(player, "seekTo", "(I)V", {VmValue::Int(100)});
        vm.CallOn(player, "setVolume", "(FF)V", {VmValue::Float(0.5F), VmValue::Float(0.25F)});
        vm.CallOn(player, "start", "()V");
        std::vector<std::int16_t> pcm(4);
        REQUIRE(MixVideoPcmIntoStereo(*vm.context, pcm, 8000) == 1);
        CHECK(pcm == std::vector<std::int16_t>{400, 200, 401, 200});
        vm.context->stream_mute[3] = true;
        pcm.assign(4, 0);
        REQUIRE(MixVideoPcmIntoStereo(*vm.context, pcm, 8000) == 1);
        CHECK(pcm == std::vector<std::int16_t>(4, 0));
        vm.context->stream_mute[3] = false;
        const auto error = DispatchSurfaceHolderCallbacks(vm.interpreter, *vm.context, SurfaceHolderPhase::destroyed);
        REQUIRE_FALSE(error);
        CHECK_FALSE(vm.context->media_players.at(player.Value()).display_view.IsValid());
        CHECK_FALSE(vm.context->video_views.at(player.Value()).latest_frame);
        static_cast<void>(vm.Pump());  // decoding continues, no reattachment
        REQUIRE_FALSE(DispatchSurfaceHolderCallbacks(vm.interpreter, *vm.context, SurfaceHolderPhase::created));
        CHECK_FALSE(vm.context->media_players.at(player.Value()).display_view.IsValid());
        const auto holder = vm.CallOn(surface, "getHolder", "()Landroid/view/SurfaceHolder;").ref;
        vm.CallOn(player, "setDisplay", "(Landroid/view/SurfaceHolder;)V", {VmValue::Ref(holder)});
        CHECK(vm.context->media_players.at(player.Value()).display_view == surface);
        vm.CallOn(player, "setLooping", "(Z)V", {VmValue::Int(0)});
        vm.context->uptime_millis += 1000;
        vm.PumpMediaEvents();
        vm.PumpMediaEvents();
        CHECK(vm.Completions() == 1);
        CHECK(vm.Player("getCompletedPlayer") == player);
        vm.CallOn(player, "stop", "()V");
        vm.CallOn(player, "prepare", "()V");
        vm.CallOn(player, "reset", "()V");
        CHECK(vm.context->video_views.empty());
        CHECK_FALSE(vm.context->media_players.at(player.Value()).source_data);
        vm.CallOn(player, "release", "()V");
        CHECK(vm.context->media_players.empty());
    }
}

TEST_CASE("DVM-249 ordinary video FD range survives close and prepared callback release") {
    VideoVm vm(FakeFactory());
    vm.context->package_name = "org.ogplay.video";
    const std::array payload{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5}, std::byte{6}};
    vm.context->apk_bytes = MakeStoredZip("res/raw/movie.mp4", payload);
    vm.context->archive = ogplay::loader::ParseApkArchive(vm.context->apk_bytes);
    vm.context->arsc.entries.push_back({.resource_id = 0x7f040001U,
                                        .type_name = "raw",
                                        .entry_name = "movie",
                                        .string_value = "res/raw/movie.mp4",
                                        .value_type = 3});
    const auto surface = vm.NewSurfaceView(), player = vm.NewPlainPlayer();
    const auto context = vm.CallOn(surface, "getContext", "()Landroid/content/Context;").ref;
    const auto resources = vm.CallOn(context, "getResources", "()Landroid/content/res/Resources;").ref;
    const auto afd = vm.CallOn(resources, "openRawResourceFd", "(I)Landroid/content/res/AssetFileDescriptor;",
                               {VmValue::Int(0x7f040001)})
                         .ref;
    const auto fd = vm.CallOn(afd, "getFileDescriptor", "()Ljava/io/FileDescriptor;").ref;
    const auto offset = vm.CallOn(afd, "getStartOffset", "()J").AsLong();
    vm.CallOn(player, "setDataSource", "(Ljava/io/FileDescriptor;JJ)V",
              {VmValue::Ref(fd), VmValue::Long(offset + 1), VmValue::Long(3)});
    vm.CallOn(afd, "close", "()V");
    std::shared_ptr<const ogplay::video::VideoDataSource> captured;
    vm.context->video_source_player_factory = [&](auto source) {
        captured = source;
        return FakeFactory()({});
    };
    vm.CallOn(player, "prepare", "()V");
    REQUIRE(captured);
    CHECK(captured->Size() == 3);
    std::array<std::byte, 3> read{};
    CHECK(captured->ReadAt(0, read) == 3);
    CHECK(read == std::array{std::byte{2}, std::byte{3}, std::byte{4}});
    const auto listener = vm.NewListener();
    vm.interpreter.SetStaticFieldBits("LVideoListener;", "stopOnPrepared", "Z", 1);
    vm.CallOn(player, "setOnPreparedListener", "(Landroid/media/MediaPlayer$OnPreparedListener;)V",
              {VmValue::Ref(listener)});
    vm.PumpMediaEvents();
    CHECK(vm.Prepared() == 1);
    CHECK(vm.context->video_views.empty());
    CHECK(vm.context->media_players.empty());
}

TEST_CASE("DVM-249 ordinary MediaPlayer decodes a real local FFmpeg video" * doctest::skip(OGPLAY_HAS_FFMPEG == 0)) {
    REQUIRE_MESSAGE(ogplay::video::FfmpegAvailable(), ogplay::video::FfmpegUnavailableReason());
    VideoVm vm(ogplay::video::VideoPlayerFactory{});
    vm.context->video_source_player_factory = ogplay::video::MakeFfmpegVideoSourcePlayerFactory();
    const auto surface = vm.NewSurfaceView(), player = vm.NewPlainPlayer();
    vm.PreparePlain(player, surface);
    CHECK(vm.CallOn(player, "getVideoWidth", "()I").AsInt() > 0);
    CHECK(vm.CallOn(player, "getVideoHeight", "()I").AsInt() > 0);
    vm.CallOn(player, "start", "()V");
    REQUIRE(vm.Pump() == 1);
    REQUIRE(vm.context->video_views.at(player.Value()).latest_frame);
    std::vector<std::int16_t> pcm(480);
    CHECK(MixVideoPcmIntoStereo(*vm.context, pcm, 24000) == 1);
    vm.CallOn(player, "release", "()V");
}

TEST_CASE("ADR-0136 VideoView records a codec missing from ogplay_ffmpeg by name" *
          doctest::skip(OGPLAY_HAS_FFMPEG == 0)) {
    VideoVm vm(ogplay::video::VideoPlayerFactory{});
    vm.context->video_source_player_factory = ogplay::video::MakeFfmpegVideoSourcePlayerFactory();
    const auto view = vm.NewVideoView();
    const auto listener = vm.NewListener();
    vm.CallOn(view, "setOnErrorListener", "(Landroid/media/MediaPlayer$OnErrorListener;)V", {VmValue::Ref(listener)});
    vm.CallOn(view, "setVideoPath", "(Ljava/lang/String;)V",
              {VmValue::Ref(vm.interpreter.NewStringUtf8("/sdcard/hevc-aac.mp4"))});
    CHECK(vm.Pump() == 0U);
    CHECK(vm.Errors() == 1);
    CHECK(vm.Prepared() == 0);
    const auto gaps = vm.ledger.Unimplemented();
    CHECK(std::any_of(gaps.begin(), gaps.end(), [](const auto& gap) { return gap.id == "android.video.codec.hevc"; }));
}

TEST_CASE("DVM-249 Surface binding rejects stale competing and wake requests") {
    VideoVm vm(FakeFactory());
    const auto view = vm.NewSurfaceView(), player = vm.NewPlainPlayer(), second = vm.NewPlainPlayer();
    const auto holder = vm.CallOn(view, "getHolder", "()Landroid/view/SurfaceHolder;").ref;
    const auto surface = vm.CallOn(holder, "getSurface", "()Landroid/view/Surface;").ref;
    const auto outcome = [&](VmObjectRef object, const char* name, const char* signature, VmValue argument) {
        const auto type = vm.model.ObjectClass(object);
        const auto slot = vm.linker.FindVtableIndex(type, name, signature);
        REQUIRE(slot);
        return vm.interpreter.Call(vm.linker.Class(type).vtable[*slot], std::array{VmValue::Ref(object), argument});
    };
    vm.PreparePlain(player, view);
    vm.CallOn(player, "start", "()V");
    static_cast<void>(vm.Pump());
    CHECK(outcome(second, "setSurface", "(Landroid/view/Surface;)V", VmValue::Ref(surface)).exception.IsValid());
    CHECK_FALSE(vm.context->media_players.at(second.Value()).display_view.IsValid());
    vm.CallOn(holder, "setKeepScreenOn", "(Z)V", {VmValue::Int(0)});
    CHECK(outcome(holder, "setKeepScreenOn", "(Z)V", VmValue::Int(1)).exception.IsValid());
    const auto gaps = vm.ledger.Unimplemented();
    CHECK(std::any_of(gaps.begin(), gaps.end(),
                      [](const auto& gap) { return gap.id == "dexvm.surface_keep_screen_on"; }));
    // A host window that already inhibits display sleep satisfies the request,
    // including MediaPlayer.setScreenOnWhilePlaying during start.
    vm.context->host_display_sleep_inhibited = true;
    CHECK_FALSE(outcome(holder, "setKeepScreenOn", "(Z)V", VmValue::Int(1)).exception.IsValid());
    vm.CallOn(player, "pause", "()V");
    vm.CallOn(player, "setScreenOnWhilePlaying", "(Z)V", {VmValue::Int(1)});
    vm.CallOn(player, "start", "()V");
    CHECK(vm.ledger.Unimplemented().size() == gaps.size());
    vm.CallOn(player, "pause", "()V");
    vm.CallOn(player, "setSurface", "(Landroid/view/Surface;)V", {VmValue::Ref(VmObjectRef{})});
    CHECK_FALSE(vm.context->video_views.at(player.Value()).latest_frame);
    vm.CallOn(player, "setSurface", "(Landroid/view/Surface;)V", {VmValue::Ref(surface)});
    CHECK(vm.Pump() == 1);  // same paused position is decoded for the new binding
    vm.CallOn(player, "setSurface", "(Landroid/view/Surface;)V", {VmValue::Ref(surface)});
    CHECK(vm.context->video_views.at(player.Value()).latest_frame.has_value());  // repeat is idempotent
    vm.CallOn(player, "start", "()V");
    vm.CallOn(player, "setSurface", "(Landroid/view/Surface;)V", {VmValue::Ref(VmObjectRef{})});
    vm.context->uptime_millis += 100;
    CHECK(vm.Pump() == 0);  // unbound video does not publish a host frame
    REQUIRE_FALSE(DispatchSurfaceHolderCallbacks(vm.interpreter, *vm.context, SurfaceHolderPhase::destroyed));
    CHECK(outcome(player, "setSurface", "(Landroid/view/Surface;)V", VmValue::Ref(surface)).exception.IsValid());
    vm.CallOn(player, "release", "()V");
    vm.CallOn(second, "release", "()V");
}

TEST_CASE("DVM-249 async video preparation failure GC and owner shutdown are honest") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        InterpreterConfig config;
        config.backend = backend;
        VideoVm vm(FakeFactory(), config);
        const auto player = vm.NewPlainPlayer(), listener = vm.NewListener();
        vm.CallOn(player, "setOnPreparedListener", "(Landroid/media/MediaPlayer$OnPreparedListener;)V",
                  {VmValue::Ref(listener)});
        vm.CallOn(player, "setOnErrorListener", "(Landroid/media/MediaPlayer$OnErrorListener;)V",
                  {VmValue::Ref(listener)});
        vm.CallOn(player, "setDataSource", "(Ljava/lang/String;)V",
                  {VmValue::Ref(vm.interpreter.NewStringUtf8(kGuestVideoPath))});
        vm.CallOn(player, "prepareAsync", "()V");
        CHECK(vm.Prepared() == 0);
        const auto roots = vm.interpreter.ProtectReferences(std::array{player});
        static_cast<void>(vm.interpreter.CollectGarbage("ordinary-video-preparing"));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (vm.Prepared() == 0 && std::chrono::steady_clock::now() < deadline) {
            vm.PumpMediaEvents();
            std::this_thread::yield();
        }
        REQUIRE(vm.Prepared() == 1);
        CHECK(vm.Player() == player);
        CHECK(vm.Errors() == 0);
        vm.CallOn(player, "reset", "()V");
        vm.context->video_source_player_factory = [](auto) -> std::unique_ptr<ogplay::video::VideoPlayer> {
            throw ogplay::video::VideoPlayerError("invalid video source");
        };
        vm.CallOn(player, "setDataSource", "(Ljava/lang/String;)V",
                  {VmValue::Ref(vm.interpreter.NewStringUtf8(kGuestVideoPath))});
        vm.CallOn(player, "prepareAsync", "()V");
        const auto error_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (vm.Errors() == 0 && std::chrono::steady_clock::now() < error_deadline) {
            vm.PumpMediaEvents();
            std::this_thread::yield();
        }
        REQUIRE(vm.Errors() == 1);
        vm.PumpMediaEvents();
        CHECK(vm.Errors() == 1);
        CHECK(vm.Completions() == 0);
        CHECK(vm.Prepared() == 1);
        CHECK(vm.context->video_views.empty());
        ReleaseAndroidMediaResources(*vm.context);
        CHECK(vm.context->media_players.empty());
        CHECK(vm.context->encoded_audio_leases.empty());
    }
    VideoVm vm(FakeFactory());
    vm.runtime->session->Environment().PushLocalFrame(1, 64);
    const auto player = vm.NewPlainPlayer(), surface = vm.NewSurfaceView();
    vm.PreparePlain(player, surface);
    REQUIRE(vm.context->video_views.size() == 1);
    // A live managed holder is a session root. Retire its generation before
    // testing collection of the unreachable owner in the real bridge.
    const auto retired = RetireSurfaceHolderGeneration(vm.interpreter, *vm.context);
    REQUIRE_MESSAGE(!retired, retired.value_or(""));
    vm.context->content_view = VmObjectRef{};
    vm.content_root = VmObjectRef{};
    static_cast<void>(vm.runtime->session->Environment().PopLocalFrame(1));
    static_cast<void>(vm.interpreter.CollectGarbage("ordinary-video-unreachable-owner"));
    CHECK(vm.context->video_views.empty());
    CHECK(vm.context->media_players.empty());
}

TEST_CASE("DVM-339 MediaPlayer size event fixes buffer and stretches pixels to the View frame") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        InterpreterConfig config;
        config.backend = backend;
        VideoVm* active = nullptr;
        VmObjectRef active_player;
        int changed = 0;
        auto callback = IntrinsicClassBuilder::Class("Lfixture/VideoBufferCallback;", "Ljava/lang/Object;",
                                                     {"Landroid/view/SurfaceHolder$Callback;"});
        for (const auto* name : {"surfaceCreated", "surfaceDestroyed"})
            callback.VirtualMethod(name, "(Landroid/view/SurfaceHolder;)V",
                                   [](IntrinsicContext&) { return VmValue::Void(); });
        callback.VirtualMethod("surfaceChanged", "(Landroid/view/SurfaceHolder;III)V", [&](IntrinsicContext& c) {
            ++changed;
            CHECK(c.arguments[2].AsInt() == 480);
            CHECK(c.arguments[3].AsInt() == 320);
            active->CallOn(active_player, "start", "()V");
            return VmValue::Void();
        });
        VideoVm vm(
            [](const std::filesystem::path&) {
                ogplay::video::VideoMetadata metadata;
                metadata.width = 480;
                metadata.height = 320;
                metadata.duration_ms = 1000;
                struct GradientPlayer final : ogplay::video::VideoPlayer {
                    ogplay::video::FakeVideoPlayer fake;
                    explicit GradientPlayer(ogplay::video::VideoMetadata value) : fake(value, 10) {}
                    const ogplay::video::VideoMetadata& Metadata() const noexcept override { return fake.Metadata(); }
                    std::size_t ReadPcm(std::span<std::int16_t> samples) override { return fake.ReadPcm(samples); }
                    void SeekTo(std::int64_t position) override { fake.SeekTo(position); }
                    std::optional<ogplay::video::VideoFrame> TakeFrame(std::int64_t position) override {
                        auto frame = fake.TakeFrame(position);
                        if (frame)
                            for (unsigned y = 0; y < frame->height; ++y)
                                for (unsigned x = 0; x < frame->width; ++x) {
                                    const auto offset = (y * frame->width + x) * 4U;
                                    frame->rgba8[offset] = static_cast<std::uint8_t>(1U + x % 254U);
                                    frame->rgba8[offset + 1] = static_cast<std::uint8_t>(1U + y % 254U);
                                    frame->rgba8[offset + 2] = 77;
                                }
                        return frame;
                    }
                };
                return std::unique_ptr<ogplay::video::VideoPlayer>(std::make_unique<GradientPlayer>(metadata));
            },
            config, {std::move(callback).Build()});
        const auto view = vm.NewSurfaceView(), player = vm.NewPlainPlayer(), listener = vm.NewListener();
        active = &vm;
        active_player = player;
        const auto holder = vm.CallOn(view, "getHolder", "()Landroid/view/SurfaceHolder;").ref;
        const auto surface_callback = vm.interpreter.NewIntrinsicInstance("Lfixture/VideoBufferCallback;");
        const auto roots =
            vm.interpreter.ProtectReferences(std::array{view, player, listener, holder, surface_callback});
        vm.CallOn(holder, "addCallback", "(Landroid/view/SurfaceHolder$Callback;)V", {VmValue::Ref(surface_callback)});
        const auto slot = vm.linker.FindVtableIndex(vm.model.ObjectClass(listener), "onVideoSizeChanged",
                                                    "(Landroid/media/MediaPlayer;II)V");
        REQUIRE(slot);
        auto& method = vm.linker.MutableMethod(vm.linker.Class(vm.model.ObjectClass(listener)).vtable[*slot]);
        method.kind = MethodKind::intrinsic;
        int size_events = 0;
        method.implementation = [&](IntrinsicContext& c) {
            ++size_events;
            CHECK(c.arguments[1].AsInt() == 480);
            CHECK(c.arguments[2].AsInt() == 320);
            vm.CallOn(holder, "setFixedSize", "(II)V", {c.arguments[1], c.arguments[2]});
            return VmValue::Void();
        };
        vm.CallOn(player, "setOnVideoSizeChangedListener", "(Landroid/media/MediaPlayer$OnVideoSizeChangedListener;)V",
                  {VmValue::Ref(listener)});
        vm.PreparePlain(player, view);
        vm.PumpMediaEvents();
        CHECK(size_events == 1);
        REQUIRE_FALSE(PumpAndroidSurfaceEvents(vm.interpreter, *vm.context));
        CHECK(vm.context->surface_callback_sizes.at(holder.Value()) == std::pair{480U, 320U});
        const auto frame = vm.CallOn(holder, "getSurfaceFrame", "()Landroid/graphics/Rect;").ref;
        CHECK(vm.CallOn(frame, "width", "()I").AsInt() == 64);
        CHECK(vm.CallOn(frame, "height", "()I").AsInt() == 32);
        CHECK(vm.CallOn(view, "getWidth", "()I").AsInt() == 64);
        CHECK(vm.CallOn(view, "getHeight", "()I").AsInt() == 32);
        CHECK(changed == 1);
        CHECK(vm.CallOn(player, "isPlaying", "()Z").AsInt() == 1);
        CHECK_FALSE(vm.context->surface_buffer_queued.at(holder.Value()));
        static_cast<void>(vm.Pump());
        CHECK(vm.context->surface_buffer_queued.at(holder.Value()));
        RefreshAndroidSurfaceFrames(*vm.context);
        static_cast<void>(EnsureAndroidUiTraversal(vm.interpreter, *vm.context, AndroidUiTraversalReason::OwnerTick,
                                                   AndroidUiTraversalPhase::ResolveOnly));
        REQUIRE(PublishAndroidPresentation(*vm.context));
        const auto presentation = AcquireAndroidPresentation(*vm.context);
        REQUIRE(presentation);
        const auto id = *FindViewUiNode(*vm.context, view.Value());
        REQUIRE(presentation->scaled_surface_frames.contains(id));
        const auto& source = presentation->video_frames.at(id);
        std::vector<std::uint8_t> pixels(64U * 32U * 4U, 17);
        ComposeVideoViews(*presentation, pixels, 64, 32, false);
        for (auto y : {0U, 15U, 31U})
            for (auto x : {0U, 31U, 63U}) {
                const auto src = ((y * 320U / 32U) * 480U + x * 480U / 64U) * 4U;
                const auto dst = (y * 64U + x) * 4U;
                for (unsigned c = 0; c < 3; ++c)
                    CHECK(pixels[dst + c] == source.rgba8[src + c]);
                CHECK(pixels[dst + 3] == 255);
            }
        CHECK(vm.ledger.Unimplemented().empty());
    }
}
