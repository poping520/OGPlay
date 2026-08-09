#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/audio/java_sound_pool.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"
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

TEST_CASE("Android guest Java audio handlers own SoundPool lifecycle") {
    ogplay::runtime::JniClassRegistry classes;
    const auto java_class = classes.RegisterClass(
        {"fixture/JavaAudio", {},
         {{"destroySoundPool", "()V", "audio.destroy_sound_pool", true},
          {"initSoundPoolArray", "()V", "audio.init_sound_pool_array", true},
          {"stopAllSounds", "()V", "audio.stop_all_sounds", true},
          {"stopAllPool", "(I)V", "audio.stop_all_pool", true},
          {"stopAllBig", "(I)V", "audio.stop_all_big", true}},
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
    REQUIRE(stop_all.has_value());
    REQUIRE(stop_pool.has_value());
    REQUIRE(stop_big.has_value());
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

    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *method, {},
        ogplay::runtime::JniArgumentSource::va_list));
    CHECK(sound_pool.DestructionCount() == 1U);

    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *initialize, {},
        ogplay::runtime::JniArgumentSource::variadic));
    CHECK(sound_pool.Active());
    CHECK(sound_pool.InitializationCount() == 1U);
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *initialize, {},
        ogplay::runtime::JniArgumentSource::value_array));
    CHECK(sound_pool.InitializationCount() == 1U);

    CHECK(sound_pool.TrackVoice(
        7U, 1U, ogplay::audio::JavaSoundPoolKind::pool));
    CHECK(sound_pool.TrackVoice(
        8U, 2U, ogplay::audio::JavaSoundPoolKind::pool));
    CHECK(sound_pool.TrackVoice(
        9U, 0U, ogplay::audio::JavaSoundPoolKind::big));
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
}
