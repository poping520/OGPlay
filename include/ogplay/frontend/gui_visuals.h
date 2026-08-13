#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "ogplay/loader/apk_manifest.h"

namespace ogplay::frontend {

[[nodiscard]] std::vector<std::uint32_t> ResizeArgbBilinear(
    std::span<const std::uint32_t> source, std::uint32_t source_width,
    std::uint32_t source_height, std::uint32_t destination_width,
    std::uint32_t destination_height);

inline constexpr std::uint32_t kLauncherIconSize = 128;

enum class ApplicationVisualFallback : std::uint8_t {
    icon_attribute_missing,
    icon_resources_unavailable,
    icon_resource_missing,
    icon_path_unsupported,
    icon_entry_unavailable,
    icon_decode_failed,
    label_attribute_missing,
    label_literal_empty,
    label_resources_unavailable,
    label_resource_missing,
};

struct ApkApplicationVisuals final {
    loader::AndroidManifestFacts manifest;
    std::string display_name;
    // Empty means that the view must render its built-in placeholder tile.
    std::vector<std::byte> icon_png;
    std::vector<ApplicationVisualFallback> fallbacks;

    [[nodiscard]] bool Used(ApplicationVisualFallback fallback) const noexcept;
};

// APK/archive/manifest damage is fatal. Resource, label and image failures are
// recorded in fallbacks and use the documented package/placeholder behavior.
[[nodiscard]] ApkApplicationVisuals ExtractApkApplicationVisuals(
    std::span<const std::byte> apk_bytes);

}  // namespace ogplay::frontend
