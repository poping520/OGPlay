#include "import_ui.h"

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_time.h>
#include <GLES2/gl2.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "imgui.h"

#include "ogplay/core/logger.h"
#include "ogplay/frontend/gui_import.h"
#include "ogplay/runtime/integration/host_image_decode.h"
#include "ogplay/session/quirk_registry.h"

namespace ogplay::frontend {
namespace {

enum class DialogKind : std::uint8_t { apk, external };

struct DialogRequest final {
    Uint32 event_type{};
    DialogKind kind{DialogKind::apk};
    std::shared_ptr<class DialogMailbox> mailbox;
};

struct DialogResult final {
    DialogKind kind{DialogKind::apk};
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

void SDLCALL DialogCallback(void* userdata, const char* const* files,
                            const int filter) {
    static_cast<void>(filter);
    std::unique_ptr<DialogRequest> request(
        static_cast<DialogRequest*>(userdata));
    DialogResult result;
    result.kind = request->kind;
    if (files == nullptr) {
        result.error = SDL_GetError();
        if (result.error.empty()) result.error = "host file dialog failed";
    } else if (files[0] != nullptr) {
        result.path = std::filesystem::path(files[0]);
    }
    request->mailbox->Publish(std::move(result));
    SDL_Event event{};
    event.type = request->event_type;
    static_cast<void>(SDL_PushEvent(&event));
}

[[nodiscard]] std::string UtcNow() {
    SDL_Time ticks{};
    SDL_DateTime value{};
    if (!SDL_GetCurrentTime(&ticks) ||
        !SDL_TimeToDateTime(ticks, &value, false)) {
        throw std::runtime_error(std::string("cannot read host UTC time: ") +
                                 SDL_GetError());
    }
    std::ostringstream output;
    output << std::setfill('0') << std::setw(4) << value.year << '-'
           << std::setw(2) << value.month << '-' << std::setw(2) << value.day
           << 'T' << std::setw(2) << value.hour << ':' << std::setw(2)
           << value.minute << ':' << std::setw(2) << value.second << 'Z';
    return output.str();
}

class PreviewTexture final {
public:
    PreviewTexture() = default;
    ~PreviewTexture() { Reset(); }
    PreviewTexture(const PreviewTexture&) = delete;
    PreviewTexture& operator=(const PreviewTexture&) = delete;

    void Load(const std::span<const std::byte> png) {
        Reset();
        const auto decoded = runtime::DecodeImageToArgb(png);
        if (!decoded.has_value()) return;
        std::vector<std::uint8_t> rgba(decoded->argb.size() * 4U);
        for (std::size_t index = 0; index < decoded->argb.size(); ++index) {
            const auto argb = decoded->argb[index];
            rgba[index * 4U] = static_cast<std::uint8_t>(argb >> 16U);
            rgba[index * 4U + 1U] = static_cast<std::uint8_t>(argb >> 8U);
            rgba[index * 4U + 2U] = static_cast<std::uint8_t>(argb);
            rgba[index * 4U + 3U] = static_cast<std::uint8_t>(argb >> 24U);
        }
        glGenTextures(1, &texture_);
        glBindTexture(GL_TEXTURE_2D, texture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, decoded->width, decoded->height,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        if (glGetError() != GL_NO_ERROR) Reset();
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    [[nodiscard]] GLuint Id() const noexcept { return texture_; }

private:
    void Reset() noexcept {
        if (texture_ != 0) glDeleteTextures(1, &texture_);
        texture_ = 0;
    }
    GLuint texture_{};
};

[[nodiscard]] std::uint64_t FallbackCode(
    const ApplicationVisualFallback fallback) noexcept {
    return static_cast<std::uint64_t>(fallback);
}

}  // namespace

class GuiImportUi::Impl final {
public:
    Impl(SDL_Window* window, LibraryStore& store,
         std::filesystem::path source_root, core::Logger& logger)
        : window_(window), store_(store), source_root_(std::move(source_root)),
          logger_(logger) {
        event_type_ = SDL_RegisterEvents(1);
        if (event_type_ == 0) {
            throw std::runtime_error("SDL could not allocate a GUI dialog event");
        }
        try {
            const auto config = LoadGuiConfig(store_.Root());
            profiles_dir_ = config.profiles_dir.value_or(source_root_ / "data" / "profiles");
            const auto quirks = session::QuirkRegistry::Load(
                source_root_ / "data" / "quirks.toml", source_root_);
            profiles_ = std::make_shared<const session::TitleProfileCatalog>(
                session::TitleProfileCatalog::LoadDirectory(profiles_dir_, quirks));
            logger_.Write(core::LogLevel::info, "frontend.gui.import",
                          "Profile catalog ready", {},
                          {{"profiles_dir", profiles_dir_.string()}});
        } catch (const std::exception& error) {
            profiles_error_ = error.what();
            logger_.Write(core::LogLevel::warn, "frontend.gui.import",
                          "Profile catalog unavailable", {},
                          {{"reason", profiles_error_}});
        }
    }

