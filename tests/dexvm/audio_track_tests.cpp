#include "boot_dex.h"
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "ogplay/audio/encoded_music.h"
#include "ogplay/audio/java_sound_pool_mixer.h"
#include "ogplay/audio/open_sles_pcm_mixer.h"
#include "ogplay/audio/pcm_mix.h"
#include "ogplay/core/capability_ledger.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/integration/dexvm_android.h"

namespace {

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

struct PositionListenerRecorder final {
    std::vector<VmObjectRef> periodic;
    std::vector<VmObjectRef> markers;
    bool write_on_periodic{};
    VmObjectRef write_array;
    std::int32_t write_count{};
    std::int32_t write_result{-999};
};

struct LoadCompleteRecorder final {
    std::vector<std::int32_t> sounds;
    std::vector<std::int32_t> statuses;
};

struct PreparedRecorder final {
    std::vector<VmObjectRef> players;
};

std::vector<IntrinsicClassDecl> AudioTrackTestCatalog(
    PositionListenerRecorder* recorder,
    LoadCompleteRecorder* loads = nullptr,
    PreparedRecorder* prepared = nullptr) {
    std::vector<IntrinsicClassDecl> result;
    auto listener = IntrinsicClassBuilder::Class(
        "Ltest/AudioPositionListener;", "Ljava/lang/Object;",
        {"Landroid/media/AudioTrack$OnPlaybackPositionUpdateListener;"});
    listener.VirtualMethod(
        "onPeriodicNotification", "(Landroid/media/AudioTrack;)V",
        [recorder](IntrinsicContext& call) {
            const auto track = call.arguments[0].ref;
            recorder->periodic.push_back(track);
            if (recorder->write_on_periodic) {
                const auto klass = call.vm.Model().ObjectClass(track);
                const auto index = call.vm.Linker().FindVtableIndex(
                    klass, "write", "([BII)I");
                if (!index.has_value()) {
                    throw std::runtime_error("AudioTrack write is missing");
                }
                const auto outcome = call.vm.Call(
                    call.vm.Linker().Class(klass).vtable[*index],
                    std::vector<VmValue>{
                        VmValue::Ref(track), VmValue::Ref(recorder->write_array),
                        VmValue::Int(0), VmValue::Int(recorder->write_count)});
                if (outcome.exception.IsValid()) {
                    throw std::runtime_error(outcome.exception_message);
                }
                recorder->write_result = outcome.value.AsInt();
            }
            return VmValue::Void();
        });
    listener.VirtualMethod(
        "onMarkerReached", "(Landroid/media/AudioTrack;)V",
        [recorder](IntrinsicContext& call) {
            recorder->markers.push_back(call.arguments[0].ref);
            return VmValue::Void();
        });
    result.push_back(std::move(listener).Build());

    auto throwing = IntrinsicClassBuilder::Class(
        "Ltest/ThrowingAudioPositionListener;", "Ljava/lang/Object;",
        {"Landroid/media/AudioTrack$OnPlaybackPositionUpdateListener;"});
    const auto fail = [](IntrinsicContext&) -> VmValue {
        throw VmJavaThrow{"Ljava/lang/IllegalStateException;",
                          "position listener failure"};
    };
    throwing.VirtualMethod(
        "onPeriodicNotification", "(Landroid/media/AudioTrack;)V", fail);
    throwing.VirtualMethod(
        "onMarkerReached", "(Landroid/media/AudioTrack;)V", fail);
    result.push_back(std::move(throwing).Build());

    auto load_listener = IntrinsicClassBuilder::Class(
        "Ltest/SoundLoadListener;", "Ljava/lang/Object;",
        {"Landroid/media/SoundPool$OnLoadCompleteListener;"});
    load_listener.VirtualMethod(
        "onLoadComplete", "(Landroid/media/SoundPool;II)V",
        [loads](IntrinsicContext& call) {
            if (loads != nullptr) {
                loads->sounds.push_back(call.arguments[1].AsInt());
                loads->statuses.push_back(call.arguments[2].AsInt());
            }
            return VmValue::Void();
        });
    result.push_back(std::move(load_listener).Build());

    auto prepared_listener = IntrinsicClassBuilder::Class(
        "Ltest/MediaPreparedListener;", "Ljava/lang/Object;",
        {"Landroid/media/MediaPlayer$OnPreparedListener;"});
    prepared_listener.VirtualMethod(
        "onPrepared", "(Landroid/media/MediaPlayer;)V",
        [prepared](IntrinsicContext& call) {
            if (prepared != nullptr) {
                prepared->players.push_back(call.arguments[0].ref);
            }
            return VmValue::Void();
        });
    result.push_back(std::move(prepared_listener).Build());
    return result;
}

struct AudioTrackVm final {
    JniStringStore strings;
    JniPrimitiveArrayStore arrays;
    JavaObjectModel model{strings, arrays};
    DexClassLinker linker;
    ogplay::core::CapabilityLedger ledger;
    ogplay::audio::OpenSlesPcmMixer mixer;
    std::shared_ptr<DexVmAndroidContext> context{
        std::make_shared<DexVmAndroidContext>()};
    PositionListenerRecorder recorder;
    LoadCompleteRecorder loads;
    PreparedRecorder prepared;
    Interpreter vm;
    VmThreadRuntime threads;

    explicit AudioTrackVm(
        const std::optional<std::uint32_t> native_output_sample_rate =
            std::nullopt)
        : vm([this, native_output_sample_rate]() -> DexClassLinker& {
                 if (native_output_sample_rate.has_value()) {
                     context->native_output_sample_rate =
                         *native_output_sample_rate;
                 }
                 context->pcm_playback = &mixer;
                 linker.RegisterIntrinsics(CoreIntrinsicCatalog());
                 linker.RegisterIntrinsics(AndroidIntrinsicCatalog(context));
                 linker.RegisterIntrinsics(AudioTrackTestCatalog(
                     &recorder, &loads, &prepared));
                 ogplay::test::RegisterBootDex(linker);
                 linker.Link();
                 return linker;
             }(),
             model, nullptr, ledger, {}),
          threads(vm) {
        vm.Monitors().SetTimeSource([] { return std::int64_t{1}; });
        context->threads = &threads;
        RegisterAndroidSchedulerStateTable(vm, context);
        RegisterAndroidAudioTrackStateTable(vm, context);
        const auto looper =
            linker.ResolveDescriptor("Landroid/os/Looper;");
        const auto prepare = linker.FindDirectMethod(
            looper, "prepareMainLooper", "()V");
        REQUIRE(prepare.has_value());
        const auto prepare_result = vm.Call(*prepare, {});
        REQUIRE_MESSAGE(!prepare_result.exception.IsValid(),
                        prepare_result.exception_message);
    }

    ~AudioTrackVm() {
        ShutdownAndroidScheduler(*context);
        threads.Shutdown();
    }

