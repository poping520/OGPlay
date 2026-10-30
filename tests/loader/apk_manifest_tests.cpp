#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ogplay/loader/apk_manifest.h"

namespace {

void Append16(std::vector<std::byte>& bytes, const std::uint16_t value) {
    bytes.push_back(static_cast<std::byte>(value & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
}

void Append32(std::vector<std::byte>& bytes, const std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
    }
}

void Set32(std::vector<std::byte>& bytes, const std::size_t offset, const std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes[offset + shift / 8U] = static_cast<std::byte>((value >> shift) & 0xffU);
    }
}

void AppendAscii(std::vector<std::byte>& bytes, const std::string_view text) {
    for (const auto value : text)
        bytes.push_back(static_cast<std::byte>(value));
}

std::uint32_t Crc32(const std::vector<std::byte>& bytes) {
    std::uint32_t crc = 0xffffffffU;
    for (const auto byte : bytes) {
        crc ^= std::to_integer<std::uint8_t>(byte);
        for (unsigned bit = 0; bit < 8; ++bit) {
            const auto mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xedb88320U & mask);
        }
    }
    return ~crc;
}

std::vector<std::byte> StoredApk(const std::string_view name, const std::vector<std::byte>& payload) {
    const auto crc = Crc32(payload);
    std::vector<std::byte> bytes;
    Append32(bytes, 0x04034b50);
    Append16(bytes, 20);
    Append16(bytes, 0);
    Append16(bytes, 0);
    Append16(bytes, 0);
    Append16(bytes, 0);
    Append32(bytes, crc);
    Append32(bytes, static_cast<std::uint32_t>(payload.size()));
    Append32(bytes, static_cast<std::uint32_t>(payload.size()));
    Append16(bytes, static_cast<std::uint16_t>(name.size()));
    Append16(bytes, 0);
    AppendAscii(bytes, name);
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    const auto central_offset = static_cast<std::uint32_t>(bytes.size());
    Append32(bytes, 0x02014b50);
    Append16(bytes, 20);
    Append16(bytes, 20);
    Append16(bytes, 0);
    Append16(bytes, 0);
    Append16(bytes, 0);
    Append16(bytes, 0);
    Append32(bytes, crc);
    Append32(bytes, static_cast<std::uint32_t>(payload.size()));
    Append32(bytes, static_cast<std::uint32_t>(payload.size()));
    Append16(bytes, static_cast<std::uint16_t>(name.size()));
    Append16(bytes, 0);
    Append16(bytes, 0);
    Append16(bytes, 0);
    Append16(bytes, 0);
    Append32(bytes, 0);
    Append32(bytes, 0);
    AppendAscii(bytes, name);
    const auto central_size = static_cast<std::uint32_t>(bytes.size()) - central_offset;
    Append32(bytes, 0x06054b50);
    Append16(bytes, 0);
    Append16(bytes, 0);
    Append16(bytes, 1);
    Append16(bytes, 1);
    Append32(bytes, central_size);
    Append32(bytes, central_offset);
    Append16(bytes, 0);
    return bytes;
}

std::vector<std::byte> StringPool(const std::vector<std::string>& strings, const bool utf8) {
    std::vector<std::byte> data;
    std::vector<std::uint32_t> offsets;
    for (const auto& string : strings) {
        offsets.push_back(static_cast<std::uint32_t>(data.size()));
        if (utf8) {
            data.push_back(static_cast<std::byte>(string.size()));
            data.push_back(static_cast<std::byte>(string.size()));
            AppendAscii(data, string);
            data.push_back(std::byte{0});
        } else {
            Append16(data, static_cast<std::uint16_t>(string.size()));
            for (const auto value : string)
                Append16(data, static_cast<std::uint8_t>(value));
            Append16(data, 0);
        }
    }
    while (data.size() % 4U != 0)
        data.push_back(std::byte{0});

    std::vector<std::byte> chunk;
    Append16(chunk, 0x0001);
    Append16(chunk, 28);
    Append32(chunk, 0);
    Append32(chunk, static_cast<std::uint32_t>(strings.size()));
    Append32(chunk, 0);
    Append32(chunk, utf8 ? 0x100U : 0U);
    Append32(chunk, static_cast<std::uint32_t>(28U + offsets.size() * 4U));
    Append32(chunk, 0);
    for (const auto offset : offsets)
        Append32(chunk, offset);
    chunk.insert(chunk.end(), data.begin(), data.end());
    Set32(chunk, 4, static_cast<std::uint32_t>(chunk.size()));
    return chunk;
}

struct Attribute final {
    std::uint32_t name{};
    std::uint32_t raw_value{0xffffffffU};
    std::uint8_t type{};
    std::uint32_t data{};
    std::uint32_t namespace_index{0xffffffffU};
};

std::vector<std::byte> StartElement(const std::uint32_t name, const std::vector<Attribute>& attributes) {
    std::vector<std::byte> chunk;
    Append16(chunk, 0x0102);
    Append16(chunk, 16);
    Append32(chunk, static_cast<std::uint32_t>(36U + attributes.size() * 20U));
    Append32(chunk, 1);
    Append32(chunk, 0xffffffffU);
    Append32(chunk, 0xffffffffU);
    Append32(chunk, name);
    Append16(chunk, 20);
    Append16(chunk, 20);
    Append16(chunk, static_cast<std::uint16_t>(attributes.size()));
    Append16(chunk, 0);
    Append16(chunk, 0);
    Append16(chunk, 0);
    for (const auto& attribute : attributes) {
        Append32(chunk, attribute.namespace_index);
        Append32(chunk, attribute.name);
        Append32(chunk, attribute.raw_value);
        Append16(chunk, 8);
        chunk.push_back(std::byte{0});
        chunk.push_back(static_cast<std::byte>(attribute.type));
        Append32(chunk, attribute.data);
    }
    return chunk;
}

std::vector<std::byte> EndElement(const std::uint32_t name) {
    std::vector<std::byte> chunk;
    Append16(chunk, 0x0103);
    Append16(chunk, 16);
    Append32(chunk, 24);
    Append32(chunk, 1);
    Append32(chunk, 0xffffffffU);
    Append32(chunk, 0xffffffffU);
    Append32(chunk, name);
    return chunk;
}

void Append(std::vector<std::byte>& destination, const std::vector<std::byte>& source) {
    destination.insert(destination.end(), source.begin(), source.end());
}

std::vector<std::byte> Manifest(const bool utf8 = false, const std::string_view package = "org.example.game",
                                const std::uint8_t version_type = 0x10,
                                const std::vector<Attribute>& application_attributes = {}) {
    const std::vector<std::string> strings{"manifest",
                                           "package",
                                           "versionCode",
                                           "versionName",
                                           "uses-sdk",
                                           "minSdkVersion",
                                           "targetSdkVersion",
                                           std::string(package),
                                           "1.2.3",
                                           "http://schemas.android.com/apk/res/android",
                                           "application",
                                           "icon",
                                           "label",
                                           "OGPlay Game"};
    std::vector<std::byte> result;
    Append16(result, 0x0003);
    Append16(result, 8);
    Append32(result, 0);
    Append(result, StringPool(strings, utf8));
    Append(result, StartElement(0, {{1, 7, 0x03, 7}, {2, 0xffffffffU, version_type, 7, 9}, {3, 8, 0x03, 8, 9}}));
    Append(result, StartElement(4, {{5, 0xffffffffU, 0x10, 5, 9}, {6, 0xffffffffU, 0x10, 19, 9}}));
    Append(result, EndElement(4));
    Append(result, StartElement(10, application_attributes));
    Append(result, EndElement(10));
    Append(result, EndElement(0));
    Set32(result, 4, static_cast<std::uint32_t>(result.size()));
    return result;
}

