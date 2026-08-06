#include "ogplay/session/profile_session_bootstrap.h"

namespace ogplay::session {

ProfileSessionBootstrapResult BootstrapProfileSession(
    const TitleProfileCatalog& profiles, const TitleIdentity& identity,
    const TitleProfile& generic_default,
    const std::span<const ProfileVfsMountInput> vfs_inputs,
    const std::span<const ProfileJavaImplementation> java_implementations,
    const input::InputTemplateCatalog& input_templates,
    const std::span<const ProfileAudioResource> audio_resources) {
    const auto* matched = profiles.Match(identity);
    const auto selection = matched == nullptr
                               ? ProfileSessionSelection::generic_default
                               : ProfileSessionSelection::exact_profile;
    const auto& selected = matched == nullptr ? generic_default : *matched;
    return {selection,
            AssembleProfileSessionPlan(selected, vfs_inputs,
                                       java_implementations, input_templates,
                                       audio_resources)};
}

}  // namespace ogplay::session