    VmValue CallStatic(const char* name, const char* descriptor,
                       std::vector<VmValue> arguments) {
        const auto klass = linker.ResolveDescriptor("Landroid/media/AudioTrack;");
        const auto method = linker.FindDirectMethod(klass, name, descriptor);
        REQUIRE(method.has_value());
        const auto outcome = vm.Call(*method, arguments);
        REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
        return outcome.value;
    }

    VmObjectRef NewTrack(const std::int32_t sample_rate,
                         const std::int32_t channels,
                         const std::int32_t encoding,
                         const std::int32_t buffer_size,
                         const std::int32_t mode, const std::int32_t stream = 3) {
        const auto klass = linker.ResolveDescriptor("Landroid/media/AudioTrack;");
        const auto instance = vm.NewIntrinsicInstance("Landroid/media/AudioTrack;");
        const auto ctor = linker.FindDirectMethod(klass, "<init>", "(IIIIII)V");
        REQUIRE(ctor.has_value());
        const std::vector arguments{
            VmValue::Ref(instance), VmValue::Int(stream), VmValue::Int(sample_rate),
            VmValue::Int(channels), VmValue::Int(encoding),
            VmValue::Int(buffer_size), VmValue::Int(mode)};
        const auto outcome = vm.Call(*ctor, arguments);
        REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
        return instance;
    }

    VmValue CallOn(const VmObjectRef receiver, const char* name,
                   const char* descriptor,
                   std::vector<VmValue> arguments = {}) {
        const auto klass = model.ObjectClass(receiver);
        const auto index = linker.FindVtableIndex(klass, name, descriptor);
        REQUIRE(index.has_value());
        arguments.insert(arguments.begin(), VmValue::Ref(receiver));
        const auto outcome = vm.Call(linker.Class(klass).vtable[*index], arguments);
        REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
        return outcome.value;
    }

    VmValue CallDirect(const VmObjectRef receiver, const char* name,
                       const char* descriptor,
                       std::vector<VmValue> arguments = {}) {
        const auto klass = model.ObjectClass(receiver);
        const auto method = linker.FindDirectMethod(klass, name, descriptor);
        REQUIRE(method.has_value());
        arguments.insert(arguments.begin(), VmValue::Ref(receiver));
        const auto outcome = vm.Call(*method, arguments);
        REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
        return outcome.value;
    }

    VmCallOutcome CallOnOutcome(const VmObjectRef receiver, const char* name,
                                const char* descriptor,
                                std::vector<VmValue> arguments = {}) {
        const auto klass = model.ObjectClass(receiver);
        const auto index = linker.FindVtableIndex(klass, name, descriptor);
        REQUIRE(index.has_value());
        arguments.insert(arguments.begin(), VmValue::Ref(receiver));
        return vm.Call(linker.Class(klass).vtable[*index], arguments);
    }

    VmObjectRef NewListener(const char* descriptor =
        "Ltest/AudioPositionListener;") {
        return vm.NewIntrinsicInstance(descriptor);
    }

    void MixFrames(const std::size_t frames, const std::uint32_t rate = 4000U) {
        std::vector<std::int16_t> output(frames * 2U);
        static_cast<void>(mixer.MixAdditiveStereoPcm16(output, rate));
    }

    VmObjectRef ByteArray(const std::vector<std::byte>& values) {
        const auto array = model.NewPrimitiveArray(
            linker.ResolveDescriptor("[B"), JniPrimitiveKind::byte,
            static_cast<JniSize>(values.size()));
        model.WriteByteRegion(array, 0, values);
        return array;
    }

    VmObjectRef ShortArray(const std::vector<std::int16_t>& values) {
        const auto array = model.NewPrimitiveArray(
            linker.ResolveDescriptor("[S"), JniPrimitiveKind::short_integer,
            static_cast<JniSize>(values.size()));
        for (std::size_t index = 0; index < values.size(); ++index) {
            model.SetPrimitiveElement(
                array, static_cast<JniSize>(index),
                static_cast<std::uint16_t>(values[index]));
        }
        return array;
    }
};

}  // namespace

TEST_CASE("AudioTrack native output sample rate comes from the session") {
    AudioTrackVm injected{44100U};
    CHECK(injected.CallStatic(
              "getNativeOutputSampleRate", "(I)I",
              {VmValue::Int(3)}).AsInt() == 44100);

    AudioTrackVm defaults;
    CHECK(defaults.CallStatic(
              "getNativeOutputSampleRate", "(I)I",
              {VmValue::Int(3)}).AsInt() == 48000);
}

TEST_CASE("DVM-84 AudioTrack streams PCM through the OpenSL mixer") {
    AudioTrackVm fixture;
    CHECK(fixture.CallStatic("getMinVolume", "()F", {}).AsFloat() == 0.0F);
    CHECK(fixture.CallStatic("getMaxVolume", "()F", {}).AsFloat() == 1.0F);
    const auto minimum = fixture.CallStatic(
        "getMinBufferSize", "(III)I",
        {VmValue::Int(4000), VmValue::Int(4), VmValue::Int(2)}).AsInt();
    REQUIRE(minimum == 800);
    CHECK(fixture.CallStatic(
              "getMinBufferSize", "(III)I",
              {VmValue::Int(3999), VmValue::Int(4), VmValue::Int(2)}).AsInt() ==
          -2);

    const auto track = fixture.NewTrack(4000, 4, 2, minimum, 1);
    std::vector<std::byte> pcm(static_cast<std::size_t>(minimum));
    for (std::size_t offset = 0; offset < pcm.size(); offset += 2U) {
        pcm[offset] = std::byte{0xe8};
        pcm[offset + 1U] = std::byte{0x03};
    }
    const auto array = fixture.ByteArray(pcm);
    CHECK(fixture.CallOn(track, "write", "([BII)I",
                         {VmValue::Ref(array), VmValue::Int(0),
                          VmValue::Int(minimum)}).AsInt() == minimum);
    CHECK(fixture.CallOn(track, "setStereoVolume", "(FF)I",
                         {VmValue::Float(1.0F), VmValue::Float(0.5F)}).AsInt() == 0);
    static_cast<void>(fixture.CallOn(track, "play", "()V"));
    CHECK(fixture.CallOn(track, "getPlayState", "()I").AsInt() == 3);

    std::array<std::int16_t, 8> mixed{};
    static_cast<void>(fixture.mixer.MixAdditiveStereoPcm16(mixed, 4000U));
    CHECK(mixed[0] == 1000);
    CHECK(mixed[1] == 500);
    CHECK(fixture.CallOn(track, "getPlaybackHeadPosition", "()I").AsInt() == 4);
    static_cast<void>(fixture.CallOn(track, "stop", "()V"));
    CHECK(fixture.CallOn(track, "getPlaybackHeadPosition", "()I").AsInt() == 0);
    static_cast<void>(fixture.CallOn(track, "release", "()V"));
    CHECK(fixture.CallOn(track, "getState", "()I").AsInt() == 0);
}

