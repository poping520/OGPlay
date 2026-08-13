// SharedPreferences XML, shared by the framework HLE and the DexVM handlers
// (ADR-0020, design 03 §6).

#include "ogplay/runtime/framework/preferences_xml.h"

#include <array>
#include <charconv>
#include <optional>
#include <span>
#include <vector>

namespace ogplay::runtime {
namespace {

[[nodiscard]] std::string EscapeXml(const std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const auto character : text) {
        switch (character) {
            case '&': out.append("&amp;"); break;
            case '<': out.append("&lt;"); break;
            case '>': out.append("&gt;"); break;
            case '"': out.append("&quot;"); break;
            case '\'': out.append("&apos;"); break;
            default: out.push_back(character);
        }
    }
    return out;
}

[[nodiscard]] std::string UnescapeXml(const std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] != '&') {
            out.push_back(text[index]);
            continue;
        }
        const auto end = text.find(';', index);
        if (end == std::string_view::npos) {
            throw PreferencesXmlError("preferences XML has an unterminated "
                                      "entity");
        }
        const auto entity = text.substr(index + 1, end - index - 1);
        if (entity == "amp") {
            out.push_back('&');
        } else if (entity == "lt") {
            out.push_back('<');
        } else if (entity == "gt") {
            out.push_back('>');
        } else if (entity == "quot") {
            out.push_back('"');
        } else if (entity == "apos") {
            out.push_back('\'');
        } else {
            // Numeric and custom entities are outside the platform's own
            // output, so accepting them would be guessing.
            throw PreferencesXmlError(
                "preferences XML has an unsupported entity: &" +
                std::string(entity) + ";");
        }
        index = end;
    }
    return out;
}

void SkipSpace(const std::string_view text, std::size_t& cursor) {
    while (cursor < text.size() &&
           (text[cursor] == ' ' || text[cursor] == '\t' ||
            text[cursor] == '\n' || text[cursor] == '\r')) {
        ++cursor;
    }
}

// Reads name="value"; the platform always quotes with double quotes.
[[nodiscard]] bool ReadAttribute(const std::string_view text,
                                 std::size_t& cursor, std::string& name,
                                 std::string& value) {
    SkipSpace(text, cursor);
    if (cursor >= text.size() || text[cursor] == '/' || text[cursor] == '>') {
        return false;
    }
    const auto name_start = cursor;
    while (cursor < text.size() && text[cursor] != '=' &&
           text[cursor] != ' ' && text[cursor] != '>') {
        ++cursor;
    }
    name = std::string(text.substr(name_start, cursor - name_start));
    SkipSpace(text, cursor);
    if (cursor >= text.size() || text[cursor] != '=') {
        throw PreferencesXmlError("preferences XML attribute has no value: " +
                                  name);
    }
    ++cursor;
    SkipSpace(text, cursor);
    if (cursor >= text.size() || text[cursor] != '"') {
        throw PreferencesXmlError("preferences XML attribute is not quoted: " +
                                  name);
    }
    ++cursor;
    const auto value_start = cursor;
    while (cursor < text.size() && text[cursor] != '"') ++cursor;
    if (cursor >= text.size()) {
        throw PreferencesXmlError("preferences XML attribute is unterminated");
    }
    value = UnescapeXml(text.substr(value_start, cursor - value_start));
    ++cursor;
    return true;
}

template <typename Integer>
[[nodiscard]] Integer ParseInteger(const std::string& text,
                                   const std::string_view kind) {
    Integer value{};
    const auto* first = text.data();
    const auto* last = first + text.size();
    const auto result = std::from_chars(first, last, value);
    if (result.ec != std::errc{} || result.ptr != last) {
        throw PreferencesXmlError("preferences XML has a malformed " +
                                  std::string(kind) + ": " + text);
    }
    return value;
}

[[nodiscard]] std::string RenderFloat(const float value) {
    std::array<char, 64> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                                      value, std::chars_format::general);
    if (result.ec != std::errc{}) {
        throw PreferencesXmlError("cannot render a preferences float");
    }
    return {buffer.data(), result.ptr};
}

}  // namespace

std::string PreferencesGuestPath(const std::string_view package,
                                 const std::string_view name) {
    return "/data/data/" + std::string(package) + "/shared_prefs/" +
           std::string(name) + ".xml";
}

