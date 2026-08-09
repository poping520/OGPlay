#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/audio/java_sound_pool.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"
#include "ogplay/runtime/framework/framework_lifecycle.h"
#include "ogplay/runtime/jni/jni_class_registry.h"
#include "ogplay/runtime/jni/jni_invocation.h"

TEST_CASE("Android guest call session validates its complete launch request") {
    ogplay::runtime::VirtualFileSystem filesystem;
    const std::array<std::byte, 4> bytes{};
    const ogplay::loader::Elf32ModuleInput module{
        "root.so", bytes, ogplay::memory::GuestAddress{0x10000000U}};

    CHECK_THROWS_WITH_AS(
        static_cast<void>(ogplay::runtime::AndroidGuestCallSession::Start(
            {19, "", std::span{&module, 1}, {}, 64, 36,
             1000, 1, &filesystem, {}})),
        "Android guest call session request is incomplete",
        ogplay::runtime::AndroidGuestCallSessionError);
    CHECK_THROWS_WITH_AS(
        static_cast<void>(ogplay::runtime::AndroidGuestCallSession::Start(
            {19, "root.so", std::span{&module, 1}, {}, 64, 36,
             1000, 1, nullptr, {}})),
        "Android guest call session request is incomplete",
        ogplay::runtime::AndroidGuestCallSessionError);
}

TEST_CASE("Android guest Java display handler records screen sleep policy") {
    ogplay::runtime::JniClassRegistry classes;
    const auto java_class = classes.RegisterClass(
        {"fixture/JavaDisplay", {},
         {{"changeDisplayMode", "(I)V", "display.change_mode", true}}, {}});
    const auto method = classes.GetMethodId(
        java_class, "changeDisplayMode", "(I)V", true);
    REQUIRE(method.has_value());
    ogplay::runtime::JniInvocationEngine invocations{classes};
    ogplay::runtime::FrameworkScreenPolicyState screen_policy;
    ogplay::runtime::BindAndroidGuestJavaDisplayHandlers(
        invocations, screen_policy);

    const std::array<ogplay::runtime::JniValue, 1> allow_sleep{
        ogplay::runtime::JniInt{1}};
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *method, allow_sleep,
        ogplay::runtime::JniArgumentSource::variadic));
    REQUIRE(screen_policy.SleepAllowed().has_value());
    CHECK(*screen_policy.SleepAllowed());

    const std::array<ogplay::runtime::JniValue, 1> keep_awake{
        ogplay::runtime::JniInt{0}};
    static_cast<void>(invocations.InvokeStatic(
        2U, java_class, *method, keep_awake,
        ogplay::runtime::JniArgumentSource::value_array));
    REQUIRE(screen_policy.SleepAllowed().has_value());
    CHECK_FALSE(*screen_policy.SleepAllowed());
    CHECK(screen_policy.RequestCount() == 2U);
}