TEST_CASE("DexVM streaming write releases VM lock while byte-budget blocked") {
    AudioTrackVm fixture;
    constexpr std::int32_t buffer_size = 800;
    const auto track = fixture.NewTrack(4000, 4, 2, buffer_size, 1);
    const auto array = fixture.ByteArray(
        std::vector<std::byte>(buffer_size, std::byte{1}));
    const std::vector write_arguments{
        VmValue::Ref(array), VmValue::Int(0), VmValue::Int(buffer_size)};
    REQUIRE(fixture.CallOn(track, "write", "([BII)I", write_arguments)
                .AsInt() == buffer_size);

    const auto klass = fixture.model.ObjectClass(track);
    const auto index = fixture.linker.FindVtableIndex(
        klass, "write", "([BII)I");
    REQUIRE(index.has_value());
    const auto method = fixture.linker.Class(klass).vtable[*index];
    auto arguments = write_arguments;
    arguments.insert(arguments.begin(), VmValue::Ref(track));
    std::atomic<std::int32_t> result{-99};
    std::atomic<bool> exception{};
    std::jthread writer([&] {
        const auto outcome = fixture.vm.Call(method, arguments);
        exception = outcome.exception.IsValid();
        if (!exception.load()) result = outcome.value.AsInt();
    });
    bool parked{};
    for (std::size_t attempt = 0; attempt < 100000U; ++attempt) {
        if (fixture.mixer.BlockingWriterCount() == 1U) {
            parked = true;
            break;
        }
        std::this_thread::yield();
    }
    if (!parked) {
        static_cast<void>(fixture.mixer.InterruptBlockingWaits());
    }
    REQUIRE(parked);

    static_cast<void>(fixture.CallOn(track, "play", "()V"));
    fixture.MixFrames(400U);
    writer.join();
    CHECK_FALSE(exception.load());
    CHECK(result.load() == buffer_size);
    CHECK(fixture.mixer.QueuedBytes(
              fixture.context->audio_tracks.at(track.Value()).player) <=
          static_cast<std::size_t>(buffer_size));
}

TEST_CASE("DVM-84 static short writes initialize and GC releases PCM players") {
    AudioTrackVm fixture;
    const auto track = fixture.NewTrack(8000, 12, 2, 8, 0);
    CHECK(fixture.CallOn(track, "getState", "()I").AsInt() == 2);
    const auto samples = fixture.ShortArray({100, -100, 200, -200});
    CHECK(fixture.CallOn(track, "write", "([SII)I",
                         {VmValue::Ref(samples), VmValue::Int(0),
                          VmValue::Int(4)}).AsInt() == 4);
    CHECK(fixture.CallOn(track, "getState", "()I").AsInt() == 1);
    REQUIRE(fixture.context->audio_tracks.size() == 1U);
    const auto player = fixture.context->audio_tracks.at(track.Value()).player;
    REQUIRE(fixture.mixer.HasPlayer(player));
    const auto swept = fixture.vm.CollectGarbage("dvm84-audio-track");
    CHECK(swept.freed_objects >= 1U);
    CHECK_FALSE(fixture.mixer.HasPlayer(player));
    CHECK(fixture.context->audio_tracks.empty());
}

TEST_CASE("AudioTrack periodic notifications follow mixer head boundaries") {
    AudioTrackVm fixture;
    constexpr std::int32_t buffer_size = 800;
    const auto track = fixture.NewTrack(4000, 4, 2, buffer_size, 1);
    const auto listener = fixture.NewListener();
    const auto pcm = fixture.ByteArray(
        std::vector<std::byte>(buffer_size, std::byte{}));
    CHECK(fixture.CallOn(
              track, "setPositionNotificationPeriod", "(I)I",
              {VmValue::Int(3)}).AsInt() == 0);
    static_cast<void>(fixture.CallOn(
        track, "setPlaybackPositionUpdateListener",
        "(Landroid/media/AudioTrack$OnPlaybackPositionUpdateListener;)V",
        {VmValue::Ref(listener)}));
    CHECK(fixture.CallOn(
              track, "write", "([BII)I",
              {VmValue::Ref(pcm), VmValue::Int(0),
               VmValue::Int(buffer_size)}).AsInt() == buffer_size);
    static_cast<void>(fixture.CallOn(track, "play", "()V"));

    fixture.MixFrames(10U);
    CHECK_FALSE(PumpAndroidAudioTracks(fixture.vm, *fixture.context).has_value());
    REQUIRE(fixture.recorder.periodic.size() == 3U);
    CHECK(std::ranges::all_of(
        fixture.recorder.periodic,
        [track](const auto receiver) { return receiver == track; }));
    fixture.MixFrames(2U);
    CHECK_FALSE(PumpAndroidAudioTracks(fixture.vm, *fixture.context).has_value());
    REQUIRE(fixture.recorder.periodic.size() == 4U);
    CHECK(fixture.recorder.periodic.back() == track);

    static_cast<void>(fixture.CallOn(
        track, "setPlaybackPositionUpdateListener",
        "(Landroid/media/AudioTrack$OnPlaybackPositionUpdateListener;)V",
        {VmValue::Ref(VmObjectRef{})}));
    fixture.MixFrames(6U);
    CHECK_FALSE(PumpAndroidAudioTracks(fixture.vm, *fixture.context).has_value());
    CHECK(fixture.recorder.periodic.size() == 4U);
}

TEST_CASE("AudioTrack notification getters expose persistent configured state") {
    AudioTrackVm fixture;
    const auto track = fixture.NewTrack(4000, 4, 2, 800, 1);

    CHECK(fixture.CallOn(
              track, "getPositionNotificationPeriod", "()I").AsInt() == 0);
    CHECK(fixture.CallOn(
              track, "getNotificationMarkerPosition", "()I").AsInt() == 0);
    CHECK(fixture.CallOn(
              track, "setPositionNotificationPeriod", "(I)I",
              {VmValue::Int(17)}).AsInt() == 0);
    CHECK(fixture.CallOn(
              track, "setNotificationMarkerPosition", "(I)I",
              {VmValue::Int(23)}).AsInt() == 0);
    CHECK(fixture.CallOn(
              track, "getPositionNotificationPeriod", "()I").AsInt() == 17);
    CHECK(fixture.CallOn(
              track, "getNotificationMarkerPosition", "()I").AsInt() == 23);

    CHECK(fixture.CallOn(
              track, "setPositionNotificationPeriod", "(I)I",
              {VmValue::Int(-1)}).AsInt() == -2);
    CHECK(fixture.CallOn(
              track, "setNotificationMarkerPosition", "(I)I",
              {VmValue::Int(-1)}).AsInt() == -2);
    CHECK(fixture.CallOn(
              track, "getPositionNotificationPeriod", "()I").AsInt() == 17);
    CHECK(fixture.CallOn(
              track, "getNotificationMarkerPosition", "()I").AsInt() == 23);

    static_cast<void>(fixture.CallOn(track, "pause", "()V"));
    static_cast<void>(fixture.CallOn(track, "flush", "()V"));
    static_cast<void>(fixture.CallOn(track, "stop", "()V"));
    CHECK(fixture.CallOn(
              track, "getPositionNotificationPeriod", "()I").AsInt() == 17);
    CHECK(fixture.CallOn(
              track, "getNotificationMarkerPosition", "()I").AsInt() == 23);

    static_cast<void>(fixture.CallOn(track, "release", "()V"));
    CHECK(fixture.CallOn(
              track, "getPositionNotificationPeriod", "()I").AsInt() == 0);
    CHECK(fixture.CallOn(
              track, "getNotificationMarkerPosition", "()I").AsInt() == 0);

    const auto uninitialized =
        fixture.vm.NewIntrinsicInstance("Landroid/media/AudioTrack;");
    CHECK(fixture.CallOn(
              uninitialized, "getNotificationMarkerPosition", "()I").AsInt() ==
          0);
}

