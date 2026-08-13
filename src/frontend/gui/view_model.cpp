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
            tile.detail = "暂无精确 Profile；等待题库收录后重新导入。";
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
            } else if (Contains(context.running_packages, entry.key)) {
                tile.status = LibraryTileStatus::running;
                tile.detail = "游戏正在运行；请先退出游戏。";
            } else {
                tile.status = LibraryTileStatus::ready;
            }
        } else if (Contains(context.running_packages, entry.key)) {
            tile.status = LibraryTileStatus::running;
            tile.detail = "游戏正在运行；请先退出游戏。";
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

std::uint32_t GuiEventWaitMilliseconds(const bool bounded_smoke) noexcept {
    return bounded_smoke ? 0U : 100U;
}

}  // namespace ogplay::frontend
