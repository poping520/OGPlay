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

// User input.toml layer merged with built-in defaults.
struct ControlLayer final {
    std::vector<input::ZoneSpec> zones;
    std::vector<ControlBindingEntry> bindings;
};

[[nodiscard]] ControlLayer LoadControlOverrideText(std::string_view text);
[[nodiscard]] ControlLayer LoadControlOverride(const std::filesystem::path& path);
// A missing file is an empty user layer: the key editor creates it on first save.
[[nodiscard]] ControlLayer LoadControlOverrideIfPresent(const std::filesystem::path& path);
// User layer as override TOML (schema 1); coordinates are rounded to 0.01 guest pixels.
[[nodiscard]] std::string FormatControlLayer(const ControlLayer& layer);
// Verifies the text parses back, then replaces the file through a sibling temporary file.
void SaveControlLayer(const std::filesystem::path& path, const ControlLayer& layer);

// JSON mirrors the TOML shape: {zone: [...], binding: [...]}.
[[nodiscard]] ControlLayer DecodeControlLayerJson(core::JsonValue value);
[[nodiscard]] core::JsonWriter::Value WriteControlLayerJson(core::JsonWriter& writer,
                                                            const ControlLayer& layer);
// Built-in defaults plus the user layer. Later bindings replace conflicts; an unbind
// removes a built-in binding. Every referenced zone must be placed by the user.
[[nodiscard]] input::ControlConfig BuildControlConfig(const ControlLayer& user);

}  // namespace ogplay::session