TEST_CASE("AudioTrack marker notification fires once and rearms on set") {
    AudioTrackVm fixture;
    constexpr std::int32_t buffer_size = 800;
    const auto track = fixture.NewTrack(4000, 4, 2, buffer_size, 1);
    const auto listener = fixture.NewListener();
    const auto pcm = fixture.ByteArray(
        std::vector<std::byte>(buffer_size, std::byte{}));
    static_cast<void>(fixture.CallOn(
        track, "setPlaybackPositionUpdateListener",
        "(Landroid/media/AudioTrack$OnPlaybackPositionUpdateListener;)V",
        {VmValue::Ref(listener)}));
    CHECK(fixture.CallOn(
              track, "setNotificationMarkerPosition", "(I)I",
              {VmValue::Int(5)}).AsInt() == 0);
    CHECK(fixture.CallOn(
              track, "write", "([BII)I",
              {VmValue::Ref(pcm), VmValue::Int(0),
               VmValue::Int(buffer_size)}).AsInt() == buffer_size);
    static_cast<void>(fixture.CallOn(track, "play", "()V"));

    fixture.MixFrames(4U);
    CHECK_FALSE(PumpAndroidAudioTracks(fixture.vm, *fixture.context).has_value());
    CHECK(fixture.recorder.markers.empty());
    fixture.MixFrames(2U);
    CHECK_FALSE(PumpAndroidAudioTracks(fixture.vm, *fixture.context).has_value());
    REQUIRE(fixture.recorder.markers.size() == 1U);
    CHECK(fixture.recorder.markers.front() == track);
    CHECK_FALSE(PumpAndroidAudioTracks(fixture.vm, *fixture.context).has_value());
    CHECK(fixture.recorder.markers.size() == 1U);

    CHECK(fixture.CallOn(
              track, "setNotificationMarkerPosition", "(I)I",
              {VmValue::Int(8)}).AsInt() == 0);
    fixture.MixFrames(2U);
    CHECK_FALSE(PumpAndroidAudioTracks(fixture.vm, *fixture.context).has_value());
    REQUIRE(fixture.recorder.markers.size() == 2U);
    CHECK(fixture.recorder.markers.back() == track);
}

TEST_CASE("AudioTrack notification baseline handles pause flush and release") {
    AudioTrackVm fixture;
    constexpr std::int32_t buffer_size = 800;
    const auto track = fixture.NewTrack(4000, 4, 2, buffer_size, 1);
    const auto listener = fixture.NewListener();
    const auto pcm = fixture.ByteArray(
        std::vector<std::byte>(buffer_size, std::byte{}));
    static_cast<void>(fixture.CallOn(
        track, "setPlaybackPositionUpdateListener",
        "(Landroid/media/AudioTrack$OnPlaybackPositionUpdateListener;)V",
        {VmValue::Ref(listener)}));
    CHECK(fixture.CallOn(
              track, "setPositionNotificationPeriod", "(I)I",
              {VmValue::Int(2)}).AsInt() == 0);
    CHECK(fixture.CallOn(
              track, "write", "([BII)I",
              {VmValue::Ref(pcm), VmValue::Int(0),
               VmValue::Int(buffer_size)}).AsInt() == buffer_size);
    static_cast<void>(fixture.CallOn(track, "play", "()V"));
    fixture.MixFrames(2U);
    CHECK_FALSE(PumpAndroidAudioTracks(fixture.vm, *fixture.context).has_value());
    REQUIRE(fixture.recorder.periodic.size() == 1U);

    static_cast<void>(fixture.CallOn(track, "pause", "()V"));
    fixture.MixFrames(4U);
    CHECK_FALSE(PumpAndroidAudioTracks(fixture.vm, *fixture.context).has_value());
    CHECK(fixture.recorder.periodic.size() == 1U);
    static_cast<void>(fixture.CallOn(track, "play", "()V"));
    fixture.MixFrames(2U);
    CHECK_FALSE(PumpAndroidAudioTracks(fixture.vm, *fixture.context).has_value());
    REQUIRE(fixture.recorder.periodic.size() == 2U);

    static_cast<void>(fixture.CallOn(track, "stop", "()V"));
    CHECK(fixture.CallOn(track, "getPlaybackHeadPosition", "()I").AsInt() == 0);
    CHECK_FALSE(PumpAndroidAudioTracks(fixture.vm, *fixture.context).has_value());
    CHECK(fixture.recorder.periodic.size() == 2U);
    static_cast<void>(fixture.CallOn(track, "play", "()V"));
    fixture.MixFrames(2U);
    CHECK_FALSE(PumpAndroidAudioTracks(fixture.vm, *fixture.context).has_value());
    REQUIRE(fixture.recorder.periodic.size() == 3U);

    static_cast<void>(fixture.CallOn(track, "pause", "()V"));
    static_cast<void>(fixture.CallOn(track, "flush", "()V"));
    CHECK(fixture.CallOn(track, "getPlaybackHeadPosition", "()I").AsInt() == 2);
    CHECK_FALSE(PumpAndroidAudioTracks(fixture.vm, *fixture.context).has_value());
    CHECK(fixture.recorder.periodic.size() == 3U);
    static_cast<void>(fixture.CallOn(track, "play", "()V"));
    CHECK(fixture.CallOn(
              track, "write", "([BII)I",
              {VmValue::Ref(pcm), VmValue::Int(0),
               VmValue::Int(buffer_size)}).AsInt() == buffer_size);
    fixture.MixFrames(2U);
    CHECK_FALSE(PumpAndroidAudioTracks(fixture.vm, *fixture.context).has_value());
    REQUIRE(fixture.recorder.periodic.size() == 4U);

    static_cast<void>(fixture.CallOn(track, "release", "()V"));
    CHECK_FALSE(PumpAndroidAudioTracks(fixture.vm, *fixture.context).has_value());
    CHECK(fixture.recorder.periodic.size() == 4U);
}

