#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "ogplay/audio/open_sles_pcm_mixer.h"
#include "ogplay/core/capability_ledger.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/integration/dexvm_android.h"

namespace {

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

struct AudioTrackVm final {
    JniStringStore strings;
    JniPrimitiveArrayStore arrays;
    JavaObjectModel model{strings, arrays};
    DexClassLinker linker;
    ogplay::core::CapabilityLedger ledger;
    ogplay::audio::OpenSlesPcmMixer mixer;
    std::shared_ptr<DexVmAndroidContext> context{
        std::make_shared<DexVmAndroidContext>()};
    Interpreter vm;

    AudioTrackVm()
        : vm([this]() -> DexClassLinker& {
                 context->pcm_playback = &mixer;
                 linker.RegisterIntrinsics(CoreIntrinsicCatalog());
                 linker.RegisterIntrinsics(AndroidIntrinsicCatalog(context));
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

TEST_CASE("DVM-84 AudioTrack streams PCM through the OpenSL mixer") {
    AudioTrackVm fixture;
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