TEST_CASE("Android guest Java audio handlers own SoundPool lifecycle") {
    ogplay::runtime::JniClassRegistry classes;
    const auto java_class = classes.RegisterClass(
        {"fixture/JavaAudio", {},
         {{"destroySoundPool", "()V", "audio.destroy_sound_pool", true},
          {"initSoundPoolArray", "()V", "audio.init_sound_pool_array", true},
          {"stopAllSounds", "()V", "audio.stop_all_sounds", true},
          {"stopAllPool", "(I)V", "audio.stop_all_pool", true},
          {"stopAllBig", "(I)V", "audio.stop_all_big", true},
          {"isSoundLoaded", "(II)I", "audio.is_sound_loaded", true},
          {"isSoundLoadedBig", "(I)I", "audio.is_sound_loaded_big", true},
          {"unloadSound", "(II)V", "audio.unload_sound", true},
          {"unloadSoundBig", "(I)V", "audio.unload_sound_big", true},
          {"playSound", "(IIF)V", "audio.play_sound", true},
          {"playSoundBig", "(IF)V", "audio.play_sound_big", true},
          {"loadSound", "(II)V", "audio.load_sound", true},
          {"loadSoundBig", "(I)V", "audio.load_sound_big", true},
          {"pauseSound", "(II)V", "audio.pause_sound", true},
          {"pauseSoundBig", "(I)V", "audio.pause_sound_big", true},
          {"resumeSound", "(II)V", "audio.resume_sound", true},
          {"resumeSoundBig", "(I)V", "audio.resume_sound_big", true},
          {"stopSound", "(II)V", "audio.stop_sound", true},
          {"stopSoundBig", "(I)V", "audio.stop_sound_big", true},
          {"setVolume", "(IIF)V", "audio.set_volume", true},
          {"setVolumeBig", "(IF)V", "audio.set_volume_big", true},
          {"setPitch", "(IIF)V", "audio.set_pitch", true},
          {"resetSound", "(I)V", "audio.reset_sound", true}},
         {}});
    const auto method = classes.GetMethodId(
        java_class, "destroySoundPool", "()V", true);
    REQUIRE(method.has_value());
    const auto initialize = classes.GetMethodId(
        java_class, "initSoundPoolArray", "()V", true);
    REQUIRE(initialize.has_value());
    const auto stop_all = classes.GetMethodId(
        java_class, "stopAllSounds", "()V", true);
    const auto stop_pool = classes.GetMethodId(
        java_class, "stopAllPool", "(I)V", true);
    const auto stop_big = classes.GetMethodId(
        java_class, "stopAllBig", "(I)V", true);
    const auto is_loaded = classes.GetMethodId(
        java_class, "isSoundLoaded", "(II)I", true);
    const auto is_loaded_big = classes.GetMethodId(
        java_class, "isSoundLoadedBig", "(I)I", true);
    const auto unload = classes.GetMethodId(
        java_class, "unloadSound", "(II)V", true);
    const auto unload_big = classes.GetMethodId(
        java_class, "unloadSoundBig", "(I)V", true);
    const auto play = classes.GetMethodId(
        java_class, "playSound", "(IIF)V", true);
    const auto play_big = classes.GetMethodId(
        java_class, "playSoundBig", "(IF)V", true);
    const auto load = classes.GetMethodId(
        java_class, "loadSound", "(II)V", true);
    const auto load_big = classes.GetMethodId(
        java_class, "loadSoundBig", "(I)V", true);
    const auto pause = classes.GetMethodId(
        java_class, "pauseSound", "(II)V", true);
    const auto pause_big = classes.GetMethodId(
        java_class, "pauseSoundBig", "(I)V", true);
    const auto resume = classes.GetMethodId(
        java_class, "resumeSound", "(II)V", true);
    const auto resume_big = classes.GetMethodId(
        java_class, "resumeSoundBig", "(I)V", true);
    const auto stop = classes.GetMethodId(
        java_class, "stopSound", "(II)V", true);
    const auto stop_big_voice = classes.GetMethodId(
        java_class, "stopSoundBig", "(I)V", true);
    const auto set_volume = classes.GetMethodId(
        java_class, "setVolume", "(IIF)V", true);
    const auto set_volume_big = classes.GetMethodId(
        java_class, "setVolumeBig", "(IF)V", true);
    const auto set_pitch = classes.GetMethodId(
        java_class, "setPitch", "(IIF)V", true);
    const auto reset = classes.GetMethodId(
        java_class, "resetSound", "(I)V", true);
    REQUIRE(stop_all.has_value());
    REQUIRE(stop_pool.has_value());
    REQUIRE(stop_big.has_value());
    REQUIRE(is_loaded.has_value());
    REQUIRE(is_loaded_big.has_value());
    REQUIRE(unload.has_value());
    REQUIRE(unload_big.has_value());
    REQUIRE(play.has_value());
    REQUIRE(play_big.has_value());
    REQUIRE(load.has_value());
    REQUIRE(load_big.has_value());
    REQUIRE(pause.has_value());
    REQUIRE(pause_big.has_value());
    REQUIRE(resume.has_value());
    REQUIRE(resume_big.has_value());
    REQUIRE(stop.has_value());
    REQUIRE(stop_big_voice.has_value());
    REQUIRE(set_volume.has_value());
    REQUIRE(set_volume_big.has_value());
    REQUIRE(set_pitch.has_value());
    REQUIRE(reset.has_value());
    ogplay::runtime::JniInvocationEngine invocations{classes};
    CHECK_THROWS_WITH_AS(
        static_cast<void>(invocations.InvokeStatic(
            1U, java_class, *method, {},
            ogplay::runtime::JniArgumentSource::variadic)),
        "JNI method implementation has no registered handler: "
        "audio.destroy_sound_pool",
        ogplay::runtime::JniInvocationError);

    ogplay::audio::JavaSoundPoolState sound_pool;
    ogplay::runtime::BindAndroidGuestJavaAudioHandlers(
        invocations, sound_pool);
    CHECK(sound_pool.Active());
    CHECK(std::holds_alternative<std::monostate>(
        invocations.InvokeStatic(
            1U, java_class, *method, {},
            ogplay::runtime::JniArgumentSource::variadic)));
    CHECK_FALSE(sound_pool.Active());
    CHECK(sound_pool.DestructionCount() == 1U);
    CHECK_FALSE(sound_pool.MarkLoaded(
        ogplay::audio::JavaSoundPoolKind::pool, 2));
    CHECK(sound_pool.LoadedResourceCount() == 0U);

    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *method, {},
        ogplay::runtime::JniArgumentSource::va_list));
    CHECK(sound_pool.DestructionCount() == 1U);

    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *initialize, {},
        ogplay::runtime::JniArgumentSource::variadic));
    CHECK(sound_pool.Active());
    CHECK(sound_pool.InitializationCount() == 1U);

    const std::array<ogplay::runtime::JniValue, 2> pool_resource{
        ogplay::runtime::JniInt{2}, ogplay::runtime::JniInt{7}};
    const std::array<ogplay::runtime::JniValue, 1> big_resource{
        ogplay::runtime::JniInt{7}};
    const std::array<ogplay::runtime::JniValue, 3> pool_play{
        ogplay::runtime::JniInt{2}, ogplay::runtime::JniInt{7},
        ogplay::runtime::JniFloat{0.5F}};
    const std::array<ogplay::runtime::JniValue, 2> big_play{
        ogplay::runtime::JniInt{7}, ogplay::runtime::JniFloat{0.75F}};
    CHECK(std::get<ogplay::runtime::JniInt>(invocations.InvokeStatic(
              1U, java_class, *is_loaded, pool_resource,
              ogplay::runtime::JniArgumentSource::value_array)) == -1);
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *play, pool_play,
        ogplay::runtime::JniArgumentSource::variadic));
    CHECK(sound_pool.ActiveVoiceCount() == 0U);
    CHECK(sound_pool.PendingLoadCount() == 1U);
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *load, pool_resource,
        ogplay::runtime::JniArgumentSource::value_array));
    CHECK(sound_pool.PendingLoadCount() == 1U);
    CHECK(sound_pool.RequestLoad(
        ogplay::audio::JavaSoundPoolKind::pool, 99));
    CHECK(sound_pool.PendingLoadCount() == 2U);
    CHECK(sound_pool.Unload(
        ogplay::audio::JavaSoundPoolKind::pool, 99));
    CHECK(sound_pool.PendingLoadCount() == 1U);
    CHECK(sound_pool.MarkLoaded(
        ogplay::audio::JavaSoundPoolKind::pool, 2));
    CHECK(sound_pool.PendingLoadCount() == 0U);
    CHECK(std::get<ogplay::runtime::JniInt>(invocations.InvokeStatic(
              1U, java_class, *is_loaded, pool_resource,
              ogplay::runtime::JniArgumentSource::variadic)) == 0);
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *play, pool_play,
        ogplay::runtime::JniArgumentSource::va_list));
    CHECK(sound_pool.ActiveVoiceCount() == 1U);
    CHECK(std::get<ogplay::runtime::JniInt>(invocations.InvokeStatic(
              1U, java_class, *is_loaded_big, big_resource,
              ogplay::runtime::JniArgumentSource::va_list)) == -1);
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *load_big, big_resource,
        ogplay::runtime::JniArgumentSource::variadic));
    CHECK(sound_pool.PendingLoadCount() == 1U);
    CHECK(sound_pool.MarkLoaded(
        ogplay::audio::JavaSoundPoolKind::big, 7));
    CHECK(sound_pool.PendingLoadCount() == 0U);
    CHECK(std::get<ogplay::runtime::JniInt>(invocations.InvokeStatic(
              1U, java_class, *is_loaded_big, big_resource,
              ogplay::runtime::JniArgumentSource::value_array)) == 0);
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *play_big, big_play,
        ogplay::runtime::JniArgumentSource::value_array));
    CHECK(sound_pool.ActiveVoiceCount() == 2U);
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *pause, pool_resource,
        ogplay::runtime::JniArgumentSource::variadic));
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *pause_big, big_resource,
        ogplay::runtime::JniArgumentSource::va_list));
    const auto paused_pool = sound_pool.Snapshot(
        ogplay::audio::JavaSoundPoolKind::pool, 2, 7);
    const auto paused_big = sound_pool.Snapshot(
        ogplay::audio::JavaSoundPoolKind::big, 7, 0);
    REQUIRE(paused_pool.has_value());
    REQUIRE(paused_big.has_value());
    CHECK(paused_pool->status ==
          ogplay::audio::JavaSoundPoolVoiceStatus::paused);
    CHECK(paused_big->status ==
          ogplay::audio::JavaSoundPoolVoiceStatus::paused);
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *resume, pool_resource,
        ogplay::runtime::JniArgumentSource::value_array));
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *resume_big, big_resource,
        ogplay::runtime::JniArgumentSource::variadic));
    const std::array<ogplay::runtime::JniValue, 3> pool_properties{
        ogplay::runtime::JniInt{2}, ogplay::runtime::JniInt{7},
        ogplay::runtime::JniFloat{1.5F}};
    const std::array<ogplay::runtime::JniValue, 2> big_volume{
        ogplay::runtime::JniInt{7}, ogplay::runtime::JniFloat{0.5F}};
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *set_volume, pool_play,
        ogplay::runtime::JniArgumentSource::va_list));
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *set_volume_big, big_volume,
        ogplay::runtime::JniArgumentSource::value_array));
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *set_pitch, pool_properties,
        ogplay::runtime::JniArgumentSource::variadic));
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *reset, big_resource,
        ogplay::runtime::JniArgumentSource::va_list));
    const auto pool_snapshot = sound_pool.Snapshot(
        ogplay::audio::JavaSoundPoolKind::pool, 2, 7);
    const auto big_snapshot = sound_pool.Snapshot(
        ogplay::audio::JavaSoundPoolKind::big, 7, 0);
    REQUIRE(pool_snapshot.has_value());
    REQUIRE(big_snapshot.has_value());
    CHECK(pool_snapshot->status ==
          ogplay::audio::JavaSoundPoolVoiceStatus::playing);
    CHECK(pool_snapshot->volume == 0.5F);
    CHECK(pool_snapshot->pitch == 1.5F);
    CHECK(big_snapshot->status ==
          ogplay::audio::JavaSoundPoolVoiceStatus::playing);
    CHECK(big_snapshot->volume == 0.5F);
    CHECK(big_snapshot->reset_count == 1U);
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *stop, pool_resource,
        ogplay::runtime::JniArgumentSource::value_array));
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *stop_big_voice, big_resource,
        ogplay::runtime::JniArgumentSource::variadic));
    CHECK(sound_pool.ActiveVoiceCount() == 0U);
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *play_big, big_play,
        ogplay::runtime::JniArgumentSource::value_array));
    CHECK(sound_pool.ActiveVoiceCount() == 1U);
    CHECK(sound_pool.LoadedResourceCount() == 2U);
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *unload, pool_resource,
        ogplay::runtime::JniArgumentSource::variadic));
    CHECK(std::get<ogplay::runtime::JniInt>(invocations.InvokeStatic(
              1U, java_class, *is_loaded, pool_resource,
              ogplay::runtime::JniArgumentSource::va_list)) == -1);
    CHECK(sound_pool.ActiveVoiceCount() == 1U);
    CHECK(sound_pool.LoadedResourceCount() == 1U);
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *unload_big, big_resource,
        ogplay::runtime::JniArgumentSource::value_array));
    CHECK(sound_pool.LoadedResourceCount() == 0U);
    CHECK(sound_pool.ActiveVoiceCount() == 0U);
    CHECK(sound_pool.MarkLoaded(
        ogplay::audio::JavaSoundPoolKind::big, 7));
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *initialize, {},
        ogplay::runtime::JniArgumentSource::value_array));
    CHECK(sound_pool.InitializationCount() == 1U);

    CHECK(sound_pool.MarkLoaded(
        ogplay::audio::JavaSoundPoolKind::pool, 7));
    CHECK(sound_pool.MarkLoaded(
        ogplay::audio::JavaSoundPoolKind::pool, 8));
    CHECK(sound_pool.MarkLoaded(
        ogplay::audio::JavaSoundPoolKind::big, 9));
    CHECK(sound_pool.Play(
        ogplay::audio::JavaSoundPoolKind::pool, 7, 1, 1.0F));
    CHECK(sound_pool.Play(
        ogplay::audio::JavaSoundPoolKind::pool, 8, 2, 0.5F));
    CHECK(sound_pool.Play(
        ogplay::audio::JavaSoundPoolKind::big, 9, 0, 0.25F));
    CHECK(sound_pool.Play(
        ogplay::audio::JavaSoundPoolKind::pool, 7, 1, 0.25F));
    CHECK_FALSE(sound_pool.Play(
        ogplay::audio::JavaSoundPoolKind::pool, 7, 3, 1.25F));
    CHECK(sound_pool.ActiveVoiceCount() == 3U);
    const std::array<ogplay::runtime::JniValue, 1> keep_seven{
        ogplay::runtime::JniInt{7}};
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *stop_pool, keep_seven,
        ogplay::runtime::JniArgumentSource::variadic));
    CHECK(sound_pool.ActiveVoiceCount() == 2U);

    const std::array<ogplay::runtime::JniValue, 1> stop_every_big{
        ogplay::runtime::JniInt{-1}};
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *stop_big, stop_every_big,
        ogplay::runtime::JniArgumentSource::va_list));
    CHECK(sound_pool.ActiveVoiceCount() == 1U);
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *stop_all, {},
        ogplay::runtime::JniArgumentSource::value_array));
    CHECK(sound_pool.ActiveVoiceCount() == 0U);
    CHECK(sound_pool.LoadedResourceCount() == 4U);
    sound_pool.Destroy();
    CHECK(sound_pool.LoadedResourceCount() == 0U);
    CHECK(sound_pool.PendingLoadCount() == 0U);
    CHECK(std::get<ogplay::runtime::JniInt>(invocations.InvokeStatic(
              1U, java_class, *is_loaded_big, big_resource,
              ogplay::runtime::JniArgumentSource::variadic)) == -1);
}