    void OpenApkDialog() {
        if (dialog_open_ || stage_ == Stage::analyzing) return;
        open_popup_ = true;
        stage_ = Stage::waiting_apk;
        error_.clear();
        dialog_open_ = true;
        static constexpr SDL_DialogFileFilter filters[]{{"Android APK", "apk"}};
        SDL_ShowOpenFileDialog(DialogCallback,
                               new DialogRequest{event_type_, DialogKind::apk, mailbox_},
                               window_, filters, 1, nullptr, false);
    }

    bool HandleEvent(const SDL_Event& event) {
        if (event.type != event_type_) return false;
        for (auto& result : mailbox_->Take()) {
            dialog_open_ = false;
            if (!result.error.empty()) {
                if (result.kind == DialogKind::apk) {
                    Fail("无法打开 APK 选择器：" + result.error);
                } else {
                    external_error_ = "无法打开目录选择器：" + result.error;
                }
                continue;
            }
            if (!result.path.has_value()) {
                if (result.kind == DialogKind::apk) close_popup_ = true;
                continue;
            }
            if (result.kind == DialogKind::apk) {
                StartAnalysis(*result.path);
            } else {
                auto selected =
                    std::filesystem::absolute(*result.path).lexically_normal();
                std::error_code error;
                if (!std::filesystem::is_directory(selected, error) || error) {
                    external_error_ = "所选数据包目录不存在或不可读：" +
                                      selected.string();
                } else {
                    external_dir_ = std::move(selected);
                    external_error_.clear();
                }
            }
        }
        return true;
    }

