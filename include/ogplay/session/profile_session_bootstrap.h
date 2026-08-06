#pragma once

#include <cstdint>
#include <span>

#include "ogplay/session/profile_session_plan.h"

namespace ogplay::session {

enum class ProfileSessionSelection : std::uint8_t {
    exact_profile,
    generic_default,
};

struct ProfileSessionBootstrapResult final {
    ProfileSessionSelection selection{ProfileSessionSelection::generic_default};
    ProfileSessionPlan plan;
};

[[nodiscard]] ProfileSessionBootstrapResult BootstrapProfileSession(
    const TitleProfileCatalog& profiles, const TitleIdentity& identity,
    const TitleProfile& generic_default,
    std::span<const ProfileVfsMountInput> vfs_inputs,
    std::span<const ProfileJavaImplementation> java_implementations,
    const input::InputTemplateCatalog& input_templates,
    std::span<const ProfileAudioResource> audio_resources);

[[nodiscard]] ProfileSessionBootstrapResult BootstrapProfileSession(
    const TitleProfileCatalog& profiles, const TitleIdentity& identity,
    const TitleProfile& generic_default,
    std::span<const ProfileVfsMountInput> vfs_inputs,
    const ProfileRuntimeCatalog& runtime_catalog,
    std::span<const ProfileAudioResource> audio_resources);

}  // namespace ogplay::session
