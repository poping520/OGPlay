#include "settings_ui.h"

#include <SDL3/SDL_dialog.h>

#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "imgui.h"

#include "ogplay/core/logger.h"
#include "ogplay/frontend/gui_model.h"

#include "ui_button.h"

namespace ogplay::frontend {
namespace {

struct DialogResult final {
    std::optional<std::filesystem::path> path;
    std::string error;
};

class DialogMailbox final {
public:
    void Publish(DialogResult result) {
        std::scoped_lock lock(mutex_);
        results_.push_back(std::move(result));
    }

    [[nodiscard]] std::vector<DialogResult> Take() {
        std::scoped_lock lock(mutex_);
        return std::exchange(results_, {});
    }

private:
    std::mutex mutex_;
    std::vector<DialogResult> results_;
};

struct DialogRequest final {
    Uint32 event_type{};
    std::shared_ptr<DialogMailbox> mailbox;
};

[[nodiscard]] std::filesystem::path Utf8Path(const char* text) {
    const auto* begin = reinterpret_cast<const char8_t*>(text);
    return std::filesystem::path(std::u8string(begin, begin + std::strlen(text)));
}

[[nodiscard]] std::string PathUtf8(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

void SDLCALL DialogCallback(void* userdata, const char* const* files,
                            const int filter) {
    static_cast<void>(filter);
    std::unique_ptr<DialogRequest> request(
        static_cast<DialogRequest*>(userdata));
    DialogResult result;
    if (files == nullptr) {
        result.error = SDL_GetError();
        if (result.error.empty()) result.error = "host folder dialog failed";
    } else if (files[0] != nullptr) {
        result.path = Utf8Path(files[0]);
    }
    request->mailbox->Publish(std::move(result));
    SDL_Event event{};
    event.type = request->event_type;
    static_cast<void>(SDL_PushEvent(&event));
}

[[nodiscard]] std::uint64_t ErrorCode(const GuiModelError& error) noexcept {
    return static_cast<std::uint64_t>(error.Code());
}

}  // namespace

class GuiSettingsUi::Impl final {
public:
    Impl(SDL_Window* window, std::filesystem::path library_root,
         core::Logger& logger)
        : window_(window), library_root_(std::move(library_root)),
          logger_(logger) {
        event_type_ = SDL_RegisterEvents(1);
        if (event_type_ == 0) {
            throw std::runtime_error(
                "SDL could not allocate a settings dialog event");
        }
    }

    void Open() {
        if (dialog_open_) return;
        config_ = {};
        error_.clear();
        try {
            config_ = LoadGuiConfig(library_root_);
        } catch (const GuiModelError& error) {
            SetError("现有设置不可读；修正目录并保存可覆盖损坏配置：" +
                         std::string(error.what()),
                     error);
        }
        open_popup_ = true;
    }

    bool HandleEvent(const SDL_Event& event) {
        if (event.type != event_type_) return false;
        for (auto& result : mailbox_->Take()) {
            dialog_open_ = false;
            if (!result.error.empty()) {
                SetError("无法打开目录选择器：" + result.error);
                continue;
            }
            if (!result.path.has_value()) continue;
            auto selected =
                std::filesystem::absolute(*result.path).lexically_normal();
            std::error_code error;
            if (!std::filesystem::is_directory(selected, error) || error) {
                SetError("所选目录不存在或不可读：" + PathUtf8(selected));
                continue;
            }
            config_.profiles_dir = std::move(selected);
            error_.clear();
        }
        return true;
    }

