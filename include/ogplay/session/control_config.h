#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

// Merges built-in default -> template -> profile -> user override. The user's template id
// replaces the profile's. Later bindings replace earlier ones that share a source in the same
// mode or in "*"; zones replace by name. Every referenced zone must be placed.
[[nodiscard]] input::ControlConfig BuildControlConfig(
    const ControlLayer* profile, const ControlLayer* user,
    const std::filesystem::path& templates_directory);

}  // namespace ogplay::session