std::string RenderPreferencesXml(const PreferenceMap& values) {
    // The map is ordered, so the same content always renders identically.
    std::string out =
        "<?xml version='1.0' encoding='utf-8' standalone='yes' ?>\n<map>\n";
    for (const auto& [key, value] : values) {
        const auto name = EscapeXml(key);
        if (const auto* flag = std::get_if<bool>(&value)) {
            out += "    <boolean name=\"" + name + "\" value=\"" +
                   (*flag ? "true" : "false") + "\" />\n";
        } else if (const auto* number = std::get_if<std::int32_t>(&value)) {
            out += "    <int name=\"" + name + "\" value=\"" +
                   std::to_string(*number) + "\" />\n";
        } else if (const auto* wide = std::get_if<std::int64_t>(&value)) {
            out += "    <long name=\"" + name + "\" value=\"" +
                   std::to_string(*wide) + "\" />\n";
        } else if (const auto* real = std::get_if<float>(&value)) {
            out += "    <float name=\"" + name + "\" value=\"" +
                   RenderFloat(*real) + "\" />\n";
        } else {
            // The platform puts string content in the element body.
            out += "    <string name=\"" + name + "\">" +
                   EscapeXml(std::get<std::string>(value)) + "</string>\n";
        }
    }
    out += "</map>\n";
    return out;
}

PreferenceMap ParsePreferencesXml(const std::string_view xml) {
    PreferenceMap values;
    std::size_t cursor = 0;
    bool saw_map = false;
    while (cursor < xml.size()) {
        SkipSpace(xml, cursor);
        if (cursor >= xml.size()) break;
        if (xml[cursor] != '<') {
            throw PreferencesXmlError(
                "preferences XML has text outside an element");
        }
        ++cursor;
        if (xml.compare(cursor, 4, "?xml") == 0) {
            const auto end = xml.find("?>", cursor);
            if (end == std::string_view::npos) {
                throw PreferencesXmlError(
                    "preferences XML declaration is unterminated");
            }
            cursor = end + 2;
            continue;
        }
        if (cursor < xml.size() && (xml[cursor] == '!' || xml[cursor] == '?')) {
            // Comments, DTDs and processing instructions are not part of
            // what the platform writes.
            throw PreferencesXmlError(
                "preferences XML contains an unsupported construct");
        }
        const bool closing = cursor < xml.size() && xml[cursor] == '/';
        if (closing) ++cursor;
        const auto name_start = cursor;
        while (cursor < xml.size() && xml[cursor] != ' ' &&
               xml[cursor] != '>' && xml[cursor] != '/') {
            ++cursor;
        }
        const auto element = xml.substr(name_start, cursor - name_start);
        if (closing) {
            if (element != "map") {
                throw PreferencesXmlError(
                    "preferences XML has an unexpected closing element: " +
                    std::string(element));
            }
            const auto end = xml.find('>', cursor);
            if (end == std::string_view::npos) {
                throw PreferencesXmlError("preferences XML is unterminated");
            }
            cursor = end + 1;
            continue;
        }
        if (element == "map") {
            if (saw_map) {
                throw PreferencesXmlError(
                    "preferences XML has more than one map");
            }
            saw_map = true;
            const auto end = xml.find('>', cursor);
            if (end == std::string_view::npos) {
                throw PreferencesXmlError("preferences XML is unterminated");
            }
            cursor = end + 1;
            continue;
        }
        if (!saw_map) {
            throw PreferencesXmlError(
                "preferences XML entry is outside the map element");
        }
        std::string key;
        std::string raw;
        bool has_key = false;
        bool has_value = false;
        std::string attribute;
        std::string attribute_value;
        while (ReadAttribute(xml, cursor, attribute, attribute_value)) {
            if (attribute == "name") {
                key = attribute_value;
                has_key = true;
            } else if (attribute == "value") {
                raw = attribute_value;
                has_value = true;
            } else {
                throw PreferencesXmlError(
                    "preferences XML has an unknown attribute: " + attribute);
            }
        }
        if (!has_key) {
            throw PreferencesXmlError(
                "preferences XML entry has no name attribute");
        }
        SkipSpace(xml, cursor);
        const bool self_closing = cursor < xml.size() && xml[cursor] == '/';
        if (self_closing) ++cursor;
        if (cursor >= xml.size() || xml[cursor] != '>') {
            throw PreferencesXmlError("preferences XML element is unterminated");
        }
        ++cursor;
        if (!self_closing) {
            // Only <string> carries body text; anything else is unexpected.
            const auto close = xml.find("</", cursor);
            if (close == std::string_view::npos) {
                throw PreferencesXmlError(
                    "preferences XML element is unterminated: " +
                    std::string(element));
            }
            raw = UnescapeXml(xml.substr(cursor, close - cursor));
            has_value = true;
            const auto end = xml.find('>', close);
            if (end == std::string_view::npos) {
                throw PreferencesXmlError("preferences XML is unterminated");
            }
            const auto closing_name = xml.substr(close + 2, end - close - 2);
            if (closing_name != element) {
                throw PreferencesXmlError(
                    "preferences XML closing element does not match: " +
                    std::string(closing_name));
            }
            cursor = end + 1;
        }
        if (!has_value) {
            throw PreferencesXmlError(
                "preferences XML entry has no value: " + key);
        }
        if (element == "boolean") {
            if (raw != "true" && raw != "false") {
                throw PreferencesXmlError(
                    "preferences XML has a malformed boolean: " + raw);
            }
            values[key] = raw == "true";
        } else if (element == "int") {
            values[key] = ParseInteger<std::int32_t>(raw, "int");
        } else if (element == "long") {
            values[key] = ParseInteger<std::int64_t>(raw, "long");
        } else if (element == "float") {
            float parsed{};
            const auto result = std::from_chars(
                raw.data(), raw.data() + raw.size(), parsed,
                std::chars_format::general);
            if (result.ec != std::errc{} ||
                result.ptr != raw.data() + raw.size()) {
                throw PreferencesXmlError(
                    "preferences XML has a malformed float: " + raw);
            }
            values[key] = parsed;
        } else if (element == "string") {
            values[key] = raw;
        } else {
            // string sets and any future element type are recorded as a gap
            // rather than silently dropped.
            throw PreferencesXmlError(
                "preferences XML has an unsupported element: " +
                std::string(element));
        }
    }
    if (!saw_map) {
        throw PreferencesXmlError("preferences XML has no map element");
    }
    return values;
}

