#include <cstddef>
#include <string>
#include <type_traits>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/session/profile_session_plan.h"

namespace {

static_assert(!std::is_copy_constructible_v<ogplay::session::ProfileSessionPlan>);
static_assert(std::is_move_constructible_v<ogplay::session::ProfileSessionPlan>);
static_assert(!std::is_move_assignable_v<ogplay::session::ProfileSessionPlan>);

class RecordingPlayer final : public ogplay::audio::MusicPlayer {
public:
    void Play(const ogplay::audio::MusicPlaybackRequest& request) override {
        loop = request.loop;
        bytes.assign(request.encoded.begin(), request.encoded.end());
    }

    bool loop{};
    std::vector<std::byte> bytes;
};

[[nodiscard]] ogplay::session::TitleProfile CompleteProfile() {
    ogplay::session::TitleProfile profile;
    profile.identity.package = "fixture.session";
    profile.runtime.lifecycle = ogplay::session::ProfileLifecycle::custom_jni;
    profile.runtime.surface = {1280, 720};
    profile.data = ogplay::session::ProfileData{
        .mounts = {{"/assets", ogplay::session::ProfileSource::apk, true}},
        .working_directory = "/assets",
        .manifest = {{"config.bin", true}},
    };
    profile.java_classes = {
        {"fixture/SessionBridge", {{"ready", "()V", "session.ready"}}}};
    profile.input = ogplay::session::ProfileInput{"pointer_only"};
    profile.audio = ogplay::session::ProfileAudio{
        ogplay::session::ProfileCoverMusic{
            ogplay::session::ProfileSource::apk, "res/raw/music.ogg", true}};
    return profile;
}

[[nodiscard]] std::vector<ogplay::session::ProfileVfsMountInput> VfsInputs() {
    return {{"/assets", ogplay::session::ProfileSource::apk,
             {{"config.bin", {std::byte{1}}}}}};
}

[[nodiscard]] std::vector<ogplay::session::ProfileJavaImplementation>
JavaImplementations() {
    return {{"session.ready", [](const ogplay::runtime::JniInvocation&) {
                 return ogplay::runtime::JniValue{std::monostate{}};
             }}};
}

[[nodiscard]] ogplay::input::InputTemplateCatalog InputTemplates() {
    return {"direct",
            {{"direct",
              [](const std::span<const ogplay::hal::InputEvent> events) {
                  return std::vector<ogplay::hal::InputEvent>(events.begin(),
                                                              events.end());
              }},
             {"pointer_only",
              [](const std::span<const ogplay::hal::InputEvent> events) {
                  std::vector<ogplay::hal::InputEvent> result;
                  for (const auto& event : events) {
                      if (event.type ==
                          ogplay::hal::InputEventType::pointer_motion) {
                          result.push_back(event);
                      }
                  }
                  return result;
              }}}};
}

[[nodiscard]] std::vector<ogplay::session::ProfileAudioResource>
AudioResources() {
    return {{ogplay::session::ProfileSource::apk, "res/raw/music.ogg",
             {std::byte{0x4F}, std::byte{0x67}, std::byte{0x67}}}};
}

}  // namespace

TEST_CASE("Profile session plan owns every generic assembly") {
    const auto profile = CompleteProfile();
    const auto vfs_inputs = VfsInputs();
    const auto java = JavaImplementations();
    const auto inputs = InputTemplates();
    const ogplay::session::ProfileRuntimeCatalog runtime{java, inputs};
    auto audio = AudioResources();
    const auto expected_music = audio.front().contents;
    auto plan = ogplay::session::AssembleProfileSessionPlan(
        profile, vfs_inputs, runtime, audio);
    audio.front().contents.clear();

    CHECK(plan.Profile().identity.package == "fixture.session");
    CHECK(plan.Lifecycle().render ==
          ogplay::session::LifecycleCallbackRoute::custom_jni);
    CHECK(plan.Filesystem().Stat("/assets/config.bin").size == 1);
    REQUIRE(plan.JavaBindings().size() == 1);
    CHECK(plan.JavaBindings().front().implementation == "session.ready");
    CHECK(std::holds_alternative<std::monostate>(
        plan.JavaInvocations().InvokeVirtual(
            1, ogplay::runtime::JniReference{7},
            plan.JavaBindings().front().class_identity,
            plan.JavaBindings().front().method_id, {},
            ogplay::runtime::JniArgumentSource::value_array)));
    CHECK(plan.InputTemplate() == "pointer_only");
    const std::vector events{
        ogplay::hal::InputEvent{.type = ogplay::hal::InputEventType::key},
        ogplay::hal::InputEvent{
            .type = ogplay::hal::InputEventType::pointer_motion}};
    CHECK(plan.MapInput(events).size() == 1);
    CHECK(plan.HasCoverMusic());
    RecordingPlayer player;
    CHECK(plan.PlayCoverMusic(player));
    CHECK(player.loop);
    CHECK(player.bytes == expected_music);
}

TEST_CASE("Profile session plan rejects every unresolved subassembly") {
    const auto vfs_inputs = VfsInputs();
    const auto java = JavaImplementations();
    const auto inputs = InputTemplates();
    const auto audio = AudioResources();

    auto profile = CompleteProfile();
    profile.runtime.lifecycle = static_cast<ogplay::session::ProfileLifecycle>(99);
    CHECK_THROWS_AS(static_cast<void>(
                        ogplay::session::AssembleProfileSessionPlan(
                            profile, vfs_inputs, java, inputs, audio)),
                    ogplay::session::LifecycleSequenceError);

    profile = CompleteProfile();
    CHECK_THROWS_AS(static_cast<void>(
                        ogplay::session::AssembleProfileSessionPlan(
                            profile, {}, java, inputs, audio)),
                    ogplay::session::ProfileVfsError);

    CHECK_THROWS_AS(static_cast<void>(
                        ogplay::session::AssembleProfileSessionPlan(
                            CompleteProfile(), vfs_inputs, {}, inputs, audio)),
                    ogplay::session::ProfileJavaError);

    profile = CompleteProfile();
    profile.input->profile = "missing";
    CHECK_THROWS_WITH_AS(
        static_cast<void>(ogplay::session::AssembleProfileSessionPlan(
            profile, vfs_inputs, java, inputs, audio)),
        "Profile input template is not registered: missing",
        ogplay::session::ProfileSessionPlanError);

    CHECK_THROWS_AS(static_cast<void>(
                        ogplay::session::AssembleProfileSessionPlan(
                            CompleteProfile(), vfs_inputs, java, inputs, {})),
                    ogplay::session::ProfileAudioError);
}

TEST_CASE("Profile session plan keeps explicit generic defaults") {
    ogplay::session::TitleProfile profile;
    const auto inputs = InputTemplates();
    auto plan = ogplay::session::AssembleProfileSessionPlan(
        profile, {}, {}, inputs, {});

    CHECK(plan.Lifecycle().lifecycle ==
          ogplay::session::ProfileLifecycle::native_activity);
    CHECK(plan.InputTemplate() == "direct");
    CHECK_FALSE(plan.HasCoverMusic());
    RecordingPlayer player;
    CHECK_FALSE(plan.PlayCoverMusic(player));
}
