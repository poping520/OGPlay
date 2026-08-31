#include "ogplay/frontend/gui_view_model.h"

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
        LibraryTile tile;
        tile.key = entry.key;
        tile.display_name = entry.metadata ? entry.metadata->display_name : entry.key;
        tile.icon_png = entry.icon_png;
        tile.running = Contains(context.running_packages, entry.key);
        if (entry.Damaged() || !entry.metadata.has_value()) {
            tile.status = LibraryTileStatus::damaged;
            tile.detail = "库条目损坏：" + entry.damage_reason.value_or(
                "元数据不可用") + "。可右键删除后重新导入。";
        } else if (context.profile_catalog_error.has_value()) {
            tile.status = LibraryTileStatus::profile_catalog_unavailable;
            tile.detail = "Profile 目录不可用：" +
                          *context.profile_catalog_error +
                          "。请在设置中选择有效目录或恢复内置数据。";
        } else if (!entry.metadata->profile_id.has_value()) {
            tile.status = LibraryTileStatus::missing_profile;
            tile.detail = "暂无精确 Profile；将尝试通用启动路径，兼容性尚未确认。";
        } else if (Contains(context.external_required_packages, entry.key)) {
            std::error_code error;
            const auto external = entry.metadata->external_dir;
            if (!external.has_value() ||
                !std::filesystem::is_directory(*external, error) || error) {
                tile.status = LibraryTileStatus::missing_external;
                tile.detail = external.has_value()
                                  ? "记录的数据包目录不可用：" +
                                        PathUtf8(*external) +
                                        "。请重新导入并指认目录。"
                                  : "未指认必需数据包目录；请重新导入并指认目录。";
            } else if (tile.running) {
                tile.status = LibraryTileStatus::running;
                tile.detail = "游戏正在运行；请先退出游戏。";
            } else if (context.system_directory_error.has_value()) {
                tile.status = LibraryTileStatus::setup_required;
                tile.detail = *context.system_directory_error;
            } else {
                tile.status = LibraryTileStatus::ready;
            }
        } else if (tile.running) {
            tile.status = LibraryTileStatus::running;
            tile.detail = "游戏正在运行；请先退出游戏。";
        } else if (context.system_directory_error.has_value()) {
            tile.status = LibraryTileStatus::setup_required;
            tile.detail = *context.system_directory_error;
        } else {
            tile.status = LibraryTileStatus::ready;
        }
        tile.can_launch = !tile.running &&
                          !context.system_directory_error.has_value() &&
                          (tile.status == LibraryTileStatus::ready ||
                           tile.status == LibraryTileStatus::missing_profile);
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

    if (!entry.metadata.has_value() || entry.Damaged()) {
        result.profile = {LibraryConditionStatus::unavailable, "无法判断",
                          "库条目元数据不可用。"};
        result.external = {LibraryConditionStatus::unavailable, "无法判断",
                           "库条目元数据不可用。"};
    } else {
        const auto& metadata = *entry.metadata;
        result.package = metadata.package;
        result.version = metadata.version_name.empty()
                             ? "versionCode " +
                                   std::to_string(metadata.version_code)
                             : metadata.version_name;
        if (context.profile_catalog_error.has_value()) {
            result.profile = {
                LibraryConditionStatus::unavailable, "无法判断",
                "Profile 目录不可用：" + *context.profile_catalog_error};
            result.external = {
                LibraryConditionStatus::unavailable, "无法判断",
                "Profile 不可用，无法判断是否需要外部数据。"};
        } else if (!metadata.profile_id.has_value()) {
            result.profile = {LibraryConditionStatus::missing, "未匹配",
                              "暂无精确 Profile；将使用通用启动路径。"};
            result.external = {
                LibraryConditionStatus::unavailable, "无法判断",
                "缺少精确 Profile，无法判断是否需要外部数据。"};
        } else {
            result.profile = {LibraryConditionStatus::ready,
                              *metadata.profile_id, "精确 Profile 已匹配。"};
            if (!Contains(context.external_required_packages, entry.key)) {
                result.external = {LibraryConditionStatus::not_required,
                                   "不需要", "Profile 未声明必需外部数据。"};
            } else {
                std::error_code error;
                if (!metadata.external_dir.has_value() ||
                    !std::filesystem::is_directory(*metadata.external_dir,
                                                   error) ||
                    error) {
                    result.external = {
                        LibraryConditionStatus::missing, "缺失",
                        metadata.external_dir.has_value()
                            ? "记录的数据包目录不可用：" +
                                  PathUtf8(*metadata.external_dir)
                            : "未指认必需数据包目录。"};
                } else {
                    result.external = {
                        LibraryConditionStatus::ready, "已就绪",
                        "原地引用：" + PathUtf8(*metadata.external_dir)};
                }
            }
        }
    }

    if (context.system_directory_error.has_value()) {
        result.system = {LibraryConditionStatus::missing, "未配置",
                         *context.system_directory_error};
    } else {
        result.system = {LibraryConditionStatus::ready, "已配置",
                         "Android 系统库目录可用。"};
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

std::optional<std::string> GuiSystemDirectoryError(const GuiConfig& config) {
    if (!config.system_dir.has_value()) {
        return "Android 系统库未设置；请在设置中选择有效目录。";
    }
    std::error_code error;
    if (!std::filesystem::is_directory(*config.system_dir, error) || error) {
        return "Android 系统库目录不可用：" + PathUtf8(*config.system_dir) +
               "。请在设置中修正。";
    }
    return std::nullopt;
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
