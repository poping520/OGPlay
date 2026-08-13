#include "ogplay/frontend/gui_view_model.h"

#include <algorithm>
#include <system_error>

namespace ogplay::frontend {
namespace {

[[nodiscard]] bool Contains(const std::vector<std::string>& values,
                            const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

}  // namespace

std::vector<LibraryTile> BuildLibraryTiles(
    const std::span<const LibraryEntry> entries,
    const LibraryViewContext& context) {
    std::vector<LibraryTile> tiles;
    tiles.reserve(entries.size());
    for (const auto& entry : entries) {
        LibraryTile tile;
        tile.key = entry.key;
        tile.display_name = entry.metadata ? entry.metadata->display_name : entry.key;
        tile.icon_png = entry.icon_png;
        if (entry.Damaged() || !entry.metadata.has_value()) {
            tile.status = LibraryTileStatus::damaged;
            tile.detail = entry.damage_reason.value_or("Library metadata is unavailable");
        } else if (!entry.metadata->profile_id.has_value()) {
            tile.status = LibraryTileStatus::missing_profile;
            tile.detail = "No exact Profile matched this package";
        } else if (Contains(context.external_required_packages, entry.key)) {
            std::error_code error;
            const auto external = entry.metadata->external_dir;
            if (!external.has_value() ||
                !std::filesystem::is_directory(*external, error) || error) {
                tile.status = LibraryTileStatus::missing_external;
                tile.detail = external.has_value()
                                  ? external->string()
                                  : "Required external data directory is not configured";
            } else if (Contains(context.running_packages, entry.key)) {
                tile.status = LibraryTileStatus::running;
            } else {
                tile.status = LibraryTileStatus::ready;
            }
        } else if (Contains(context.running_packages, entry.key)) {
            tile.status = LibraryTileStatus::running;
        } else {
            tile.status = LibraryTileStatus::ready;
        }
        tiles.push_back(std::move(tile));
    }
    std::sort(tiles.begin(), tiles.end(), [](const LibraryTile& left,
                                              const LibraryTile& right) {
        if (left.display_name != right.display_name) {
            return left.display_name < right.display_name;
        }
        return left.key < right.key;
    });
    return tiles;
}

std::filesystem::path SelectCjkFont(
    const std::span<const std::filesystem::path> candidates) {
    for (const auto& candidate : candidates) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error) && !error) {
            return candidate;
        }
    }
    return {};
}

}  // namespace ogplay::frontend
