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

#include "ogplay/core/text.h"
#include "title_profile_toml.h"

namespace ogplay::session {
namespace {

using Table = detail::TomlValue::Table;
using Array = detail::TomlValue::Array;

[[nodiscard]] const detail::TomlValue& Require(const Table& table, const std::string_view key,
                                               const std::string_view field) {
    const auto iterator = table.find(key);
    if (iterator == table.end()) {
        throw TitleProfileError(std::string(field) + " is missing");
    }
    return iterator->second;
}

[[nodiscard]] const detail::TomlValue* Optional(const Table& table, const std::string_view key) {
    const auto iterator = table.find(key);
    return iterator == table.end() ? nullptr : &iterator->second;
}

template <typename Value>
[[nodiscard]] const Value& As(const detail::TomlValue& value, const std::string_view field,
                              const std::string_view expected) {
    const auto* result = std::get_if<Value>(&value.value);
    if (result == nullptr) {
        throw TitleProfileError(std::string(field) + " must be " + std::string(expected));
    }
    return *result;
}

[[nodiscard]] const Table& AsTable(const detail::TomlValue& value, const std::string_view field) {
    return As<Table>(value, field, "a table");
}

[[nodiscard]] const Array& AsArray(const detail::TomlValue& value, const std::string_view field) {
    return As<Array>(value, field, "an array");
}

[[nodiscard]] const std::string& AsString(const detail::TomlValue& value, const std::string_view field) {
    const auto& result = As<std::string>(value, field, "a string");
    if (result.empty()) {
        throw TitleProfileError(std::string(field) + " must not be empty");
    }
    return result;
}

[[nodiscard]] std::int64_t AsInteger(const detail::TomlValue& value, const std::string_view field,
                                     const std::int64_t minimum, const std::int64_t maximum) {
    const auto result = As<std::int64_t>(value, field, "an integer");
    if (result < minimum || result > maximum) {
        throw TitleProfileError(std::string(field) + " is outside the supported range");
    }
    return result;
}

void ExactKeys(const Table& table, const std::string_view field, const std::initializer_list<std::string_view> allowed,
               const std::initializer_list<std::string_view> required = {}) {
    detail::ValidateExactTableKeys(table, field, allowed, required);
}

[[nodiscard]] bool ValidHash(const std::string_view digest) {
    return digest.size() == 64 && std::all_of(digest.begin(), digest.end(), [](const char character) {
               return std::isdigit(static_cast<unsigned char>(character)) != 0 ||
                      (character >= 'a' && character <= 'f');
           });
}

template <typename Value>
void RequireUnique(const std::vector<Value>& values, const std::string_view field) {
    std::set<Value> unique(values.begin(), values.end());
    if (unique.size() != values.size()) {
        throw TitleProfileError(std::string(field) + " must not contain duplicates");
    }
}

[[nodiscard]] ProfileIdentity DecodeIdentity(const Table& root, const std::string_view expected_package) {
    const auto& table = AsTable(Require(root, "identity", "identity"), "identity");
    ExactKeys(table, "identity", {"package", "version_code", "so_sha256"}, {"package"});
    ProfileIdentity result;
    result.package = AsString(Require(table, "package", "identity.package"), "identity.package");
    if (!core::IsValidPackageName(result.package)) {
        throw TitleProfileError("identity.package is invalid");
    }
    if (result.package != expected_package) {
        throw TitleProfileError("identity.package does not match the profile filename");
    }
    if (const auto* version_value = Optional(table, "version_code")) {
        const auto& versions = AsArray(*version_value, "identity.version_code");
        if (versions.empty()) {
            throw TitleProfileError("identity.version_code must not be empty");
        }
        for (std::size_t index = 0; index < versions.size(); ++index) {
            result.version_codes.push_back(static_cast<std::uint32_t>(
                AsInteger(versions[index], "identity.version_code", 1, std::numeric_limits<std::uint32_t>::max())));
        }
        RequireUnique(result.version_codes, "identity.version_code");
    }
    if (const auto* hash_value = Optional(table, "so_sha256")) {
        const auto& hashes = AsArray(*hash_value, "identity.so_sha256");
        if (hashes.empty()) {
            throw TitleProfileError("identity.so_sha256 must not be empty");
        }
        for (const auto& value : hashes) {
            auto digest = AsString(value, "identity.so_sha256");
            if (!ValidHash(digest)) {
                throw TitleProfileError("identity.so_sha256 must contain lowercase SHA-256 digests");
            }
            result.so_sha256.push_back(std::move(digest));
        }
        RequireUnique(result.so_sha256, "identity.so_sha256");
    }
    return result;
}

[[nodiscard]] std::size_t LineCount(const std::string_view text) {
    if (text.empty()) return 0;
    return static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n')) + (text.back() == '\n' ? 0U : 1U);
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
    return std::any_of(left.begin(), left.end(),
                       [&](const auto& value) { return std::find(right.begin(), right.end(), value) != right.end(); });
}

[[nodiscard]] bool ApplicabilityOverlaps(const auto& left, const auto& right) {
    return left.empty() || right.empty() || Intersects(left, right);
}

}  // namespace

TitleProfile LoadTitleProfileText(const std::string_view text, const std::string_view expected_package) {
    if (!core::IsValidUtf8(text)) {
        throw TitleProfileError("Title Profile is not valid UTF-8");
    }
    if (LineCount(text) > 200) throw TitleProfileError("Title Profile exceeds 200 lines");
    if (!core::IsValidPackageName(expected_package)) {
        throw TitleProfileError("expected package is invalid");
    }
    const auto root = detail::ParseDataToml(text);
    ExactKeys(root, "profile", {"schema", "identity"}, {"schema", "identity"});
    TitleProfile result;
    result.schema = static_cast<std::uint32_t>(AsInteger(Require(root, "schema", "schema"), "schema", 4, 4));
    result.identity = DecodeIdentity(root, expected_package);
    return result;
}

TitleProfile LoadTitleProfile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw TitleProfileError("cannot open Title Profile: " + path.string());
    const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return LoadTitleProfileText(text, PackageFromFilename(path.filename().string()));
}

