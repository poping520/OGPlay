#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ogplay::frontend {

enum class GuiModelErrorCode : std::uint8_t {
    invalid_argument,
    io_error,
    duplicate_package,
    corrupt_config,
    not_found,
};

class GuiModelError final : public std::runtime_error {
public:
    GuiModelError(GuiModelErrorCode code, std::string message,
                  std::filesystem::path path = {});

    [[nodiscard]] GuiModelErrorCode Code() const noexcept;
    [[nodiscard]] const std::filesystem::path& Path() const noexcept;

private:
    GuiModelErrorCode code_;
    std::filesystem::path path_;
};

struct GuiConfig final {
    std::optional<std::filesystem::path> profiles_dir;

    bool operator==(const GuiConfig&) const = default;
};

[[nodiscard]] GuiConfig LoadGuiConfig(const std::filesystem::path& library_root);
void ValidateGuiConfigDirectories(const GuiConfig& config);
void SaveGuiConfig(const std::filesystem::path& library_root,
                   const GuiConfig& config);

struct LibraryMetadata final {
    std::string package;
    std::string display_name;
    std::uint32_t version_code{};
    std::string version_name;
    std::string imported_at;
    std::optional<std::string> profile_id;
    std::optional<std::filesystem::path> external_dir;

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

class LibraryStore final {
public:
    explicit LibraryStore(std::filesystem::path library_root);

    [[nodiscard]] const std::filesystem::path& Root() const noexcept;
    [[nodiscard]] std::filesystem::path EntriesRoot() const;
    [[nodiscard]] std::vector<LibraryEntry> LoadEntries() const;
    void Import(const LibraryImport& request);
    void Remove(std::string_view key);

private:
    std::filesystem::path root_;
};

}  // namespace ogplay::frontend
