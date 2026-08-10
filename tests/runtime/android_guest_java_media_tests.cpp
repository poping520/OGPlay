#include <array>
#include <cstddef>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/runtime/integration/android_guest_call_session.h"
#include "ogplay/runtime/jni/jni_array.h"
#include "ogplay/runtime/jni/jni_class_registry.h"
#include "ogplay/runtime/jni/jni_environment.h"
#include "ogplay/runtime/jni/jni_invocation.h"
#include "ogplay/runtime/jni/jni_object.h"

TEST_CASE("Android guest media handlers expose resources and exact Java compatibility state") {
    using namespace ogplay::runtime;
    JniClassRegistry classes;
    const auto resources = classes.RegisterClass(
        {"fixture/Resources", {},
         {{"getSoundRaw", "(I)[B", "resource.sound_full", true},
          {"getResourceLengthSoundRaw", "(I)I", "resource.sound_length", true}},
         {}});
    const auto media = classes.RegisterClass(
        {"fixture/Media", {},
         {{"loadMusic", "(I)V", "audio.java_noop", true},
          {"isMusicLoaded", "(I)I", "audio.java_true", true},
          {"playMusic", "(IFI)I", "audio.java_unavailable", true},
          {"isMediaPlaying", "(I)Z", "audio.java_false", true},
          {"setMasterVolume", "(F)V", "audio.master_volume.set", true},
          {"getMasterVolume", "()F", "audio.master_volume.get", true},
          {"setVolumeMusic", "(FI)V", "audio.music_volume.set", true},
          {"getVolumeMusic", "(I)F", "audio.music_volume.get", true}},
         {}});
    const auto audio_track = classes.RegisterClass(
        {"android/media/AudioTrack", {},
         {{"<init>", "(IIIIII)V", "audio.track.construct", false},
          {"getMinBufferSize", "(III)I", "audio.track.minimum_buffer", true},
          {"play", "()V", "audio.track.play", false},
          {"pause", "()V", "audio.track.pause", false},
          {"stop", "()V", "audio.track.stop", false},
          {"release", "()V", "audio.track.release", false},
          {"write", "([BII)I", "audio.track.write", false}},
         {}});
    JniInvocationEngine invocations{classes};
    JniEnvironment environment;
    environment.AttachThread(9U);
    JniStringStore strings;
    JniPrimitiveArrayStore arrays;
    AndroidGuestMovieState movie_state;
    AndroidGuestLegacyMediaState media_state;
    BindAndroidGuestJavaMediaHandlers(
        invocations, environment, strings, arrays, movie_state, media_state,
        [](const std::int32_t resource) {
            if (resource != 7) return std::vector<std::byte>{};
            return std::vector<std::byte>{std::byte{0x4f}, std::byte{0x67},
                                          std::byte{0x67}};
        });

    const auto invoke = [&](const JniObjectIdentity java_class,
                            const char* name, const char* signature,
                            const std::span<const JniValue> arguments = {}) {
        const auto method = classes.GetMethodId(
            java_class, name, signature, true);
        REQUIRE(method.has_value());
        return invocations.InvokeStatic(
            9U, java_class, *method, arguments,
            JniArgumentSource::value_array);
    };
    const std::array<JniValue, 1> resource{JniInt{7}};
    CHECK(std::get<JniInt>(invoke(
              resources, "getResourceLengthSoundRaw", "(I)I", resource)) == 3);
    const auto bytes_reference = std::get<JniReference>(
        invoke(resources, "getSoundRaw", "(I)[B", resource));
    const auto bytes = environment.ResolveObjectForHle(9U, bytes_reference);
    REQUIRE(bytes.has_value());
    CHECK(std::get<std::vector<JniByte>>(
              arrays.Region(*bytes, 0, arrays.Length(*bytes))) ==
          std::vector<JniByte>{0x4f, 0x67, 0x67});
    const std::array<JniValue, 1> missing{JniInt{8}};
    CHECK_THROWS_WITH_AS(
        static_cast<void>(invoke(
            resources, "getSoundRaw", "(I)[B", missing)),
        "Android guest numbered sound resource is missing or oversized",
        AndroidGuestCallSessionError);

    static_cast<void>(invoke(media, "loadMusic", "(I)V", resource));
    CHECK(media_state.CallbackCount("loadMusic") == 1U);
    CHECK(std::get<JniInt>(invoke(
              media, "isMusicLoaded", "(I)I", resource)) == 1);
    const std::array<JniValue, 3> play{
        JniInt{7}, JniFloat{0.5F}, JniInt{1}};
    CHECK(std::get<JniInt>(invoke(
              media, "playMusic", "(IFI)I", play)) == -1);
    CHECK(std::get<JniBoolean>(invoke(
              media, "isMediaPlaying", "(I)Z", resource)) == 0U);

    const std::array<JniValue, 1> master{JniFloat{0.25F}};
    static_cast<void>(invoke(
        media, "setMasterVolume", "(F)V", master));
    CHECK(std::get<JniFloat>(invoke(
              media, "getMasterVolume", "()F")) == doctest::Approx(0.25F));
    const std::array<JniValue, 2> music_volume{
        JniFloat{0.75F}, JniInt{7}};
    static_cast<void>(invoke(
        media, "setVolumeMusic", "(FI)V", music_volume));
    CHECK(std::get<JniFloat>(invoke(
              media, "getVolumeMusic", "(I)F", resource)) ==
          doctest::Approx(0.75F));

    const std::array<JniValue, 3> format{
        JniInt{44100}, JniInt{12}, JniInt{2}};
    const auto minimum = std::get<JniInt>(invoke(
        audio_track, "getMinBufferSize", "(III)I", format));
    CHECK(minimum == 35280);
    const auto track_identity = AllocateJniHostObjectIdentity();
    const auto track_reference = environment.PublishLocalObject(9U, track_identity);
    const auto constructor = classes.GetMethodId(
        audio_track, "<init>", "(IIIIII)V", false);
    REQUIRE(constructor.has_value());
    const std::array<JniValue, 6> configuration{
        JniInt{3}, JniInt{44100}, JniInt{12}, JniInt{2},
        JniInt{minimum}, JniInt{1}};
    static_cast<void>(invocations.InvokeNonvirtual(
        9U, track_reference, audio_track, audio_track, *constructor,
        configuration, JniArgumentSource::value_array));

    const auto bytes_identity = arrays.New(JniPrimitiveKind::byte, 8);
    arrays.SetRegion(bytes_identity, 0, JniPrimitiveArrayData{
        std::vector<JniByte>{0, 1, 2, 3, 4, 5, 6, 7}});
    const auto bytes_ref = environment.PublishLocalObject(9U, bytes_identity);
    const auto write = classes.GetMethodId(
        audio_track, "write", "([BII)I", false);
    REQUIRE(write.has_value());
    const std::array<JniValue, 3> write_arguments{
        bytes_ref, JniInt{2}, JniInt{4}};
    CHECK(std::get<JniInt>(invocations.InvokeVirtual(
              9U, track_reference, audio_track, *write, write_arguments,
              JniArgumentSource::value_array)) == 4);
    CHECK(media_state.AudioTrack(track_identity).bytes_written == 4U);

    const auto play_track = classes.GetMethodId(
        audio_track, "play", "()V", false);
    const auto pause = classes.GetMethodId(audio_track, "pause", "()V", false);
    const auto stop = classes.GetMethodId(audio_track, "stop", "()V", false);
    const auto release = classes.GetMethodId(
        audio_track, "release", "()V", false);
    REQUIRE(play_track.has_value());
    REQUIRE(pause.has_value());
    REQUIRE(stop.has_value());
    REQUIRE(release.has_value());
    static_cast<void>(invocations.InvokeVirtual(
        9U, track_reference, audio_track, *play_track, {},
        JniArgumentSource::value_array));
    CHECK(media_state.AudioTrack(track_identity).playing);
    static_cast<void>(invocations.InvokeVirtual(
        9U, track_reference, audio_track, *pause, {},
        JniArgumentSource::value_array));
    CHECK(media_state.AudioTrack(track_identity).paused);
    static_cast<void>(invocations.InvokeVirtual(
        9U, track_reference, audio_track, *stop, {},
        JniArgumentSource::value_array));
    CHECK_FALSE(media_state.AudioTrack(track_identity).playing);
    CHECK_FALSE(media_state.AudioTrack(track_identity).paused);
    static_cast<void>(invocations.InvokeVirtual(
        9U, track_reference, audio_track, *release, {},
        JniArgumentSource::value_array));
    CHECK(media_state.AudioTrack(track_identity).released);
    CHECK_THROWS_AS(
        static_cast<void>(invocations.InvokeVirtual(
            9U, track_reference, audio_track, *write, write_arguments,
            JniArgumentSource::value_array)),
        AndroidGuestCallSessionError);
    environment.DetachThread(9U);
}