TEST_CASE("AudioTrack listener can refill stream during periodic callback") {
    AudioTrackVm fixture;
    constexpr std::int32_t buffer_size = 800;
    const auto track = fixture.NewTrack(4000, 4, 2, buffer_size, 1);
    const auto listener = fixture.NewListener();
    const auto pcm = fixture.ByteArray(
        std::vector<std::byte>(buffer_size, std::byte{}));
    fixture.recorder.write_on_periodic = true;
    fixture.recorder.write_array = pcm;
    fixture.recorder.write_count = buffer_size;
    static_cast<void>(fixture.CallOn(
        track, "setPlaybackPositionUpdateListener",
        "(Landroid/media/AudioTrack$OnPlaybackPositionUpdateListener;)V",
        {VmValue::Ref(listener)}));
    CHECK(fixture.CallOn(
              track, "setPositionNotificationPeriod", "(I)I",
              {VmValue::Int(400)}).AsInt() == 0);
    CHECK(fixture.CallOn(
              track, "write", "([BII)I",
              {VmValue::Ref(pcm), VmValue::Int(0),
               VmValue::Int(buffer_size)}).AsInt() == buffer_size);
    static_cast<void>(fixture.CallOn(track, "play", "()V"));
    fixture.MixFrames(400U);
    CHECK_FALSE(PumpAndroidAudioTracks(fixture.vm, *fixture.context).has_value());
    CHECK(fixture.recorder.periodic.size() == 1U);
    CHECK(fixture.recorder.write_result == buffer_size);
    const auto player = fixture.context->audio_tracks.at(track.Value()).player;
    CHECK(fixture.mixer.QueueState(player).count == 1U);
}

TEST_CASE("AudioTrack refill defers without dropping overdue periodic callbacks") {
    AudioTrackVm fixture;
    constexpr std::int32_t buffer_size = 800;
    const auto track = fixture.NewTrack(4000, 4, 2, buffer_size, 1);
    const auto listener = fixture.NewListener();
    const auto pcm = fixture.ByteArray(
        std::vector<std::byte>(buffer_size, std::byte{}));
    fixture.recorder.write_on_periodic = true;
    fixture.recorder.write_array = pcm;
    constexpr std::int32_t period_frames = 100;
    constexpr std::int32_t bytes_per_frame = 2;
    fixture.recorder.write_count = period_frames * bytes_per_frame;
    static_cast<void>(fixture.CallOn(
        track, "setPlaybackPositionUpdateListener",
        "(Landroid/media/AudioTrack$OnPlaybackPositionUpdateListener;)V",
        {VmValue::Ref(listener)}));
    CHECK(fixture.CallOn(
              track, "setPositionNotificationPeriod", "(I)I",
              {VmValue::Int(period_frames)}).AsInt() == 0);
    CHECK(fixture.CallOn(
              track, "write", "([BII)I",
              {VmValue::Ref(pcm), VmValue::Int(0),
               VmValue::Int(buffer_size)}).AsInt() == buffer_size);
    static_cast<void>(fixture.CallOn(track, "play", "()V"));

    fixture.MixFrames(400U);
    const auto player = fixture.context->audio_tracks.at(track.Value()).player;
    for (std::size_t delivered = 1U; delivered <= 4U; ++delivered) {
        CHECK_FALSE(
            PumpAndroidAudioTracks(fixture.vm, *fixture.context).has_value());
        CHECK(fixture.recorder.periodic.size() == delivered);
        CHECK(fixture.recorder.write_result ==
              period_frames * bytes_per_frame);
        CHECK(fixture.context->audio_tracks.at(track.Value()).last_notified_head ==
              delivered * static_cast<std::size_t>(period_frames));
        const auto progress = SnapshotAndroidAudioTracks(*fixture.context);
        REQUIRE(progress.size() == 1U);
        CHECK(progress[0].periodic_callbacks_generated == 4U);
        CHECK(progress[0].periodic_callbacks_delivered == delivered);
        CHECK(progress[0].periodic_callbacks_deferred == 4U - delivered);
    }
    CHECK_FALSE(PumpAndroidAudioTracks(fixture.vm, *fixture.context).has_value());
    CHECK(fixture.recorder.periodic.size() == 4U);
    CHECK(fixture.mixer.QueuedBytes(player) ==
          static_cast<std::size_t>(buffer_size));
    CHECK(fixture.mixer.BlockingWriterCount() == 0U);
    const auto snapshots = SnapshotAndroidAudioTracks(*fixture.context);
    REQUIRE(snapshots.size() == 1U);
    CHECK(snapshots[0].periodic_callbacks_generated == 4U);
    CHECK(snapshots[0].periodic_callbacks_delivered == 4U);
    CHECK(snapshots[0].periodic_callbacks_deferred == 0U);
}

TEST_CASE("AudioTrack buffer eighth refill remains stable for logical 30 seconds") {
    AudioTrackVm fixture;
    constexpr std::int32_t sample_rate = 48000;
    constexpr std::int32_t bytes_per_frame = 4;
    constexpr std::int32_t buffer_size = 32768;
    constexpr std::int32_t buffer_frames = buffer_size / bytes_per_frame;
    constexpr std::int32_t period_frames = buffer_frames / 8;
    constexpr std::size_t output_chunk_frames = 1024U;
    constexpr std::size_t output_chunks =
        (static_cast<std::size_t>(sample_rate) * 30U +
         output_chunk_frames - 1U) /
        output_chunk_frames;
    const auto track =
        fixture.NewTrack(sample_rate, 12, 2, buffer_size, 1);
    const auto listener = fixture.NewListener();
    const auto pcm = fixture.ByteArray(
        std::vector<std::byte>(buffer_size, std::byte{}));
    fixture.recorder.write_on_periodic = true;
    fixture.recorder.write_array = pcm;
    fixture.recorder.write_count = period_frames * bytes_per_frame;
    static_cast<void>(fixture.CallOn(
        track, "setPlaybackPositionUpdateListener",
        "(Landroid/media/AudioTrack$OnPlaybackPositionUpdateListener;)V",
        {VmValue::Ref(listener)}));
    CHECK(fixture.CallOn(
              track, "setPositionNotificationPeriod", "(I)I",
              {VmValue::Int(period_frames)}).AsInt() == 0);
    CHECK(fixture.CallOn(
              track, "write", "([BII)I",
              {VmValue::Ref(pcm), VmValue::Int(0),
               VmValue::Int(buffer_size)}).AsInt() == buffer_size);
    static_cast<void>(fixture.CallOn(track, "play", "()V"));

    for (std::size_t chunk = 0; chunk < output_chunks; ++chunk) {
        fixture.MixFrames(output_chunk_frames, sample_rate);
        REQUIRE_FALSE(
            PumpAndroidAudioTracks(fixture.vm, *fixture.context).has_value());
    }

    const auto snapshots = SnapshotAndroidAudioTracks(*fixture.context);
    REQUIRE(snapshots.size() == 1U);
    const auto& snapshot = snapshots[0];
    CHECK(snapshot.periodic_callbacks_generated == output_chunks);
    CHECK(snapshot.periodic_callbacks_delivered == output_chunks);
    CHECK(snapshot.periodic_callbacks_deferred == 0U);
    CHECK(snapshot.underrun_count == 0U);
    CHECK(snapshot.underrun_output_frames == 0U);
    CHECK(snapshot.consumed_frames == output_chunks * output_chunk_frames);
    CHECK(snapshot.written_frames ==
          static_cast<std::uint64_t>(buffer_frames) +
              output_chunks * static_cast<std::uint64_t>(period_frames));
    CHECK(snapshot.queued_bytes == static_cast<std::size_t>(buffer_size));
    CHECK(fixture.recorder.periodic.size() == output_chunks);
}

