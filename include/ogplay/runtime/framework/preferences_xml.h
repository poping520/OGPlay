#pragma once

// SharedPreferences on-disk format (ADR-0020, design 03 §6). The platform
// stores them as XML under /data/data/<pkg>/shared_prefs/, and some titles
// read that file directly instead of going through the API, so the file
// view and the API view have to be the same fact.
//
// This is a checked reader for exactly the subset the platform writes, not
// a general XML parser: entities, DTDs and namespaces are rejected.

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

#include "ogplay/runtime/vfs/vfs.h"

namespace ogplay::runtime {

// float is here because the platform writes it; OGPlay exposes no getFloat
// yet, but a real prefs file must still round-trip rather than fail to load.
using PreferenceValue =
    std::variant<bool, std::int32_t, std::int64_t, float, std::string>;
using PreferenceMap = std::map<std::string, PreferenceValue, std::less<>>;

class PreferencesXmlError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// /data/data/<package>/shared_prefs/<name>.xml
[[nodiscard]] std::string PreferencesGuestPath(std::string_view package,
                                               std::string_view name);

[[nodiscard]] std::string RenderPreferencesXml(const PreferenceMap& values);
// Throws PreferencesXmlError on anything outside the supported subset,
// rather than dropping entries a title is counting on.
[[nodiscard]] PreferenceMap ParsePreferencesXml(std::string_view xml);

// Empty map when the file does not exist yet, which is the first-run case.
[[nodiscard]] PreferenceMap LoadPreferences(VirtualFileSystem& filesystem,
                                            const std::string& guest_path);
// commit()/apply() flush point: the VFS close persists it when a sandbox
// is attached.
void StorePreferences(VirtualFileSystem& filesystem,
                      const std::string& guest_path,
                      const PreferenceMap& values);

}  // namespace ogplay::runtime
