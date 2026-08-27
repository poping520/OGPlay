#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "ogplay/audio/open_sles_pcm_mixer.h"
#include "ogplay/core/capability_ledger.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/interpreter.h"
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

std::vector<IntrinsicClassDecl> AudioTrackTestCatalog(
    PositionListenerRecorder* recorder) {
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
    Interpreter vm;

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
                 linker.RegisterIntrinsics(AudioTrackTestCatalog(&recorder));
                 linker.Link();
                 return linker;
             }(),
             model, nullptr, ledger, {}) {
        RegisterAndroidAudioTrackStateTable(vm, context);
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
                         const std::int32_t mode) {
        const auto klass = linker.ResolveDescriptor("Landroid/media/AudioTrack;");
        const auto instance = vm.NewIntrinsicInstance("Landroid/media/AudioTrack;");
        const auto ctor = linker.FindDirectMethod(klass, "<init>", "(IIIIII)V");
        REQUIRE(ctor.has_value());
        const std::vector arguments{
            VmValue::Ref(instance), VmValue::Int(3), VmValue::Int(sample_rate),
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

    static_cast<void>(fixture.CallOn(track, "flush", "()V"));
    CHECK(fixture.CallOn(track, "getPlaybackHeadPosition", "()I").AsInt() == 0);
    CHECK_FALSE(PumpAndroidAudioTracks(fixture.vm, *fixture.context).has_value());
    CHECK(fixture.recorder.periodic.size() == 3U);
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
              {VmValue::Int(2)}).AsInt() == 0);
    CHECK(fixture.CallOn(
              track, "write", "([BII)I",
              {VmValue::Ref(pcm), VmValue::Int(0),
               VmValue::Int(buffer_size)}).AsInt() == buffer_size);
    static_cast<void>(fixture.CallOn(track, "play", "()V"));
    fixture.MixFrames(2U);
    CHECK_FALSE(PumpAndroidAudioTracks(fixture.vm, *fixture.context).has_value());
    CHECK(fixture.recorder.periodic.size() == 1U);
    CHECK(fixture.recorder.write_result == buffer_size);
    const auto player = fixture.context->audio_tracks.at(track.Value()).player;
    CHECK(fixture.mixer.QueueState(player).count == 2U);
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
    const auto listener_outcome = fixture.CallOnOutcome(
        uninitialized, "setPlaybackPositionUpdateListener",
        "(Landroid/media/AudioTrack$OnPlaybackPositionUpdateListener;)V",
        {VmValue::Ref(listener)});
    CHECK(listener_outcome.exception.IsValid());
    CHECK(fixture.linker.Class(fixture.model.ObjectClass(
              listener_outcome.exception)).descriptor ==
          "Ljava/lang/IllegalStateException;");
    CHECK(listener_outcome.exception_message ==
          "AudioTrack is not initialized");

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
    CHECK(error->find("onPeriodicNotification raised") != std::string::npos);
    CHECK(error->find("position listener failure") != std::string::npos);
}
