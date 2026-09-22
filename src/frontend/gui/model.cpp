#include "ogplay/frontend/gui_model.h"
#include "ogplay/frontend/gui_settings.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cctype>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <system_error>
#include <utility>

#include "ogplay/core/text.h"
#include "ogplay/runtime/vfs/sandbox_store.h"
#include "ogplay/runtime/vfs/vfs.h"

namespace ogplay::frontend {
namespace {

constexpr std::uint32_t kSchema = 1;

[[nodiscard]] bool SafeEntryKey(const std::string_view key) {
    if (key.empty() || key == "." || key == ".." ||
        key.find('/') != std::string_view::npos ||
        key.find('\\') != std::string_view::npos) {
        return false;
    }
    return true;
}

[[nodiscard]] bool ImportTemporaryKey(const std::string_view key) {
    return key.size() > std::string_view("..importing").size() &&
           key.front() == '.' && key.ends_with(".importing");
}

[[nodiscard]] std::string EscapeToml(const std::string_view text) {
    std::string result;
    result.reserve(text.size() + 2);
    result.push_back('"');
    for (const char character : text) {
        switch (character) {
        case '\\': result.append("\\\\"); break;
        case '"': result.append("\\\""); break;
        case '\n': result.append("\\n"); break;
        case '\r': result.append("\\r"); break;
        case '\t': result.append("\\t"); break;
        default:
            if (static_cast<unsigned char>(character) < 0x20U) {
                throw GuiModelError(GuiModelErrorCode::invalid_argument,
                                    "TOML string contains a control character");
            }
            result.push_back(character);
            break;
        }
    }
    result.push_back('"');
    return result;
}

[[nodiscard]] std::string ParseTomlString(const std::string_view value) {
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') {
        throw std::runtime_error("value is not a TOML basic string");
    }
    std::string result;
    for (std::size_t index = 1; index + 1 < value.size(); ++index) {
        const char character = value[index];
        if (character != '\\') {
            if (static_cast<unsigned char>(character) < 0x20U) {
                throw std::runtime_error("TOML string contains a control character");
            }
            if (character == '"') throw std::runtime_error("TOML string has an unescaped quote");
            result.push_back(character);
            continue;
        }
        if (++index + 1 >= value.size()) throw std::runtime_error("truncated TOML escape");
        switch (value[index]) {
        case '\\': result.push_back('\\'); break;
        case '"': result.push_back('"'); break;
        case 'n': result.push_back('\n'); break;
        case 'r': result.push_back('\r'); break;
        case 't': result.push_back('\t'); break;
        default: throw std::runtime_error("unsupported TOML escape");
        }
    }
    if (!core::IsValidUtf8(result)) {
        throw std::runtime_error("TOML string is not valid UTF-8");
    }
    return result;
}

using FlatToml = std::map<std::string, std::string, std::less<>>;

[[nodiscard]] FlatToml ParseFlatToml(const std::string_view text) {
    if (!core::IsValidUtf8(text)) {
        throw std::runtime_error("TOML document is not valid UTF-8");
    }
    FlatToml values;
    std::size_t begin = 0;
    while (begin <= text.size()) {
        const auto end = text.find('\n', begin);
        auto line = std::string(core::TrimAsciiWhitespace(text.substr(
            begin, end == std::string_view::npos ? text.size() - begin : end - begin)));
        if (!line.empty()) {
            const auto equals = line.find('=');
            if (equals == std::string::npos) {
                throw std::runtime_error("TOML line must contain one assignment");
            }
            auto key = std::string(core::TrimAsciiWhitespace(
                std::string_view(line).substr(0, equals)));
            auto value = std::string(core::TrimAsciiWhitespace(
                std::string_view(line).substr(equals + 1)));
            if (key.empty() || value.empty() || !values.emplace(key, value).second) {
                throw std::runtime_error("TOML contains an empty or duplicate key");
            }
        }
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return values;
}

[[nodiscard]] std::string ReadText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open file");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

[[nodiscard]] std::vector<std::byte> ReadIcon(const std::filesystem::path& path) {
    constexpr std::uintmax_t maximum_icon_bytes = 4U * 1024U * 1024U;
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0 || size > maximum_icon_bytes) return {};
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (!input) return {};
    return bytes;
}

void WriteText(const std::filesystem::path& path, const std::string_view text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot create file");
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) throw std::runtime_error("cannot write complete file");
}