TEST_CASE("AudioTrack notification setters report errors and callback faults") {
    AudioTrackVm fixture;
    constexpr std::int32_t buffer_size = 800;
    const auto track = fixture.NewTrack(4000, 4, 2, buffer_size, 1);
    CHECK(fixture.CallOn(
              track, "setPositionNotificationPeriod", "(I)I",
              {VmValue::Int(-1)}).AsInt() == -2);
    CHECK(fixture.CallOn(
              track, "setNotificationMarkerPosition", "(I)I",
              {VmValue::Int(-1)}).AsInt() == -2);

    const auto uninitialized =
        fixture.vm.NewIntrinsicInstance("Landroid/media/AudioTrack;");
    CHECK(fixture.CallOn(
              uninitialized, "setPositionNotificationPeriod", "(I)I",
              {VmValue::Int(1)}).AsInt() == -3);
    CHECK(fixture.CallOn(
              uninitialized, "setNotificationMarkerPosition", "(I)I",
              {VmValue::Int(1)}).AsInt() == -3);
    const auto listener = fixture.NewListener();
    static_cast<void>(fixture.CallOn(
        uninitialized, "setPlaybackPositionUpdateListener",
        "(Landroid/media/AudioTrack$OnPlaybackPositionUpdateListener;)V",
        {VmValue::Ref(listener)}));

    const auto throwing = fixture.NewListener(
        "Ltest/ThrowingAudioPositionListener;");
    const auto pcm = fixture.ByteArray(
        std::vector<std::byte>(buffer_size, std::byte{}));
    static_cast<void>(fixture.CallOn(
        track, "setPlaybackPositionUpdateListener",
        "(Landroid/media/AudioTrack$OnPlaybackPositionUpdateListener;)V",
        {VmValue::Ref(throwing)}));
    CHECK(fixture.CallOn(
              track, "setPositionNotificationPeriod", "(I)I",
              {VmValue::Int(1)}).AsInt() == 0);
    CHECK(fixture.CallOn(
              track, "write", "([BII)I",
              {VmValue::Ref(pcm), VmValue::Int(0),
               VmValue::Int(buffer_size)}).AsInt() == buffer_size);
    static_cast<void>(fixture.CallOn(track, "play", "()V"));
    fixture.MixFrames(1U);
    const auto error = PumpAndroidAudioTracks(fixture.vm, *fixture.context);
    REQUIRE(error.has_value());
    CHECK(error->find("handleMessage raised") != std::string::npos);
    CHECK(error->find("position listener failure") != std::string::npos);
}

TEST_CASE("AudioManager volume mute and isMusicActive follow session playback") {
    AudioTrackVm fixture;
    const auto manager =
        fixture.vm.NewIntrinsicInstance("Landroid/media/AudioManager;");
    CHECK(fixture.CallOn(manager, "getStreamMaxVolume", "(I)I",
                         {VmValue::Int(3)}).AsInt() == 15);
    static_cast<void>(fixture.CallOn(
        manager, "setStreamVolume", "(III)V",
        {VmValue::Int(3), VmValue::Int(4), VmValue::Int(0)}));
    CHECK(fixture.CallOn(manager, "getStreamVolume", "(I)I",
                         {VmValue::Int(3)}).AsInt() == 4);
    static_cast<void>(fixture.CallOn(
        manager, "setStreamMute", "(IZ)V",
        {VmValue::Int(3), VmValue::Int(1)}));
    CHECK(fixture.CallOn(manager, "getStreamVolume", "(I)I",
                         {VmValue::Int(3)}).AsInt() == 0);
    CHECK(fixture.CallOn(manager, "isMusicActive", "()Z").AsInt() == 0);
    const auto track = fixture.NewTrack(4000, 4, 2, 800, 1);
    const auto pcm = fixture.ByteArray(std::vector<std::byte>(800, std::byte{}));
    CHECK(fixture.CallOn(track, "write", "([BII)I",
                         {VmValue::Ref(pcm), VmValue::Int(0),
                          VmValue::Int(800)}).AsInt() == 800);
    static_cast<void>(fixture.CallOn(track, "play", "()V"));
    CHECK(fixture.CallOn(manager, "isMusicActive", "()Z").AsInt() == 1);
}

TEST_CASE("SoundPool BootDex natives isolate two pools") {
    const auto path = std::filesystem::path{OGPLAY_SOURCE_DIR} /
                      "tests/fixtures/audio/short-vorbis.ogg";
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    const std::vector<char> chars{std::istreambuf_iterator<char>(input), {}};
    std::vector<std::byte> ogg(chars.size());
    for (std::size_t index = 0; index < chars.size(); ++index) {
        ogg[index] = static_cast<std::byte>(chars[index]);
    }
    AudioTrackVm fixture;
    ogplay::audio::JavaSoundPoolMixer mixer{
        [&ogg](const ogplay::audio::EncodedAudioSource&) { return ogg; }};
    fixture.context->encoded_audio_playback = &mixer;
    const auto make_pool = [&] {
        const auto pool =
            fixture.vm.NewIntrinsicInstance("Landroid/media/SoundPool;");
        const auto klass =
            fixture.linker.ResolveDescriptor("Landroid/media/SoundPool;");
        const auto ctor =
            fixture.linker.FindDirectMethod(klass, "<init>", "(III)V");
        REQUIRE(ctor.has_value());
        const std::vector arguments{
            VmValue::Ref(pool), VmValue::Int(2), VmValue::Int(3),
            VmValue::Int(0)};
        const auto outcome = fixture.vm.Call(*ctor, arguments);
        REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
        return pool;
    };
    const auto first = make_pool();
    const auto second = make_pool();
    const auto load = [&](const VmObjectRef pool) {
        return fixture.CallOn(
            pool, "load", "(Ljava/lang/String;I)I",
            {VmValue::Ref(fixture.vm.NewStringUtf8("http://sdcard/a.ogg")),
             VmValue::Int(1)}).AsInt();
    };
    const auto sound_a = load(first);
    const auto sound_b = load(second);
    REQUIRE(sound_a != 0);
    REQUIRE(sound_b != 0);
    const auto stream_a = fixture.CallOn(
        first, "play", "(IFFIIF)I",
        {VmValue::Int(sound_a), VmValue::Float(1.0F), VmValue::Float(0.0F),
         VmValue::Int(1), VmValue::Int(0), VmValue::Float(1.0F)}).AsInt();
    const auto stream_b = fixture.CallOn(
        second, "play", "(IFFIIF)I",
        {VmValue::Int(sound_b), VmValue::Float(0.0F), VmValue::Float(1.0F),
         VmValue::Int(1), VmValue::Int(-1), VmValue::Float(1.0F)}).AsInt();
    REQUIRE(stream_a != 0);
    REQUIRE(stream_b != 0);
    CHECK(stream_a != stream_b);
    CHECK(fixture.CallOn(first, "unload", "(I)Z", {VmValue::Int(sound_a)})
              .AsInt() == 1);
    static_cast<void>(fixture.CallOn(first, "release", "()V"));
    CHECK(mixer.ActiveVoiceCount() >= 1U);
}