    bool Draw() {
        if (open_popup_) {
            ImGui::OpenPopup("设置");
            open_popup_ = false;
        }
        bool saved{};
        if (ImGui::BeginPopupModal("设置", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("游戏库（只读）");
            ImGui::TextWrapped("%s", PathUtf8(library_root_).c_str());
            ImGui::Separator();
            DrawDirectory("Profile 目录", config_.profiles_dir,
                          "选择 Profile 目录");
            ImGui::TextWrapped("Profile 目录留空时使用 OGPlay 内置默认目录。");
            if (!error_.empty()) {
                ImGui::Spacing();
                ImGui::TextColored({1.0F, 0.42F, 0.35F, 1.0F}, "%s",
                                   error_.c_str());
                ImGui::TextWrapped("下一步：选择一个存在的目录，或清除此项后保存。");
            }
            ImGui::Separator();
            ImGui::BeginDisabled(dialog_open_);
            const auto save = GuiButton("保存##settings");
            ImGui::EndDisabled();
            if (save) saved = Save();
            ImGui::SameLine();
            if (GuiButton("取消##settings")) {
                ImGui::CloseCurrentPopup();
                error_.clear();
            }
            ImGui::EndPopup();
        }
        return saved;
    }

private:
    void DrawDirectory(const char* label,
                       std::optional<std::filesystem::path>& value,
                       const char* button) {
        ImGui::TextUnformatted(label);
        if (value.has_value()) {
            ImGui::TextWrapped("%s", PathUtf8(*value).c_str());
        } else {
            ImGui::TextDisabled("未设置");
        }
        if (GuiButton(button)) OpenFolderDialog();
        if (value.has_value()) {
            ImGui::SameLine();
            if (GuiButton("使用内置 Profile")) {
                value.reset();
                error_.clear();
            }
        }
    }

    void OpenFolderDialog() {
        if (dialog_open_) return;
        dialog_open_ = true;
        SDL_ShowOpenFolderDialog(
            DialogCallback,
            new DialogRequest{event_type_, mailbox_}, window_, nullptr,
            false);
    }

    bool Save() {
        try {
            ValidateGuiConfigDirectories(config_);
            SaveGuiConfig(library_root_, config_);
            logger_.Write(
                core::LogLevel::info, "frontend.gui.settings",
                "GUI settings saved", {},
                {{"profiles_dir", config_.profiles_dir.has_value()
                                      ? PathUtf8(*config_.profiles_dir)
                                      : std::string{}}});
            ImGui::CloseCurrentPopup();
            error_.clear();
            return true;
        } catch (const GuiModelError& error) {
            SetError("设置保存失败：" + std::string(error.what()), error);
            return false;
        }
    }

    void SetError(std::string message) {
        error_ = std::move(message);
        logger_.Write(core::LogLevel::error, "frontend.gui.settings",
                      "settings workflow failed", {},
                      {{"reason", error_}});
    }

    void SetError(std::string message, const GuiModelError& error) {
        if (!error.Path().empty()) {
            message += "\n路径：" + PathUtf8(error.Path());
        }
        error_ = std::move(message);
        logger_.Write(core::LogLevel::error, "frontend.gui.settings",
                      "settings workflow failed", {},
                      {{"reason", std::string(error.what())},
                       {"code", ErrorCode(error)},
                       {"path", PathUtf8(error.Path())}});
    }

    SDL_Window* window_{};
    std::filesystem::path library_root_;
    core::Logger& logger_;
    Uint32 event_type_{};
    std::shared_ptr<DialogMailbox> mailbox_{std::make_shared<DialogMailbox>()};
    GuiConfig config_;
    bool dialog_open_{};
    bool open_popup_{};
    std::string error_;
};

GuiSettingsUi::GuiSettingsUi(SDL_Window* window,
                             std::filesystem::path library_root,
                             core::Logger& logger)
    : impl_(std::make_unique<Impl>(window, std::move(library_root), logger)) {}

GuiSettingsUi::~GuiSettingsUi() = default;

void GuiSettingsUi::Open() { impl_->Open(); }

bool GuiSettingsUi::HandleEvent(const SDL_Event& event) {
    return impl_->HandleEvent(event);
}

bool GuiSettingsUi::Draw() { return impl_->Draw(); }

}  // namespace ogplay::frontend