[[nodiscard]] std::string PathUtf8(const std::filesystem::path& path) {
    const auto bytes = path.generic_u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

[[nodiscard]] std::filesystem::path Utf8Path(const std::string_view text) {
    const auto* begin = reinterpret_cast<const char8_t*>(text.data());
    return std::filesystem::path(std::u8string(begin, begin + text.size()));
}

[[nodiscard]] std::uint32_t ParseUint32(const std::string_view value,
                                        const std::string_view field) {
    std::uint32_t result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (value.empty() || parsed.ec != std::errc{} ||
        parsed.ptr != value.data() + value.size()) {
        throw std::runtime_error(std::string(field) + " is not an unsigned integer");
    }
    return result;
}

[[nodiscard]] const std::string& Require(const FlatToml& values,
                                         const std::string_view key) {
    const auto item = values.find(key);
    if (item == values.end()) throw std::runtime_error(std::string(key) + " is missing");
    return item->second;
}

void ExactKeys(const FlatToml& values,
               const std::initializer_list<std::string_view> allowed) {
    for (const auto& [key, value] : values) {
        static_cast<void>(value);
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) {
            throw std::runtime_error("unknown TOML key: " + key);
        }
    }
}

void ValidateMetadata(const LibraryMetadata& metadata) {
    if (!core::IsValidPackageName(metadata.package)) {
        throw GuiModelError(GuiModelErrorCode::invalid_argument,
                            "library package name is invalid");
    }
    if (metadata.display_name.empty() || !core::IsValidUtf8(metadata.display_name) ||
        !core::IsValidUtf8(metadata.version_name) || metadata.imported_at.empty() ||
        !core::IsValidUtf8(metadata.imported_at)) {
        throw GuiModelError(GuiModelErrorCode::invalid_argument,
                            "library metadata is incomplete or invalid");
    }
    if (metadata.profile_id.has_value() &&
        (metadata.profile_id->empty() || !core::IsValidUtf8(*metadata.profile_id))) {
        throw GuiModelError(GuiModelErrorCode::invalid_argument,
                            "library profile id is invalid");
    }
    if (metadata.external_dir.has_value() && !metadata.external_dir->is_absolute()) {
        throw GuiModelError(GuiModelErrorCode::invalid_argument,
                            "library external directory must be absolute");
    }
}

[[nodiscard]] std::string EncodeMetadata(const LibraryMetadata& metadata) {
    ValidateMetadata(metadata);
    std::string text = "schema = 1\npackage = " + EscapeToml(metadata.package) +
                       "\ndisplay_name = " + EscapeToml(metadata.display_name) +
                       "\nversion_code = " + std::to_string(metadata.version_code) +
                       "\nversion_name = " + EscapeToml(metadata.version_name) +
                       "\nimported_at = " + EscapeToml(metadata.imported_at) + "\n";
    if (metadata.profile_id.has_value()) {
        text.append("profile_id = ").append(EscapeToml(*metadata.profile_id)).push_back('\n');
    }
    if (metadata.external_dir.has_value()) {
        text.append("external_dir = ")
            .append(EscapeToml(PathUtf8(*metadata.external_dir)))
            .push_back('\n');
    }
    return text;
}

