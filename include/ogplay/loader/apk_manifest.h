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
struct AndroidManifestMetaDataValueReference final {
    std::uint32_t resource_id{};
};

struct AndroidManifestMetaDataResourceReference final {
    std::uint32_t resource_id{};
};

using AndroidManifestMetaDataValue =
    std::variant<std::int32_t, bool, std::string,
                 AndroidManifestMetaDataValueReference,
                 AndroidManifestMetaDataResourceReference>;

struct AndroidManifestMetaData final {
    std::string name;
    AndroidManifestMetaDataValue value;
};

struct AndroidManifestPermissionDefinition final {
    std::string name;
    std::string package_name;
    std::uint32_t protection_level{};
    std::optional<std::string> group;
    std::uint32_t flags{};
    std::uint32_t description_res{};
    std::optional<std::string> nonlocalized_description;
    std::vector<AndroidManifestMetaData> meta_data;
};

enum class AndroidManifestComponentKind : std::uint8_t {
    activity,
    activity_alias,
};

struct AndroidManifestIntentFilter final {
    std::vector<std::string> actions;
    std::vector<std::string> categories;
    // Preserve the presence of constraints not parsed by the bounded resolver.
    bool has_data{};
};

struct AndroidManifestServiceComponent final {
    std::string name;
    bool enabled{true};
    std::vector<AndroidManifestIntentFilter> intent_filters;
    std::optional<bool> exported;
    std::string process_name;
    std::optional<std::string> permission;
    std::vector<AndroidManifestMetaData> meta_data;
    std::uint32_t flags{};
};

[[nodiscard]] inline bool AndroidManifestServiceExported(
    const AndroidManifestServiceComponent& component) {
    return component.exported.value_or(!component.intent_filters.empty());
}

struct AndroidManifestReceiverComponent final {
    std::string name;
    bool enabled{true};
    std::optional<bool> exported;
    bool has_intent_filter{};
    std::string process_name;
    std::optional<std::string> permission;
    std::vector<AndroidManifestMetaData> meta_data;
};

[[nodiscard]] inline bool AndroidManifestReceiverExported(
    const AndroidManifestReceiverComponent& component) {
    return component.exported.value_or(component.has_intent_filter);
}

struct AndroidManifestActivityComponent final {
    AndroidManifestComponentKind kind{AndroidManifestComponentKind::activity};
    std::string name;
    std::optional<std::string> target_activity;
    bool enabled{true};
    std::vector<AndroidManifestIntentFilter> intent_filters;
    std::optional<std::uint32_t> theme;
    // Absent means API 19 default: exported iff the component has intent-filters.
    std::optional<bool> exported;
};

[[nodiscard]] inline bool AndroidManifestActivityExported(
    const AndroidManifestActivityComponent& component) {
    return component.exported.value_or(!component.intent_filters.empty());
}

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
    std::optional<std::uint32_t> application_theme;
    std::optional<AndroidManifestLabel> application_label;
    // Compatibility projection of the resolved launch target class. New
    // startup code should use ResolveLauncherComponent to retain alias identity.
    std::optional<std::string> launcher_activity;
    // Normalized process Application class. The framework default is published
    // explicitly so startup callers do not need to reconstruct Manifest rules.
    std::string application_class{"android.app.Application"};
    std::string application_process_name;
    std::optional<std::string> application_permission;
    // Activity and activity-alias declarations in Manifest document order.
    std::vector<AndroidManifestActivityComponent> activity_components;
    std::vector<AndroidManifestMetaData> application_meta_data;
    std::vector<std::string> requested_permissions;
    std::vector<AndroidManifestPermissionDefinition> defined_permissions;
    bool application_enabled{true};
    std::vector<AndroidManifestServiceComponent> service_components;
    std::vector<AndroidManifestReceiverComponent> receiver_components;
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
