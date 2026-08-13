#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "ogplay/frontend/gui_model.h"

namespace ogplay::frontend {

enum class LibraryTileStatus : std::uint8_t {
    damaged,
    missing_profile,
    missing_external,
    running,
    ready,
};

struct LibraryViewContext final {
    std::vector<std::string> running_packages;
    std::vector<std::string> external_required_packages;
};

struct LibraryTile final {
    std::string key;
    std::string display_name;
    LibraryTileStatus status{LibraryTileStatus::damaged};
    std::vector<std::byte> icon_png;
    std::string detail;
};

[[nodiscard]] std::vector<LibraryTile> BuildLibraryTiles(
    std::span<const LibraryEntry> entries, const LibraryViewContext& context);

// Returns the first regular file, preserving candidate order. An empty result
// means the view must keep ImGui's ASCII font and log a warning.
[[nodiscard]] std::filesystem::path SelectCjkFont(
    std::span<const std::filesystem::path> candidates);

}  // namespace ogplay::frontend