std::vector<std::byte> PackageFactsManifest(const bool invalid_metadata = false, const bool include_receivers = false,
                                            const std::uint32_t boolean_data = 1U,
                                            const bool include_permission = false) {
    const std::vector<std::string> strings{"manifest",
                                           "package",
                                           "versionCode",
                                           "application",
                                           "uses-permission",
                                           "name",
                                           "meta-data",
                                           "value",
                                           "resource",
                                           "org.example.game",
                                           "android.permission.INTERNET",
                                           "com.example.configuration",
                                           "live",
                                           "com.example.number",
                                           "com.example.resource",
                                           "com.example.enabled",
                                           "com.example.value-reference",
                                           "http://schemas.android.com/apk/res/android",
                                           "receiver",
                                           "process",
                                           "permission",
                                           "enabled",
                                           "exported",
                                           "intent-filter",
                                           ".Receiver",
                                           ".Disabled",
                                           ":app",
                                           ":remote",
                                           "org.example.APP",
                                           "org.example.RECEIVER",
                                           "receiver.string",
                                           "receiver.bool",
                                           "receiver.int",
                                           "receiver.value-ref",
                                           "receiver.resource",
                                           "ready",
                                           ".C2D_MESSAGE",
                                           "protectionLevel",
                                           "permissionGroup",
                                           "permissionFlags",
                                           "description",
                                           "org.example.GROUP",
                                           "permission.string",
                                           "permission.bool",
                                           "permission.int",
                                           "permission.value-ref",
                                           "permission.resource",
                                           "action",
                                           "category",
                                           "data",
                                           "priority",
                                           "label",
                                           "icon",
                                           "OTHER"};
    const auto index = [&](const std::string_view value) {
        const auto found = std::find(strings.begin(), strings.end(), value);
        REQUIRE(found != strings.end());
        return static_cast<std::uint32_t>(std::distance(strings.begin(), found));
    };
    const auto android_namespace = index("http://schemas.android.com/apk/res/android");
    std::vector<std::byte> result;
    Append16(result, 0x0003);
    Append16(result, 8);
    Append32(result, 0);
    Append(result, StringPool(strings, false));
    Append(result, StartElement(index("manifest"),
                                {{index("package"), index("org.example.game"), 0x03, index("org.example.game")},
                                 {index("versionCode"), 0xffffffffU, 0x10, 7, android_namespace}}));
    Append(result, StartElement(index("uses-permission"), {{index("name"), index("android.permission.INTERNET"), 0x03,
                                                            index("android.permission.INTERNET"), android_namespace}}));
    Append(result, EndElement(index("uses-permission")));
    if (include_permission) {
        Append(result,
               StartElement(index("permission"),
                            {{index("name"), index(".C2D_MESSAGE"), 0x03, index(".C2D_MESSAGE"), android_namespace},
                             {index("protectionLevel"), 0xffffffffU, 0x10, 2, android_namespace},
                             {index("permissionGroup"), index("org.example.GROUP"), 0x03, index("org.example.GROUP"),
                              android_namespace},
                             {index("permissionFlags"), 0xffffffffU, 0x10, 1, android_namespace},
                             {index("description"), 0xffffffffU, 0x01, 0x7f030002U, android_namespace}}));
        const auto metadata = [&](const char* name, const Attribute value) {
            Append(result, StartElement(index("meta-data"),
                                        {{index("name"), index(name), 0x03, index(name), android_namespace}, value}));
            Append(result, EndElement(index("meta-data")));
        };
        metadata("permission.string", {index("value"), index("ready"), 0x03, index("ready"), android_namespace});
        metadata("permission.bool", {index("value"), 0xffffffffU, 0x12, 1, android_namespace});
        metadata("permission.int", {index("value"), 0xffffffffU, 0x10, 42, android_namespace});
        metadata("permission.value-ref", {index("value"), 0xffffffffU, 0x01, 0x7f05004fU, android_namespace});
        metadata("permission.resource", {index("resource"), 0xffffffffU, 0x01, 0x7f030001U, android_namespace});
        Append(result, EndElement(index("permission")));
    }
    std::vector<Attribute> application_attributes;
    if (include_receivers) {
        application_attributes = {
            {index("enabled"), 0xffffffffU, 0x12, boolean_data, android_namespace},
            {index("process"), index(":app"), 0x03, index(":app"), android_namespace},
            {index("permission"), index("org.example.APP"), 0x03, index("org.example.APP"), android_namespace}};
    }
    Append(result, StartElement(index("application"), application_attributes));
    std::vector<Attribute> string_metadata{{index("name"), index("com.example.configuration"), 0x03,
                                            index("com.example.configuration"), android_namespace}};
    if (!invalid_metadata) {
        string_metadata.push_back({index("value"), index("live"), 0x03, index("live"), android_namespace});
    }
    if (include_receivers) {
        Append(result, StartElement(index("receiver"),
                                    {{index("name"), index(".Receiver"), 0x03, index(".Receiver"), android_namespace},
                                     {index("enabled"), 0xffffffffU, 0x12, boolean_data, android_namespace},
                                     {index("exported"), 0xffffffffU, 0x12, boolean_data, android_namespace},
                                     {index("process"), index(":remote"), 0x03, index(":remote"), android_namespace},
                                     {index("permission"), index("org.example.RECEIVER"), 0x03,
                                      index("org.example.RECEIVER"), android_namespace}}));
        Append(result, StartElement(index("intent-filter"),
                                    {{index("priority"), 0xffffffffU, 0x10, 0xfffffff9U, android_namespace},
                                     {index("label"), index("ready"), 0x03, index("ready"), android_namespace},
                                     {index("icon"), 0xffffffffU, 0x01, 0x7f030001U, android_namespace}}));
        Append(result, StartElement(index("action"), {{index("name"), index("org.example.RECEIVER"), 0x03,
                                                       index("org.example.RECEIVER"), android_namespace}}));
        Append(result, EndElement(index("action")));
        Append(result, StartElement(index("category"), {{index("name"), index("org.example.game"), 0x03,
                                                         index("org.example.game"), android_namespace}}));
        Append(result, EndElement(index("category")));
        Append(result, EndElement(index("intent-filter")));
        Append(result, StartElement(index("intent-filter"),
                                    {{index("label"), 0xffffffffU, 0x01, 0x7f050001U, android_namespace}}));
        Append(result, StartElement(index("action"),
                                    {{index("name"), index("OTHER"), 0x03, index("OTHER"), android_namespace}}));
        Append(result, EndElement(index("action")));
        Append(result, StartElement(index("data"), {}));
        Append(result, EndElement(index("data")));
        Append(result, EndElement(index("intent-filter")));
        const auto metadata = [&](const char* name, const Attribute value) {
            Append(result, StartElement(index("meta-data"),
                                        {{index("name"), index(name), 0x03, index(name), android_namespace}, value}));
            Append(result, EndElement(index("meta-data")));
        };
        metadata("receiver.string", {index("value"), index("ready"), 0x03, index("ready"), android_namespace});
        metadata("receiver.bool", {index("value"), 0xffffffffU, 0x12, boolean_data, android_namespace});
        metadata("receiver.int", {index("value"), 0xffffffffU, 0x10, 42, android_namespace});
        metadata("receiver.value-ref", {index("value"), 0xffffffffU, 0x01, 0x7f05004fU, android_namespace});
        metadata("receiver.resource", {index("resource"), 0xffffffffU, 0x01, 0x7f030001U, android_namespace});
        Append(result, EndElement(index("receiver")));
        Append(result, StartElement(index("receiver"),
                                    {{index("name"), index(".Disabled"), 0x03, index(".Disabled"), android_namespace},
                                     {index("enabled"), 0xffffffffU, 0x12, 0, android_namespace},
                                     {index("exported"), 0xffffffffU, 0x12, 0, android_namespace}}));
        Append(result, EndElement(index("receiver")));
    }
    Append(result, StartElement(index("meta-data"), string_metadata));
    Append(result, EndElement(index("meta-data")));
    if (!invalid_metadata) {
        Append(result, StartElement(index("meta-data"), {{index("name"), index("com.example.number"), 0x03,
                                                          index("com.example.number"), android_namespace},
                                                         {index("value"), 0xffffffffU, 0x10, 42, android_namespace}}));
        Append(result, EndElement(index("meta-data")));
        Append(result,
               StartElement(index("meta-data"), {{index("name"), index("com.example.value-reference"), 0x03,
                                                  index("com.example.value-reference"), android_namespace},
                                                 {index("value"), 0xffffffffU, 0x01, 0x7f05004fU, android_namespace}}));
        Append(result, EndElement(index("meta-data")));
        Append(result, StartElement(index("meta-data"),
                                    {{index("name"), index("com.example.resource"), 0x03, index("com.example.resource"),
                                      android_namespace},
                                     {index("resource"), 0xffffffffU, 0x01, 0x7f030001U, android_namespace}}));
        Append(result, EndElement(index("meta-data")));
        Append(result, StartElement(index("meta-data"),
                                    {{index("name"), index("com.example.enabled"), 0x03, index("com.example.enabled"),
                                      android_namespace},
                                     {index("value"), 0xffffffffU, 0x12, boolean_data, android_namespace}}));
        Append(result, EndElement(index("meta-data")));
    }
    Append(result, EndElement(index("application")));
    Append(result, EndElement(index("manifest")));
    Set32(result, 4, static_cast<std::uint32_t>(result.size()));
    return result;
}

struct ComponentFixture final {
    std::string tag{"activity"};
    std::string name;
    std::optional<std::string> target;
    bool enabled{true};
    std::vector<std::vector<std::string>> filters;
    bool filter_has_data{};
    std::optional<std::uint32_t> theme;
    std::optional<bool> exported;
    std::optional<std::string> process;
    std::optional<std::string> permission;
    bool stop_with_task{};
    bool isolated_process{};
    bool single_user{};
    bool service_meta_data{};
    std::optional<std::uint32_t> launch_mode;
    std::optional<std::string> task_affinity;
};

std::vector<std::byte> StartupManifest(const std::optional<std::string>& application_name,
                                       const std::vector<ComponentFixture>& components,
                                       const bool application_enabled = true,
                                       std::optional<std::uint32_t> theme = std::nullopt,
                                       const std::optional<std::string> application_process = std::nullopt,
                                       const std::optional<std::string> application_permission = std::nullopt,
                                       const std::optional<std::string> application_affinity = std::nullopt) {
    std::vector<std::string> strings{"manifest",
                                     "package",
                                     "versionCode",
                                     "application",
                                     "name",
                                     "enabled",
                                     "targetActivity",
                                     "activity",
                                     "activity-alias",
                                     "intent-filter",
                                     "action",
                                     "category",
                                     "service",
                                     "data",
                                     "theme",
                                     "exported",
                                     "org.example.game",
                                     "http://schemas.android.com/apk/res/android",
                                     "process",
                                     "permission",
                                     "stopWithTask",
                                     "isolatedProcess",
                                     "singleUser",
                                     "meta-data",
                                     "launchMode",
                                     "taskAffinity",
                                     "value",
                                     "resource",
                                     "service.string",
                                     "service.bool",
                                     "service.int",
                                     "service.value-ref",
                                     "service.resource",
                                     "ready"};
    const auto add = [&](const std::string& value) {
        if (std::find(strings.begin(), strings.end(), value) == strings.end()) {
            strings.push_back(value);
        }
    };
    if (application_name.has_value()) add(*application_name);
    if (application_process) add(*application_process);
    if (application_permission) add(*application_permission);
    if (application_affinity) add(*application_affinity);
    for (const auto& component : components) {
        add(component.name);
        if (component.target.has_value()) add(*component.target);
        if (component.process) add(*component.process);
        if (component.permission) add(*component.permission);
        if (component.task_affinity) add(*component.task_affinity);
        for (const auto& filter : component.filters) {
            for (const auto& value : filter)
                add(value);
        }
    }
    const auto index = [&](const std::string_view value) {
        const auto found = std::find(strings.begin(), strings.end(), value);
        REQUIRE(found != strings.end());
        return static_cast<std::uint32_t>(std::distance(strings.begin(), found));
    };
    const auto android_namespace = index("http://schemas.android.com/apk/res/android");

    std::vector<std::byte> result;
    Append16(result, 0x0003);
    Append16(result, 8);
    Append32(result, 0);
    Append(result, StringPool(strings, false));
    Append(result, StartElement(index("manifest"),
                                {{index("package"), index("org.example.game"), 0x03, index("org.example.game")},
                                 {index("versionCode"), 0xffffffffU, 0x10, 1, android_namespace}}));
    std::vector<Attribute> application_attributes;
    if (theme) application_attributes.push_back({index("theme"), 0xffffffffU, 0x01, *theme, android_namespace});
    if (!application_enabled) {
        application_attributes.push_back({index("enabled"), 0xffffffffU, 0x12, 0, android_namespace});
    }
    if (application_name.has_value()) {
        application_attributes.push_back(
            {index("name"), index(*application_name), 0x03, index(*application_name), android_namespace});
    }
    if (application_process) {
        application_attributes.push_back(
            {index("process"), index(*application_process), 0x03, index(*application_process), android_namespace});
    }
    if (application_permission) {
        application_attributes.push_back({index("permission"), index(*application_permission), 0x03,
                                          index(*application_permission), android_namespace});
    }
    if (application_affinity) {
        application_attributes.push_back({index("taskAffinity"), index(*application_affinity), 0x03,
                                          index(*application_affinity), android_namespace});
    }
    Append(result, StartElement(index("application"), application_attributes));
    for (const auto& component : components) {
        std::vector<Attribute> attributes{
            {index("name"), index(component.name), 0x03, index(component.name), android_namespace}};
        if (component.launch_mode)
            attributes.push_back({index("launchMode"), 0xffffffffU, 0x10, *component.launch_mode, android_namespace});
        if (component.task_affinity)
            attributes.push_back({index("taskAffinity"), index(*component.task_affinity), 0x03,
                                  index(*component.task_affinity), android_namespace});
        if (component.theme)
            attributes.push_back({index("theme"), 0xffffffffU, 0x01, *component.theme, android_namespace});
        if (!component.enabled) {
            attributes.push_back({index("enabled"), 0xffffffffU, 0x12, 0, android_namespace});
        }
        if (component.target.has_value()) {
            attributes.push_back(
                {index("targetActivity"), index(*component.target), 0x03, index(*component.target), android_namespace});
        }
        if (component.exported.has_value()) {
            attributes.push_back(
                {index("exported"), 0xffffffffU, 0x12, *component.exported ? 1U : 0U, android_namespace});
        }
        if (component.process) {
            attributes.push_back(
                {index("process"), index(*component.process), 0x03, index(*component.process), android_namespace});
        }
        if (component.permission) {
            attributes.push_back({index("permission"), index(*component.permission), 0x03, index(*component.permission),
                                  android_namespace});
        }
        for (const auto& [enabled, name] : {std::pair{component.stop_with_task, "stopWithTask"},
                                            std::pair{component.isolated_process, "isolatedProcess"},
                                            std::pair{component.single_user, "singleUser"}}) {
            if (enabled) attributes.push_back({index(name), 0xffffffffU, 0x12, 1, android_namespace});
        }
        Append(result, StartElement(index(component.tag), attributes));
        for (const auto& filter : component.filters) {
            Append(result, StartElement(index("intent-filter"), {}));
            if (component.filter_has_data) {
                Append(result, StartElement(index("data"), {}));
                Append(result, EndElement(index("data")));
            }
            for (const auto& value : filter) {
                const auto tag = value.starts_with("android.intent.action.") ? "action" : "category";
                Append(result, StartElement(index(tag),
                                            {{index("name"), index(value), 0x03, index(value), android_namespace}}));
                Append(result, EndElement(index(tag)));
            }
            Append(result, EndElement(index("intent-filter")));
        }
        if (component.service_meta_data) {
            const auto metadata = [&](const char* name, const Attribute value) {
                Append(result,
                       StartElement(index("meta-data"),
                                    {{index("name"), index(name), 0x03, index(name), android_namespace}, value}));
                Append(result, EndElement(index("meta-data")));
            };
            metadata("service.string", {index("value"), index("ready"), 0x03, index("ready"), android_namespace});
            metadata("service.bool", {index("value"), 0xffffffffU, 0x12, 1, android_namespace});
            metadata("service.int", {index("value"), 0xffffffffU, 0x10, 42, android_namespace});
            metadata("service.value-ref", {index("value"), 0xffffffffU, 0x01, 0x7f05004fU, android_namespace});
            metadata("service.resource", {index("resource"), 0xffffffffU, 0x01, 0x7f030001U, android_namespace});
        }
        Append(result, EndElement(index(component.tag)));
    }
    Append(result, EndElement(index("application")));
    Append(result, EndElement(index("manifest")));
    Set32(result, 4, static_cast<std::uint32_t>(result.size()));
    return result;
}