PreferenceMap LoadPreferences(VirtualFileSystem& filesystem,
                               const std::string& guest_path) {
    std::vector<std::byte> bytes;
    VfsFileInfo info;
    try {
        info = filesystem.Stat(guest_path);
    } catch (const VfsError& error) {
        if (error.ErrorNumber() == 2) return {};  // first run: no file yet
        throw PreferencesXmlError("cannot stat " + guest_path + ": " +
                                  error.what());
    }

    std::optional<std::int32_t> descriptor;
    try {
        descriptor =
            filesystem.Open(guest_path, VfsOpenOptions{.read = true});
        bytes.resize(static_cast<std::size_t>(info.size));
        std::size_t cursor = 0;
        while (cursor < bytes.size()) {
            const auto got = filesystem.Read(
                *descriptor, std::span(bytes).subspan(cursor));
            if (got == 0) break;
            cursor += got;
        }
        filesystem.Close(*descriptor);
        descriptor.reset();
        bytes.resize(cursor);
    } catch (const VfsError& error) {
        if (descriptor.has_value()) {
            try {
                filesystem.Close(*descriptor);
            } catch (const VfsError&) {
                // Preserve the read/stat failure that explains the damage.
            }
        }
        throw PreferencesXmlError("cannot read " + guest_path + ": " +
                                  error.what());
    }
    if (bytes.empty()) return {};
    return ParsePreferencesXml(
        std::string_view(reinterpret_cast<const char*>(bytes.data()),
                         bytes.size()));
}

void StorePreferences(VirtualFileSystem& filesystem,
                      const std::string& guest_path,
                      const PreferenceMap& values) {
    const auto text = RenderPreferencesXml(values);
    std::vector<std::byte> bytes(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        bytes[index] = static_cast<std::byte>(text[index]);
    }
    try {
        const auto descriptor = filesystem.Open(
            guest_path, VfsOpenOptions{.write = true, .create = true,
                                       .truncate = true});
        std::size_t cursor = 0;
        while (cursor < bytes.size()) {
            cursor += filesystem.Write(descriptor,
                                       std::span(bytes).subspan(cursor));
        }
        filesystem.Close(descriptor);
    } catch (const VfsError& error) {
        throw PreferencesXmlError("cannot write " + guest_path + ": " +
                                  error.what());
    }
}

}  // namespace ogplay::runtime
