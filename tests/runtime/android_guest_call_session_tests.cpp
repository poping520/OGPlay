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
          {"initSoundPoolArray", "()V", "audio.init_sound_pool_array", true}},
         {}});
    const auto method = classes.GetMethodId(
        java_class, "destroySoundPool", "()V", true);
    REQUIRE(method.has_value());
    const auto initialize = classes.GetMethodId(
        java_class, "initSoundPoolArray", "()V", true);
    REQUIRE(initialize.has_value());
    ogplay::runtime::JniInvocationEngine invocations{classes};
    CHECK_THROWS_WITH_AS(
        static_cast<void>(invocations.InvokeStatic(
            1U, java_class, *method, {},
            ogplay::runtime::JniArgumentSource::variadic)),
        "JNI method implementation has no registered handler",
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
}