constexpr std::string_view kMain = "android.intent.action.MAIN";
constexpr std::string_view kLauncher = "android.intent.category.LAUNCHER";

}  // namespace

TEST_CASE("Android Manifest class names follow KitKat PackageParser rules") {
    using ogplay::loader::NormalizeAndroidManifestClassName;
    CHECK(NormalizeAndroidManifestClassName("org.example.game", ".App") == "org.example.game.App");
    CHECK(NormalizeAndroidManifestClassName("org.example.game", "App") == "org.example.game.App");
    CHECK(NormalizeAndroidManifestClassName("org.example.game", "org.other.App") == "org.other.App");
    CHECK_THROWS_AS(static_cast<void>(NormalizeAndroidManifestClassName("org.example.game", "Bad.App")),
                    ogplay::loader::AndroidManifestStartupError);
}

TEST_CASE("binary AndroidManifest publishes default and custom Application classes") {
    const auto defaults = ogplay::loader::ParseAndroidBinaryManifest(StartupManifest(std::nullopt, {}));
    CHECK(defaults.application_class == "android.app.Application");

    const auto custom = ogplay::loader::ParseAndroidBinaryManifest(StartupManifest(".GameApplication", {}));
    CHECK(custom.application_class == "org.example.game.GameApplication");
}

TEST_CASE("binary AndroidManifest resolves direct launcher in document order") {
    const std::vector<ComponentFixture> components{
        {"activity", ".First", std::nullopt, true, {{std::string(kMain), std::string(kLauncher)}}},
        {"activity", ".Second", std::nullopt, true, {{std::string(kMain), std::string(kLauncher)}}}};
    for (int pass = 0; pass < 3; ++pass) {
        const auto facts = ogplay::loader::ParseAndroidBinaryManifest(StartupManifest(std::nullopt, components));
        const auto launcher = ogplay::loader::ResolveLauncherComponent(facts);
        CHECK(launcher.component_name == "org.example.game.First");
        CHECK(launcher.activity_class == "org.example.game.First");
        CHECK_FALSE(launcher.via_alias);
        CHECK(facts.launcher_activity == launcher.activity_class);
    }
}

TEST_CASE("binary AndroidManifest resolves activity-alias to its target") {
    const auto facts = ogplay::loader::ParseAndroidBinaryManifest(StartupManifest(
        std::nullopt,
        {{"activity", ".RealActivity", std::nullopt, true, {}},
         {"activity-alias", ".Launcher", ".RealActivity", true, {{std::string(kMain), std::string(kLauncher)}}}}));
    const auto launcher = ogplay::loader::ResolveLauncherComponent(facts);
    CHECK(launcher.component_name == "org.example.game.Launcher");
    CHECK(launcher.activity_class == "org.example.game.RealActivity");
    CHECK(launcher.via_alias);
    REQUIRE(facts.activity_components.size() == 2);
    CHECK(facts.activity_components[1].target_activity == "org.example.game.RealActivity");
}

TEST_CASE("binary AndroidManifest preserves duplicate Activity declarations and filters") {
    using namespace ogplay::loader;
    ComponentFixture first;
    first.name = ".Main";
    first.filters = {{std::string(kMain), std::string(kLauncher)}};
    ComponentFixture view;
    view.name = "org.example.game.Main";
    view.filters = {{"android.intent.action.VIEW"}};
    ComponentFixture last;
    last.name = "Main";
    last.launch_mode = 2;
    last.theme = 0x01030007U;
    const auto facts = ParseAndroidBinaryManifest(StartupManifest(std::nullopt, {first, view, last}));
    REQUIRE(facts.activity_components.size() == 3);
    for (const auto& component : facts.activity_components)
        CHECK(component.name == "org.example.game.Main");
    CHECK(facts.activity_components[0].intent_filters[0].actions[0] == kMain);
    CHECK(facts.activity_components[1].intent_filters[0].actions[0] == "android.intent.action.VIEW");
    CHECK(facts.activity_components[2].intent_filters.empty());
    const auto* registered = FindAndroidManifestActivityComponent(facts.activity_components, "org.example.game.Main");
    REQUIRE(registered == &facts.activity_components.back());
    CHECK(registered->launch_mode == 2);
    CHECK(registered->theme == 0x01030007U);
    CHECK(FindAndroidManifestActivityComponent(facts.activity_components, "missing") == nullptr);
    CHECK(ResolveLauncherComponent(facts).activity_class == "org.example.game.Main");

    last.enabled = false;
    const auto disabled = ParseAndroidBinaryManifest(StartupManifest(std::nullopt, {first, view, last}));
    CHECK_FALSE(disabled.launcher_activity.has_value());
}

TEST_CASE("binary AndroidManifest alias snapshots the first preceding duplicate target") {
    using namespace ogplay::loader;
    ComponentFixture first;
    first.name = ".Target";
    first.theme = 0x7f030001U;
    first.launch_mode = 1;
    ComponentFixture second = first;
    second.theme = 0x7f030002U;
    second.launch_mode = 2;
    ComponentFixture alias;
    alias.tag = "activity-alias";
    alias.name = ".Alias";
    alias.target = ".Target";
    alias.filters = {{std::string(kMain), std::string(kLauncher)}};
    ComponentFixture later = second;
    later.theme = 0x7f030003U;
    later.launch_mode = 3;
    const auto facts = ParseAndroidBinaryManifest(StartupManifest(std::nullopt, {first, second, alias, later}));
    REQUIRE(facts.activity_components.size() == 4);
    CHECK(facts.activity_components[2].theme == first.theme);
    CHECK(facts.activity_components[2].launch_mode == first.launch_mode);
    CHECK(FindAndroidManifestActivityComponent(facts.activity_components, "org.example.game.Target")->launch_mode == 3);
    CHECK(ResolveLauncherComponent(facts).activity_class == "org.example.game.Target");
}

TEST_CASE("launcher action and category must belong to one intent-filter") {
    const auto facts = ogplay::loader::ParseAndroidBinaryManifest(StartupManifest(
        std::nullopt, {{"activity", ".Split", std::nullopt, true, {{std::string(kMain)}, {std::string(kLauncher)}}}}));
    CHECK_FALSE(facts.launcher_activity.has_value());
    try {
        static_cast<void>(ogplay::loader::ResolveLauncherComponent(facts));
        FAIL("expected no-launcher error");
    } catch (const ogplay::loader::AndroidManifestStartupError& error) {
        CHECK(error.Reason() == ogplay::loader::AndroidManifestStartupErrorReason::no_launcher);
    }
}

TEST_CASE("disabled launcher components do not resolve") {
    for (const auto& components : std::vector<std::vector<ComponentFixture>>{
             {{"activity", ".Disabled", std::nullopt, false, {{std::string(kMain), std::string(kLauncher)}}}},
             {{"activity", ".Target", std::nullopt, true, {}},
              {"activity-alias", ".Alias", ".Target", false, {{std::string(kMain), std::string(kLauncher)}}}}}) {
        const auto facts = ogplay::loader::ParseAndroidBinaryManifest(StartupManifest(std::nullopt, components));
        CHECK_THROWS_AS(static_cast<void>(ogplay::loader::ResolveLauncherComponent(facts)),
                        ogplay::loader::AndroidManifestStartupError);
    }
}

TEST_CASE("enabled activity-alias resolves independently of target enabled state") {
    const auto facts = ogplay::loader::ParseAndroidBinaryManifest(StartupManifest(
        std::nullopt, {{"activity", ".Target", std::nullopt, false, {}},
                       {"activity-alias", ".Alias", ".Target", true, {{std::string(kMain), std::string(kLauncher)}}}}));
    const auto launcher = ogplay::loader::ResolveLauncherComponent(facts);
    CHECK(launcher.component_name == "org.example.game.Alias");
    CHECK(launcher.activity_class == "org.example.game.Target");
}

TEST_CASE("activity-alias requires a previously declared Activity target") {
    try {
        static_cast<void>(ogplay::loader::ParseAndroidBinaryManifest(StartupManifest(
            std::nullopt,
            {{"activity-alias", ".Alias", ".Missing", true, {{std::string(kMain), std::string(kLauncher)}}}})));
        FAIL("expected alias target error");
    } catch (const ogplay::loader::AndroidManifestStartupError& error) {
        CHECK(error.Reason() == ogplay::loader::AndroidManifestStartupErrorReason::alias_target_not_found);
    }
}

