#include "ogplay/session/quirk_registry.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <utility>

#include "ogplay/session/title_profile.h"
#include "title_profile_toml.h"

namespace ogplay::session {
namespace {

using Table = detail::TomlValue::Table;

[[nodiscard]] bool ValidUtf8(const std::string_view text) {
    std::size_t index = 0;
    while (index < text.size()) {
        const auto first = static_cast<unsigned char>(text[index++]);
        if (first <= 0x7FU) continue;
        std::uint32_t codepoint = 0;
        std::size_t remaining = 0;
        if ((first & 0xE0U) == 0xC0U) {
            codepoint = first & 0x1FU;
            remaining = 1;
        } else if ((first & 0xF0U) == 0xE0U) {
            codepoint = first & 0x0FU;
            remaining = 2;
        } else if ((first & 0xF8U) == 0xF0U) {
            codepoint = first & 0x07U;
            remaining = 3;
        } else {
            return false;
        }
        if (index + remaining > text.size()) return false;
        for (std::size_t offset = 0; offset < remaining; ++offset) {
            const auto continuation = static_cast<unsigned char>(text[index++]);
            if ((continuation & 0xC0U) != 0x80U) return false;
            codepoint = (codepoint << 6U) | (continuation & 0x3FU);
        }
        const auto minimum = remaining == 1 ? 0x80U
                             : remaining == 2 ? 0x800U
                                              : 0x10000U;
        if (codepoint < minimum || codepoint > 0x10FFFFU ||
            (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] const detail::TomlValue& Require(const Table& table,
                                                const std::string_view key,
                                                const std::string_view field) {
    const auto iterator = table.find(key);
    if (iterator == table.end()) {
        throw QuirkRegistryError(std::string(field) + " is missing");
    }
    return iterator->second;
}

[[nodiscard]] const Table& AsTable(const detail::TomlValue& value,
                                   const std::string_view field) {
    const auto* table = std::get_if<Table>(&value.value);
    if (table == nullptr) {
        throw QuirkRegistryError(std::string(field) + " must be a table");
    }
    return *table;
}

[[nodiscard]] std::string AsString(const detail::TomlValue& value,
                                   const std::string_view field) {
    const auto* text = std::get_if<std::string>(&value.value);
    if (text == nullptr || text->empty() ||
        std::all_of(text->begin(), text->end(), [](const char character) {
            return std::isspace(static_cast<unsigned char>(character)) != 0;
        })) {
        throw QuirkRegistryError(std::string(field) + " must be a non-empty string");
    }
    return *text;
}

[[nodiscard]] bool ValidId(const std::string_view value) {
    if (value.empty() || value.front() < 'a' || value.front() > 'z') return false;
    return std::all_of(value.begin() + 1, value.end(), [](const char character) {
        return (character >= 'a' && character <= 'z') ||
               (character >= '0' && character <= '9') || character == '_';
    });
}

[[nodiscard]] bool ValidOwner(const std::string_view owner) {
    std::size_t begin = 0;
    std::size_t components = 0;
    while (begin <= owner.size()) {
        const auto end = owner.find('/', begin);
        const auto component = owner.substr(
            begin, end == std::string_view::npos ? owner.size() - begin : end - begin);
        if (!ValidId(component)) return false;
        ++components;
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return components >= 2;
}

void ExactDefinitionKeys(const Table& table, const std::string_view id) {
    static const std::set<std::string_view> required{
        "summary", "reason", "risk", "test", "owner"};
    for (const auto field : required) {
        if (!table.contains(field)) {
            throw QuirkRegistryError(std::string(id) + " is missing " +
                                     std::string(field));
        }
    }
    for (const auto& [key, value] : table) {
        static_cast<void>(value);
        if (!required.contains(key)) {
            throw QuirkRegistryError(std::string(id) + " has unknown field " + key);
        }
    }
}

[[nodiscard]] std::pair<std::filesystem::path, std::string> DecodeTestReference(
    const std::string_view reference,
    const std::optional<std::filesystem::path>& source_root,
    const std::string_view field) {
    const auto separator = reference.find(':');
    if (separator == std::string_view::npos || separator == 0 ||
        separator + 1 >= reference.size()) {
        throw QuirkRegistryError(std::string(field) +
                                 " must be tests/<file>.cpp:<case-name>");
    }
    const auto path_text = reference.substr(0, separator);
    const auto case_name = std::string(reference.substr(separator + 1));
    if (path_text.find('\\') != std::string_view::npos) {
        throw QuirkRegistryError(std::string(field) + " must use slash separators");
    }
    const std::filesystem::path relative(path_text);
    if (relative.is_absolute() || relative.extension() != ".cpp" ||
        relative.lexically_normal().generic_string() != path_text) {
        throw QuirkRegistryError(std::string(field) + " has an unsafe test path");
    }
    auto component = relative.begin();
    if (component == relative.end() || component->generic_string() != "tests") {
        throw QuirkRegistryError(std::string(field) + " must point below tests/");
    }
    if (!source_root.has_value()) return {relative, case_name};
    const auto path = *source_root / relative;
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw QuirkRegistryError(std::string(field) + " cannot open " + path.string());
    }
    const std::string source((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
    if (source.find(case_name) == std::string::npos) {
        throw QuirkRegistryError(std::string(field) +
                                 " case name is absent from the test source");
    }
    return {relative, case_name};
}

[[nodiscard]] QuirkDefinition DecodeDefinition(
    const std::string_view id, const detail::TomlValue& value,
    const std::optional<std::filesystem::path>& source_root) {
    if (!ValidId(id)) throw QuirkRegistryError("invalid quirk id " + std::string(id));
    const auto& table = AsTable(value, id);
    ExactDefinitionKeys(table, id);
    QuirkDefinition result;
    result.id = id;
    result.summary = AsString(Require(table, "summary", id), "summary");
    result.reason = AsString(Require(table, "reason", id), "reason");
    result.risk = AsString(Require(table, "risk", id), "risk");
    result.test = AsString(Require(table, "test", id), "test");
    static_cast<void>(DecodeTestReference(result.test, source_root, "test"));
    result.owner = AsString(Require(table, "owner", id), "owner");
    if (!ValidOwner(result.owner)) {
        throw QuirkRegistryError("owner must be a module path");
    }
    return result;
}

[[nodiscard]] std::map<std::string, QuirkDefinition, std::less<>>
LoadDefinitions(const std::filesystem::path& path,
                const std::optional<std::filesystem::path>& source_root) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw QuirkRegistryError("cannot open quirk registry: " + path.string());
    }
    const std::string text((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    if (!ValidUtf8(text)) {
        throw QuirkRegistryError("quirk registry is not valid UTF-8");
    }
    detail::TomlValue::Table root;
    try {
        root = detail::ParseDataToml(text);
    } catch (const TitleProfileError& error) {
        throw QuirkRegistryError(std::string("invalid quirk registry TOML: ") +
                                 error.what());
    }
    const auto& schema = Require(root, "schema", "schema");
    const auto* schema_value = std::get_if<std::int64_t>(&schema.value);
    if (schema_value == nullptr || *schema_value != 1) {
        throw QuirkRegistryError("quirk registry schema must be 1");
    }
    std::map<std::string, QuirkDefinition, std::less<>> definitions;
    for (const auto& [id, value] : root) {
        if (id == "schema") continue;
        auto definition = DecodeDefinition(id, value, source_root);
        definitions.emplace(id, std::move(definition));
    }
    return definitions;
}

}  // namespace

QuirkRegistry::QuirkRegistry(
    std::map<std::string, QuirkDefinition, std::less<>> definitions)
    : definitions_(std::move(definitions)) {}

QuirkRegistry QuirkRegistry::Load(const std::filesystem::path& path,
                                  const std::filesystem::path& source_root) {
    return QuirkRegistry(LoadDefinitions(path, source_root));
}

QuirkRegistry QuirkRegistry::LoadPackaged(const std::filesystem::path& path) {
    return QuirkRegistry(LoadDefinitions(path, std::nullopt));
}

const QuirkDefinition* QuirkRegistry::Find(const std::string_view id) const noexcept {
    const auto iterator = definitions_.find(id);
    return iterator == definitions_.end() ? nullptr : &iterator->second;
}

const std::map<std::string, QuirkDefinition, std::less<>>&
QuirkRegistry::Definitions() const noexcept {
    return definitions_;
}

void QuirkRegistry::Validate(const TitleProfile& profile) const {
    if (!profile.quirks.has_value()) return;
    for (const auto& id : profile.quirks->enabled) {
        if (!definitions_.contains(id)) {
            throw QuirkRegistryError("Title Profile references unregistered quirk " + id);
        }
        if (!profile.quirks->parameters.contains(id)) {
            throw QuirkRegistryError("Title Profile is missing parameters for quirk " + id);
        }
    }
    for (const auto& [id, parameters] : profile.quirks->parameters) {
        static_cast<void>(parameters);
        if (std::find(profile.quirks->enabled.begin(), profile.quirks->enabled.end(), id) ==
            profile.quirks->enabled.end()) {
            throw QuirkRegistryError(
                "Title Profile has parameters for disabled quirk " + id);
        }
    }
}

}  // namespace ogplay::session