[[nodiscard]] LibraryMetadata DecodeMetadata(const std::string_view text) {
    const auto values = ParseFlatToml(text);
    ExactKeys(values, {"schema", "package", "display_name", "version_code",
                       "version_name", "imported_at", "profile_id", "external_dir"});
    if (ParseUint32(Require(values, "schema"), "schema") != kSchema) {
        throw std::runtime_error("library metadata schema is not supported");
    }
    LibraryMetadata metadata;
    metadata.package = ParseTomlString(Require(values, "package"));
    metadata.display_name = ParseTomlString(Require(values, "display_name"));
    metadata.version_code = ParseUint32(Require(values, "version_code"), "version_code");
    metadata.version_name = ParseTomlString(Require(values, "version_name"));
    metadata.imported_at = ParseTomlString(Require(values, "imported_at"));
    if (const auto item = values.find("profile_id"); item != values.end()) {
        metadata.profile_id = ParseTomlString(item->second);
    }
    if (const auto item = values.find("external_dir"); item != values.end()) {
        metadata.external_dir = Utf8Path(ParseTomlString(item->second));
    }
    ValidateMetadata(metadata);
    return metadata;
}

[[nodiscard]] std::string EncodeConfig(const GuiConfig& config) {
    ValidateGuiConfigValues(config);
    std::string text = "schema = 2\n";
    if (config.profiles_dir) text += "profiles_dir = " + EscapeToml(PathUtf8(*config.profiles_dir)) + "\n";
    for (const auto& [key, value] : config.values) {
        text += key + " = ";
        if (const auto flag = std::get_if<bool>(&value)) text += *flag ? "true" : "false";
        else if (const auto number = std::get_if<std::uint32_t>(&value)) text += std::to_string(*number);
        else text += EscapeToml(std::get<std::string>(value));
        text += "\n";
    }
    return text;
}

[[nodiscard]] GuiConfig DecodeConfig(const std::string_view text) {
    const auto values = ParseFlatToml(text);
    const auto schema = ParseUint32(Require(values, "schema"), "schema");
    if (schema != 1 && schema != 2) throw std::runtime_error("GUI config schema is not supported");
    if (schema == 1) ExactKeys(values, {"schema", "system_dir", "profiles_dir"});
    GuiConfig config;
    for (const auto& [key, value] : values) {
        if (key == "schema") continue;
        if (schema == 1 && key == "system_dir") { static_cast<void>(ParseTomlString(value)); continue; }
        const auto& field = FindGuiSetting(key);
        if (std::holds_alternative<bool>(field.initial)) {
            if (value != "true" && value != "false") throw std::runtime_error(key + " requires a boolean");
            SetGuiSetting(config, key, value == "true");
        } else if (std::holds_alternative<std::uint32_t>(field.initial)) SetGuiSetting(config, key, ParseUint32(value, key));
        else SetGuiSetting(config, key, ParseTomlString(value));
    }
    return config;
}

void RemoveTree(const std::filesystem::path& path) {
    std::error_code error;
    static_cast<void>(std::filesystem::remove_all(path, error));
    if (error) throw std::filesystem::filesystem_error("remove_all", path, error);
}

}  // namespace

GuiModelError::GuiModelError(const GuiModelErrorCode code, std::string message,
                             std::filesystem::path path)
    : std::runtime_error(std::move(message)), code_(code), path_(std::move(path)) {}

GuiModelErrorCode GuiModelError::Code() const noexcept { return code_; }

const std::filesystem::path& GuiModelError::Path() const noexcept { return path_; }

GuiConfig LoadGuiConfig(const std::filesystem::path& library_root) {
    const auto path = library_root / "config.toml";
    auto backup = path;
    backup += ".bak";
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        if (error) {
            throw GuiModelError(GuiModelErrorCode::io_error,
                                "cannot inspect GUI config", path);
        }
        error.clear();
        if (!std::filesystem::exists(backup, error)) {
            if (error) {
                throw GuiModelError(GuiModelErrorCode::io_error,
                                    "cannot inspect GUI config backup", backup);
            }
            return {};
        }
        std::filesystem::rename(backup, path, error);
        if (error) {
            throw GuiModelError(GuiModelErrorCode::io_error,
                                "cannot recover GUI config backup", backup);
        }
    }
    try {
        return DecodeConfig(ReadText(path));
    } catch (const std::exception& exception) {
        throw GuiModelError(GuiModelErrorCode::corrupt_config,
                            std::string("GUI config is damaged: ") + exception.what(), path);
    }
}