TEST_CASE("binary AndroidManifest exposes exact package version and SDK facts") {
    const auto facts = ogplay::loader::ParseAndroidBinaryManifest(Manifest());
    CHECK(facts.package == "org.example.game");
    CHECK(facts.version_code == 7);
    CHECK(facts.version_name == "1.2.3");
    CHECK(facts.min_sdk == 5);
    CHECK(facts.target_sdk == 19);
    CHECK_FALSE(facts.application_icon.has_value());
    CHECK_FALSE(facts.application_label.has_value());

    const auto utf8 = ogplay::loader::ParseAndroidBinaryManifest(Manifest(true));
    CHECK(utf8.package == facts.package);
    CHECK(utf8.version_code == facts.version_code);

    const auto apk = StoredApk("AndroidManifest.xml", Manifest());
    const auto archive = ogplay::loader::ParseApkArchive(apk);
    const auto imported = ogplay::loader::ReadAndroidManifest(apk, archive);
    CHECK(imported.package == facts.package);
    CHECK(imported.version_name == facts.version_name);
}

TEST_CASE("binary AndroidManifest distinguishes application visual attributes") {
    const auto referenced = ogplay::loader::ParseAndroidBinaryManifest(
        Manifest(false, "org.example.game", 0x10,
                 {{11, 0xffffffffU, 0x01, 0x7f020001U, 9}, {12, 0xffffffffU, 0x01, 0x7f030002U, 9}}));
    CHECK(referenced.application_icon == 0x7f020001U);
    REQUIRE(referenced.application_label.has_value());
    REQUIRE(std::holds_alternative<std::uint32_t>(*referenced.application_label));
    CHECK(std::get<std::uint32_t>(*referenced.application_label) == 0x7f030002U);

    const auto literal =
        ogplay::loader::ParseAndroidBinaryManifest(Manifest(false, "org.example.game", 0x10, {{12, 13, 0x03, 13, 9}}));
    REQUIRE(literal.application_label.has_value());
    REQUIRE(std::holds_alternative<std::string>(*literal.application_label));
    CHECK(std::get<std::string>(*literal.application_label) == "OGPlay Game");
}

TEST_CASE("binary AndroidManifest exposes package permission and application meta-data") {
    const auto facts = ogplay::loader::ParseAndroidBinaryManifest(PackageFactsManifest());
    REQUIRE(facts.requested_permissions.size() == 1U);
    CHECK(facts.requested_permissions.front() == "android.permission.INTERNET");
    REQUIRE(facts.application_meta_data.size() == 5U);
    CHECK(facts.application_meta_data[0].name == "com.example.configuration");
    CHECK(std::get<std::string>(facts.application_meta_data[0].value) == "live");
    CHECK(std::get<std::int32_t>(facts.application_meta_data[1].value) == 42);
    CHECK(std::get<ogplay::loader::AndroidManifestMetaDataValueReference>(facts.application_meta_data[2].value)
              .resource_id == 0x7f05004fU);
    CHECK(std::get<ogplay::loader::AndroidManifestMetaDataResourceReference>(facts.application_meta_data[3].value)
              .resource_id == 0x7f030001U);
    CHECK(std::get<bool>(facts.application_meta_data[4].value));
    CHECK_THROWS_WITH(static_cast<void>(ogplay::loader::ParseAndroidBinaryManifest(PackageFactsManifest(true))),
                      "binary AndroidManifest application meta-data requires value or resource");
}

TEST_CASE("Manifest permission definitions are separate from requests") {
    using namespace ogplay::loader;
    const auto facts = ParseAndroidBinaryManifest(PackageFactsManifest(false, false, 1U, true));
    REQUIRE(facts.requested_permissions.size() == 1);
    CHECK(facts.requested_permissions[0] == "android.permission.INTERNET");
    REQUIRE(facts.defined_permissions.size() == 1);
    const auto& definition = facts.defined_permissions[0];
    CHECK(definition.name == "org.example.game.C2D_MESSAGE");
    CHECK(definition.package_name == "org.example.game");
    CHECK(definition.protection_level == 2U);
    CHECK(definition.group == "org.example.GROUP");
    CHECK(definition.flags == 1U);
    CHECK(definition.description_res == 0x7f030002U);
    REQUIRE(definition.meta_data.size() == 5);
    CHECK(std::get<std::string>(definition.meta_data[0].value) == "ready");
    CHECK(std::get<bool>(definition.meta_data[1].value));
    CHECK(std::get<std::int32_t>(definition.meta_data[2].value) == 42);
    CHECK(std::get<AndroidManifestMetaDataValueReference>(definition.meta_data[3].value).resource_id == 0x7f05004fU);
    CHECK(std::get<AndroidManifestMetaDataResourceReference>(definition.meta_data[4].value).resource_id == 0x7f030001U);
}

TEST_CASE("binary AndroidManifest preserves receiver facts and own meta-data") {
    const auto facts = ogplay::loader::ParseAndroidBinaryManifest(PackageFactsManifest(false, true));
    CHECK(facts.application_process_name == "org.example.game:app");
    REQUIRE(facts.receiver_components.size() == 2);
    const auto& receiver = facts.receiver_components[0];
    CHECK(receiver.name == "org.example.game.Receiver");
    CHECK(receiver.enabled);
    CHECK(ogplay::loader::AndroidManifestReceiverExported(receiver));
    CHECK(receiver.process_name == "org.example.game:remote");
    CHECK(receiver.permission == "org.example.RECEIVER");
    REQUIRE(receiver.intent_filters.size() == 2);
    const auto& first = receiver.intent_filters[0];
    CHECK(first.actions == std::vector<std::string>{"org.example.RECEIVER"});
    CHECK(first.categories == std::vector<std::string>{"org.example.game"});
    CHECK_FALSE(first.has_data);
    CHECK(first.priority == -7);
    REQUIRE(first.label.has_value());
    CHECK(std::get<std::string>(*first.label) == "ready");
    CHECK(first.icon == 0x7f030001U);
    const auto& second = receiver.intent_filters[1];
    CHECK(second.actions == std::vector<std::string>{"OTHER"});
    CHECK(second.categories.empty());
    CHECK(second.has_data);
    CHECK(second.priority == 0);
    REQUIRE(second.label.has_value());
    CHECK(std::get<std::uint32_t>(*second.label) == 0x7f050001U);
    REQUIRE(receiver.meta_data.size() == 5);
    CHECK(std::get<std::string>(receiver.meta_data[0].value) == "ready");
    CHECK(std::get<bool>(receiver.meta_data[1].value));
    CHECK(std::get<std::int32_t>(receiver.meta_data[2].value) == 42);
    CHECK(std::get<ogplay::loader::AndroidManifestMetaDataValueReference>(receiver.meta_data[3].value).resource_id ==
          0x7f05004fU);
    CHECK(std::get<ogplay::loader::AndroidManifestMetaDataResourceReference>(receiver.meta_data[4].value).resource_id ==
          0x7f030001U);
    CHECK(facts.application_meta_data.size() == 5);
    const auto& disabled = facts.receiver_components[1];
    CHECK_FALSE(disabled.enabled);
    CHECK_FALSE(ogplay::loader::AndroidManifestReceiverExported(disabled));
    CHECK(disabled.process_name == "org.example.game:app");
    CHECK(disabled.permission == "org.example.APP");
    CHECK(disabled.meta_data.empty());
}

TEST_CASE("binary AndroidManifest boolean values use nonzero truth semantics") {
    for (const auto data : {0U, 1U, 2U, 0xffffffffU}) {
        CAPTURE(data);
        const auto facts = ogplay::loader::ParseAndroidBinaryManifest(PackageFactsManifest(false, true, data));
        const bool expected = data != 0U;
        CHECK(facts.application_enabled == expected);
        REQUIRE(facts.receiver_components.size() == 2U);
        const auto& receiver = facts.receiver_components.front();
        CHECK(receiver.enabled == expected);
        REQUIRE(receiver.exported.has_value());
        CHECK(*receiver.exported == expected);
        REQUIRE(receiver.meta_data.size() == 5U);
        CHECK(std::get<bool>(receiver.meta_data[1].value) == expected);
        REQUIRE(facts.application_meta_data.size() == 5U);
        CHECK(std::get<bool>(facts.application_meta_data[4].value) == expected);
    }
}

TEST_CASE("binary AndroidManifest visual getters use API19 defaults and coercion") {
    const auto literal = ogplay::loader::ParseAndroidBinaryManifest(
        Manifest(false, "org.example.game", 0x10, {{11, 13, 0x03, 13, 9}, {12, 0xffffffffU, 0x10, 7, 9}}));
    CHECK(literal.application_icon == 0);
    CHECK(std::get<std::string>(*literal.application_label) == "7");
}

TEST_CASE("binary AndroidManifest rejects identity type and structure errors") {
    CHECK_THROWS_WITH(static_cast<void>(ogplay::loader::ParseAndroidBinaryManifest(Manifest(false, "invalid"))),
                      "binary AndroidManifest package is invalid");
    CHECK_THROWS_WITH(
        static_cast<void>(ogplay::loader::ParseAndroidBinaryManifest(Manifest(false, "org.example.game", 0x03))),
        "binary AndroidManifest attribute versionCode is not an integer");

    auto mismatched_end = Manifest();
    mismatched_end[mismatched_end.size() - 4] = std::byte{4};
    CHECK(ogplay::loader::ParseAndroidBinaryManifest(mismatched_end).package == "org.example.game");
}

TEST_CASE("binary AndroidManifest rejects hostile string pool offsets") {
    auto invalid_offset = Manifest();
    Set32(invalid_offset, 8 + 28, 0xffffffffU);
    CHECK_THROWS_WITH(static_cast<void>(ogplay::loader::ParseAndroidBinaryManifest(invalid_offset)),
                      "binary XML element name string is unavailable");

    auto invalid_size = Manifest();
    Set32(invalid_size, 4, static_cast<std::uint32_t>(invalid_size.size() - 1U));
    CHECK_THROWS(static_cast<void>(ogplay::loader::ParseAndroidBinaryManifest(invalid_size)));
}

TEST_CASE("DVM-112 Manifest preserves service facts independently of activities") {
    using namespace ogplay::loader;
    const auto facts = ParseAndroidBinaryManifest(StartupManifest(
        std::nullopt, {
                          {"service", ".Local", {}, true, {{"android.intent.action.LOCAL", "example.CATEGORY"}}, true},
                          {"activity", ".Main", {}, true, {{std::string(kMain), std::string(kLauncher)}}},
                          {"service", "Disabled", {}, false, {{"android.intent.action.DISABLED"}}},
                          {"service", "other.package.Plain", {}, true, {}},
                      }));
    REQUIRE(facts.service_components.size() == 3);
    CHECK(facts.application_enabled);
    const auto& local = facts.service_components[0];
    CHECK(local.name == "org.example.game.Local");
    CHECK(local.enabled);
    REQUIRE(local.intent_filters.size() == 1);
    CHECK(local.intent_filters[0].actions == std::vector<std::string>{"android.intent.action.LOCAL"});
    CHECK(local.intent_filters[0].categories == std::vector<std::string>{"example.CATEGORY"});
    CHECK(local.intent_filters[0].has_data);
    CHECK(facts.service_components[1].name == "org.example.game.Disabled");
    CHECK_FALSE(facts.service_components[1].enabled);
    CHECK_FALSE(facts.service_components[1].intent_filters[0].has_data);
    CHECK(facts.service_components[2].name == "other.package.Plain");
    CHECK(facts.service_components[2].intent_filters.empty());
    REQUIRE(facts.activity_components.size() == 1);
    CHECK(ResolveLauncherComponent(facts).activity_class == "org.example.game.Main");
    CHECK_FALSE(facts.activity_components[0].intent_filters[0].has_data);
    const auto disabled = ParseAndroidBinaryManifest(
        StartupManifest(std::nullopt, {{"service", ".Local", {}, true, {{"android.intent.action.LOCAL"}}}}, false));
    CHECK_FALSE(disabled.application_enabled);
    CHECK(disabled.service_components[0].enabled);
}

