#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/frontend/gui_model.h"

namespace ogplay::frontend {

enum class LibraryTileStatus : std::uint8_t {
    damaged,
    profile_catalog_unavailable,
    missing_profile,
    missing_external,
    running,
    ready,
};

enum class LibraryConditionStatus : std::uint8_t {
    ready,
    missing,
    not_required,
    unavailable,
};

struct LibraryViewContext final {
    std::vector<std::string> running_packages;
    std::vector<std::string> external_required_packages;
    std::optional<std::string> profile_catalog_error;
};

struct LibraryTile final {
    std::string key;
    std::string display_name;
    LibraryTileStatus status{LibraryTileStatus::damaged};
    std::vector<std::byte> icon_png;
    std::string detail;
    bool running{};
    bool can_launch{};
};

struct LibraryCondition final {
    LibraryConditionStatus status{LibraryConditionStatus::unavailable};
    std::string value;
    std::string detail;
};

struct LibraryDetail final {
    std::string key;
    std::string display_name;
    std::string package;
    std::string version;
    LibraryTileStatus status{LibraryTileStatus::damaged};
    std::vector<std::byte> icon_png;
    std::string detail;
    LibraryCondition profile;
    LibraryCondition external;
    bool can_launch{};
    bool can_delete{};
};

class LibrarySelection final {
public:
    void Select(std::string_view key, std::span<const LibraryTile> tiles);
    void Reconcile(std::span<const LibraryTile> tiles);
    [[nodiscard]] const std::optional<std::string>& Key() const noexcept;

private:
    std::optional<std::string> key_;
    std::size_t index_{};
};

struct GuiMessage final {
    std::string title;
    std::string message;
};

// FIFO presentation model. A queued diagnostic may become active only while
// no unrelated popup is open, so process results cannot displace workflows.
class GuiMessageQueue final {
public:
    void Push(std::string title, std::string message);
    [[nodiscard]] bool ActivateNext(bool another_popup_open);
    [[nodiscard]] const GuiMessage* Active() const noexcept;
    void DismissActive() noexcept;

private:
    std::deque<GuiMessage> pending_;
    std::optional<GuiMessage> active_;
};

[[nodiscard]] std::vector<LibraryTile> BuildLibraryTiles(
    std::span<const LibraryEntry> entries, const LibraryViewContext& context);

[[nodiscard]] LibraryDetail BuildLibraryDetail(
    const LibraryEntry& entry, const LibraryTile& tile,
    const LibraryViewContext& context);

// Returns the first regular file, preserving candidate order. An empty result
// means the view must keep ImGui's ASCII font and log a warning.
[[nodiscard]] std::filesystem::path SelectCjkFont(
    std::span<const std::filesystem::path> candidates);

[[nodiscard]] std::uint32_t GuiEventWaitMilliseconds(
    bool bounded_smoke) noexcept;

}  // namespace ogplay::frontend