void ValidateGuiConfigDirectories(const GuiConfig& config) {
    const auto require_directory = [](const std::filesystem::path& path,
                                      const std::string_view name) {
        std::error_code error;
        if (path.empty() || !std::filesystem::is_directory(path, error) || error) {
            throw GuiModelError(GuiModelErrorCode::not_found,
                                std::string(name) + " is unavailable", path);
        }
    };
    ValidateGuiConfigValues(config);
    for (const auto& field : GuiSettings()) {
        if (!field.directory) continue;
        const auto text = std::get<std::string>(GuiSetting(config, field.key));
        if (!text.empty()) require_directory(Utf8Path(text), field.label);
    }
}

void SaveGuiConfig(const std::filesystem::path& library_root,
                   const GuiConfig& config) {
    try {
        std::filesystem::create_directories(library_root);
        const auto target = library_root / "config.toml";
        auto temporary = target;
        temporary += ".tmp";
        auto backup = target;
        backup += ".bak";
        WriteText(temporary, EncodeConfig(config));
        std::error_code error;
        static_cast<void>(std::filesystem::remove(backup, error));
        if (error) {
            throw std::filesystem::filesystem_error("remove", backup, error);
        }
        const auto had_target = std::filesystem::exists(target, error);
        if (error) {
            throw std::filesystem::filesystem_error("exists", target, error);
        }
        if (had_target) {
            std::filesystem::rename(target, backup, error);
            if (error) {
                throw std::filesystem::filesystem_error(
                    "rename", target, backup, error);
            }
        }
        std::filesystem::rename(temporary, target, error);
        if (error) {
            static_cast<void>(std::filesystem::remove(temporary));
            if (had_target) {
                std::error_code restore_error;
                std::filesystem::rename(backup, target, restore_error);
            }
            throw std::filesystem::filesystem_error("rename", temporary, target, error);
        }
        static_cast<void>(std::filesystem::remove(backup, error));
    } catch (const GuiModelError&) {
        throw;
    } catch (const std::exception& exception) {
        throw GuiModelError(GuiModelErrorCode::io_error,
                            std::string("cannot save GUI config: ") + exception.what(),
                            library_root / "config.toml");
    }
}

LibraryStore::LibraryStore(std::filesystem::path library_root)
    : root_() {
    if (library_root.empty()) {
        throw GuiModelError(GuiModelErrorCode::invalid_argument,
                            "library root must not be empty");
    }
    root_ = std::filesystem::absolute(std::move(library_root)).lexically_normal();
}

const std::filesystem::path& LibraryStore::Root() const noexcept { return root_; }

std::filesystem::path LibraryStore::EntriesRoot() const { return root_ / "library"; }

std::string LibraryStore::NextInstallationId(
    const std::string_view package) const {
    if (!core::IsValidPackageName(package) || package.find('-') != std::string_view::npos) {
        throw GuiModelError(GuiModelErrorCode::invalid_argument,
                            "package cannot be used as an installation id");
    }
    std::set<std::uint32_t> occupied;
    const auto collect = [&](const std::filesystem::path& parent) {
        std::error_code error;
        if (!std::filesystem::exists(parent, error)) return;
        for (std::filesystem::directory_iterator it(parent, error), end;
             !error && it != end; it.increment(error)) {
            if (!it->is_directory(error)) continue;
            const auto name = PathUtf8(it->path().filename());
            if (name == package) {
                occupied.insert(1U);
                continue;
            }
            if (!name.starts_with(package) || name.size() <= package.size() + 1U ||
                name[package.size()] != '-') continue;
            std::uint32_t number{};
            const auto suffix = std::string_view(name).substr(package.size() + 1U);
            const auto parsed = std::from_chars(suffix.data(), suffix.data() + suffix.size(), number);
            if (parsed.ec == std::errc{} && parsed.ptr == suffix.data() + suffix.size() &&
                number >= 2U && suffix.front() != '0') {
                occupied.insert(number);
            }
        }
        if (error) {
            throw GuiModelError(GuiModelErrorCode::io_error,
                                "cannot inspect installation ids", parent);
        }
    };
    collect(EntriesRoot());
    collect(root_ / "sandbox");
    std::uint32_t number = 1U;
    while (occupied.contains(number)) ++number;
    return number == 1U ? std::string(package)
                        : std::string(package) + "-" + std::to_string(number);
}