TEST_CASE("Manifest preserves service query fields and owned metadata") {
    using namespace ogplay::loader;
    ComponentFixture explicit_service;
    explicit_service.tag = "service";
    explicit_service.name = ".Push";
    explicit_service.exported = true;
    explicit_service.process = ":push";
    explicit_service.permission = "org.example.PUSH";
    explicit_service.stop_with_task = true;
    explicit_service.isolated_process = true;
    explicit_service.service_meta_data = true;
    ComponentFixture inherited_service;
    inherited_service.tag = "service";
    inherited_service.name = ".Inherited";
    inherited_service.exported = true;
    inherited_service.single_user = true;
    const auto facts = ParseAndroidBinaryManifest(StartupManifest(std::nullopt, {explicit_service, inherited_service},
                                                                  true, std::nullopt, ":app", "org.example.APP"));
    REQUIRE(facts.service_components.size() == 2);
    const auto& push = facts.service_components[0];
    CHECK(push.name == "org.example.game.Push");
    CHECK(push.process_name == "org.example.game:push");
    CHECK(push.permission == "org.example.PUSH");
    CHECK(AndroidManifestServiceExported(push));
    CHECK(push.flags == 3U);
    REQUIRE(push.meta_data.size() == 5);
    CHECK(push.meta_data[0].name == "service.string");
    CHECK(std::get<std::string>(push.meta_data[0].value) == "ready");
    CHECK(std::get<bool>(push.meta_data[1].value));
    CHECK(std::get<std::int32_t>(push.meta_data[2].value) == 42);
    CHECK(std::get<AndroidManifestMetaDataValueReference>(push.meta_data[3].value).resource_id == 0x7f05004fU);
    CHECK(std::get<AndroidManifestMetaDataResourceReference>(push.meta_data[4].value).resource_id == 0x7f030001U);
    const auto& inherited = facts.service_components[1];
    CHECK(inherited.process_name == "org.example.game:app");
    CHECK(inherited.permission == "org.example.APP");
    CHECK_FALSE(AndroidManifestServiceExported(inherited));
    CHECK(inherited.flags == 0x40000000U);
    CHECK(inherited.meta_data.empty());
}

TEST_CASE("DVM-112 Manifest rejects invalid service names") {
    using namespace ogplay::loader;
    CHECK_THROWS_AS(static_cast<void>(ParseAndroidBinaryManifest(
                        StartupManifest(std::nullopt, {{"service", "Bad.Name", {}, true, {}}}))),
                    AndroidManifestStartupError);
    CHECK_THROWS_AS(
        static_cast<void>(ParseAndroidBinaryManifest(StartupManifest(std::nullopt, {{"service", "", {}, true, {}}}))),
        AndroidManifestStartupError);
}

TEST_CASE("DVM-121 Manifest retains application and Activity themes with alias inheritance") {
    ComponentFixture activity;
    activity.name = ".Main";
    activity.theme = 0x7f030001U;
    ComponentFixture alias;
    alias.tag = "activity-alias";
    alias.name = ".Alias";
    alias.target = ".Main";
    alias.theme = 0x7f030002U;  // API19 alias does not override the target theme.
    const auto facts =
        ogplay::loader::ParseAndroidBinaryManifest(StartupManifest(std::nullopt, {activity, alias}, true, 0x01030007U));
    CHECK(facts.application_theme == 0x01030007U);
    REQUIRE(facts.activity_components.size() == 2);
    CHECK(facts.activity_components[0].theme == 0x7f030001U);
    CHECK(facts.activity_components[1].theme == 0x7f030001U);
}

TEST_CASE("DVM-180 Manifest resolves Activity exported from attribute or intent-filters") {
    using namespace ogplay::loader;
    ComponentFixture filtered;
    filtered.name = ".Filtered";
    filtered.filters = {{std::string(kMain), std::string(kLauncher)}};
    ComponentFixture private_activity;
    private_activity.name = ".Private";
    ComponentFixture forced_private;
    forced_private.name = ".ForcedPrivate";
    forced_private.filters = {{std::string(kMain)}};
    forced_private.exported = false;
    ComponentFixture forced_public;
    forced_public.name = ".ForcedPublic";
    forced_public.exported = true;
    ComponentFixture alias_target;
    alias_target.name = ".Target";
    ComponentFixture alias;
    alias.tag = "activity-alias";
    alias.name = ".Alias";
    alias.target = ".Target";
    alias.filters = {{std::string(kLauncher)}};
    const auto facts = ParseAndroidBinaryManifest(StartupManifest(
        std::nullopt, {filtered, private_activity, forced_private, forced_public, alias_target, alias}));
    REQUIRE(facts.activity_components.size() == 6);
    CHECK(facts.activity_components[0].exported == true);
    CHECK(facts.activity_components[1].exported == false);
    CHECK(facts.activity_components[2].exported == false);
    CHECK(facts.activity_components[3].exported == true);
    CHECK(facts.activity_components[4].exported == false);
    CHECK(facts.activity_components[5].exported == true);
    CHECK(AndroidManifestActivityExported(facts.activity_components[0]));
    CHECK_FALSE(AndroidManifestActivityExported(facts.activity_components[1]));
}

TEST_CASE("DVM-195 Manifest keeps activity metadata separate from application and siblings") {
    ComponentFixture first;
    first.name = ".First";
    first.service_meta_data = true;
    ComponentFixture second;
    second.name = ".Second";
    const auto facts = ogplay::loader::ParseAndroidBinaryManifest(StartupManifest(std::nullopt, {first, second}));
    REQUIRE(facts.activity_components.size() == 2);
    REQUIRE(facts.activity_components[0].meta_data.size() == 5);
    CHECK(std::get<std::string>(facts.activity_components[0].meta_data[0].value) == "ready");
    CHECK(facts.activity_components[1].meta_data.empty());
    CHECK(facts.application_meta_data.empty());
}

TEST_CASE("DVM-223 Manifest retains launch modes and rejects invalid values") {
    ComponentFixture activity;
    activity.name = ".Result";
    for (const auto mode : {0U, 1U, 2U, 3U}) {
        activity.launch_mode = mode;
        const auto facts = ogplay::loader::ParseAndroidBinaryManifest(StartupManifest(std::nullopt, {activity}));
        REQUIRE(facts.activity_components.size() == 1);
        CHECK(facts.activity_components[0].launch_mode == mode);
    }
    activity.launch_mode = 4U;
    CHECK(ogplay::loader::ParseAndroidBinaryManifest(StartupManifest(std::nullopt, {activity}))
              .activity_components[0]
              .launch_mode == 4U);
}

TEST_CASE("DVM-250 Manifest task affinity inherits application and alias snapshots") {
    ComponentFixture activity;
    activity.name = ".Root";
    ComponentFixture alias;
    alias.tag = "activity-alias";
    alias.name = ".Alias";
    alias.target = ".Root";
    const auto parse = [&](std::optional<std::string> application_affinity = std::nullopt) {
        return ogplay::loader::ParseAndroidBinaryManifest(StartupManifest(
            std::nullopt, {activity, alias}, true, std::nullopt, std::nullopt, std::nullopt, application_affinity));
    };
    auto facts = parse();
    CHECK(facts.activity_components[0].task_affinity == "org.example.game");
    CHECK(facts.activity_components[1].task_affinity == "org.example.game");
    facts = parse("org.example.custom");
    CHECK(facts.activity_components[0].task_affinity == "org.example.custom");
    CHECK(facts.activity_components[1].task_affinity == "org.example.custom");
    activity.task_affinity = "";
    facts = parse("org.example.custom");
    CHECK(facts.activity_components[0].task_affinity == "");
    CHECK(facts.activity_components[1].task_affinity == "");
}

namespace {
std::vector<std::byte> TextureManifest(const std::vector<std::string>& declarations, const bool utf8,
                                       const bool nested = false,
                                       const std::optional<std::vector<Attribute>>& override_names = {}) {
    std::vector<std::string> strings{"manifest",
                                     "application",
                                     "package",
                                     "versionCode",
                                     "name",
                                     "supports-gl-texture",
                                     "http://schemas.android.com/apk/res/android",
                                     "org.example.game"};
    strings.insert(strings.end(), declarations.begin(), declarations.end());
    std::vector<std::byte> bytes;
    Append16(bytes, 0x0003);
    Append16(bytes, 8);
    Append32(bytes, 0);
    Append(bytes, StringPool(strings, utf8));
    Append(bytes, StartElement(0, {{2, 7, 0x03, 7}, {3, 0xffffffffU, 0x10, 1, 6}}));
    if (nested) Append(bytes, StartElement(1, {}));
    for (std::uint32_t index = 0; index < declarations.size(); ++index) {
        const auto attributes = (index == 1 ? override_names : std::nullopt)
                                    .value_or(std::vector<Attribute>{{4, 8U + index, 0x03, 8U + index, 6}});
        Append(bytes, StartElement(5, attributes));
        Append(bytes, EndElement(5));
    }
    if (!nested) Append(bytes, StartElement(1, {}));
    Append(bytes, EndElement(1));
    Append(bytes, EndElement(0));
    Set32(bytes, 4, static_cast<std::uint32_t>(bytes.size()));
    return bytes;
}
}  // namespace

TEST_CASE("binary Manifest preserves top-level supports-gl-texture declarations") {
    const std::vector<std::string> declarations{"GL_AMD_compressed_ATC_texture", "GL_UNKNOWN_format",
                                                "GL_IMG_texture_compression_pvrtc", "GL_AMD_compressed_ATC_texture"};
    for (const bool utf8 : {false, true}) {
        const auto facts = ogplay::loader::ParseAndroidBinaryManifest(TextureManifest(declarations, utf8));
        CHECK(facts.supported_gl_textures == std::vector<std::string>(declarations.begin(), declarations.begin() + 3));
        CHECK(ogplay::loader::ParseAndroidBinaryManifest(TextureManifest(declarations, utf8, true))
                  .supported_gl_textures.empty());
        CHECK(ogplay::loader::ParseAndroidBinaryManifest(TextureManifest({}, utf8)).supported_gl_textures.empty());
    }
}

