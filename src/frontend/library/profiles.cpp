#include "profiles_private.h"
#include "ogplay/app/host.h"
#include "ogplay/loader/apk.h"
#include "ogplay/loader/apk_native.h"

namespace ogplay::app {
ProfileCatalog::ProfileCatalog() : impl_(std::make_shared<Impl>(session::TitleProfileCatalog({}))) {}

ProfileCatalog ProfileCatalog::LoadBundled() {
    ProfileCatalog result;
    result.impl_ = std::make_shared<Impl>(
        session::TitleProfileCatalog::LoadOptionalDirectory(BundledDataPaths().profiles_directory));
    return result;
}

ProfileCatalog ProfileCatalog::Load(const std::filesystem::path& directory) {
    ProfileCatalog result;
    result.impl_ = std::make_shared<Impl>(session::TitleProfileCatalog::LoadDirectory(directory));
    return result;
}

std::optional<ProfileSummary> ProfileCatalog::Find(std::string_view id) const {
    for (const auto& profile : impl_->catalog.Profiles())
        if (profile.identity.package == id) return ProfileAccess::Summary(profile);
    return {};
}

std::optional<ProfileSummary> ProfileCatalog::Match(std::span<const std::byte> apk) const {
    const auto archive = loader::ParseApkArchive(apk);
    const auto selected = session::SelectApkCompatibilityProfile(
        loader::ReadAndroidManifest(apk, archive), loader::ReadApkArmNativeLibraries(apk, archive), impl_->catalog);
    if (selected.profile) return ProfileAccess::Summary(*selected.profile);
    return {};
}
}  // namespace ogplay::app
