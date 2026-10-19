#include "ogplay/session/profile_apk.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace ogplay::session {
CompatibilityProfileSelection SelectApkCompatibilityProfile(
    const loader::AndroidManifestFacts& manifest,
    const std::span<const loader::ApkNativeLibrary> libraries,
    const TitleProfileCatalog& profiles) {
    const TitleProfile* selected{};
    std::vector<std::string> notes;
    for (const auto& profile : profiles.Profiles()) {
        const auto& identity = profile.identity;
        if (identity.package != manifest.package) continue;
        if (!identity.version_codes.empty() &&
            std::find(identity.version_codes.begin(),
                      identity.version_codes.end(),
                      manifest.version_code) == identity.version_codes.end()) {
            continue;
        }
        if (!identity.so_sha256.empty()) {
            const auto library = std::find_if(
                libraries.begin(), libraries.end(), [&](const auto& candidate) {
                    const bool hash_matches =
                        std::find(identity.so_sha256.begin(),
                                  identity.so_sha256.end(),
                                  candidate.sha256) !=
                        identity.so_sha256.end();
                    return hash_matches;
                });
            if (library == libraries.end()) continue;
        }
        if (selected != nullptr) {
            throw TitleProfileError(
                "APK matches multiple compatibility profiles");
        }
        selected = &profile;
        notes = {"schema=" + std::to_string(profile.schema),
                 identity.version_codes.empty() ? "version=any"
                                                : "version=guarded",
                 identity.so_sha256.empty() ? "hash=none"
                                            : "hash=guarded"};
    }
    if (selected == nullptr) {
        notes = {"generic startup", "no applicable compatibility profile"};
    }
    return {selected, std::move(notes)};
}

}  // namespace ogplay::session