TEST_CASE("binary Manifest skips malformed supports-gl-texture names and preserves other declarations") {
    const std::vector<std::string> declarations{"GL_AMD_compressed_ATC_texture", "", "GL_UNKNOWN_format",
                                                "GL_AMD_compressed_ATC_texture"};
    for (const bool utf8 : {false, true}) {
        for (const auto& attributes : {std::vector<Attribute>{}, std::vector<Attribute>{{4, 9, 0x03, 9, 6}},
                                       std::vector<Attribute>{{4, 9, 0x03, 9, 0xffffffffU}},
                                       std::vector<Attribute>{{4, 0xffffffffU, 0x10, 7, 6}},
                                       std::vector<Attribute>{{4, 0xffffffffU, 0x03, 0xfffffffeU, 6}}}) {
            const auto facts =
                ogplay::loader::ParseAndroidBinaryManifest(TextureManifest(declarations, utf8, false, attributes));
            CHECK(facts.package == "org.example.game");
            CHECK(facts.supported_gl_textures ==
                  std::vector<std::string>{"GL_AMD_compressed_ATC_texture", "GL_UNKNOWN_format"});
            REQUIRE(facts.diagnostics.size() == 1);
            CHECK(facts.diagnostics[0].find("skipped supports-gl-texture") != std::string::npos);
        }
    }
}

namespace {
struct Api19Manifest {
    std::vector<std::string> strings;
    std::vector<std::byte> nodes;

    std::uint32_t Index(std::string_view text) {
        const auto it = std::find(strings.begin(), strings.end(), text);
        if (it != strings.end()) return static_cast<std::uint32_t>(it - strings.begin());
        strings.emplace_back(text);
        return static_cast<std::uint32_t>(strings.size() - 1);
    }

    Attribute Value(std::string_view name, std::uint8_t type, std::uint32_t data) {
        return {Index(name), 0xffffffffU, type, data, Index("http://schemas.android.com/apk/res/android")};
    }

    Attribute Text(std::string_view name, std::string_view value) { return Value(name, 3, Index(value)); }

    void Start(std::string_view tag, std::vector<Attribute> attrs = {}) {
        Append(nodes, StartElement(Index(tag), attrs));
    }

    void End(std::string_view tag) { Append(nodes, EndElement(Index(tag))); }

    void Root(std::vector<Attribute> attrs = {}) {
        attrs.push_back({Index("package"), 0xffffffffU, 3, Index("org.example.game")});
        Start("manifest", attrs);
    }

    std::vector<std::byte> Bytes(const bool utf8 = true) {
        std::vector<std::byte> bytes;
        Append16(bytes, 3);
        Append16(bytes, 8);
        Append32(bytes, 0);
        Append(bytes, StringPool(strings, utf8));
        Append(bytes, nodes);
        Set32(bytes, 4, static_cast<std::uint32_t>(bytes.size()));
        return bytes;
    }
};

std::vector<std::byte> ManifestBoolArsc() {
    const auto types = StringPool({"bool"}, true);
    const auto keys = StringPool({"enabled"}, true);
    std::vector<std::byte> package(284);
    package[0] = std::byte{0};
    package[1] = std::byte{2};
    package[2] = std::byte{28};
    package[3] = std::byte{1};
    Set32(package, 8, 0x7f);
    const std::string name = "org.example.game";
    for (std::size_t i = 0; i < name.size(); ++i)
        package[12 + 2 * i] = static_cast<std::byte>(name[i]);
    Set32(package, 268, 284);
    Set32(package, 276, 284 + static_cast<std::uint32_t>(types.size()));
    Append(package, types);
    Append(package, keys);
    for (const auto sdk : {0U, 23U}) {
        std::vector<std::byte> type;
        Append16(type, 0x201);
        Append16(type, 56);
        Append32(type, 76);
        Append32(type, 1);
        Append32(type, 1);
        Append32(type, 60);
        Append32(type, 36);
        type.resize(56);
        type[44] = static_cast<std::byte>(sdk);
        Append32(type, 0);
        Append16(type, 8);
        Append16(type, 0);
        Append32(type, 0);
        Append16(type, 8);
        type.push_back(std::byte{0});
        type.push_back(std::byte{0x12});
        Append32(type, sdk == 0 ? 1 : 0);
        Append(package, type);
    }
    Set32(package, 4, static_cast<std::uint32_t>(package.size()));
    std::vector<std::byte> table;
    Append16(table, 2);
    Append16(table, 12);
    Append32(table, 0);
    Append32(table, 1);
    Append(table, StringPool({"unused"}, true));
    Append(table, package);
    Set32(table, 4, static_cast<std::uint32_t>(table.size()));
    return table;
}

std::vector<std::byte> TwoEntryApk(const std::vector<std::byte>& manifest, const std::vector<std::byte>& resources) {
    std::vector<std::byte> locals, central;
    for (const auto& [name, payload] :
         {std::pair{"AndroidManifest.xml", manifest}, std::pair{"resources.arsc", resources}}) {
        const auto zip = StoredApk(name, payload);
        const auto local_size = 30U + std::string_view(name).size() + payload.size();
        const auto central_size = 46U + std::string_view(name).size();
        const auto old = central.size();
        central.insert(central.end(), zip.begin() + static_cast<std::ptrdiff_t>(local_size),
                       zip.begin() + static_cast<std::ptrdiff_t>(local_size) +
                           static_cast<std::ptrdiff_t>(central_size));
        Set32(central, old + 42, static_cast<std::uint32_t>(locals.size()));
        locals.insert(locals.end(), zip.begin(), zip.begin() + static_cast<std::ptrdiff_t>(local_size));
    }
    const auto offset = static_cast<std::uint32_t>(locals.size());
    Append(locals, central);
    Append32(locals, 0x06054b50);
    Append16(locals, 0);
    Append16(locals, 0);
    Append16(locals, 2);
    Append16(locals, 2);
    Append32(locals, static_cast<std::uint32_t>(central.size()));
    Append32(locals, offset);
    Append16(locals, 0);
    return locals;
}
}  // namespace

TEST_CASE("WU11 metadata uses KitKat Bundle types and overwrite precedence") {
    using namespace ogplay::loader;
    Api19Manifest xml;
    xml.Root();
    xml.Start("application");
    const auto metadata = [&](std::string_view key, std::uint8_t type, std::uint32_t data, bool resource = false) {
        auto attrs = std::vector{xml.Text("name", key), xml.Value("value", type, data)};
        if (resource) attrs.push_back(xml.Value("resource", 1, 0x7f010000));
        xml.Start("meta-data", attrs);
        xml.End("meta-data");
    };
    metadata("float", 4, 0x40066666);
    metadata("color", 0x1c, 0xff123456);
    metadata("ignored", 5, 0x00000e02);
    metadata("ignored", 6, 0x00010000);
    metadata("", 0x10, 1);
    metadata("", 0x10, 2);
    metadata("precedence", 4, 0x40400000, true);
    xml.End("application");
    xml.End("manifest");
    const auto facts = ParseAndroidBinaryManifest(xml.Bytes());
    REQUIRE(facts.application_meta_data.size() == 4);
    CHECK(std::get<float>(facts.application_meta_data[0].value) == doctest::Approx(2.1F));
    CHECK(std::get<std::int32_t>(facts.application_meta_data[1].value) == static_cast<std::int32_t>(0xff123456U));
    CHECK(facts.application_meta_data[2].name.empty());
    CHECK(std::get<std::int32_t>(facts.application_meta_data[2].value) == 2);
    CHECK(std::get<AndroidManifestMetaDataResourceReference>(facts.application_meta_data[3].value).resource_id ==
          0x7f010000);
    CHECK(facts.diagnostics.size() == 2);
}

TEST_CASE("WU11 Manifest resolves typed references using API19 resources") {
    using namespace ogplay::loader;
    Api19Manifest xml;
    xml.Root({xml.Value("versionCode", 1, 0x7f020000), xml.Value("versionName", 1, 0x7f030000)});
    xml.Start("application", {xml.Value("enabled", 1, 0x7f010000), xml.Value("label", 0x10, 42)});
    xml.Start("meta-data",
              {xml.Text("name", "resolved"), xml.Value("value", 1, 0x7f040000), xml.Value("resource", 1, 0)});
    xml.End("meta-data");
    xml.End("application");
    xml.End("manifest");
    const AndroidManifestResourceResolver resolver =
        [](std::uint32_t id) -> std::optional<AndroidManifestResourceValue> {
        if (id == 0x7f010000) return AndroidManifestResourceValue{0x10, 2, {}};
        if (id == 0x7f020000) return AndroidManifestResourceValue{0x1c, 0, {}};
        if (id == 0x7f030000) return AndroidManifestResourceValue{3, 0, ""};
        if (id == 0x7f040000) return AndroidManifestResourceValue{4, 0x40400000, {}};
        return std::nullopt;
    };
    const auto facts = ParseAndroidBinaryManifest(xml.Bytes(), resolver);
    CHECK(facts.application_enabled);
    CHECK(facts.version_code == 0);
    CHECK(facts.version_name == "");
    CHECK(std::get<std::string>(*facts.application_label) == "42");
    CHECK(std::get<float>(facts.application_meta_data[0].value) == 3.0F);
    CHECK_THROWS_WITH_AS(
        static_cast<void>(ParseAndroidBinaryManifest(
            xml.Bytes(), [](auto) { return std::optional{AndroidManifestResourceValue{1, 0x7f010000, {}}}; })),
        "binary AndroidManifest resource cycle or depth limit", std::runtime_error);
    CHECK_THROWS_AS(static_cast<void>(ParseAndroidBinaryManifest(
                        xml.Bytes(), [](auto) { return std::optional<AndroidManifestResourceValue>{}; })),
                    std::runtime_error);

    for (const auto type : {0x10U, 0x11U, 0x12U, 0x1fU}) {
        for (const auto number : {0U, 2U}) {
            Api19Manifest scalar;
            scalar.Root();
            scalar.Start("application", {scalar.Value("enabled", static_cast<std::uint8_t>(type), number)});
            scalar.End("application");
            scalar.End("manifest");
            CHECK(ParseAndroidBinaryManifest(scalar.Bytes()).application_enabled == (number != 0));
        }
    }
    for (const auto text : {"true", "TRUE", "1", "True", "false", "0"}) {
        Api19Manifest scalar;
        scalar.Root();
        scalar.Start("application", {scalar.Text("enabled", text)});
        scalar.End("application");
        scalar.End("manifest");
        const auto expected =
            std::string_view(text) == "true" || std::string_view(text) == "TRUE" || std::string_view(text) == "1";
        CHECK(ParseAndroidBinaryManifest(scalar.Bytes()).application_enabled == expected);
    }
    for (const auto type : {0U, 4U}) {
        Api19Manifest scalar;
        scalar.Root();
        scalar.Start("application",
                     {scalar.Value("enabled", static_cast<std::uint8_t>(type), type == 4U ? 0x3f800000U : 0U)});
        scalar.End("application");
        scalar.End("manifest");
        CHECK(ParseAndroidBinaryManifest(scalar.Bytes()).application_enabled == (type == 0));
    }
    for (const auto& [text, expected] : std::vector<std::pair<std::string, std::int32_t>>{
             {"17", 17}, {"017", 15}, {"0x17", 23}, {"#17", 23}, {"-7", -7}, {"+7", 7}}) {
        Api19Manifest scalar;
        scalar.Root();
        scalar.Start("application");
        scalar.Start("receiver", {scalar.Text("name", ".R")});
        scalar.Start("intent-filter", {scalar.Text("priority", text)});
        scalar.End("intent-filter");
        scalar.End("receiver");
        scalar.End("application");
        scalar.End("manifest");
        CHECK(ParseAndroidBinaryManifest(scalar.Bytes()).receiver_components[0].intent_filters[0].priority == expected);
    }
    for (const auto text : {" 17", "17 ", "-2147483648", "0x80000000"}) {
        Api19Manifest scalar;
        scalar.Root();
        scalar.Start("application");
        scalar.Start("receiver", {scalar.Text("name", ".R")});
        scalar.Start("intent-filter", {scalar.Text("priority", text)});
        scalar.End("intent-filter");
        scalar.End("receiver");
        scalar.End("application");
        scalar.End("manifest");
        CHECK_THROWS_AS(static_cast<void>(ParseAndroidBinaryManifest(scalar.Bytes())), std::runtime_error);
    }
    CHECK_THROWS_AS(static_cast<void>(ParseAndroidBinaryManifest(
                        xml.Bytes(), [](auto) { return std::optional{AndroidManifestResourceValue{3, 0, {}}}; })),
                    std::runtime_error);
    const AndroidManifestResourceResolver null_resource = [](auto) {
        return std::optional{AndroidManifestResourceValue{0, 0, {}}};
    };
    Api19Manifest nulls;
    nulls.Root({nulls.Value("versionName", 1, 0x7f010000)});
    nulls.Start("application", {nulls.Value("name", 1, 0x7f010000)});
    nulls.End("application");
    nulls.End("manifest");
    const auto null_facts = ParseAndroidBinaryManifest(nulls.Bytes(), null_resource);
    CHECK_FALSE(null_facts.version_name);
    CHECK(null_facts.application_class == "android.app.Application");
    Api19Manifest null_key;
    null_key.Root();
    null_key.Start("application");
    null_key.Start("meta-data", {null_key.Value("name", 1, 0x7f010000), null_key.Value("value", 0x10, 7)});
    null_key.End("meta-data");
    null_key.End("application");
    null_key.End("manifest");
    CHECK_THROWS_AS(static_cast<void>(ParseAndroidBinaryManifest(null_key.Bytes(), null_resource)), std::runtime_error);
    Api19Manifest duplicate_null;
    duplicate_null.Root();
    duplicate_null.Start("application",
                         {duplicate_null.Value("enabled", 0, 0), duplicate_null.Value("enabled", 0x12, 1)});
    duplicate_null.End("application");
    duplicate_null.End("manifest");
    CHECK(ParseAndroidBinaryManifest(duplicate_null.Bytes()).application_enabled);
    Api19Manifest apk_xml;
    apk_xml.Root();
    apk_xml.Start("application", {apk_xml.Value("enabled", 1, 0x7f010000)});
    apk_xml.End("application");
    apk_xml.End("manifest");
    const auto apk = TwoEntryApk(apk_xml.Bytes(), ManifestBoolArsc());
    CHECK(ReadAndroidManifest(apk, ParseApkArchive(apk)).application_enabled);
}

