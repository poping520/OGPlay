#include "ogplay/session/title_profile.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <type_traits>
#include <utility>

#include "ogplay/session/quirk_registry.h"
#include "title_profile_toml.h"

namespace ogplay::session {
namespace detail {
[[nodiscard]] ProfileRuntime DecodeProfileRuntime(const TomlValue::Table& root);
}
namespace {

using Table = detail::TomlValue::Table;
using Array = detail::TomlValue::Array;

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
        throw TitleProfileError(std::string(field) + " is missing");
    }
    return iterator->second;
}

[[nodiscard]] const detail::TomlValue* Optional(const Table& table,
                                                 const std::string_view key) {
    const auto iterator = table.find(key);
    return iterator == table.end() ? nullptr : &iterator->second;
}

template <typename Value>
[[nodiscard]] const Value& As(const detail::TomlValue& value,
                              const std::string_view field,
                              const std::string_view expected) {
    const auto* result = std::get_if<Value>(&value.value);
    if (result == nullptr) {
        throw TitleProfileError(std::string(field) + " must be " +
                                std::string(expected));
    }
    return *result;
}

[[nodiscard]] const Table& AsTable(const detail::TomlValue& value,
                                   const std::string_view field) {
    return As<Table>(value, field, "a table");
}

[[nodiscard]] const Array& AsArray(const detail::TomlValue& value,
                                   const std::string_view field) {
    return As<Array>(value, field, "an array");
}

[[nodiscard]] const std::string& AsString(const detail::TomlValue& value,
                                          const std::string_view field) {
    const auto& result = As<std::string>(value, field, "a string");
    if (result.empty()) {
        throw TitleProfileError(std::string(field) + " must not be empty");
    }
    return result;
}

[[nodiscard]] std::int64_t AsInteger(const detail::TomlValue& value,
                                     const std::string_view field,
                                     const std::int64_t minimum,
                                     const std::int64_t maximum) {
    const auto result = As<std::int64_t>(value, field, "an integer");
    if (result < minimum || result > maximum) {
        throw TitleProfileError(std::string(field) + " is outside the supported range");
    }
    return result;
}

[[nodiscard]] bool AsBoolean(const detail::TomlValue& value,
                             const std::string_view field) {
    return As<bool>(value, field, "boolean");
}

void ExactKeys(const Table& table, const std::string_view field,
               const std::initializer_list<std::string_view> allowed,
               const std::initializer_list<std::string_view> required = {}) {
    for (const auto key : required) {
        if (!table.contains(key)) {
            throw TitleProfileError(std::string(field) + " is missing " +
                                    std::string(key));
        }
    }
    for (const auto& [key, value] : table) {
        static_cast<void>(value);
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) {
            throw TitleProfileError(std::string(field) + " has unknown field " + key);
        }
    }
}

[[nodiscard]] bool ValidPackage(const std::string_view package) {
    bool component_start = true;
    std::size_t components = 1;
    for (const char character : package) {
        const auto byte = static_cast<unsigned char>(character);
        if (character == '.') {
            if (component_start) return false;
            component_start = true;
            ++components;
        } else if (component_start) {
            if (std::isalpha(byte) == 0) return false;
            component_start = false;
        } else if (std::isalnum(byte) == 0 && character != '_') {
            return false;
        }
    }
    return !component_start && components >= 2;
}

[[nodiscard]] bool ValidId(const std::string_view value) {
    if (value.empty() ||
        std::islower(static_cast<unsigned char>(value.front())) == 0) {
        return false;
    }
    return std::all_of(value.begin() + 1, value.end(), [](const char character) {
        const auto byte = static_cast<unsigned char>(character);
        return std::islower(byte) != 0 || std::isdigit(byte) != 0 || character == '_';
    });
}

