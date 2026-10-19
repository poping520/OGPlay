#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "ogplay/loader/apk.h"
#include "ogplay/loader/apk_manifest.h"
#include "ogplay/loader/apk_native.h"
#include "ogplay/session/title_profile.h"

namespace ogplay::session {

struct CompatibilityProfileSelection final {
    const TitleProfile* profile{};
    std::vector<std::string> applicability_notes;
};

// Optional compatibility selection. A null profile is a successful generic
// result, including pure-Java APKs and hash mismatches.
[[nodiscard]] CompatibilityProfileSelection SelectApkCompatibilityProfile(
    const loader::AndroidManifestFacts& manifest,
    std::span<const loader::ApkNativeLibrary> libraries,
    const TitleProfileCatalog& profiles);

}  // namespace ogplay::session