TEST_CASE("WU11 repeated components preserve declarations and independent filters") {
    using namespace ogplay::loader;
    for (const auto tag : {"activity", "service", "receiver"}) {
        CAPTURE(tag);
        Api19Manifest xml;
        xml.Root();
        xml.Start("application");
        for (const bool enabled : {true, false}) {
            xml.Start(tag, {xml.Text("name", ".Repeated"), xml.Value("enabled", 0x12, enabled ? 1U : 0U)});
            xml.Start("intent-filter");
            xml.Start("action", {xml.Text("name", enabled ? "FIRST" : "SECOND")});
            xml.End("action");
            xml.End("intent-filter");
            xml.End(tag);
        }
        xml.End("application");
        xml.End("manifest");
        const auto facts = ParseAndroidBinaryManifest(xml.Bytes());
        const auto check = [](const auto& components) {
            REQUIRE(components.size() == 2);
            CHECK(components[0].enabled);
            CHECK_FALSE(components[1].enabled);
            CHECK(components[0].intent_filters[0].actions == std::vector<std::string>{"FIRST"});
            CHECK(components[1].intent_filters[0].actions == std::vector<std::string>{"SECOND"});
        };
        if (std::string_view(tag) == "activity") check(facts.activity_components);
        if (std::string_view(tag) == "service") check(facts.service_components);
        if (std::string_view(tag) == "receiver") check(facts.receiver_components);
    }
}

TEST_CASE("WU11 Manifest identity preserves zero absent and empty defaults") {
    using namespace ogplay::loader;
    for (const bool version : {false, true}) {
        Api19Manifest xml;
        std::vector<Attribute> attrs{xml.Text("versionName", "")};
        if (version) attrs.push_back(xml.Value("versionCode", 0x10, 0));
        xml.Root(attrs);
        xml.Start("application");
        xml.End("application");
        xml.End("manifest");
        const auto facts = ParseAndroidBinaryManifest(xml.Bytes());
        CHECK(facts.version_code == 0);
        CHECK(facts.version_name == "");
        CHECK_FALSE(facts.min_sdk);
        CHECK_FALSE(facts.target_sdk);
    }
    for (const auto min : {0U, 5U}) {
        Api19Manifest xml;
        xml.Root();
        xml.Start("uses-sdk", {xml.Value("minSdkVersion", 0x10, min)});
        xml.End("uses-sdk");
        xml.Start("uses-sdk", {xml.Value("minSdkVersion", 0x10, min), xml.Value("targetSdkVersion", 0x10, 0)});
        xml.End("uses-sdk");
        xml.End("manifest");
        const auto facts = ParseAndroidBinaryManifest(xml.Bytes());
        CHECK(facts.min_sdk == min);
        CHECK(facts.target_sdk == 0);
    }
    Api19Manifest xml;
    xml.Root();
    xml.Start("uses-sdk", {xml.Value("minSdkVersion", 0x10, 5)});
    xml.End("uses-sdk");
    xml.End("manifest");
    CHECK(ParseAndroidBinaryManifest(xml.Bytes()).target_sdk == 5);
    CHECK_NOTHROW(static_cast<void>(ParseAndroidBinaryManifest(Manifest(false, ".org..example."))));
    CHECK_NOTHROW(static_cast<void>(ParseAndroidBinaryManifest(Manifest(false, "android"))));
    Api19Manifest referenced_package;
    auto package = referenced_package.Value("package", 1, 0x7f010000);
    package.namespace_index = 0xffffffffU;
    referenced_package.Start("manifest", {package});
    referenced_package.End("manifest");
    CHECK_THROWS_AS(static_cast<void>(ParseAndroidBinaryManifest(
                        referenced_package.Bytes(),
                        [](auto) { return std::optional{AndroidManifestResourceValue{3, 0, "org.example.game"}}; })),
                    std::runtime_error);
}

TEST_CASE("WU11 Manifest takes first attribute and skips duplicate application subtree") {
    using namespace ogplay::loader;
    Api19Manifest xml;
    xml.Root();
    xml.Start("application", {xml.Value("enabled", 0x12, 0), xml.Value("enabled", 0x12, 1)});
    xml.End("application");
    xml.Start("application", {xml.Text("name", "")});
    xml.Start("service");  // Missing required name must not escape skipCurrentTag.
    xml.End("service");
    xml.End("application");
    xml.End("manifest");
    const auto facts = ParseAndroidBinaryManifest(xml.Bytes());
    CHECK_FALSE(facts.application_enabled);
    CHECK(facts.application_class == "android.app.Application");
    CHECK(facts.service_components.empty());
}

TEST_CASE("WU11 Manifest ignores missing permission requests and preserves empty names") {
    using namespace ogplay::loader;
    Api19Manifest xml;
    xml.Root();
    xml.Start("uses-permission");
    xml.End("uses-permission");
    xml.Start("uses-permission", {xml.Value("name", 0x10, 42)});
    xml.End("uses-permission");
    xml.Start("uses-permission", {xml.Text("name", "")});
    xml.End("uses-permission");
    xml.Start("uses-permission", {xml.Text("name", "old"), xml.Value("maxSdkVersion", 0x10, 18)});
    xml.End("uses-permission");
    xml.Start("uses-permission", {xml.Text("name", "new"), xml.Value("maxSdkVersion", 0x10, 19)});
    xml.End("uses-permission");
    xml.End("manifest");
    CHECK(ParseAndroidBinaryManifest(xml.Bytes()).requested_permissions == std::vector<std::string>{"", "new"});
}

TEST_CASE("WU11 Manifest preserves permission declarations and API19 integer domains") {
    using namespace ogplay::loader;
    Api19Manifest xml;
    xml.Root();
    for (const auto level : {0x102U, 4U}) {
        xml.Start("permission", {xml.Text("name", ".PERM"), xml.Value("protectionLevel", 0x10, level),
                                 xml.Value("permissionFlags", 0x10, 0xff), xml.Text("description", "ignored")});
        xml.End("permission");
    }
    xml.End("manifest");
    const auto facts = ParseAndroidBinaryManifest(xml.Bytes());
    REQUIRE(facts.defined_permissions.size() == 2);
    CHECK(facts.defined_permissions[0].protection_level == 0x102);
    CHECK(facts.defined_permissions[1].protection_level == 4);
    CHECK(facts.defined_permissions[0].flags == 0xff);
    CHECK(facts.defined_permissions[0].description_res == 0);
    CHECK_FALSE(facts.defined_permissions[0].nonlocalized_description);
    Api19Manifest bad;
    bad.Root();
    bad.Start("permission", {bad.Text("name", ".PERM"), bad.Value("protectionLevel", 0x10, 0x10)});
    bad.End("permission");
    bad.End("manifest");
    CHECK_THROWS(static_cast<void>(ParseAndroidBinaryManifest(bad.Bytes())));
}

TEST_CASE("WU11 Manifest ignores unused malformed strings and preserves Java code units") {
    using namespace ogplay::loader;
    Api19Manifest xml;
    xml.Root();
    xml.Start("application");
    xml.Start("meta-data", {xml.Text("name", "nul"), xml.Text("value", std::string("a\0b", 3))});
    xml.End("meta-data");
    xml.End("application");
    xml.End("manifest");
    xml.Index(std::string("\xff", 1));
    auto bytes = xml.Bytes();
    const auto facts = ParseAndroidBinaryManifest(bytes);
    CHECK(std::get<std::string>(facts.application_meta_data[0].value) == std::string("a\0b", 3));
    // Corrupt the used value's UTF8 declared UTF16 length: stringAt returns null.
    const auto value_index = xml.Index(std::string("a\0b", 3));
    const auto pool_start = 8U;
    const auto data_start = 28U + xml.strings.size() * 4U;
    const auto table = pool_start + 28 + value_index * 4;
    std::size_t relative{};
    for (unsigned i = 0; i < 4; ++i)
        relative |= std::to_integer<std::size_t>(bytes[table + i]) << (i * 8U);
    bytes[pool_start + data_start + relative] = std::byte{99};
    CHECK(std::holds_alternative<AndroidManifestMetaDataNullString>(
        ParseAndroidBinaryManifest(bytes).application_meta_data[0].value));
}