    bool Draw() {
        PollAnalysis();
        if (open_popup_) {
            ImGui::OpenPopup("导入游戏");
            open_popup_ = false;
        }
        bool imported{};
        if (ImGui::BeginPopupModal("导入游戏", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            if (close_popup_) {
                ImGui::CloseCurrentPopup();
                close_popup_ = false;
                Reset();
            } else if (stage_ == Stage::waiting_apk) {
                ImGui::TextUnformatted("请在系统文件选择器中选择 APK…");
                if (ImGui::Button("取消")) {
                    ImGui::CloseCurrentPopup();
                    Reset();
                }
            } else if (stage_ == Stage::analyzing) {
                ImGui::TextUnformatted("正在解析 APK、匹配 Profile 并提取图标…");
            } else if (stage_ == Stage::failed) {
                ImGui::TextWrapped("%s", error_.c_str());
                ImGui::Spacing();
                ImGui::TextWrapped("下一步：重新选择 APK；若提示 Profile 目录错误，请在设置中修正。");
                if (ImGui::Button("重新选择")) OpenApkDialog();
                ImGui::SameLine();
                if (ImGui::Button("关闭")) {
                    ImGui::CloseCurrentPopup();
                    Reset();
                }
            } else if (stage_ == Stage::summary && analysis_.has_value()) {
                imported = DrawSummary();
                if (imported) {
                    ImGui::CloseCurrentPopup();
                    Reset();
                }
            }
            ImGui::EndPopup();
        }
        return imported;
    }

    std::vector<std::string> ExternalRequiredPackages(
        const std::vector<LibraryEntry>& entries) const {
        std::vector<std::string> result;
        if (!profiles_) return result;
        for (const auto& entry : entries) {
            if (!entry.metadata.has_value() ||
                !entry.metadata->profile_id.has_value()) {
                continue;
            }
            const auto summary = session::FindApkProfileSummary(
                *profiles_, *entry.metadata->profile_id);
            if (summary.has_value() && summary->requires_external_data) {
                result.push_back(entry.key);
            }
        }
        return result;
    }

private:
    enum class Stage : std::uint8_t { idle, waiting_apk, analyzing, summary, failed };

    void StartAnalysis(const std::filesystem::path& path) {
        if (!profiles_) {
            Fail("Profile 目录不可用：" + profiles_error_);
            return;
        }
        stage_ = Stage::analyzing;
        analysis_.reset();
        external_dir_.reset();
        external_error_.clear();
        const auto catalog = profiles_;
        analysis_future_ = std::async(
            std::launch::async,
            [path, catalog] { return AnalyzeApkImportFile(path, *catalog); });
    }

    void PollAnalysis() {
        if (stage_ != Stage::analyzing || !analysis_future_.valid() ||
            analysis_future_.wait_for(std::chrono::seconds(0)) !=
                std::future_status::ready) {
            return;
        }
        try {
            auto analysis = analysis_future_.get();
            for (const auto& entry : store_.LoadEntries()) {
                if (entry.key == analysis.manifest.package) {
                    throw GuiModelError(
                        GuiModelErrorCode::duplicate_package,
                        "该 package 已在游戏库中；请先删除既有条目", entry.directory);
                }
            }
            for (const auto fallback : analysis.visual_fallbacks) {
                logger_.Write(core::LogLevel::warn, "frontend.gui.import.visual",
                              "APK visual fallback used", {},
                              {{"package", analysis.manifest.package},
                               {"fallback", FallbackCode(fallback)}});
            }
            preview_.Load(analysis.icon_png);
            analysis_ = std::move(analysis);
            stage_ = Stage::summary;
        } catch (const std::exception& error) {
            Fail(std::string("APK 解析失败：") + error.what());
        }
    }

    void OpenExternalDialog() {
        if (dialog_open_) return;
        dialog_open_ = true;
        SDL_ShowOpenFolderDialog(
            DialogCallback,
            new DialogRequest{event_type_, DialogKind::external, mailbox_},
            window_, nullptr, false);
    }

    bool DrawSummary() {
        const auto& analysis = *analysis_;
        if (preview_.Id() != 0) {
            ImGui::Image(static_cast<ImTextureID>(preview_.Id()), {96.0F, 96.0F});
        } else {
            ImGui::Button("?##preview", {96.0F, 96.0F});
        }
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::Text("名称：%s", analysis.display_name.c_str());
        ImGui::Text("包名：%s", analysis.manifest.package.c_str());
        ImGui::Text("版本：%u%s%s", analysis.manifest.version_code,
                    analysis.manifest.version_name.has_value() ? " / " : "",
                    analysis.manifest.version_name.value_or("").c_str());
        if (analysis.profile.has_value()) {
            ImGui::Text("Profile：%s", analysis.profile->display_name.c_str());
        } else {
            ImGui::TextColored({1.0F, 0.68F, 0.25F, 1.0F},
                               "暂不支持：没有精确 Profile（仍可入库）");
        }
        ImGui::EndGroup();
        if (analysis.profile.has_value() &&
            analysis.profile->requires_external_data) {
            ImGui::Separator();
            ImGui::TextWrapped("该游戏需要外部数据包。可暂时跳过，入库后将显示“缺数据包”。");
            if (ImGui::Button("选择数据包目录")) OpenExternalDialog();
            if (external_dir_.has_value()) {
                ImGui::TextWrapped("原地引用：%s", external_dir_->string().c_str());
                ImGui::TextWrapped("请勿移动或删除此目录。");
            }
            if (!external_error_.empty()) {
                ImGui::TextWrapped("%s", external_error_.c_str());
            }
        }
        ImGui::Separator();
        ImGui::BeginDisabled(dialog_open_);
        const auto confirm = ImGui::Button("确认导入");
        ImGui::EndDisabled();
        if (confirm) {
            try {
                store_.Import(BuildLibraryImport(
                    analysis, external_dir_, UtcNow()));
                logger_.Write(core::LogLevel::info, "frontend.gui.import",
                              "game imported", {},
                              {{"package", analysis.manifest.package}});
                return true;
            } catch (const std::exception& error) {
                Fail(std::string("导入失败：") + error.what());
                return false;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("取消")) {
            ImGui::CloseCurrentPopup();
            Reset();
        }
        return false;
    }

    void Fail(std::string message) {
        error_ = std::move(message);
        stage_ = Stage::failed;
        logger_.Write(core::LogLevel::error, "frontend.gui.import",
                      "import workflow failed", {}, {{"reason", error_}});
    }

    void Reset() {
        stage_ = Stage::idle;
        analysis_.reset();
        external_dir_.reset();
        external_error_.clear();
        error_.clear();
        preview_.Load({});
    }

    SDL_Window* window_{};
    LibraryStore& store_;
    std::filesystem::path source_root_;
    core::Logger& logger_;
    Uint32 event_type_{};
    std::shared_ptr<DialogMailbox> mailbox_{std::make_shared<DialogMailbox>()};
    std::filesystem::path profiles_dir_;
    std::shared_ptr<const session::TitleProfileCatalog> profiles_;
    std::string profiles_error_;
    Stage stage_{Stage::idle};
    bool dialog_open_{};
    bool open_popup_{};
    bool close_popup_{};
    std::future<ApkImportAnalysis> analysis_future_;
    std::optional<ApkImportAnalysis> analysis_;
    std::optional<std::filesystem::path> external_dir_;
    std::string external_error_;
    std::string error_;
    PreviewTexture preview_;
};

GuiImportUi::GuiImportUi(SDL_Window* window, LibraryStore& store,
                         std::filesystem::path source_root,
                         core::Logger& logger)
    : impl_(std::make_unique<Impl>(window, store, std::move(source_root), logger)) {}

GuiImportUi::~GuiImportUi() = default;

void GuiImportUi::OpenApkDialog() { impl_->OpenApkDialog(); }

bool GuiImportUi::HandleEvent(const SDL_Event& event) {
    return impl_->HandleEvent(event);
}

bool GuiImportUi::Draw() { return impl_->Draw(); }

std::vector<std::string> GuiImportUi::ExternalRequiredPackages(
    const std::vector<LibraryEntry>& entries) const {
    return impl_->ExternalRequiredPackages(entries);
}

}  // namespace ogplay::frontend
