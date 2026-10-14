#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/core/json.h"
#include "ogplay/input/control_mapper.h"

namespace ogplay::session {

// A binding entry from a configuration layer; unbind entries remove earlier bindings that
// share a source in the same mode (or in the every-mode "*").
struct ControlBindingEntry final {
    input::Binding binding;
    bool unbind{};
};

// Profile [input] table or a user override file.
struct ControlLayer final {
    std::optional<std::string> template_id;
    std::vector<input::ZoneSpec> zones;
    std::vector<ControlBindingEntry> bindings;
};

// data/input/templates/<id>.toml: bindings plus the zone names a game must place.
struct ControlTemplate final {
    std::string id;
    std::string summary;
    std::vector<std::string> zones;
    std::vector<ControlBindingEntry> bindings;
};

[[nodiscard]] ControlTemplate LoadControlTemplateText(std::string_view text,
                                                      std::string_view expected_id);
[[nodiscard]] ControlTemplate LoadControlTemplate(const std::filesystem::path& path);
[[nodiscard]] ControlLayer LoadControlOverrideText(std::string_view text);
[[nodiscard]] ControlLayer LoadControlOverride(const std::filesystem::path& path);
// A missing file is an empty user layer: the key editor creates it on first save.
[[nodiscard]] ControlLayer LoadControlOverrideIfPresent(const std::filesystem::path& path);
// Sorted by id; a missing directory yields no templates.
[[nodiscard]] std::vector<ControlTemplate> ListControlTemplates(
    const std::filesystem::path& templates_directory);

// User layer as override TOML (schema 1); coordinates are rounded to 0.01 guest pixels.
[[nodiscard]] std::string FormatControlLayer(const ControlLayer& layer);
// Verifies the text parses back, then replaces the file through a sibling temporary file.
void SaveControlLayer(const std::filesystem::path& path, const ControlLayer& layer);

// JSON mirrors the TOML shape: {template?, zone: [...], binding: [...]}.
[[nodiscard]] ControlLayer DecodeControlLayerJson(core::JsonValue value);
[[nodiscard]] core::JsonWriter::Value WriteControlLayerJson(core::JsonWriter& writer,
                                                            const ControlLayer& layer);
[[nodiscard]] core::JsonWriter::Value WriteControlTemplateJson(core::JsonWriter& writer,
                                                               const ControlTemplate& item);

// Merges built-in default -> template -> profile -> user override. The user's template id
// replaces the profile's. Later bindings replace earlier ones that share a source in the same
// mode or in "*"; zones replace by name. Every referenced zone must be placed.
[[nodiscard]] input::ControlConfig BuildControlConfig(
    const ControlLayer* profile, const ControlLayer* user,
    const std::filesystem::path& templates_directory);

}  // namespace ogplay::session