TEST_CASE("WU11 Manifest uses declared headers and stops at root end") {
    using namespace ogplay::loader;
    Api19Manifest xml;
    xml.Root();
    xml.Start("application");
    xml.End("application");
    xml.End("manifest");
    auto bytes = xml.Bytes();
    // Unknown preamble chunk before the pool; extra bytes after the XML document.
    const std::vector<std::byte> unknown{std::byte{0x77}, std::byte{0x77}, std::byte{8}, std::byte{0},
                                         std::byte{8},    std::byte{0},    std::byte{0}, std::byte{0}};
    bytes.insert(bytes.begin() + 8, unknown.begin(), unknown.end());
    Set32(bytes, 4, static_cast<std::uint32_t>(bytes.size()));
    bytes.push_back(std::byte{0xff});
    CHECK(ParseAndroidBinaryManifest(bytes).package == "org.example.game");
    // A second malformed document after the first root is not consumed.
    auto second = xml.Bytes();
    second.insert(second.end(), unknown.begin(), unknown.end());
    Set32(second, 4, static_cast<std::uint32_t>(second.size()));
    CHECK(ParseAndroidBinaryManifest(second).package == "org.example.game");
}

TEST_CASE("WU11 Manifest accepts extended node headers and ignores typed layout fields") {
    using namespace ogplay::loader;
    Api19Manifest xml;
    xml.Root();
    xml.Start("application");
    xml.End("application");
    xml.End("manifest");
    // Widen all start/end headers, preserving extensions at the declared offset.
    std::vector<std::byte> nodes;
    for (std::size_t at = 0; at < xml.nodes.size();) {
        std::uint32_t size{};
        for (unsigned i = 0; i < 4; ++i)
            size |= std::to_integer<std::uint32_t>(xml.nodes[at + 4 + i]) << (i * 8U);
        std::vector<std::byte> node(xml.nodes.begin() + static_cast<std::ptrdiff_t>(at),
                                    xml.nodes.begin() + static_cast<std::ptrdiff_t>(at + size));
        node.insert(node.begin() + 16, 4, std::byte{0});
        node[2] = std::byte{20};
        Set32(node, 4, size + 4);
        if (at == 0) {
            // Package remains a STRING; Res_value.size/res0 do not change its value.
            node[20 + 20 + 12] = std::byte{1};
            node[20 + 20 + 14] = std::byte{255};
        }
        Append(nodes, node);
        at += size;
    }
    // Namespaces, text, resource map and unknown nodes carry larger headers.
    const auto extra = [&](std::uint16_t type, std::uint16_t header, std::size_t ext) {
        std::vector<std::byte> node;
        Append16(node, type);
        Append16(node, header);
        Append32(node, static_cast<std::uint32_t>(header + ext));
        node.resize(header + ext, std::byte{0xff});
        return node;
    };
    const auto root_end = nodes.size() - 28;
    std::vector<std::byte> middle;
    Append(middle, extra(0x100, 20, 8));
    Append(middle, extra(0x101, 20, 8));
    Append(middle, extra(0x104, 20, 12));
    Append(middle, extra(0x180, 12, 4));
    Append(middle, extra(0x177, 20, 0));
    nodes.insert(nodes.begin() + static_cast<std::ptrdiff_t>(root_end), middle.begin(), middle.end());
    xml.nodes = nodes;
    CHECK(ParseAndroidBinaryManifest(xml.Bytes()).package == "org.example.game");
    xml.nodes[2] = std::byte{12};  // Actual short node header remains invalid.
    CHECK_THROWS(static_cast<void>(ParseAndroidBinaryManifest(xml.Bytes())));
}

TEST_CASE("WU11 Manifest typed attribute lookup follows resource ids and first match") {
    using namespace ogplay::loader;
    Api19Manifest xml;
    xml.Root();
    // Raw spelling differs: the resource map is authoritative for TypedArray.
    xml.Start("application", {xml.Value("otherSpelling", 0x12, 0), xml.Value("enabled", 0x12, 1)});
    xml.End("application");
    xml.End("manifest");
    const auto enabled = xml.Index("enabled");
    const auto other = xml.Index("otherSpelling");
    auto bytes = xml.Bytes();
    const auto pool_size = StringPool(xml.strings, true).size();
    std::vector<std::byte> map;
    Append16(map, 0x180);
    Append16(map, 8);
    Append32(map, static_cast<std::uint32_t>(8 + xml.strings.size() * 4));
    for (std::size_t i = 0; i < xml.strings.size(); ++i)
        Append32(map, i == enabled || i == other ? 0x0101000e : 0);
    bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(8 + pool_size), map.begin(), map.end());
    Set32(bytes, 4, static_cast<std::uint32_t>(bytes.size()));
    CHECK_FALSE(ParseAndroidBinaryManifest(bytes).application_enabled);
}

TEST_CASE("WU11 Manifest permission registration takes first valid group definition") {
    using namespace ogplay::loader;
    Api19Manifest xml;
    xml.Root();
    for (const auto group : {"missing", "org.example.GROUP", "org.example.GROUP"}) {
        xml.Start("permission", {xml.Text("name", ".PERM"), xml.Text("permissionGroup", group)});
        xml.End("permission");
    }
    // Groups can be declared after permissions; platform groups also exist at API19.
    xml.Start("permission-group", {xml.Text("name", "org.example.GROUP")});
    xml.End("permission-group");
    xml.Start("permission",
              {xml.Text("name", ".SYSTEM"), xml.Text("permissionGroup", "android.permission-group.CAMERA")});
    xml.End("permission");
    xml.End("manifest");
    const auto facts = ParseAndroidBinaryManifest(xml.Bytes());
    REQUIRE(facts.defined_permissions.size() == 4);
    CHECK_FALSE(facts.defined_permissions[0].registered);
    CHECK(facts.defined_permissions[1].registered);
    CHECK_FALSE(facts.defined_permissions[2].registered);
    CHECK(facts.defined_permissions[3].registered);
}

TEST_CASE("WU11 Manifest UTF pools preserve noncanonical code units and ignore terminator bytes") {
    using namespace ogplay::loader;
    for (const bool utf8 : {false, true}) {
        Api19Manifest xml;
        xml.Root();
        xml.Start("application");
        xml.Start("meta-data", {xml.Text("name", "key"), xml.Text("value", utf8 ? "xy" : "q")});
        xml.End("meta-data");
        xml.End("application");
        xml.End("manifest");
        const auto index = xml.Index(utf8 ? "xy" : "q");
        auto bytes = xml.Bytes(utf8);
        const auto table = 8 + 28 + index * 4;
        std::uint32_t relative{};
        for (unsigned i = 0; i < 4; ++i)
            relative |= std::to_integer<std::uint32_t>(bytes[table + i]) << (i * 8U);
        const auto at = 8 + 28 + xml.strings.size() * 4 + relative;
        std::string expected;
        if (utf8) {
            bytes[at] = std::byte{1};  // Declared UTF16 length, byte length stays 2.
            bytes[at + 2] = std::byte{0xc0};
            bytes[at + 3] = std::byte{0x80};
            bytes[at + 4] = std::byte{0x7f};  // Per-string terminator is not checked.
            expected.assign(1, '\0');
        } else {
            bytes[at + 2] = std::byte{0};
            bytes[at + 3] = std::byte{0xd8};
            bytes[at + 4] = std::byte{1};  // Nonzero per-string UTF16 terminator.
            expected = "\xed\xa0\x80";
        }
        CHECK(std::get<std::string>(ParseAndroidBinaryManifest(bytes).application_meta_data[0].value) == expected);
    }
}

TEST_CASE("WU11 Manifest ignores unknown blocks with a valid node header") {
    using namespace ogplay::loader;
    Api19Manifest xml;
    xml.Root();
    std::vector<std::byte> node;
    Append16(node, 0x177);
    Append16(node, 16);
    Append32(node, 16);
    node.resize(16);
    Append(xml.nodes, node);
    xml.Start("application");
    xml.End("application");
    xml.End("manifest");
    CHECK(ParseAndroidBinaryManifest(xml.Bytes()).package == "org.example.game");
}

TEST_CASE("WU11 Manifest skips a second application including invalid descendants") {
    using namespace ogplay::loader;
    Api19Manifest xml;
    xml.Root();
    xml.Start("application");
    xml.End("application");
    xml.Start("application", {xml.Text("name", "")});
    xml.Start("service");
    xml.End("service");
    xml.End("application");
    xml.End("manifest");
    const auto facts = ParseAndroidBinaryManifest(xml.Bytes());
    CHECK(facts.application_class == "android.app.Application");
    CHECK(facts.service_components.empty());
}

TEST_CASE("WU11 Manifest honors XML header size and retains aligned chunk bounds") {
    using namespace ogplay::loader;
    Api19Manifest xml;
    xml.Root();
    xml.Start("application");
    xml.End("application");
    xml.End("manifest");
    auto bytes = xml.Bytes();
    bytes.insert(bytes.begin() + 8, 4, std::byte{0});
    bytes[2] = std::byte{12};
    Set32(bytes, 4, static_cast<std::uint32_t>(bytes.size()));
    CHECK(ParseAndroidBinaryManifest(bytes).package == "org.example.game");
    bytes[2] = std::byte{10};
    CHECK_THROWS(static_cast<void>(ParseAndroidBinaryManifest(bytes)));
}

TEST_CASE("WU11 Manifest null decoded typed strings use getter defaults") {
    using namespace ogplay::loader;
    Api19Manifest xml;
    xml.Root();
    // The string index is absent: stringAt returns null, not an empty string.
    xml.Start("application", {xml.Value("enabled", 3, 0xfffffffe)});
    xml.Start("activity", {xml.Text("name", ".A"), xml.Value("launchMode", 3, 0xfffffffe)});
    xml.End("activity");
    xml.End("application");
    xml.End("manifest");
    const auto facts = ParseAndroidBinaryManifest(xml.Bytes());
    CHECK(facts.application_enabled);
    REQUIRE(facts.activity_components.size() == 1);
    CHECK(facts.activity_components[0].launch_mode == 0);
}

TEST_CASE("WU11 Manifest raw package attribute name does not convert UTF8") {
    Api19Manifest xml;
    xml.Root();
    xml.Start("application");
    xml.End("application");
    xml.End("manifest");
    auto bytes = xml.Bytes();
    // package is the first string in this fixture; its raw name remains ASCII.
    const auto at = 8 + 28 + xml.strings.size() * 4;
    bytes[at] = std::byte{99};  // string8At ignores the declared UTF16 length.
    CHECK(ogplay::loader::ParseAndroidBinaryManifest(bytes).package == "org.example.game");
}

TEST_CASE("DVM-354 Manifest largeHeap preserves true false and absent application facts") {
    for (const auto value : {-1, 0, 1}) {
        Api19Manifest xml;
        xml.Root();
        if (value < 0)
            xml.Start("application");
        else
            xml.Start("application", {xml.Value("largeHeap", 0x12, static_cast<std::uint32_t>(value))});
        xml.End("application");
        xml.End("manifest");
        CHECK(ogplay::loader::ParseAndroidBinaryManifest(xml.Bytes()).application_large_heap == (value == 1));
    }
}
