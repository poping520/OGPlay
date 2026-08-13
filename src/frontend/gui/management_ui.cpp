#include "management_ui.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "imgui.h"

#include "ogplay/core/logger.h"

#include "ui_button.h"

namespace ogplay::frontend {
namespace {

[[nodiscard]] std::string PathUtf8(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

[[nodiscard]] std::uint64_t ErrorCode(const GuiModelError& error) noexcept {
    return static_cast<std::uint64_t>(error.Code());
}

}  // namespace

class GuiManagementUi::Impl final {
public:
    Impl(LibraryStore& store, core::Logger& logger)
        : store_(store), logger_(logger) {}

    void OpenDelete(const LibraryEntry& entry, const bool running) {
        package_ = entry.key;
        display_name_ = entry.metadata.has_value()
                            ? entry.metadata->display_name
                            : entry.key;
        external_dir_ = entry.metadata.has_value()
                            ? entry.metadata->external_dir
                            : std::nullopt;
        running_ = running;
        error_.clear();
        open_popup_ = true;
    }

    bool Draw() {
        if (open_popup_) {
            ImGui::OpenPopup("删除游戏");
            open_popup_ = false;
        }
        bool removed{};
        if (ImGui::BeginPopupModal("删除游戏", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("游戏：%s", display_name_.c_str());
            ImGui::Text("package：%s", package_.c_str());
            if (running_) {
                ImGui::TextColored({1.0F, 0.42F, 0.35F, 1.0F},
                                   "运行中的游戏不能删除。");
                ImGui::TextWrapped("下一步：先退出游戏，再右键选择删除。");
            } else {
                ImGui::Separator();
                ImGui::TextWrapped(
                    "将删除：库内 APK 副本、元数据、图标缓存和 last-run.log。");
                if (external_dir_.has_value()) {
                    ImGui::TextWrapped("不会删除原地数据包：%s",
                                       PathUtf8(*external_dir_).c_str());
                } else {
                    ImGui::TextWrapped("不会删除任何库外数据目录。");
                }
                const auto sandbox = store_.Root() / "sandbox" / package_;
                ImGui::TextWrapped("不会删除持久存档：%s",
                                   PathUtf8(sandbox).c_str());
            }
            if (!error_.empty()) {
                ImGui::Spacing();
                ImGui::TextColored({1.0F, 0.42F, 0.35F, 1.0F}, "%s",
                                   error_.c_str());
                ImGui::TextWrapped(
                    "下一步：关闭占用库文件的程序后重试；外部数据和存档未被删除。");
            }
            ImGui::Separator();
            if (!running_) {
                if (GuiButton("确认删除##management")) removed = Remove();
                ImGui::SameLine();
            }
            if (GuiButton(running_ ? "知道了##management"
                                   : "取消##management")) {
                ImGui::CloseCurrentPopup();
                Reset();
            }
            ImGui::EndPopup();
        }
        return removed;
    }

private:
    bool Remove() {
        try {
            store_.Remove(package_);
            logger_.Write(core::LogLevel::info, "frontend.gui.library",
                          "library entry removed", {},
                          {{"package", package_},
                           {"external_preserved",
                            external_dir_.has_value()}});
            ImGui::CloseCurrentPopup();
            Reset();
            return true;
        } catch (const GuiModelError& error) {
            error_ = "删除失败：" + std::string(error.what());
            if (!error.Path().empty()) {
                error_ += "\n路径：" + PathUtf8(error.Path());
            }
            logger_.Write(core::LogLevel::error, "frontend.gui.library",
                          "library removal failed", {},
                          {{"package", package_},
                           {"reason", std::string(error.what())},
                           {"code", ErrorCode(error)},
                           {"path", PathUtf8(error.Path())}});
            return false;
        }
    }

    void Reset() {
        package_.clear();
        display_name_.clear();
        external_dir_.reset();
        running_ = false;
        error_.clear();
    }

    LibraryStore& store_;
    core::Logger& logger_;
    std::string package_;
    std::string display_name_;
    std::optional<std::filesystem::path> external_dir_;
    bool running_{};
    bool open_popup_{};
    std::string error_;
};

GuiManagementUi::GuiManagementUi(LibraryStore& store, core::Logger& logger)
    : impl_(std::make_unique<Impl>(store, logger)) {}

GuiManagementUi::~GuiManagementUi() = default;

void GuiManagementUi::OpenDelete(const LibraryEntry& entry,
                                 const bool running) {
    impl_->OpenDelete(entry, running);
}

bool GuiManagementUi::Draw() { return impl_->Draw(); }

}  // namespace ogplay::frontend
