#include "frontend/gui/view_model.h"

#include <algorithm>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace ogplay::frontend {
namespace {

[[nodiscard]] bool Contains(const std::vector<std::string>& values,
                            const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

[[nodiscard]] std::string PathUtf8(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

[[nodiscard]] const LibraryTile* FindTile(
    const std::span<const LibraryTile> tiles, const std::string_view key) {
    const auto found = std::find_if(tiles.begin(), tiles.end(),
                                    [key](const LibraryTile& tile) {
                                        return tile.key == key;
                                    });
    return found == tiles.end() ? nullptr : &*found;
}

}  // namespace

void GuiMessageQueue::Push(std::string title, std::string message) {
    if (title.empty() || message.empty()) {
        throw std::invalid_argument("GUI message title and body must not be empty");
    }
    pending_.push_back(
        GuiMessage{.title = std::move(title), .message = std::move(message)});
}

bool GuiMessageQueue::ActivateNext(const bool another_popup_open) {
    if (active_.has_value() || another_popup_open || pending_.empty()) return false;
    active_ = std::move(pending_.front());
    pending_.pop_front();
    return true;
}

const GuiMessage* GuiMessageQueue::Active() const noexcept {
    return active_.has_value() ? &*active_ : nullptr;
}

void GuiMessageQueue::DismissActive() noexcept { active_.reset(); }

std::vector<LibraryTile> BuildLibraryTiles(
    const std::span<const LibraryEntry> entries,
    const LibraryViewContext& context) {
    std::vector<LibraryTile> tiles;
    tiles.reserve(entries.size());
    for (const auto& entry : entries) {
        const auto profile_error = context.profile_errors.contains(entry.key) ?
            std::optional<std::string>(context.profile_errors.at(entry.key)) : context.profile_catalog_error;
        LibraryTile tile;
        tile.key = entry.key;
        tile.display_name = entry.metadata ? entry.metadata->display_name : entry.key;
        tile.icon_png = entry.icon_png;
        tile.running = Contains(context.running_packages, entry.key);
        if (entry.Damaged() || !entry.metadata.has_value()) {
            tile.status = LibraryTileStatus::damaged;
            tile.detail = "库条目损坏：" + entry.damage_reason.value_or(
                "元数据不可用") + "。可在详情中移除后重新导入。";
        } else if (profile_error.has_value()) {
            tile.status = LibraryTileStatus::profile_catalog_unavailable;
            tile.detail = "Profile 目录不可用：" +
                          *profile_error +
                          "。请在设置中选择有效目录或恢复内置数据。";
        } else if (entry.metadata->external_dir.has_value()) {
            std::error_code error;
            const auto external = entry.metadata->external_dir;
            if (!std::filesystem::is_directory(*external, error) || error) {
                tile.status = LibraryTileStatus::missing_external;
                tile.detail = (entry.metadata->external_owned ? "游戏库中的数据包缺失：" : "记录的数据包目录不可用：") +
                              PathUtf8(*external) + "。请重新选择目录。";
            } else if (tile.running) {
                tile.status = LibraryTileStatus::running;
                tile.detail = "游戏正在运行；请先退出游戏。";
            } else {
                tile.status = LibraryTileStatus::ready;
            }
        } else if (tile.running) {
            tile.status = LibraryTileStatus::running;
            tile.detail = "游戏正在运行；请先退出游戏。";
        } else {
            tile.status = LibraryTileStatus::ready;
        }
        tile.can_launch = !tile.running && tile.status == LibraryTileStatus::ready;
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

LibraryDetail BuildLibraryDetail(const LibraryEntry& entry,
                                 const LibraryTile& tile,
                                 const LibraryViewContext& context) {
    LibraryDetail result{
        .key = tile.key,
        .display_name = tile.display_name,
        .package = tile.key,
        .version = "元数据不可用",
        .status = tile.status,
        .icon_png = tile.icon_png,
        .detail = tile.detail,
        .can_launch = tile.can_launch,
        .can_delete = !tile.running,
    };

    const auto profile_error = context.profile_errors.contains(entry.key) ?
        std::optional<std::string>(context.profile_errors.at(entry.key)) : context.profile_catalog_error;
    if (!entry.metadata.has_value() || entry.Damaged()) {
        result.external = {LibraryConditionStatus::unavailable, "无法判断",
                           "库条目元数据不可用。"};
    } else {
        const auto& metadata = *entry.metadata;
        result.package = metadata.package;
        result.version = metadata.version_name.empty()
                             ? "versionCode " +
                                   std::to_string(metadata.version_code)
                             : metadata.version_name;
        if (profile_error.has_value()) {
            result.profile = LibraryCondition{LibraryConditionStatus::unavailable, "无法判断",
                                              "Profile 目录不可用：" + *profile_error};
        } else if (metadata.profile_id.has_value()) {
            result.profile = LibraryCondition{LibraryConditionStatus::ready,
                                              *metadata.profile_id, "精确 Profile 已匹配。"};
        }
        if (!metadata.external_dir.has_value()) {
            result.external = {LibraryConditionStatus::not_required, "未指定",
                               "可按需选择外部数据目录。"};
        } else {
            std::error_code error;
            if (!std::filesystem::is_directory(*metadata.external_dir, error) || error) {
                result.external = {LibraryConditionStatus::missing, "缺失",
                                   "记录的数据包目录不可用：" + PathUtf8(*metadata.external_dir)};
            } else {
                result.external = {LibraryConditionStatus::ready, "已就绪",
                                   (metadata.external_owned ? "已复制到游戏库：" : "引用原目录：") +
                                       PathUtf8(*metadata.external_dir)};
            }
        }
    }

    return result;
}

void LibrarySelection::Select(const std::string_view key,
                              const std::span<const LibraryTile> tiles) {
    const auto found = std::find_if(tiles.begin(), tiles.end(),
                                    [key](const LibraryTile& tile) {
                                        return tile.key == key;
                                    });
    if (found == tiles.end()) return;
    index_ = static_cast<std::size_t>(found - tiles.begin());
    key_ = found->key;
}

void LibrarySelection::Reconcile(const std::span<const LibraryTile> tiles) {
    if (tiles.empty()) {
        key_.reset();
        index_ = 0;
        return;
    }
    if (key_.has_value()) {
        const auto* current = FindTile(tiles, *key_);
        if (current != nullptr) {
            index_ = static_cast<std::size_t>(current - tiles.data());
            return;
        }
    }
    index_ = std::min(index_, tiles.size() - 1U);
    key_ = tiles[index_].key;
}

const std::optional<std::string>& LibrarySelection::Key() const noexcept {
    return key_;
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

std::uint32_t GuiEventWaitMilliseconds(const bool bounded_smoke) noexcept {
    return bounded_smoke ? 0U : 100U;
}

}  // namespace ogplay::frontend
