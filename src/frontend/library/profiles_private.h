#pragma once
#include "ogplay/app/profiles.h"
#include "ogplay/session/profile_apk.h"
namespace ogplay::app {
struct ProfileCatalog::Impl { session::TitleProfileCatalog catalog; explicit Impl(session::TitleProfileCatalog value) : catalog(std::move(value)) {} };
struct ProfileAccess {
    static const session::TitleProfileCatalog& Get(const ProfileCatalog& value) { return value.impl_->catalog; }
    static ProfileSummary Summary(const session::TitleProfile& profile) {
        const auto summary = session::SummarizeCompatibilityProfile(profile);
        ProfileSummary result{summary.profile_id, summary.display_name, summary.requires_external_data};
        if (profile.quirks) result.quirks = profile.quirks->enabled;
        if (profile.input) result.input_template = profile.input->template_id;
        return result;
    }
};
}