[[nodiscard]] static std::vector<TitleProfile> LoadProfileDirectory(const std::filesystem::path& directory,
                                                                   bool allow_missing) {
    std::error_code error;
    const auto status = std::filesystem::status(directory, error);
    if (allow_missing && status.type() == std::filesystem::file_type::not_found) {
        return {};
    }
    if (!std::filesystem::is_directory(status) || error) {
        throw TitleProfileError("Title Profile directory is unavailable: " + directory.string());
    }
    std::vector<std::filesystem::path> paths;
    for (std::filesystem::directory_iterator iterator(directory, error), end; !error && iterator != end;
         iterator.increment(error)) {
        if (iterator->is_regular_file() && HasProfileSuffix(iterator->path().filename().string())) {
            paths.push_back(iterator->path());
        }
    }
    if (error) {
        throw TitleProfileError("cannot enumerate Title Profile directory: " + directory.string());
    }
    std::sort(paths.begin(), paths.end());
    std::vector<TitleProfile> profiles;
    profiles.reserve(paths.size());
    for (const auto& path : paths)
        profiles.push_back(LoadTitleProfile(path));
    return profiles;
}

TitleProfileCatalog::TitleProfileCatalog(std::vector<TitleProfile> profiles) : profiles_(std::move(profiles)) {
    Validate();
}

void TitleProfileCatalog::Validate() const {
    for (const auto& profile : profiles_) {
        const auto& identity = profile.identity;
        if (profile.schema != 4U || !core::IsValidPackageName(identity.package) ||
            std::any_of(identity.version_codes.begin(), identity.version_codes.end(),
                        [](const std::uint32_t version) { return version == 0; }) ||
            std::any_of(identity.so_sha256.begin(), identity.so_sha256.end(),
                        [](const std::string& hash) { return !ValidHash(hash); })) {
            throw TitleProfileError("Title Profile catalog contains an invalid identity");
        }
        RequireUnique(identity.version_codes, "identity.version_code");
        RequireUnique(identity.so_sha256, "identity.so_sha256");
    }
    for (std::size_t left = 0; left < profiles_.size(); ++left) {
        const auto& first = profiles_[left].identity;
        for (std::size_t right = left + 1; right < profiles_.size(); ++right) {
            const auto& second = profiles_[right].identity;
            if (first.package == second.package && ApplicabilityOverlaps(first.version_codes, second.version_codes) &&
                ApplicabilityOverlaps(first.so_sha256, second.so_sha256)) {
                throw TitleProfileError("Title Profile catalog has an ambiguous applicability scope");
            }
        }
    }
}

TitleProfileCatalog TitleProfileCatalog::LoadDirectory(const std::filesystem::path& directory) {
    return TitleProfileCatalog(LoadProfileDirectory(directory, false));
}

TitleProfileCatalog TitleProfileCatalog::LoadOptionalDirectory(const std::filesystem::path& directory) {
    return TitleProfileCatalog(LoadProfileDirectory(directory, true));
}

const std::vector<TitleProfile>& TitleProfileCatalog::Profiles() const noexcept {
    return profiles_;
}

}  // namespace ogplay::session
