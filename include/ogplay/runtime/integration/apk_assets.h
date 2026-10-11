#pragma once

#include <memory>
#include <string_view>
#include "ogplay/runtime/boundary/asset_boundary.h"

namespace ogplay::runtime {
struct DexVmAndroidContext;
// The context owns an immutable APK for the lifetime of the source. The manager
// holds it weakly; open Assets retain it, without keeping Java objects alive.
[[nodiscard]] std::shared_ptr<AndroidAssetManagerSource> ApkAssets(
    const std::shared_ptr<DexVmAndroidContext>&);
struct StoredApkAssetRange final { std::uint64_t start{}, length{}; };
[[nodiscard]] StoredApkAssetRange StoredApkAsset(
    const std::shared_ptr<DexVmAndroidContext>&, std::string_view entry);
} // namespace ogplay::runtime
