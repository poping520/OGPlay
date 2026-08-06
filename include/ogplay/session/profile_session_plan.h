#pragma once

#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/input/template_catalog.h"
#include "ogplay/session/lifecycle.h"
#include "ogplay/session/profile_asset_bundle.h"
#include "ogplay/session/profile_audio.h"
#include "ogplay/session/profile_java.h"
#include "ogplay/session/profile_runtime_catalog.h"
#include "ogplay/session/profile_vfs.h"

namespace ogplay::session {

class ProfileSessionPlanError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class ProfileSessionPlan final {
public:
    ProfileSessionPlan(const ProfileSessionPlan&) = delete;
    ProfileSessionPlan& operator=(const ProfileSessionPlan&) = delete;
    ProfileSessionPlan(ProfileSessionPlan&&) noexcept = default;
    ProfileSessionPlan& operator=(ProfileSessionPlan&&) = delete;

    [[nodiscard]] const TitleProfile& Profile() const noexcept;
    [[nodiscard]] const LifecycleTemplateDescription& Lifecycle() const noexcept;
    [[nodiscard]] runtime::VirtualFileSystem& Filesystem() noexcept;
    [[nodiscard]] const ProfileVfsAssembly& Vfs() const noexcept;
    [[nodiscard]] const runtime::JniClassRegistry& JavaClasses() const noexcept;
    [[nodiscard]] const runtime::JniInvocationEngine& JavaInvocations() const noexcept;
    [[nodiscard]] std::span<const ProfileJavaMethodBinding> JavaBindings() const noexcept;
    [[nodiscard]] std::string_view InputTemplate() const noexcept;
    [[nodiscard]] std::vector<hal::InputEvent> MapInput(
        std::span<const hal::InputEvent> events) const;
    [[nodiscard]] bool HasCoverMusic() const noexcept;
    [[nodiscard]] bool PlayCoverMusic(audio::MusicPlayer& player) const;

private:
    friend ProfileSessionPlan AssembleProfileSessionPlan(
        const TitleProfile&, std::span<const ProfileVfsMountInput>,
        std::span<const ProfileJavaImplementation>,
        const input::InputTemplateCatalog&,
        std::span<const ProfileAudioResource>);

    ProfileSessionPlan(TitleProfile profile,
                       LifecycleTemplateDescription lifecycle,
                       ProfileVfsAssembly vfs, ProfileJavaAssembly java,
                       input::InputTemplateCatalog input_templates,
                       std::string input_template,
                       std::optional<ProfileAudioPlan> cover_music);

    TitleProfile profile_;
    LifecycleTemplateDescription lifecycle_;
    ProfileVfsAssembly vfs_;
    ProfileJavaAssembly java_;
    input::InputTemplateCatalog input_templates_;
    std::string input_template_;
    std::optional<ProfileAudioPlan> cover_music_;
};

[[nodiscard]] ProfileSessionPlan AssembleProfileSessionPlan(
    const TitleProfile& profile,
    std::span<const ProfileVfsMountInput> vfs_inputs,
    std::span<const ProfileJavaImplementation> java_implementations,
    const input::InputTemplateCatalog& input_templates,
    std::span<const ProfileAudioResource> audio_resources);

[[nodiscard]] ProfileSessionPlan AssembleProfileSessionPlan(
    const TitleProfile& profile,
    std::span<const ProfileVfsMountInput> vfs_inputs,
    const ProfileRuntimeCatalog& runtime_catalog,
    std::span<const ProfileAudioResource> audio_resources);

[[nodiscard]] ProfileSessionPlan AssembleProfileSessionPlan(
    const TitleProfile& profile, const ProfileAssetBundle& assets,
    const ProfileRuntimeCatalog& runtime_catalog);

}  // namespace ogplay::session
