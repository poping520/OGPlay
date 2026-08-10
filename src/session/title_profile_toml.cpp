#include "title_profile_toml.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstddef>
#include <locale>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "ogplay/session/title_profile.h"
#include "ogplay/runtime/jni/jni_signature.h"

namespace ogplay::session::detail {
namespace {

[[nodiscard]] std::string_view Trim(const std::string_view value) {
    std::size_t first = 0;
    while (first < value.size() &&
           std::isspace(static_cast<unsigned char>(value[first])) != 0) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first &&
           std::isspace(static_cast<unsigned char>(value[last - 1])) != 0) {
        --last;
    }
    return value.substr(first, last - first);
}

[[nodiscard]] bool IsKey(const std::string_view value) {
    if (value.empty()) return false;
    for (const char character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (std::isalnum(byte) == 0 && character != '_') return false;
    }
    return true;
}

[[nodiscard]] std::vector<std::string> SplitPath(const std::string_view value) {
    std::vector<std::string> result;
    std::size_t begin = 0;
    while (begin <= value.size()) {
        const auto end = value.find('.', begin);
        const auto component =
            value.substr(begin, end == std::string_view::npos ? value.size() - begin
                                                              : end - begin);
        if (!IsKey(component)) {
            throw TitleProfileError("invalid TOML table path");
        }
        result.emplace_back(component);
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return result;
}

[[nodiscard]] std::string RemoveComment(const std::string_view line) {
    bool quoted = false;
    bool literal = false;
    bool escaped = false;
    for (std::size_t index = 0; index < line.size(); ++index) {
        const char character = line[index];
        if (quoted) {
            if (escaped) {
                escaped = false;
            } else if (character == '\\') {
                escaped = true;
            } else if (character == '"') {
                quoted = false;
            }
        } else if (literal) {
            if (character == '\'') literal = false;
        } else if (character == '"') {
            quoted = true;
        } else if (character == '\'') {
            literal = true;
        } else if (character == '#') {
            return std::string(line.substr(0, index));
        }
    }
    return std::string(line);
}

[[nodiscard]] bool ValueComplete(const std::string_view assignment) {
    const auto equals = assignment.find('=');
    if (equals == std::string_view::npos) return true;
    std::int64_t arrays = 0;
    std::int64_t tables = 0;
    bool quoted = false;
    bool literal = false;
    bool escaped = false;
    for (const char character : assignment.substr(equals + 1)) {
        if (quoted) {
            if (escaped) {
                escaped = false;
            } else if (character == '\\') {
                escaped = true;
            } else if (character == '"') {
                quoted = false;
            }
            continue;
        }
        if (literal) {
            if (character == '\'') literal = false;
            continue;
        }
        if (character == '"') {
            quoted = true;
        } else if (character == '\'') {
            literal = true;
        } else if (character == '[') {
            ++arrays;
        } else if (character == ']') {
            --arrays;
        } else if (character == '{') {
            ++tables;
        } else if (character == '}') {
            --tables;
        }
        if (arrays < 0 || tables < 0) return true;
    }
    return !quoted && !literal && arrays == 0 && tables == 0;
}

[[nodiscard]] bool InsideMultilineString(const std::string_view value) {
    std::size_t count = 0;
    std::size_t position = 0;
    while ((position = value.find("\"\"\"", position)) != std::string_view::npos) {
        ++count;
        position += 3;
    }
    return count % 2 != 0;
}

void AppendUtf8(std::string& output, const std::uint32_t codepoint) {
    if (codepoint > 0x10FFFFU || (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
        throw TitleProfileError("invalid Unicode escape in TOML string");
    }
    if (codepoint <= 0x7FU) {
        output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FFU) {
        output.push_back(static_cast<char>(0xC0U | (codepoint >> 6U)));
        output.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    } else if (codepoint <= 0xFFFFU) {
        output.push_back(static_cast<char>(0xE0U | (codepoint >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    } else {
        output.push_back(static_cast<char>(0xF0U | (codepoint >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    }
}

class ValueParser final {
public:
    explicit ValueParser(const std::string_view text) : text_(text) {}

    [[nodiscard]] TomlValue Parse() {
        auto result = ParseValue();
        SkipWhitespace();
        if (position_ != text_.size()) {
            throw TitleProfileError("unexpected trailing TOML value content");
        }
        return result;
    }

private:
    void SkipWhitespace() {
        while (position_ < text_.size() &&
               std::isspace(static_cast<unsigned char>(text_[position_])) != 0) {
            ++position_;
        }
    }

    [[nodiscard]] TomlValue ParseValue() {
        SkipWhitespace();
        if (position_ >= text_.size()) throw TitleProfileError("missing TOML value");
        switch (text_[position_]) {
        case '"': return TomlValue{ParseBasicString()};
        case '\'': return TomlValue{ParseLiteralString()};
        case '[': return TomlValue{ParseArray()};
        case '{': return TomlValue{ParseInlineTable()};
        default: return ParseScalar();
        }
    }

    [[nodiscard]] std::uint32_t ParseHexDigits(const std::size_t count) {
        if (position_ + count > text_.size()) {
            throw TitleProfileError("truncated Unicode escape in TOML string");
        }
        std::uint32_t result = 0;
        for (std::size_t index = 0; index < count; ++index) {
            const char character = text_[position_++];
            result <<= 4U;
            if (character >= '0' && character <= '9') {
                result |= static_cast<std::uint32_t>(character - '0');
            } else if (character >= 'a' && character <= 'f') {
                result |= static_cast<std::uint32_t>(character - 'a' + 10);
            } else if (character >= 'A' && character <= 'F') {
                result |= static_cast<std::uint32_t>(character - 'A' + 10);
            } else {
                throw TitleProfileError("invalid Unicode escape in TOML string");
            }
        }
        return result;
    }

    [[nodiscard]] std::string ParseBasicString() {
        const bool multiline = text_.substr(position_).starts_with("\"\"\"");
        position_ += multiline ? 3U : 1U;
        if (multiline && position_ < text_.size() && text_[position_] == '\n') {
            ++position_;
        }
        std::string result;
        while (position_ < text_.size()) {
            if (multiline && text_.substr(position_).starts_with("\"\"\"")) {
                position_ += 3;
                return result;
            }
            const char character = text_[position_++];
            if (!multiline && character == '"') return result;
            if (!multiline && (character == '\n' || character == '\r')) {
                throw TitleProfileError("multiline TOML strings are not supported");
            }
            if (character != '\\') {
                result.push_back(character);
                continue;
            }
            if (position_ >= text_.size()) {
                throw TitleProfileError("truncated TOML string escape");
            }
            const char escaped = text_[position_++];
            switch (escaped) {
            case '"': result.push_back('"'); break;
            case '\\': result.push_back('\\'); break;
            case 'b': result.push_back('\b'); break;
            case 'f': result.push_back('\f'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            case 'u': AppendUtf8(result, ParseHexDigits(4)); break;
            case 'U': AppendUtf8(result, ParseHexDigits(8)); break;
            default: throw TitleProfileError("unsupported TOML string escape");
            }
        }
        throw TitleProfileError(multiline ? "unterminated multiline TOML string"
                                          : "unterminated TOML string");
    }

    [[nodiscard]] std::string ParseLiteralString() {
        ++position_;
        const auto end = text_.find('\'', position_);
        if (end == std::string_view::npos ||
            text_.substr(position_, end - position_).find_first_of("\r\n") !=
                std::string_view::npos) {
            throw TitleProfileError("unterminated or multiline literal TOML string");
        }
        std::string result(text_.substr(position_, end - position_));
        position_ = end + 1;
        return result;
    }

    [[nodiscard]] TomlValue::Array ParseArray() {
        ++position_;
        TomlValue::Array result;
        SkipWhitespace();
        if (Consume(']')) return result;
        while (true) {
            result.push_back(ParseValue());
            SkipWhitespace();
            if (Consume(']')) return result;
            if (!Consume(',')) throw TitleProfileError("TOML array requires a comma");
            SkipWhitespace();
            if (Consume(']')) return result;
        }
    }

    [[nodiscard]] std::string ParseInlineKey() {
        SkipWhitespace();
        const auto begin = position_;
        while (position_ < text_.size()) {
            const char character = text_[position_];
            if (std::isalnum(static_cast<unsigned char>(character)) == 0 &&
                character != '_') {
                break;
            }
            ++position_;
        }
        const auto key = text_.substr(begin, position_ - begin);
        if (!IsKey(key)) throw TitleProfileError("invalid TOML inline table key");
        return std::string(key);
    }

    [[nodiscard]] TomlValue::Table ParseInlineTable() {
        ++position_;
        TomlValue::Table result;
        SkipWhitespace();
        if (Consume('}')) return result;
        while (true) {
            auto key = ParseInlineKey();
            SkipWhitespace();
            if (!Consume('=')) throw TitleProfileError("TOML inline table requires '='");
            auto value = ParseValue();
            if (!result.emplace(std::move(key), std::move(value)).second) {
                throw TitleProfileError("duplicate TOML inline table key");
            }
            SkipWhitespace();
            if (Consume('}')) return result;
            if (!Consume(',')) {
                throw TitleProfileError("TOML inline table requires a comma");
            }
        }
    }

    [[nodiscard]] TomlValue ParseScalar() {
        const auto begin = position_;
        while (position_ < text_.size()) {
            const char character = text_[position_];
            if (std::isspace(static_cast<unsigned char>(character)) != 0 ||
                character == ',' || character == ']' || character == '}') {
                break;
            }
            ++position_;
        }
        const auto token = text_.substr(begin, position_ - begin);
        if (token == "true") return TomlValue{true};
        if (token == "false") return TomlValue{false};
        if (token.empty()) throw TitleProfileError("missing TOML scalar");
        if (token.find_first_of(".eE") != std::string_view::npos) {
            double value = 0;
            std::istringstream input{std::string(token)};
            input.imbue(std::locale::classic());
            input >> std::noskipws >> value;
            if (input && input.peek() == std::char_traits<char>::eof()) {
                return TomlValue{value};
            }
        } else {
            std::int64_t value = 0;
            const auto [end, error] =
                std::from_chars(token.data(), token.data() + token.size(), value);
            if (error == std::errc{} && end == token.data() + token.size()) {
                return TomlValue{value};
            }
        }
        throw TitleProfileError("unsupported TOML scalar");
    }

    [[nodiscard]] bool Consume(const char expected) {
        if (position_ >= text_.size() || text_[position_] != expected) return false;
        ++position_;
        return true;
    }

    std::string_view text_;
    std::size_t position_{};
};

[[nodiscard]] TomlValue::Table* Traverse(TomlValue::Table& root,
                                         const std::vector<std::string>& path,
                                         const std::size_t count) {
    auto* table = &root;
    for (std::size_t index = 0; index < count; ++index) {
        auto [iterator, inserted] =
            table->try_emplace(path[index], TomlValue{TomlValue::Table{}});
        auto& node = iterator->second;
        if (auto* nested = std::get_if<TomlValue::Table>(&node.value)) {
            table = nested;
            continue;
        }
        if (auto* array = std::get_if<TomlValue::Array>(&node.value);
            array != nullptr && !array->empty()) {
            auto* nested = std::get_if<TomlValue::Table>(&array->back().value);
            if (nested != nullptr) {
                table = nested;
                continue;
            }
        }
        static_cast<void>(inserted);
        throw TitleProfileError("TOML table path crosses a scalar");
    }
    return table;
}

}  // namespace

TomlValue::Table ParseDataToml(const std::string_view text) {
    TomlValue::Table root;
    TomlValue::Table* current = &root;
    std::set<std::string, std::less<>> declared_tables;
    std::string pending;
    std::size_t position = 0;
    while (position <= text.size()) {
        const auto end = text.find('\n', position);
        auto line = text.substr(position, end == std::string_view::npos
                                              ? text.size() - position
                                              : end - position);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        auto code_storage =
            InsideMultilineString(pending) ? std::string(line) : RemoveComment(line);
        auto code = Trim(code_storage);
        if (!pending.empty()) {
            pending.push_back('\n');
            pending.append(code);
            if (ValueComplete(pending)) {
                code_storage = std::move(pending);
                pending.clear();
                code = Trim(code_storage);
            } else {
                if (end == std::string_view::npos) break;
                position = end + 1;
                continue;
            }
        }
        if (!code.empty()) {
            const bool array_table = code.starts_with("[[") && code.ends_with("]]");
            const bool table = !array_table && code.starts_with('[') &&
                               code.ends_with(']');
            if (array_table || table) {
                const auto inner = Trim(code.substr(array_table ? 2 : 1,
                                                    code.size() - (array_table ? 4 : 2)));
                const auto path = SplitPath(inner);
                auto* parent = Traverse(root, path, path.size() - 1);
                const auto joined = std::string(inner);
                if (array_table) {
                    auto [iterator, inserted] = parent->try_emplace(
                        path.back(), TomlValue{TomlValue::Array{}});
                    auto* array = std::get_if<TomlValue::Array>(&iterator->second.value);
                    if (array == nullptr) {
                        throw TitleProfileError("TOML array table conflicts with a value");
                    }
                    array->push_back(TomlValue{TomlValue::Table{}});
                    current = &std::get<TomlValue::Table>(array->back().value);
                    static_cast<void>(inserted);
                } else {
                    if (!declared_tables.insert(joined).second) {
                        throw TitleProfileError("duplicate TOML table declaration");
                    }
                    auto [iterator, inserted] = parent->try_emplace(
                        path.back(), TomlValue{TomlValue::Table{}});
                    current = std::get_if<TomlValue::Table>(&iterator->second.value);
                    if (current == nullptr) {
                        throw TitleProfileError("TOML table conflicts with a value");
                    }
                    static_cast<void>(inserted);
                }
            } else {
                if (code.find('=') == std::string_view::npos) {
                    throw TitleProfileError("TOML assignment requires '='");
                }
                if (!ValueComplete(code)) {
                    pending.assign(code);
                } else {
                    const auto equals = code.find('=');
                    const auto key = Trim(code.substr(0, equals));
                    if (!IsKey(key)) throw TitleProfileError("invalid TOML assignment key");
                    auto value = ValueParser(code.substr(equals + 1)).Parse();
                    if (!current->emplace(std::string(key), std::move(value)).second) {
                        throw TitleProfileError("duplicate TOML assignment");
                    }
                }
            }
        }
        if (end == std::string_view::npos) break;
        position = end + 1;
    }
    if (!pending.empty()) throw TitleProfileError("unterminated multiline TOML value");
    return root;
}

namespace {

using NativeTable = TomlValue::Table;
using NativeArray = TomlValue::Array;

const TomlValue& NativeRequire(const NativeTable& table,
                               const std::string_view key,
                               const std::string_view field) {
    const auto found = table.find(key);
    if (found == table.end()) throw TitleProfileError(std::string(field) + " is missing");
    return found->second;
}

const TomlValue* NativeOptional(const NativeTable& table,
                                const std::string_view key) {
    const auto found = table.find(key);
    return found == table.end() ? nullptr : &found->second;
}

template <typename Value>
const Value& NativeAs(const TomlValue& value, const std::string_view field) {
    const auto* result = std::get_if<Value>(&value.value);
    if (result == nullptr) throw TitleProfileError(std::string(field) + " has wrong type");
    return *result;
}

const std::string& NativeString(const TomlValue& value,
                                const std::string_view field) {
    const auto& result = NativeAs<std::string>(value, field);
    if (result.empty()) throw TitleProfileError(std::string(field) + " must not be empty");
    return result;
}

std::int64_t NativeInteger(const TomlValue& value, const std::string_view field,
                           const std::int64_t minimum,
                           const std::int64_t maximum) {
    const auto result = NativeAs<std::int64_t>(value, field);
    if (result < minimum || result > maximum) {
        throw TitleProfileError(std::string(field) + " is outside the supported range");
    }
    return result;
}

void NativeKeys(const NativeTable& table, const std::string_view field,
                const std::initializer_list<std::string_view> allowed,
                const std::initializer_list<std::string_view> required) {
    for (const auto key : required) {
        if (!table.contains(key)) {
            throw TitleProfileError(std::string(field) + " is missing " + std::string(key));
        }
    }
    for (const auto& [key, value] : table) {
        static_cast<void>(value);
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) {
            throw TitleProfileError(std::string(field) + " has unknown field " + key);
        }
    }
}

bool NativeJavaName(const std::string_view value, const bool slash_required) {
    std::size_t begin{};
    std::size_t components{};
    do {
        const auto end = value.find('/', begin);
        const auto component = value.substr(begin, end == std::string_view::npos
                                                       ? value.size() - begin
                                                       : end - begin);
        if (component.empty()) return false;
        const auto first = static_cast<unsigned char>(component.front());
        if (std::isalpha(first) == 0 && component.front() != '_' &&
            component.front() != '$') return false;
        if (!std::all_of(component.begin() + 1, component.end(), [](const char item) {
                const auto byte = static_cast<unsigned char>(item);
                return std::isalnum(byte) != 0 || item == '_' || item == '$';
            })) return false;
        ++components;
        if (end == std::string_view::npos) break;
        begin = end + 1;
    } while (begin <= value.size());
    return !slash_required || components >= 2;
}

ProfileNativeCallPhase NativePhase(const std::string_view value) {
    if (value == "startup") return ProfileNativeCallPhase::startup;
    if (value == "resume") return ProfileNativeCallPhase::resume;
    if (value == "frame") return ProfileNativeCallPhase::frame;
    if (value == "pause") return ProfileNativeCallPhase::pause;
    if (value == "shutdown") return ProfileNativeCallPhase::shutdown;
    if (value == "pointer_down") return ProfileNativeCallPhase::pointer_down;
    if (value == "pointer_move") return ProfileNativeCallPhase::pointer_move;
    if (value == "pointer_up") return ProfileNativeCallPhase::pointer_up;
    if (value == "key_down") return ProfileNativeCallPhase::key_down;
    if (value == "key_up") return ProfileNativeCallPhase::key_up;
    throw TitleProfileError("runtime.native_call[].phase is unsupported");
}

ProfileNativeArgumentSource NativeSource(const std::string_view value) {
    if (value == "constant") return ProfileNativeArgumentSource::constant;
    if (value == "surface_width") return ProfileNativeArgumentSource::surface_width;
    if (value == "surface_height") return ProfileNativeArgumentSource::surface_height;
    if (value == "input_x") return ProfileNativeArgumentSource::input_x;
    if (value == "input_y") return ProfileNativeArgumentSource::input_y;
    if (value == "input_pointer") return ProfileNativeArgumentSource::input_pointer;
    if (value == "input_key") return ProfileNativeArgumentSource::input_key;
    throw TitleProfileError("runtime.native_call[].arguments[].source is unsupported");
}

bool NativeIntegerKind(const runtime::JniTypeKind kind) {
    return kind == runtime::JniTypeKind::boolean || kind == runtime::JniTypeKind::byte ||
           kind == runtime::JniTypeKind::character ||
           kind == runtime::JniTypeKind::short_integer ||
           kind == runtime::JniTypeKind::integer;
}

}  // namespace

ProfileRuntime DecodeProfileRuntime(const TomlValue::Table& root) {
    const auto& table = NativeAs<NativeTable>(NativeRequire(root, "runtime", "runtime"),
                                              "runtime");
    NativeKeys(table, "runtime",
               {"api_level", "lifecycle", "maximum_ticks_per_call", "surface",
                "native_call"},
               {"api_level", "lifecycle", "surface"});
    ProfileRuntime result;
    result.api_level = static_cast<std::uint32_t>(NativeInteger(
        NativeRequire(table, "api_level", "runtime.api_level"), "runtime.api_level",
        1, std::numeric_limits<std::uint32_t>::max()));
    if (result.api_level != 19 && result.api_level != 22 && result.api_level != 23) {
        throw TitleProfileError("runtime.api_level must be 19, 22 or 23");
    }
    const auto lifecycle = NativeString(
        NativeRequire(table, "lifecycle", "runtime.lifecycle"), "runtime.lifecycle");
    if (lifecycle == "native_activity") result.lifecycle = ProfileLifecycle::native_activity;
    else if (lifecycle == "gl_surface_view") result.lifecycle = ProfileLifecycle::gl_surface_view;
    else if (lifecycle == "custom_jni") result.lifecycle = ProfileLifecycle::custom_jni;
    else throw TitleProfileError("runtime.lifecycle is unsupported");
    if (const auto* maximum_ticks =
            NativeOptional(table, "maximum_ticks_per_call");
        maximum_ticks != nullptr) {
        result.maximum_ticks_per_call = static_cast<std::uint64_t>(NativeInteger(
            *maximum_ticks, "runtime.maximum_ticks_per_call", 1,
            ProfileRuntime::kMaximumTicksPerCall));
    }
    const auto& surface = NativeAs<NativeTable>(
        NativeRequire(table, "surface", "runtime.surface"), "runtime.surface");
    NativeKeys(surface, "runtime.surface", {"width", "height"}, {"width", "height"});
    result.surface.width = static_cast<std::uint32_t>(NativeInteger(
        NativeRequire(surface, "width", "runtime.surface.width"),
        "runtime.surface.width", 1, 16384));
    result.surface.height = static_cast<std::uint32_t>(NativeInteger(
        NativeRequire(surface, "height", "runtime.surface.height"),
        "runtime.surface.height", 1, 16384));
    const auto* call_value = NativeOptional(table, "native_call");
    if (call_value == nullptr) return result;
    const auto& calls = NativeAs<NativeArray>(*call_value, "runtime.native_call");
    if (calls.empty()) throw TitleProfileError("runtime.native_call must not be empty");
    for (const auto& value : calls) {
        const auto& call = NativeAs<NativeTable>(value, "runtime.native_call[]");
        NativeKeys(call, "runtime.native_call[]",
                   {"phase", "class", "method", "signature", "dispatch", "arguments"},
                   {"phase", "class", "method", "signature", "dispatch", "arguments"});
        ProfileNativeCall decoded;
        decoded.phase = NativePhase(NativeString(
            NativeRequire(call, "phase", "runtime.native_call[].phase"),
            "runtime.native_call[].phase"));
        decoded.class_name = NativeString(
            NativeRequire(call, "class", "runtime.native_call[].class"),
            "runtime.native_call[].class");
        decoded.method = NativeString(
            NativeRequire(call, "method", "runtime.native_call[].method"),
            "runtime.native_call[].method");
        if (!NativeJavaName(decoded.class_name, true) ||
            !NativeJavaName(decoded.method, false)) {
            throw TitleProfileError("runtime.native_call[] has an invalid Java name");
        }
        decoded.signature = NativeString(
            NativeRequire(call, "signature", "runtime.native_call[].signature"),
            "runtime.native_call[].signature");
        runtime::JniMethodDescriptor descriptor;
        try { descriptor = runtime::ParseJniMethodDescriptor(decoded.signature); }
        catch (const runtime::JniSignatureError&) {
            throw TitleProfileError("runtime.native_call[].signature is invalid");
        }
        const auto dispatch = NativeString(
            NativeRequire(call, "dispatch", "runtime.native_call[].dispatch"),
            "runtime.native_call[].dispatch");
        if (dispatch == "instance") decoded.dispatch = ProfileNativeDispatch::instance;
        else if (dispatch == "static") decoded.dispatch = ProfileNativeDispatch::static_method;
        else throw TitleProfileError("runtime.native_call[].dispatch is unsupported");
        const auto& arguments = NativeAs<NativeArray>(
            NativeRequire(call, "arguments", "runtime.native_call[].arguments"),
            "runtime.native_call[].arguments");
        if (arguments.size() != descriptor.parameters.size()) {
            throw TitleProfileError("runtime.native_call[] argument count does not match signature");
        }
        for (std::size_t index = 0; index < arguments.size(); ++index) {
            const auto& argument = NativeAs<NativeTable>(
                arguments[index], "runtime.native_call[].arguments[]");
            NativeKeys(argument, "runtime.native_call[].arguments[]",
                       {"source", "value"}, {"source"});
            ProfileNativeArgument item;
            item.source = NativeSource(NativeString(
                NativeRequire(argument, "source", "native argument source"),
                "native argument source"));
            const auto* constant = NativeOptional(argument, "value");
            if (item.source == ProfileNativeArgumentSource::constant) {
                if (constant == nullptr) throw TitleProfileError("constant native argument requires value");
                item.value = static_cast<std::uint32_t>(NativeInteger(
                    *constant, "native argument value", 0,
                    std::numeric_limits<std::uint32_t>::max()));
            } else if (constant != nullptr) {
                throw TitleProfileError("non-constant native argument must not declare value");
            }
            if (!NativeIntegerKind(descriptor.parameters[index].kind)) {
                throw TitleProfileError("native argument source requires an integer JNI parameter");
            }
            const auto pointer_source = item.source == ProfileNativeArgumentSource::input_x ||
                item.source == ProfileNativeArgumentSource::input_y ||
                item.source == ProfileNativeArgumentSource::input_pointer;
            const auto pointer_phase = decoded.phase == ProfileNativeCallPhase::pointer_down ||
                decoded.phase == ProfileNativeCallPhase::pointer_move ||
                decoded.phase == ProfileNativeCallPhase::pointer_up;
            const auto key_source = item.source == ProfileNativeArgumentSource::input_key;
            const auto key_phase = decoded.phase == ProfileNativeCallPhase::key_down ||
                                   decoded.phase == ProfileNativeCallPhase::key_up;
            if ((pointer_source && !pointer_phase) || (key_source && !key_phase)) {
                throw TitleProfileError("native input argument source does not match call phase");
            }
            decoded.arguments.push_back(item);
        }
        result.native_calls.push_back(std::move(decoded));
    }
    return result;
}

}  // namespace ogplay::session::detail
