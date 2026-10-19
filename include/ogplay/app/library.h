#pragma once
#include "ogplay/app/export.h"
#include "ogplay/app/game_run.h"
#include "ogplay/app/host.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <map>
#include <memory>
#include <variant>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ogplay::app {

enum class LibraryErrorCode : std::uint8_t {
    invalid_argument,
    io_error,
    duplicate_package,
    corrupt_config,
    not_found,
    busy,
    running,
};

class OGPLAYCORE_API LibraryError final : public std::runtime_error {
public:
    LibraryError(LibraryErrorCode code, std::string message,
                  std::filesystem::path path = {});

    [[nodiscard]] LibraryErrorCode Code() const noexcept;
    [[nodiscard]] const std::filesystem::path& Path() const noexcept;

private:
    LibraryErrorCode code_;
    std::filesystem::path path_;
};

using SettingValue = std::variant<bool, std::uint32_t, std::string>;

struct LibraryConfig final {
    std::optional<std::filesystem::path> profiles_dir;
    std::map<std::string, SettingValue, std::less<>> values;

    bool operator==(const LibraryConfig&) const = default;
};

[[nodiscard]] OGPLAYCORE_API LibraryConfig LoadLibraryConfig(const std::filesystem::path& library_root);
OGPLAYCORE_API void ValidateLibraryConfigDirectories(const LibraryConfig& config);
OGPLAYCORE_API void SaveLibraryConfig(const std::filesystem::path& library_root,
                   const LibraryConfig& config);

struct LibraryMetadata final {
    std::string package;
    std::string display_name;
    std::uint32_t version_code{};
    std::string version_name;
    std::string imported_at;
    std::optional<std::string> profile_id;
    std::optional<std::filesystem::path> external_dir;
    std::string external_guest_dir{"/sdcard"};

    bool operator==(const LibraryMetadata&) const = default;
};

struct LibraryEntry final {
    std::string key;
    std::filesystem::path directory;
    std::optional<LibraryMetadata> metadata;
    std::vector<std::byte> icon_png;
    std::optional<std::string> damage_reason;

    [[nodiscard]] bool Damaged() const noexcept {
        return damage_reason.has_value();
    }
};

struct LibraryImport final {
    std::filesystem::path source_apk;
    LibraryMetadata metadata;
    std::vector<std::byte> icon_png;
};

class OGPLAYCORE_API LibraryStore final {
public:
    explicit LibraryStore(std::filesystem::path library_root);

    [[nodiscard]] const std::filesystem::path& Root() const noexcept;
    [[nodiscard]] std::filesystem::path EntriesRoot() const;
    [[nodiscard]] std::string NextInstallationId(
        std::string_view package) const;
    [[nodiscard]] std::vector<LibraryEntry> LoadEntries() const;
    std::string Import(const LibraryImport& request, bool new_instance = false);
    [[nodiscard]] bool IsRunning(std::string_view key) const;
    void Remove(std::string_view key);

private:
    std::filesystem::path root_;
};

}  // namespace ogplay::app

namespace ogplay::app {
struct SettingDefinition {
    std::string key, group, label;
    SettingValue initial;
    std::vector<std::string> choices{};
    std::uint32_t minimum{}, maximum{};
    std::string pending{};
    bool directory{};
};
[[nodiscard]] OGPLAYCORE_API const std::vector<SettingDefinition>& SettingDefinitions();
[[nodiscard]] OGPLAYCORE_API const SettingDefinition& FindSetting(std::string_view key);
[[nodiscard]] OGPLAYCORE_API SettingValue Setting(const LibraryConfig& config, std::string_view key);
OGPLAYCORE_API void SetSetting(LibraryConfig& config, std::string_view key, SettingValue value);
OGPLAYCORE_API void ValidateLibraryConfigValues(const LibraryConfig& config);
OGPLAYCORE_API void ValidateSettingValue(const SettingDefinition& field, const SettingValue& value);

// Missing keys inherit the global value (or the field default).
struct GameSettings final {
    std::map<std::string, SettingValue, std::less<>> values;
    bool operator==(const GameSettings&) const = default;
};
[[nodiscard]] OGPLAYCORE_API const std::vector<SettingDefinition>& GameSettingDefinitions();
[[nodiscard]] OGPLAYCORE_API const SettingDefinition& FindGameSetting(std::string_view key);
OGPLAYCORE_API void ValidateGameSettings(const GameSettings& settings, bool directories = false);
[[nodiscard]] OGPLAYCORE_API GameSettings LoadGameSettings(const std::filesystem::path& entry_directory);
OGPLAYCORE_API GameSettings PatchGameSettings(const LibraryStore& store, std::string_view id, const GameSettings& changes, std::span<const std::string> unset);
OGPLAYCORE_API void SaveGameSettings(const std::filesystem::path& entry_directory, const GameSettings& settings);
[[nodiscard]] OGPLAYCORE_API SettingValue EffectiveGameSetting(const GameSettings& settings, const LibraryConfig& global,
                                                   const LibraryEntry& entry, std::string_view key);
[[nodiscard]] OGPLAYCORE_API LibraryConfig EffectiveGameConfig(const GameSettings& settings, const LibraryConfig& global);
[[nodiscard]] OGPLAYCORE_API LibraryEntry EffectiveGameEntry(const LibraryEntry& entry, const GameSettings& settings);
}

namespace ogplay::app {
enum class LaunchMode { normal, preflight, diagnostic };
struct ResolvedLaunch final {
    GameRunOptions options;
    std::optional<std::uint16_t> mcp_port;
    bool dashboard_auto_open{};
};
[[nodiscard]] OGPLAYCORE_API std::filesystem::path LauncherSandboxRoot(const std::filesystem::path& root);
[[nodiscard]] OGPLAYCORE_API ResolvedLaunch ResolveLaunch(const LibraryStore& store, std::string_view installation_id, LaunchMode mode = LaunchMode::normal);
// Value overload supports previews with an explicit global configuration.
[[nodiscard]] OGPLAYCORE_API ResolvedLaunch ResolveLaunch(const LibraryStore& store, const LibraryEntry& entry, const LibraryConfig& config, LaunchMode mode = LaunchMode::normal);
class OGPLAYCORE_API RunningLease final {
public:
    RunningLease(const std::filesystem::path& root, std::string_view installation_id);
    ~RunningLease();
    RunningLease(RunningLease&&) noexcept;
    RunningLease& operator=(RunningLease&&) noexcept;
    RunningLease(const RunningLease&) = delete;
    RunningLease& operator=(const RunningLease&) = delete;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
