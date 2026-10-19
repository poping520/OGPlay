#pragma once
#include "ogplay/app/profiles.h"
#include "ogplay/session/profile_apk.h"
namespace ogplay::app {
struct ProfileCatalog::Impl { session::TitleProfileCatalog catalog; explicit Impl(session::TitleProfileCatalog value) : catalog(std::move(value)) {} };
struct ProfileAccess {
    static const session::TitleProfileCatalog& Get(const ProfileCatalog& value) { return value.impl_->catalog; }
    static ProfileSummary Summary(const session::TitleProfile& profile) {
        ProfileSummary result{profile.identity.package};
        if (profile.quirks) result.quirks = profile.quirks->enabled;
        if (profile.input) result.input_template = profile.input->template_id;
        return result;
    }
};
}