[[nodiscard]] bool ValidImplementationId(const std::string_view value) {
    std::size_t begin = 0;
    std::size_t components = 0;
    while (begin <= value.size()) {
        const auto end = value.find('.', begin);
        const auto component = value.substr(
            begin, end == std::string_view::npos ? value.size() - begin : end - begin);
        if (!ValidId(component)) return false;
        ++components;
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return components >= 2;
}

[[nodiscard]] bool ValidJavaClass(const std::string_view value) {
    std::size_t begin = 0;
    std::size_t components = 0;
    while (begin <= value.size()) {
        const auto end = value.find('/', begin);
        const auto component = value.substr(
            begin, end == std::string_view::npos ? value.size() - begin : end - begin);
        if (component.empty()) return false;
        const auto first = static_cast<unsigned char>(component.front());
        if (std::isalpha(first) == 0 && component.front() != '_' &&
            component.front() != '$') {
            return false;
        }
        for (const char character : component.substr(1)) {
            const auto byte = static_cast<unsigned char>(character);
            if (std::isalnum(byte) == 0 && character != '_' && character != '$') {
                return false;
            }
        }
        ++components;
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return components >= 2;
}

[[nodiscard]] bool ValidHash(const std::string_view digest) {
    return digest.size() == 64 &&
           std::all_of(digest.begin(), digest.end(), [](const char character) {
               return std::isdigit(static_cast<unsigned char>(character)) != 0 ||
                      (character >= 'a' && character <= 'f');
           });
}

void ValidateAbsoluteGuestPath(const std::string_view path,
                               const std::string_view field) {
    if (path.empty() || path.front() != '/' || path.find("//") != std::string_view::npos) {
        throw TitleProfileError(std::string(field) +
                                " must be a normalized absolute guest path");
    }
    std::size_t begin = 1;
    while (begin <= path.size()) {
        const auto end = path.find('/', begin);
        const auto component = path.substr(
            begin, end == std::string_view::npos ? path.size() - begin : end - begin);
        if (component == "." || component == ".." ||
            (component.empty() && path.size() != 1)) {
            throw TitleProfileError(std::string(field) +
                                    " must be a normalized absolute guest path");
        }
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
}

void ValidateRelativePath(const std::string_view path, const std::string_view field) {
    if (path.empty() || path.front() == '/' || path.back() == '/' ||
        path.find('\\') != std::string_view::npos ||
        path.find("//") != std::string_view::npos) {
        throw TitleProfileError(std::string(field) +
                                " must be a normalized relative path");
    }
    std::size_t begin = 0;
    while (begin <= path.size()) {
        const auto end = path.find('/', begin);
        const auto component = path.substr(
            begin, end == std::string_view::npos ? path.size() - begin : end - begin);
        if (component == "." || component == "..") {
            throw TitleProfileError(std::string(field) +
                                    " must be a normalized relative path");
        }
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
}

void ValidateSoundPoolPathPattern(const std::string_view pattern) {
    const auto begin = pattern.find("{resource");
    const auto end = begin == std::string_view::npos
                         ? std::string_view::npos
                         : pattern.find('}', begin);
    if (begin == std::string_view::npos || end == std::string_view::npos ||
        pattern.find('{', begin + 1U) != std::string_view::npos ||
        pattern.find('}', end + 1U) != std::string_view::npos) {
        throw TitleProfileError(
            "audio.sound_pool.path_pattern must contain one resource placeholder");
    }
    const auto placeholder = pattern.substr(begin, end - begin + 1U);
    if (placeholder == "{resource}") return;
    if (placeholder.size() != 13U ||
        !placeholder.starts_with("{resource:0") ||
        placeholder[11] < '1' || placeholder[11] > '9') {
        throw TitleProfileError(
            "audio.sound_pool.path_pattern resource placeholder is invalid");
    }
}

template <typename Value>
void RequireUnique(const std::vector<Value>& values, const std::string_view field) {
    std::set<Value> unique(values.begin(), values.end());
    if (unique.size() != values.size()) {
        throw TitleProfileError(std::string(field) + " must not contain duplicates");
    }
}

[[nodiscard]] ProfileSource DecodeSource(const std::string_view value,
                                         const std::string_view field) {
    if (value == "apk") return ProfileSource::apk;
    if (value == "obb") return ProfileSource::obb;
    if (value == "external") return ProfileSource::external;
    throw TitleProfileError(std::string(field) + " is unsupported");
}

[[nodiscard]] ProfileAbi DecodeAbi(const std::string_view value) {
    if (value == "armeabi") return ProfileAbi::armeabi;
    if (value == "armeabi-v7a") return ProfileAbi::armeabi_v7a;
    throw TitleProfileError("identity.abi must be armeabi or armeabi-v7a");
}

[[nodiscard]] bool ValidAbi(const ProfileAbi abi) noexcept {
    return abi == ProfileAbi::armeabi || abi == ProfileAbi::armeabi_v7a;
}

[[nodiscard]] ProfileIdentity DecodeIdentity(const Table& root,
                                             const std::string_view expected_package) {
    const auto& table = AsTable(Require(root, "identity", "identity"), "identity");
    ExactKeys(table, "identity", {"package", "name", "version_code", "so_sha256", "abi"},
              {"package", "version_code", "so_sha256", "abi"});
    ProfileIdentity result;
    result.package = AsString(Require(table, "package", "identity.package"),
                              "identity.package");
    if (!ValidPackage(result.package)) {
        throw TitleProfileError("identity.package is invalid");
    }
    if (result.package != expected_package) {
        throw TitleProfileError("identity.package does not match the profile filename");
    }
    if (const auto* name = Optional(table, "name")) {
        result.name = AsString(*name, "identity.name");
    }
    const auto& versions =
        AsArray(Require(table, "version_code", "identity.version_code"),
                "identity.version_code");
    if (versions.empty()) throw TitleProfileError("identity.version_code must not be empty");
    for (std::size_t index = 0; index < versions.size(); ++index) {
        result.version_codes.push_back(static_cast<std::uint32_t>(AsInteger(
            versions[index], "identity.version_code", 1,
            std::numeric_limits<std::uint32_t>::max())));
    }
    RequireUnique(result.version_codes, "identity.version_code");
    const auto& hashes = AsArray(Require(table, "so_sha256", "identity.so_sha256"),
                                 "identity.so_sha256");
    if (hashes.empty()) throw TitleProfileError("identity.so_sha256 must not be empty");
    for (const auto& value : hashes) {
        auto digest = AsString(value, "identity.so_sha256");
        if (!ValidHash(digest)) {
            throw TitleProfileError(
                "identity.so_sha256 must contain lowercase SHA-256 digests");
        }
        result.so_sha256.push_back(std::move(digest));
    }
    RequireUnique(result.so_sha256, "identity.so_sha256");
    result.abi = DecodeAbi(
        AsString(Require(table, "abi", "identity.abi"), "identity.abi"));
    return result;
}

[[nodiscard]] ProfileData DecodeData(const detail::TomlValue& value) {
    const auto& table = AsTable(value, "data");
    ExactKeys(table, "data", {"mounts", "working_directory", "manifest"});
    ProfileData result;
    if (const auto* mounts = Optional(table, "mounts")) {
        const auto& values = AsArray(*mounts, "data.mounts");
        for (const auto& item : values) {
            const auto& mount = AsTable(item, "data.mounts[]");
            ExactKeys(mount, "data.mounts[]", {"guest", "source", "required"},
                      {"guest", "source", "required"});
            ProfileMount decoded;
            decoded.guest =
                AsString(Require(mount, "guest", "data.mounts[].guest"),
                         "data.mounts[].guest");
            ValidateAbsoluteGuestPath(decoded.guest, "data.mounts[].guest");
            decoded.source = DecodeSource(
                AsString(Require(mount, "source", "data.mounts[].source"),
                         "data.mounts[].source"),
                "data.mounts[].source");
            decoded.required =
                AsBoolean(Require(mount, "required", "data.mounts[].required"),
                          "data.mounts[].required");
            result.mounts.push_back(std::move(decoded));
        }
    }
    if (const auto* directory = Optional(table, "working_directory")) {
        result.working_directory = AsString(*directory, "data.working_directory");
        ValidateAbsoluteGuestPath(*result.working_directory, "data.working_directory");
    }
    if (const auto* manifest = Optional(table, "manifest")) {
        const auto& values = AsArray(*manifest, "data.manifest");
        for (const auto& item : values) {
            const auto& entry = AsTable(item, "data.manifest[]");
            ExactKeys(entry, "data.manifest[]", {"path", "required"},
                      {"path", "required"});
            ProfileManifestEntry decoded;
            decoded.path =
                AsString(Require(entry, "path", "data.manifest[].path"),
                         "data.manifest[].path");
            ValidateRelativePath(decoded.path, "data.manifest[].path");
            decoded.required =
                AsBoolean(Require(entry, "required", "data.manifest[].required"),
                          "data.manifest[].required");
            result.manifest.push_back(std::move(decoded));
        }
    }
    return result;
}

[[nodiscard]] ProfileAudio DecodeAudio(const detail::TomlValue& value) {
    const auto& table = AsTable(value, "audio");
    ExactKeys(table, "audio", {"cover_music", "sound_pool"});
    ProfileAudio result;
    if (const auto* music = Optional(table, "cover_music")) {
        const auto& cover = AsTable(*music, "audio.cover_music");
        ExactKeys(cover, "audio.cover_music", {"source", "path", "loop"},
                  {"source", "path", "loop"});
        ProfileCoverMusic decoded;
        decoded.source = DecodeSource(
            AsString(Require(cover, "source", "audio.cover_music.source"),
                     "audio.cover_music.source"),
            "audio.cover_music.source");
        decoded.path = AsString(Require(cover, "path", "audio.cover_music.path"),
                                "audio.cover_music.path");
        ValidateRelativePath(decoded.path, "audio.cover_music.path");
        decoded.loop = AsBoolean(Require(cover, "loop", "audio.cover_music.loop"),
                                 "audio.cover_music.loop");
        result.cover_music = std::move(decoded);
    }
    if (const auto* sound_pool = Optional(table, "sound_pool")) {
        const auto& bank = AsTable(*sound_pool, "audio.sound_pool");
        ExactKeys(bank, "audio.sound_pool", {"source", "path_pattern"},
                  {"source", "path_pattern"});
        ProfileSoundPool decoded;
        decoded.source = DecodeSource(
            AsString(Require(bank, "source", "audio.sound_pool.source"),
                     "audio.sound_pool.source"),
            "audio.sound_pool.source");
        decoded.path_pattern = AsString(
            Require(bank, "path_pattern", "audio.sound_pool.path_pattern"),
            "audio.sound_pool.path_pattern");
        ValidateRelativePath(decoded.path_pattern,
                             "audio.sound_pool.path_pattern");
        ValidateSoundPoolPathPattern(decoded.path_pattern);
        result.sound_pool = std::move(decoded);
    }
    return result;
}

[[nodiscard]] std::vector<ProfileJavaClass> DecodeJava(
    const detail::TomlValue& value) {
    const auto& table = AsTable(value, "java");
    ExactKeys(table, "java", {"class"}, {"class"});
    const auto& classes = AsArray(Require(table, "class", "java.class"), "java.class");
    if (classes.empty()) throw TitleProfileError("java.class must not be empty");
    std::vector<ProfileJavaClass> result;
    std::set<std::string, std::less<>> class_names;
    for (const auto& item : classes) {
        const auto& java_class = AsTable(item, "java.class[]");
        ExactKeys(java_class, "java.class[]", {"name", "method"}, {"name", "method"});
        ProfileJavaClass decoded;
        decoded.name = AsString(Require(java_class, "name", "java.class[].name"),
                                "java.class[].name");
        if (!ValidJavaClass(decoded.name) || !class_names.insert(decoded.name).second) {
            throw TitleProfileError("java.class names must be valid and unique");
        }
        const auto& methods =
            AsArray(Require(java_class, "method", "java.class[].method"),
                    "java.class[].method");
        if (methods.empty()) {
            throw TitleProfileError("java.class[].method must not be empty");
        }
        std::set<std::pair<std::string, std::string>> method_keys;
        for (const auto& method_value : methods) {
            const auto& method = AsTable(method_value, "java.class[].method[]");
            ExactKeys(method, "java.class[].method[]",
                      {"name", "sig", "impl", "static"},
                      {"name", "sig", "impl", "static"});
            ProfileJavaMethod decoded_method;
            decoded_method.name =
                AsString(Require(method, "name", "java.class[].method[].name"),
                         "java.class[].method[].name");
            decoded_method.signature =
                AsString(Require(method, "sig", "java.class[].method[].sig"),
                         "java.class[].method[].sig");
            const auto closing = decoded_method.signature.find(')');
            if (!decoded_method.signature.starts_with('(') ||
                closing == std::string::npos ||
                closing + 1 >= decoded_method.signature.size()) {
                throw TitleProfileError("java method signature is invalid");
            }
            decoded_method.implementation =
                AsString(Require(method, "impl", "java.class[].method[].impl"),
                         "java.class[].method[].impl");
            decoded_method.is_static =
                AsBoolean(Require(method, "static",
                                  "java.class[].method[].static"),
                          "java.class[].method[].static");
            if (!ValidImplementationId(decoded_method.implementation) ||
                !method_keys.emplace(decoded_method.name, decoded_method.signature).second) {
                throw TitleProfileError(
                    "java methods must have valid bindings and unique name/signature pairs");
            }
            decoded.methods.push_back(std::move(decoded_method));
        }
        result.push_back(std::move(decoded));
    }
    return result;
}

[[nodiscard]] ProfileValue ConvertValue(const detail::TomlValue& source,
                                        const std::string_view field,
                                        const std::size_t depth) {
    if (depth > 4) {
        throw TitleProfileError(std::string(field) + " exceeds four nested tables");
    }
    return std::visit(
        [&](const auto& value) -> ProfileValue {
            using Value = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Value, Array>) {
                ProfileValue::Array result;
                result.reserve(value.size());
                for (const auto& item : value) {
                    result.push_back(ConvertValue(item, field, depth + 1));
                }
                return ProfileValue{std::move(result)};
            } else if constexpr (std::is_same_v<Value, Table>) {
                ProfileValue::Table result;
                for (const auto& [key, item] : value) {
                    if (!ValidId(key)) {
                        throw TitleProfileError(std::string(field) +
                                                " has invalid parameter " + key);
                    }
                    result.emplace(key, ConvertValue(item, field, depth + 1));
                }
                return ProfileValue{std::move(result)};
            } else {
                return ProfileValue{value};
            }
        },
        source.value);
}

[[nodiscard]] ProfileQuirks DecodeQuirks(const detail::TomlValue& value) {
    const auto& table = AsTable(value, "quirks");
    ProfileQuirks result;
    const auto& enabled =
        AsArray(Require(table, "enabled", "quirks.enabled"), "quirks.enabled");
    for (const auto& item : enabled) {
        auto id = AsString(item, "quirks.enabled[]");
        if (!ValidId(id)) throw TitleProfileError("quirks.enabled contains an invalid id");
        result.enabled.push_back(std::move(id));
    }
    RequireUnique(result.enabled, "quirks.enabled");
    for (const auto& [key, item] : table) {
        if (key == "enabled") continue;
        if (std::find(result.enabled.begin(), result.enabled.end(), key) ==
            result.enabled.end()) {
            throw TitleProfileError("quirks has parameters for a disabled quirk");
        }
        const auto& parameters = AsTable(item, "quirks parameters");
        auto converted = ConvertValue(detail::TomlValue{parameters}, "quirks." + key, 0);
        result.parameters.emplace(
            key, std::get<ProfileValue::Table>(std::move(converted.value)));
    }
    for (const auto& id : result.enabled) {
        if (!result.parameters.contains(id)) {
            throw TitleProfileError("quirks is missing an enabled parameter table");
        }
    }
    return result;
}

[[nodiscard]] ProfileInput DecodeInput(const detail::TomlValue& value) {
    const auto& table = AsTable(value, "input");
    ExactKeys(table, "input", {"profile"}, {"profile"});
    ProfileInput result{
        AsString(Require(table, "profile", "input.profile"), "input.profile")};
    if (!ValidId(result.profile)) {
        throw TitleProfileError("input.profile is not a valid template id");
    }
    return result;
}

[[nodiscard]] std::size_t LineCount(const std::string_view text) {
    if (text.empty()) return 0;
    return static_cast<std::size_t>(
               std::count(text.begin(), text.end(), '\n')) +
           (text.back() == '\n' ? 0U : 1U);
}

[[nodiscard]] bool HasProfileSuffix(const std::string_view filename) {
    constexpr std::string_view suffix = ".profile.toml";
    return filename.size() > suffix.size() && filename.ends_with(suffix);
}

[[nodiscard]] std::string PackageFromFilename(const std::string_view filename) {
    constexpr std::string_view suffix = ".profile.toml";
    if (!HasProfileSuffix(filename)) {
        throw TitleProfileError("Title Profile filename must end in .profile.toml");
    }
    return std::string(filename.substr(0, filename.size() - suffix.size()));
}

[[nodiscard]] bool Intersects(const auto& left, const auto& right) {
    return std::any_of(left.begin(), left.end(), [&](const auto& value) {
        return std::find(right.begin(), right.end(), value) != right.end();
    });
}

}  // namespace

TitleProfile LoadTitleProfileText(const std::string_view text,
                                  const std::string_view expected_package) {
    if (!ValidUtf8(text)) throw TitleProfileError("Title Profile is not valid UTF-8");
    if (LineCount(text) > 200) throw TitleProfileError("Title Profile exceeds 200 lines");
    if (!ValidPackage(expected_package)) {
        throw TitleProfileError("expected package is invalid");
    }
    const auto root = detail::ParseDataToml(text);
    ExactKeys(root, "profile",
              {"schema", "identity", "runtime", "data", "audio", "java", "quirks",
               "input"},
              {"schema", "identity", "runtime"});
    TitleProfile result;
    result.schema = static_cast<std::uint32_t>(
        AsInteger(Require(root, "schema", "schema"), "schema", 1, 1));
    result.identity = DecodeIdentity(root, expected_package);
    result.runtime = detail::DecodeProfileRuntime(root);
    if (const auto* data = Optional(root, "data")) result.data = DecodeData(*data);
    if (const auto* audio = Optional(root, "audio")) result.audio = DecodeAudio(*audio);
    if (const auto* java = Optional(root, "java")) {
        result.java_classes = DecodeJava(*java);
    }
    if (const auto* quirks = Optional(root, "quirks")) {
        result.quirks = DecodeQuirks(*quirks);
    }
    if (const auto* input = Optional(root, "input")) result.input = DecodeInput(*input);
    return result;
}

TitleProfile LoadTitleProfile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw TitleProfileError("cannot open Title Profile: " + path.string());
    const std::string text((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    return LoadTitleProfileText(text, PackageFromFilename(path.filename().string()));
}

[[nodiscard]] static std::vector<TitleProfile> LoadProfileDirectory(
    const std::filesystem::path& directory) {
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error) || error) {
        throw TitleProfileError("Title Profile directory is unavailable: " +
                                directory.string());
    }
    std::vector<std::filesystem::path> paths;
    for (std::filesystem::directory_iterator iterator(directory, error), end;
         !error && iterator != end; iterator.increment(error)) {
        if (iterator->is_regular_file() &&
            HasProfileSuffix(iterator->path().filename().string())) {
            paths.push_back(iterator->path());
        }
    }
    if (error) {
        throw TitleProfileError("cannot enumerate Title Profile directory: " +
                                directory.string());
    }
    std::sort(paths.begin(), paths.end());
    std::vector<TitleProfile> profiles;
    profiles.reserve(paths.size());
    for (const auto& path : paths) profiles.push_back(LoadTitleProfile(path));
    return profiles;
}

TitleProfileCatalog::TitleProfileCatalog(std::vector<TitleProfile> profiles)
    : profiles_(std::move(profiles)) {
    Validate(nullptr);
}

TitleProfileCatalog::TitleProfileCatalog(std::vector<TitleProfile> profiles,
                                         const QuirkRegistry& registry)
    : profiles_(std::move(profiles)) {
    Validate(&registry);
}

void TitleProfileCatalog::Validate(const QuirkRegistry* registry) const {
    for (const auto& profile : profiles_) {
        const auto& identity = profile.identity;
        if (profile.schema != 1 || !ValidPackage(identity.package) ||
            identity.version_codes.empty() || identity.so_sha256.empty() ||
            !ValidAbi(identity.abi) ||
            std::any_of(identity.version_codes.begin(), identity.version_codes.end(),
                        [](const std::uint32_t version) { return version == 0; }) ||
            std::any_of(identity.so_sha256.begin(), identity.so_sha256.end(),
                        [](const std::string& hash) { return !ValidHash(hash); })) {
            throw TitleProfileError("Title Profile catalog contains an invalid identity");
        }
        RequireUnique(identity.version_codes, "identity.version_code");
        RequireUnique(identity.so_sha256, "identity.so_sha256");
        if (profile.quirks.has_value() &&
            (!profile.quirks->enabled.empty() ||
             !profile.quirks->parameters.empty())) {
            if (registry == nullptr) {
                throw TitleProfileError(
                    "Title Profile with quirks requires a validated quirk registry");
            }
            registry->Validate(profile);
        }
    }
    for (std::size_t left = 0; left < profiles_.size(); ++left) {
        const auto& first = profiles_[left].identity;
        for (std::size_t right = left + 1; right < profiles_.size(); ++right) {
            const auto& second = profiles_[right].identity;
            if (first.package == second.package &&
                Intersects(first.version_codes, second.version_codes) &&
                Intersects(first.so_sha256, second.so_sha256)) {
                throw TitleProfileError("Title Profile catalog has an ambiguous fingerprint");
            }
        }
    }
}

TitleProfileCatalog TitleProfileCatalog::LoadDirectory(
    const std::filesystem::path& directory) {
    return TitleProfileCatalog(LoadProfileDirectory(directory));
}

TitleProfileCatalog TitleProfileCatalog::LoadDirectory(
    const std::filesystem::path& directory, const QuirkRegistry& registry) {
    return TitleProfileCatalog(LoadProfileDirectory(directory), registry);
}

const TitleProfile* TitleProfileCatalog::Match(const TitleIdentity& identity) const {
    if (!ValidPackage(identity.package) || identity.version_code == 0 ||
        !ValidHash(identity.so_sha256)) {
        throw TitleProfileError(
            "Title Profile match requires package, positive versionCode and SHA-256");
    }
    const TitleProfile* match = nullptr;
    for (const auto& profile : profiles_) {
        const auto& candidate = profile.identity;
        if (candidate.package == identity.package &&
            std::find(candidate.version_codes.begin(), candidate.version_codes.end(),
                      identity.version_code) != candidate.version_codes.end() &&
            std::find(candidate.so_sha256.begin(), candidate.so_sha256.end(),
                      identity.so_sha256) != candidate.so_sha256.end()) {
            if (match != nullptr) {
                throw TitleProfileError("Title Profile identity is ambiguous");
            }
            match = &profile;
        }
    }
    return match;
}

const std::vector<TitleProfile>& TitleProfileCatalog::Profiles() const noexcept {
    return profiles_;
}

std::string_view ToString(const ProfileLifecycle lifecycle) noexcept {
    switch (lifecycle) {
    case ProfileLifecycle::native_activity: return "native_activity";
    case ProfileLifecycle::gl_surface_view: return "gl_surface_view";
    case ProfileLifecycle::custom_jni: return "custom_jni";
    }
    return "unknown";
}

std::string_view ToString(const ProfileSource source) noexcept {
    switch (source) {
    case ProfileSource::apk: return "apk";
    case ProfileSource::obb: return "obb";
    case ProfileSource::external: return "external";
    }
    return "unknown";
}

std::string_view ToString(const ProfileAbi abi) noexcept {
    switch (abi) {
    case ProfileAbi::armeabi: return "armeabi";
    case ProfileAbi::armeabi_v7a: return "armeabi-v7a";
    }
    return "unknown";
}

std::string_view ToString(const ProfileNativeCallPhase phase) noexcept {
    switch (phase) {
    case ProfileNativeCallPhase::startup: return "startup";
    case ProfileNativeCallPhase::resume: return "resume";
    case ProfileNativeCallPhase::frame: return "frame";
    case ProfileNativeCallPhase::pause: return "pause";
    case ProfileNativeCallPhase::shutdown: return "shutdown";
    case ProfileNativeCallPhase::pointer_down: return "pointer_down";
    case ProfileNativeCallPhase::pointer_move: return "pointer_move";
    case ProfileNativeCallPhase::pointer_up: return "pointer_up";
    case ProfileNativeCallPhase::key_down: return "key_down";
    case ProfileNativeCallPhase::key_up: return "key_up";
    }
    return "unknown";
}

std::string_view ToString(const ProfileNativeDispatch dispatch) noexcept {
    switch (dispatch) {
    case ProfileNativeDispatch::instance: return "instance";
    case ProfileNativeDispatch::static_method: return "static";
    }
    return "unknown";
}

std::string_view ToString(const ProfileNativeArgumentSource source) noexcept {
    switch (source) {
    case ProfileNativeArgumentSource::constant: return "constant";
    case ProfileNativeArgumentSource::surface_width: return "surface_width";
    case ProfileNativeArgumentSource::surface_height: return "surface_height";
    case ProfileNativeArgumentSource::input_x: return "input_x";
    case ProfileNativeArgumentSource::input_y: return "input_y";
    case ProfileNativeArgumentSource::input_pointer: return "input_pointer";
    case ProfileNativeArgumentSource::input_key: return "input_key";
    }
    return "unknown";
}

}  // namespace ogplay::session
