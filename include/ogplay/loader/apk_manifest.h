#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "ogplay/loader/apk.h"

namespace ogplay::loader {

using AndroidManifestLabel = std::variant<std::uint32_t, std::string>;

enum class AndroidManifestComponentKind : std::uint8_t {
    activity,
    activity_alias,
};

struct AndroidManifestIntentFilter final {
    std::vector<std::string> actions;
    std::vector<std::string> categories;
};

struct AndroidManifestActivityComponent final {
    AndroidManifestComponentKind kind{AndroidManifestComponentKind::activity};
    std::string name;
    std::optional<std::string> target_activity;
    bool enabled{true};
    std::vector<AndroidManifestIntentFilter> intent_filters;
};

struct AndroidManifestLauncherComponent final {
    std::string component_name;
    std::string activity_class;
    bool via_alias{};
};

enum class AndroidManifestStartupErrorReason : std::uint8_t {
    invalid_class_name,
    missing_component_name,
    invalid_enabled,
    missing_alias_target,
    alias_target_not_found,
    duplicate_component,
    no_launcher,
};

class AndroidManifestStartupError final : public std::runtime_error {
public:
    AndroidManifestStartupError(AndroidManifestStartupErrorReason reason,
                                std::string message);
    [[nodiscard]] AndroidManifestStartupErrorReason Reason() const noexcept;

private:
    AndroidManifestStartupErrorReason reason_;
};

struct AndroidManifestFacts final {
    std::string package;
    std::uint32_t version_code{};
    std::optional<std::string> version_name;
    std::optional<std::uint32_t> min_sdk;
    std::optional<std::uint32_t> target_sdk;
    std::optional<std::uint32_t> application_icon;
    std::optional<AndroidManifestLabel> application_label;
    // Compatibility projection of the resolved launch target class. New
    // startup code should use ResolveLauncherComponent to retain alias identity.
    std::optional<std::string> launcher_activity;
    // Normalized process Application class. The framework default is published
    // explicitly so startup callers do not need to reconstruct Manifest rules.
    std::string application_class{"android.app.Application"};
    // Activity and activity-alias declarations in Manifest document order.
    std::vector<AndroidManifestActivityComponent> activity_components;
};

[[nodiscard]] std::string NormalizeAndroidManifestClassName(
    std::string_view package, std::string_view class_name);
[[nodiscard]] AndroidManifestLauncherComponent ResolveLauncherComponent(
    const AndroidManifestFacts& facts);

[[nodiscard]] AndroidManifestFacts ParseAndroidBinaryManifest(
    std::span<const std::byte> bytes);
[[nodiscard]] AndroidManifestFacts ReadAndroidManifest(
    std::span<const std::byte> apk_bytes, const ApkArchive& archive);

}  // namespace ogplay::loader