std::vector<LibraryEntry> LibraryStore::LoadEntries() const {
    std::vector<LibraryEntry> entries;
    std::error_code error;
    std::filesystem::create_directories(EntriesRoot(), error);
    if (error) {
        throw GuiModelError(GuiModelErrorCode::io_error,
                            "cannot create game library", EntriesRoot());
    }
    std::vector<std::filesystem::path> stale_imports;
    for (std::filesystem::directory_iterator iterator(EntriesRoot(), error), end;
         !error && iterator != end; iterator.increment(error)) {
        if (iterator->is_directory(error) &&
            ImportTemporaryKey(PathUtf8(iterator->path().filename()))) {
            stale_imports.push_back(iterator->path());
        }
    }
    if (error) {
        throw GuiModelError(GuiModelErrorCode::io_error,
                            "cannot inspect import temporary directories",
                            EntriesRoot());
    }
    for (const auto& stale : stale_imports) {
        try {
            RemoveTree(stale);
        } catch (const std::exception& exception) {
            throw GuiModelError(
                GuiModelErrorCode::io_error,
                std::string("cannot clean stale import: ") + exception.what(),
                stale);
        }
    }
    for (std::filesystem::directory_iterator iterator(EntriesRoot(), error), end;
         !error && iterator != end; iterator.increment(error)) {
        if (!iterator->is_directory(error)) {
            if (error) break;
            continue;
        }
        const auto key = PathUtf8(iterator->path().filename());
        LibraryEntry entry{.key = key, .directory = iterator->path()};
        try {
            auto metadata = DecodeMetadata(ReadText(iterator->path() / "meta.toml"));
            const auto expected_prefix = metadata.package + "-";
            if (key != metadata.package && !key.starts_with(expected_prefix))
                throw std::runtime_error("metadata package does not match installation id");
            if (!std::filesystem::is_regular_file(iterator->path() / "game.apk")) {
                throw std::runtime_error("library APK copy is missing");
            }
            entry.metadata = std::move(metadata);
            entry.icon_png = ReadIcon(iterator->path() / "icon.png");
        } catch (const std::exception& exception) {
            entry.damage_reason = exception.what();
        }
        entries.push_back(std::move(entry));
    }
    if (error) {
        throw GuiModelError(GuiModelErrorCode::io_error,
                            "cannot enumerate game library", EntriesRoot());
    }
    std::sort(entries.begin(), entries.end(), [](const LibraryEntry& left,
                                                  const LibraryEntry& right) {
        const auto& left_name = left.metadata ? left.metadata->display_name : left.key;
        const auto& right_name = right.metadata ? right.metadata->display_name : right.key;
        if (left_name != right_name) return left_name < right_name;
        return left.key < right.key;
    });
    return entries;
}