TEST_CASE("MediaPlayer BootDex instances prepare seek and mix independently") {
    const auto path = std::filesystem::path{OGPLAY_SOURCE_DIR} /
                      "tests/fixtures/audio/short-vorbis.ogg";
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    const std::vector<char> chars{std::istreambuf_iterator<char>(input), {}};
    std::vector<std::byte> ogg(chars.size());
    for (std::size_t index = 0; index < chars.size(); ++index) {
        ogg[index] = static_cast<std::byte>(chars[index]);
    }
    AudioTrackVm fixture;
    ogplay::audio::EncodedMusicMixer music;
    fixture.context->encoded_music = &music;
    const auto make_player = [&] {
        const auto player =
            fixture.vm.NewIntrinsicInstance("Landroid/media/MediaPlayer;");
        fixture.CallDirect(player, "<init>", "()V");
        return player;
    };
    const auto first = make_player();
    const auto second = make_player();
    REQUIRE(music.SetEncoded(
        fixture.context->media_players.at(first.Value()).music, ogg));
    REQUIRE(music.SetEncoded(
        fixture.context->media_players.at(second.Value()).music, ogg));
    static_cast<void>(fixture.CallOn(first, "prepare", "()V"));
    static_cast<void>(fixture.CallOn(second, "prepare", "()V"));
    static_cast<void>(fixture.CallOn(first, "setVolume", "(FF)V",
                                     {VmValue::Float(1.0F), VmValue::Float(0.0F)}));
    static_cast<void>(fixture.CallOn(second, "setLooping", "(Z)V",
                                     {VmValue::Int(1)}));
    static_cast<void>(fixture.CallOn(first, "start", "()V"));
    static_cast<void>(fixture.CallOn(second, "start", "()V"));
    CHECK(fixture.CallOn(first, "isPlaying", "()Z").AsInt() == 1);
    CHECK(fixture.CallOn(second, "getDuration", "()I").AsInt() > 0);
    static_cast<void>(fixture.CallOn(
        first, "seekTo", "(I)V",
        {VmValue::Int(fixture.CallOn(first, "getDuration", "()I").AsInt())}));
    std::vector<std::int16_t> pcm(64U * 2U);
    std::vector<std::int64_t> accumulator(pcm.size());
    music.MixIntoAccumulator(accumulator, 48000U);
    ogplay::audio::SaturateStereoPcm16(accumulator, pcm);
    CHECK(std::ranges::any_of(
        pcm, [](const std::int16_t sample) { return sample != 0; }));
    static_cast<void>(fixture.CallOn(first, "reset", "()V"));
    CHECK(fixture.CallOn(first, "isPlaying", "()Z").AsInt() == 0);
    CHECK(fixture.CallOn(second, "isPlaying", "()Z").AsInt() == 1);
}

TEST_CASE("SoundPool BootDex load overloads complete through Handler") {
    const auto path = std::filesystem::path{OGPLAY_SOURCE_DIR} /
                      "tests/fixtures/audio/short-vorbis.ogg";
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    const std::vector<char> chars{std::istreambuf_iterator<char>(input), {}};
    std::vector<std::byte> ogg(chars.size());
    for (std::size_t index = 0; index < chars.size(); ++index) {
        ogg[index] = static_cast<std::byte>(chars[index]);
    }
    AudioTrackVm fixture;
    ogplay::audio::JavaSoundPoolMixer mixer{
        [&ogg](const ogplay::audio::EncodedAudioSource&) { return ogg; }};
    fixture.context->encoded_audio_playback = &mixer;
    const auto klass =
        fixture.linker.ResolveDescriptor("Landroid/media/SoundPool;");
    CHECK(fixture.linker.FindVtableIndex(
              klass, "load", "(Ljava/lang/String;I)I").has_value());
    CHECK(fixture.linker.FindVtableIndex(
              klass, "load", "(Landroid/content/Context;II)I").has_value());
    CHECK(fixture.linker.FindVtableIndex(
              klass, "load",
              "(Landroid/content/res/AssetFileDescriptor;I)I").has_value());
    CHECK(fixture.linker.FindVtableIndex(
              klass, "load", "(Ljava/io/FileDescriptor;JJI)I").has_value());
    const auto pool =
        fixture.vm.NewIntrinsicInstance("Landroid/media/SoundPool;");
    const auto ctor =
        fixture.linker.FindDirectMethod(klass, "<init>", "(III)V");
    REQUIRE(ctor.has_value());
    const auto constructed = fixture.vm.Call(
        *ctor, std::vector<VmValue>{VmValue::Ref(pool), VmValue::Int(2),
                                    VmValue::Int(3), VmValue::Int(0)});
    REQUIRE_MESSAGE(!constructed.exception.IsValid(),
                    constructed.exception_message);
    const auto listener =
        fixture.vm.NewIntrinsicInstance("Ltest/SoundLoadListener;");
    static_cast<void>(fixture.CallOn(
        pool, "setOnLoadCompleteListener",
        "(Landroid/media/SoundPool$OnLoadCompleteListener;)V",
        {VmValue::Ref(listener)}));
    const auto sound = fixture.CallOn(
        pool, "load", "(Ljava/lang/String;I)I",
        {VmValue::Ref(fixture.vm.NewStringUtf8("http://sdcard/a.ogg")),
         VmValue::Int(1)}).AsInt();
    REQUIRE(sound != 0);
    CHECK_FALSE(PumpAndroidAudioTracks(fixture.vm, *fixture.context).has_value());
    REQUIRE(fixture.loads.sounds.size() == 1U);
    CHECK(fixture.loads.sounds[0] == sound);
    CHECK(fixture.loads.statuses[0] == 0);
}

