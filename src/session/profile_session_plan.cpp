#include "ogplay/session/profile_session_plan.h"

#include <utility>

namespace ogplay::session {

ProfileSessionPlan::ProfileSessionPlan(
    TitleProfile profile, LifecycleTemplateDescription lifecycle,
    ProfileVfsAssembly vfs, ProfileJavaAssembly java,
    input::InputTemplateCatalog input_templates, std::string input_template,
    std::optional<ProfileAudioPlan> cover_music)
    : profile_(std::move(profile)), lifecycle_(lifecycle), vfs_(std::move(vfs)),
      java_(std::move(java)), input_templates_(std::move(input_templates)),
      input_template_(std::move(input_template)),
      cover_music_(std::move(cover_music)) {}

const TitleProfile& ProfileSessionPlan::Profile() const noexcept {
    return profile_;
}

const LifecycleTemplateDescription& ProfileSessionPlan::Lifecycle() const noexcept {
    return lifecycle_;
}

runtime::VirtualFileSystem& ProfileSessionPlan::Filesystem() noexcept {
    return *vfs_.filesystem;
}

const ProfileVfsAssembly& ProfileSessionPlan::Vfs() const noexcept {
    return vfs_;
}

const runtime::JniClassRegistry& ProfileSessionPlan::JavaClasses() const noexcept {
    return *java_.classes;
}

const runtime::JniInvocationEngine&
ProfileSessionPlan::JavaInvocations() const noexcept {
    return *java_.invocations;
}

std::span<const ProfileJavaMethodBinding>
ProfileSessionPlan::JavaBindings() const noexcept {
    return java_.bindings;
}

std::string_view ProfileSessionPlan::InputTemplate() const noexcept {
    return input_template_;
}

std::vector<hal::InputEvent> ProfileSessionPlan::MapInput(
    const std::span<const hal::InputEvent> events) const {
    return input_templates_.Map(input_template_, events);
}

bool ProfileSessionPlan::HasCoverMusic() const noexcept {
    return cover_music_.has_value();
}

bool ProfileSessionPlan::PlayCoverMusic(audio::MusicPlayer& player) const {
    if (!cover_music_.has_value()) return false;
    player.Play({cover_music_->encoded, cover_music_->loop});
    return true;
}

ProfileSessionPlan AssembleProfileSessionPlan(
    const TitleProfile& profile,
    const std::span<const ProfileVfsMountInput> vfs_inputs,
    const std::span<const ProfileJavaImplementation> java_implementations,
    const input::InputTemplateCatalog& input_templates,
    const std::span<const ProfileAudioResource> audio_resources) {
    const auto lifecycle = DescribeLifecycle(profile.runtime.lifecycle);
    auto vfs = AssembleProfileVfs(profile, vfs_inputs);
    auto java = AssembleProfileJava(profile, java_implementations);
    const auto input_template = profile.input.has_value()
                                    ? profile.input->profile
                                    : std::string(input_templates.DefaultTemplate());
    if (!input_templates.Contains(input_template)) {
        throw ProfileSessionPlanError(
            "Profile input template is not registered: " + input_template);
    }
    auto cover_music = ResolveProfileAudio(profile, audio_resources);
    return ProfileSessionPlan{profile, lifecycle, std::move(vfs),
                              std::move(java), input_templates, input_template,
                              std::move(cover_music)};
}

}  // namespace ogplay::session