std::string LibraryStore::Import(const LibraryImport& request) {
    ValidateMetadata(request.metadata);
    std::error_code error;
    if (!std::filesystem::is_regular_file(request.source_apk, error) || error) {
        throw GuiModelError(GuiModelErrorCode::not_found,
                            "source APK is unavailable", request.source_apk);
    }
    std::filesystem::create_directories(EntriesRoot(), error);
    if (error) {
        throw GuiModelError(GuiModelErrorCode::io_error,
                            "cannot create game library", EntriesRoot());
    }
    std::filesystem::create_directories(root_ / "sandbox", error);
    if (error) {
        throw GuiModelError(GuiModelErrorCode::io_error,
                            "cannot create sandbox root", root_ / "sandbox");
    }
    static std::atomic_uint64_t next_import_token{};
    std::filesystem::path temporary;
    for (;;) {
        const auto token = next_import_token.fetch_add(1U, std::memory_order_relaxed);
        temporary = EntriesRoot() /
                    ("." + request.metadata.package + "." + std::to_string(token) +
                     ".importing");
        error.clear();
        if (std::filesystem::create_directory(temporary, error)) break;
        if (error) {
            throw GuiModelError(GuiModelErrorCode::io_error,
                                "cannot reserve import temporary directory", temporary);
        }
    }
    std::filesystem::path target;
    std::filesystem::path reserved_sandbox;
    try {
        std::filesystem::copy_file(request.source_apk, temporary / "game.apk",
                                   std::filesystem::copy_options::none);
        WriteText(temporary / "meta.toml", EncodeMetadata(request.metadata));
        if (!request.icon_png.empty()) {
            std::ofstream icon(temporary / "icon.png", std::ios::binary | std::ios::trunc);
            if (!icon) throw std::runtime_error("cannot create icon cache");
            icon.write(reinterpret_cast<const char*>(request.icon_png.data()),
                       static_cast<std::streamsize>(request.icon_png.size()));
            if (!icon) throw std::runtime_error("cannot write icon cache");
        }
        for (;;) {
            const auto installation_id = NextInstallationId(request.metadata.package);
            target = EntriesRoot() / installation_id;
            try {
                auto reservation = runtime::SandboxStore::Create(
                    root_ / "sandbox", installation_id, request.metadata.package);
                reserved_sandbox = reservation->Directory();
            } catch (const runtime::VfsError& reservation_error) {
                if (reservation_error.ErrorNumber() == 17) continue;
                throw;
            }
            error.clear();
            std::filesystem::rename(temporary, target, error);
            if (!error) break;
            std::error_code exists_error;
            const bool collision = std::filesystem::exists(target, exists_error);
            if (exists_error || !collision) {
                throw std::filesystem::filesystem_error(
                    "rename", temporary, target,
                    exists_error ? exists_error : error);
            }
            std::filesystem::remove_all(reserved_sandbox, exists_error);
            if (exists_error) {
                throw std::filesystem::filesystem_error(
                    "remove sandbox reservation", reserved_sandbox,
                    exists_error);
            }
            reserved_sandbox.clear();
        }
    } catch (const GuiModelError&) {
        if (std::filesystem::exists(temporary)) RemoveTree(temporary);
        throw;
    } catch (const std::exception& exception) {
        std::error_code cleanup_error;
        static_cast<void>(std::filesystem::remove_all(temporary, cleanup_error));
        if (!reserved_sandbox.empty()) {
            static_cast<void>(std::filesystem::remove_all(
                reserved_sandbox, cleanup_error));
        }
        throw GuiModelError(GuiModelErrorCode::io_error,
                            std::string("cannot import game: ") + exception.what(), target);
    }
    return PathUtf8(target.filename());
}

void LibraryStore::Remove(const std::string_view key) {
    if (!SafeEntryKey(key)) {
        throw GuiModelError(GuiModelErrorCode::invalid_argument,
                            "library entry key is unsafe");
    }
    const auto target = EntriesRoot() / Utf8Path(key);
    std::error_code error;
    if (!std::filesystem::is_directory(target, error) || error) {
        throw GuiModelError(GuiModelErrorCode::not_found,
                            "library entry does not exist", target);
    }
    try {
        RemoveTree(target);
    } catch (const std::exception& exception) {
        throw GuiModelError(GuiModelErrorCode::io_error,
                            std::string("cannot remove library entry: ") + exception.what(),
                            target);
    }
}

}  // namespace ogplay::frontend