TEST_CASE("MediaPlayer prepareAsync posts the BootDex prepared event") {
    const auto path = std::filesystem::path{OGPLAY_SOURCE_DIR} /
                      "tests/fixtures/audio/short-vorbis.ogg";
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    const std::vector<char> chars{std::istreambuf_iterator<char>(input), {}};
    std::vector<std::byte> ogg(chars.size());
    for (std::size_t index = 0; index < chars.size(); ++index) {
        ogg[index] = static_cast<std::byte>(chars[index]);
    }
    AudioTrackVm fixture;
    ogplay::audio::EncodedMusicMixer music;
    fixture.context->encoded_music = &music;
    const auto player =
        fixture.vm.NewIntrinsicInstance("Landroid/media/MediaPlayer;");
    fixture.CallDirect(player, "<init>", "()V");
    const auto listener =
        fixture.vm.NewIntrinsicInstance("Ltest/MediaPreparedListener;");
    static_cast<void>(fixture.CallOn(
        player, "setOnPreparedListener",
        "(Landroid/media/MediaPlayer$OnPreparedListener;)V",
        {VmValue::Ref(listener)}));
    REQUIRE(music.SetEncoded(
        fixture.context->media_players.at(player.Value()).music, ogg));
    static_cast<void>(fixture.CallOn(player, "prepareAsync", "()V"));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (fixture.prepared.players.empty() && std::chrono::steady_clock::now() < deadline) {
        CHECK_FALSE(
            PumpAndroidAudioTracks(fixture.vm, *fixture.context).has_value());
        std::this_thread::yield();
    }
    REQUIRE(fixture.prepared.players.size() == 1U);
    CHECK(fixture.prepared.players[0] == player);
}

TEST_CASE("MediaPlayer transport uses explicit stopped preparing and error phases") {
    using Phase = DexVmAndroidContext::MediaPlayerState::Phase;
    const auto path = std::filesystem::path{OGPLAY_SOURCE_DIR} / "tests/fixtures/audio/short-vorbis.ogg";
    std::ifstream input(path, std::ios::binary);
    const std::vector<char> raw{std::istreambuf_iterator<char>(input), {}};
    std::vector<std::byte> encoded;
    for (auto c : raw) encoded.push_back(static_cast<std::byte>(c));
    AudioTrackVm fixture;
    ogplay::audio::EncodedMusicMixer music;
    fixture.context->encoded_music = &music;
    const auto player = fixture.vm.NewIntrinsicInstance("Landroid/media/MediaPlayer;");
    fixture.CallDirect(player, "<init>", "()V");
    auto& state = fixture.context->media_players.at(player.Value());
    REQUIRE(music.SetEncoded(state.music, encoded));
    fixture.CallOn(player, "prepare", "()V");
    fixture.CallOn(player, "start", "()V");
    fixture.CallOn(player, "pause", "()V");
    fixture.CallOn(player, "pause", "()V");
    CHECK(state.phase == Phase::paused);
    fixture.CallOn(player, "stop", "()V");
    fixture.CallOn(player, "stop", "()V");
    CHECK(state.phase == Phase::stopped);
    fixture.CallOn(player, "prepare", "()V");
    fixture.CallOn(player, "start", "()V");
    CHECK(music.IsPlaying(state.music));
    fixture.CallOn(player, "stop", "()V");
    fixture.CallOn(player, "start", "()V");
    CHECK(state.phase == Phase::error);
    CHECK(state.error_event_pending);
    CHECK_FALSE(music.IsPlaying(state.music));
    fixture.CallOn(player, "reset", "()V");
    CHECK(state.phase == Phase::idle);
    CHECK_FALSE(state.error_event_pending);
    REQUIRE(music.SetEncoded(state.music, encoded));
    fixture.CallOn(player, "prepareAsync", "()V");
    CHECK(state.phase == Phase::preparing);
    CHECK(fixture.CallOnOutcome(player, "prepare", "()V").exception.IsValid());
    CHECK(fixture.CallOnOutcome(player, "prepareAsync", "()V").exception.IsValid());
    fixture.CallOn(player, "reset", "()V");
    CHECK_FALSE(PumpAndroidAudioTracks(fixture.vm, *fixture.context).has_value());
    CHECK(state.phase == Phase::idle);
    CHECK(music.CachedDecodedBytes() == 0);
    REQUIRE(music.SetEncoded(state.music, encoded));
    fixture.CallOn(player, "prepare", "()V");
    fixture.CallOn(player, "pause", "()V");
    CHECK(state.phase == Phase::error); // pause is not legal in Prepared.
    fixture.CallOn(player, "release", "()V");
    CHECK(fixture.context->media_players.empty());
}

TEST_CASE("AudioManager applies stream volume to AudioTrack PCM without cross muting") {
    AudioTrackVm fixture;
    const auto a = fixture.NewTrack(8000, 4, 2, 16, 0, 3);
    const auto b = fixture.NewTrack(8000, 4, 2, 16, 0, 4);
    const auto pa = fixture.context->audio_tracks.at(a.Value()).player;
    const auto pb = fixture.context->audio_tracks.at(b.Value()).player;
    const std::array<std::byte, 8> a_pcm{std::byte{0xe8},std::byte{3},std::byte{0xe8},std::byte{3},
                                      std::byte{0xe8},std::byte{3},std::byte{0xe8},std::byte{3}};
    const std::array<std::byte, 8> b_pcm{std::byte{0xd0},std::byte{7},std::byte{0xd0},std::byte{7},
                                      std::byte{0xd0},std::byte{7},std::byte{0xd0},std::byte{7}};
    REQUIRE(fixture.mixer.Enqueue(pa, a_pcm)); REQUIRE(fixture.mixer.Enqueue(pb, b_pcm));
    fixture.mixer.SetPlayState(pa, ogplay::audio::OpenSlesPlayState::playing);
    fixture.mixer.SetPlayState(pb, ogplay::audio::OpenSlesPlayState::playing);
    const auto manager = fixture.vm.NewIntrinsicInstance("Landroid/media/AudioManager;");
    fixture.CallOn(manager, "setStreamVolume", "(III)V", {VmValue::Int(3),VmValue::Int(15),VmValue::Int(0)});
    fixture.CallOn(manager, "setStreamMute", "(IZ)V", {VmValue::Int(4),VmValue::Int(1)});
    std::array<std::int64_t, 2> pcm{};
    static_cast<void>(fixture.mixer.MixIntoAccumulator(pcm, 8000));
    CHECK(pcm[0] == 1000); CHECK(pcm[1] == 1000);
    CHECK(fixture.mixer.PositionFrames(pb) == 1);
    fixture.CallOn(manager, "setStreamMute", "(IZ)V", {VmValue::Int(4),VmValue::Int(0)});
    fixture.CallOn(manager, "setStreamVolume", "(III)V", {VmValue::Int(4),VmValue::Int(15),VmValue::Int(0)});
    fixture.CallOn(manager, "setStreamMute", "(IZ)V", {VmValue::Int(3),VmValue::Int(1)});
    pcm.fill(0);
    static_cast<void>(fixture.mixer.MixIntoAccumulator(pcm, 8000));
    CHECK(pcm[0] == 2000); CHECK(pcm[1] == 2000);
}
