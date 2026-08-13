#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>

#include "ogplay/loader/apk.h"

namespace ogplay::loader {

using AndroidManifestLabel = std::variant<std::uint32_t, std::string>;

struct AndroidManifestFacts final {
    std::string package;
    std::uint32_t version_code{};
    std::optional<std::string> version_name;
    std::optional<std::uint32_t> min_sdk;
    std::optional<std::uint32_t> target_sdk;
    std::optional<std::uint32_t> application_icon;
    std::optional<AndroidManifestLabel> application_label;
    // Fully-qualified class of the first activity whose intent filter
    // declares action MAIN + category LAUNCHER (dex_activity entry
    // discovery, docs/design/dexvm/04-integration.md §2).
    std::optional<std::string> launcher_activity;
};

[[nodiscard]] AndroidManifestFacts ParseAndroidBinaryManifest(
    std::span<const std::byte> bytes);
[[nodiscard]] AndroidManifestFacts ReadAndroidManifest(
    std::span<const std::byte> apk_bytes, const ApkArchive& archive);

}  // namespace ogplay::loader
