#include "ogplay/frontend/gui.h"

#include <SDL3/SDL.h>
#include <GLES2/gl2.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl3.h"

#include "ogplay/core/logger.h"
#include "ogplay/frontend/gui_model.h"
#include "ogplay/frontend/gui_view_model.h"
#include "ogplay/frontend/user_data_dir.h"
#include "ogplay/runtime/integration/host_image_decode.h"

#include "import_ui.h"
#include "management_ui.h"
#include "process_manager.h"
#include "settings_ui.h"
#include "ui_button.h"

namespace ogplay::frontend {
namespace {

struct GuiOptions final {
    std::filesystem::path library_root;
    std::optional<std::uint64_t> smoke_frames;
};

[[nodiscard]] std::uint64_t ParsePositiveCount(const std::string_view text,
                                                const std::string_view option) {
    std::uint64_t value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
        value == 0) {
        throw std::invalid_argument(std::string(option) + " requires a positive integer");
    }
    return value;
}

[[nodiscard]] GuiOptions ParseOptions(const int argc, const char* const argv[]) {
    if (argc < 2 || std::string_view(argv[1]) != "gui") {
        throw std::invalid_argument("gui command entry is invalid");
    }
    std::optional<std::filesystem::path> explicit_root;
    std::optional<std::uint64_t> smoke_frames;
    for (int index = 2; index < argc; ++index) {
        const std::string_view option(argv[index]);
        if (option == "--library-root") {
            if (explicit_root.has_value()) {
                throw std::invalid_argument("gui accepts --library-root only once");
            }
            if (++index >= argc || std::string_view(argv[index]).empty()) {
                throw std::invalid_argument("gui --library-root requires a directory");
            }
            explicit_root = std::filesystem::absolute(
                std::filesystem::path(argv[index])).lexically_normal();
        } else if (option == "--smoke-frames") {
            if (smoke_frames.has_value()) {
                throw std::invalid_argument("gui accepts --smoke-frames only once");
            }
            if (++index >= argc) {
                throw std::invalid_argument("gui --smoke-frames requires a count");
            }
            smoke_frames = ParsePositiveCount(argv[index], "gui --smoke-frames");
        } else {
            throw std::invalid_argument("unknown or incomplete gui option: " +
                                        std::string(option));
        }
    }

    if (explicit_root.has_value()) return {*explicit_root, smoke_frames};
    const auto root = UserDataDirectory();
    if (!root.has_value()) {
        throw std::runtime_error(
            "host user-data directory is unavailable; use --library-root <dir>");
    }
    return {*root, smoke_frames};
}

[[nodiscard]] std::string PathUtf8(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

void PrepareLog(core::Logger& logger, const std::filesystem::path& root) {
    std::filesystem::create_directories(root);
    const auto path = root / "gui.log";
    {
        std::ofstream truncate(path, std::ios::binary | std::ios::trunc);
        if (!truncate) {
            throw std::runtime_error("cannot create GUI log: " + PathUtf8(path));
        }
    }
    logger.AddSink(std::make_shared<core::FileSink>(path), core::LogLevel::info);
}

[[noreturn]] void ThrowSdl(const std::string_view operation) {
    throw std::runtime_error(std::string(operation) + " failed: " + SDL_GetError());
}

class SdlVideo final {
public:
    SdlVideo() {
        try {
            if (!SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "1")) {
                throw std::runtime_error("SDL rejected the ANGLE OpenGL ES driver hint");
            }
            if (!SDL_Init(SDL_INIT_VIDEO)) ThrowSdl("SDL_Init");
            initialized_ = true;

            SetGlAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
            SetGlAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
            SetGlAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
            SetGlAttribute(SDL_GL_RED_SIZE, 8);
            SetGlAttribute(SDL_GL_GREEN_SIZE, 8);
            SetGlAttribute(SDL_GL_BLUE_SIZE, 8);
            SetGlAttribute(SDL_GL_ALPHA_SIZE, 8);
            SetGlAttribute(SDL_GL_DOUBLEBUFFER, 1);

            window_ = SDL_CreateWindow("OGPlay", 1280, 720,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
                                           SDL_WINDOW_HIGH_PIXEL_DENSITY);
            if (window_ == nullptr) ThrowSdl("SDL_CreateWindow");
            if (!SDL_SetWindowMinimumSize(window_, 960, 640)) {
                ThrowSdl("SDL_SetWindowMinimumSize");
            }
            context_ = SDL_GL_CreateContext(window_);
            if (context_ == nullptr) ThrowSdl("SDL_GL_CreateContext");
            if (!SDL_GL_MakeCurrent(window_, context_)) ThrowSdl("SDL_GL_MakeCurrent");
            if (!SDL_GL_SetSwapInterval(1)) ThrowSdl("SDL_GL_SetSwapInterval");
        } catch (...) {
            Cleanup();
            throw;
        }
    }

    ~SdlVideo() { Cleanup(); }

    SdlVideo(const SdlVideo&) = delete;
    SdlVideo& operator=(const SdlVideo&) = delete;

    [[nodiscard]] SDL_Window* Window() const noexcept { return window_; }
    [[nodiscard]] SDL_GLContext Context() const noexcept { return context_; }

private:
    void Cleanup() noexcept {
        if (context_ != nullptr) static_cast<void>(SDL_GL_DestroyContext(context_));
        if (window_ != nullptr) SDL_DestroyWindow(window_);
        if (initialized_) SDL_QuitSubSystem(SDL_INIT_VIDEO);
        context_ = nullptr;
        window_ = nullptr;
        initialized_ = false;
    }
    static void SetGlAttribute(const SDL_GLAttr attribute, const int value) {
        if (!SDL_GL_SetAttribute(attribute, value)) ThrowSdl("SDL_GL_SetAttribute");
    }

    bool initialized_{};
    SDL_Window* window_{};
    SDL_GLContext context_{};
};

class ImGuiSession final {
public:
    ImGuiSession(SdlVideo& video, core::Logger& logger) {
        try {
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            context_created_ = true;
            ImGui::StyleColorsDark();
            ConfigureStyle();
            ConfigureFonts(logger);
            if (!ImGui_ImplSDL3_InitForOpenGL(video.Window(), video.Context())) {
                throw std::runtime_error("ImGui SDL3 backend initialization failed");
            }
            platform_ready_ = true;
            if (!ImGui_ImplOpenGL3_Init("#version 100")) {
                throw std::runtime_error("ImGui GLES2 backend initialization failed");
            }
            renderer_ready_ = true;
        } catch (...) {
            Cleanup();
            throw;
        }
    }

    ~ImGuiSession() { Cleanup(); }

    ImGuiSession(const ImGuiSession&) = delete;
    ImGuiSession& operator=(const ImGuiSession&) = delete;

private:
    static void ConfigureStyle() {
        auto& style = ImGui::GetStyle();
        style.WindowPadding = {20.0F, 18.0F};
        style.FramePadding = {14.0F, 9.0F};
        style.ItemSpacing = {12.0F, 10.0F};
        style.ItemInnerSpacing = {8.0F, 6.0F};
        style.WindowRounding = 10.0F;
        style.ChildRounding = 10.0F;
        style.FrameRounding = 8.0F;
        style.PopupRounding = 10.0F;
        style.ScrollbarRounding = 8.0F;
        style.GrabRounding = 8.0F;
        style.WindowBorderSize = 1.0F;
        style.ChildBorderSize = 1.0F;
        style.FrameBorderSize = 1.0F;
        auto& colors = style.Colors;
        colors[ImGuiCol_WindowBg] = {0.031F, 0.055F, 0.082F, 1.0F};
        colors[ImGuiCol_ChildBg] = {0.045F, 0.074F, 0.105F, 1.0F};
        colors[ImGuiCol_PopupBg] = {0.055F, 0.082F, 0.113F, 1.0F};
        colors[ImGuiCol_Border] = {0.15F, 0.20F, 0.26F, 1.0F};
        colors[ImGuiCol_FrameBg] = {0.065F, 0.098F, 0.133F, 1.0F};
        colors[ImGuiCol_FrameBgHovered] = {0.09F, 0.14F, 0.18F, 1.0F};
        colors[ImGuiCol_FrameBgActive] = {0.11F, 0.17F, 0.21F, 1.0F};
        colors[ImGuiCol_Button] = {0.075F, 0.11F, 0.15F, 1.0F};
        colors[ImGuiCol_ButtonHovered] = {0.10F, 0.16F, 0.20F, 1.0F};
        colors[ImGuiCol_ButtonActive] = {0.12F, 0.20F, 0.24F, 1.0F};
        colors[ImGuiCol_Header] = {0.08F, 0.20F, 0.15F, 1.0F};
        colors[ImGuiCol_HeaderHovered] = {0.10F, 0.26F, 0.19F, 1.0F};
        colors[ImGuiCol_HeaderActive] = {0.12F, 0.31F, 0.22F, 1.0F};
        colors[ImGuiCol_CheckMark] = {0.26F, 0.82F, 0.48F, 1.0F};
        colors[ImGuiCol_Separator] = {0.13F, 0.18F, 0.23F, 1.0F};
        colors[ImGuiCol_Text] = {0.92F, 0.94F, 0.96F, 1.0F};
        colors[ImGuiCol_TextDisabled] = {0.52F, 0.57F, 0.63F, 1.0F};
    }

    static void ConfigureFonts(core::Logger& logger) {
        auto& io = ImGui::GetIO();
        const std::array<std::filesystem::path, 7> candidates{
            "C:/Windows/Fonts/msyh.ttc",
            "C:/Windows/Fonts/simhei.ttf",
            "/System/Library/Fonts/PingFang.ttc",
            "/System/Library/Fonts/Hiragino Sans GB.ttc",
            "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
        };
        const auto font = SelectCjkFont(candidates);
        if (font.empty()) {
            ImFontConfig fallback;
            fallback.SizePixels = 18.0F;
            io.FontDefault = io.Fonts->AddFontDefaultVector(&fallback);
            logger.Write(core::LogLevel::warn, "frontend.gui.font",
                         "CJK host font unavailable; using scalable ASCII fallback", {});
            return;
        }
        ImFontConfig config;
        config.OversampleH = 2;
        config.PixelSnapH = false;
        const auto path = font.string();
        io.FontDefault = io.Fonts->AddFontFromFileTTF(
            path.c_str(), 18.0F, &config,
            io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        if (io.FontDefault == nullptr) {
            ImFontConfig fallback;
            fallback.SizePixels = 18.0F;
            io.FontDefault = io.Fonts->AddFontDefaultVector(&fallback);
            logger.Write(core::LogLevel::warn, "frontend.gui.font",
                         "CJK host font could not be loaded; using scalable ASCII fallback", {},
                         {{"path", path}});
            return;
        }
        logger.Write(core::LogLevel::info, "frontend.gui.font",
                     "CJK host font loaded", {}, {{"path", path}});
    }

    void Cleanup() noexcept {
        if (renderer_ready_) ImGui_ImplOpenGL3_Shutdown();
        if (platform_ready_) ImGui_ImplSDL3_Shutdown();
        if (context_created_) ImGui::DestroyContext();
        renderer_ready_ = false;
        platform_ready_ = false;
        context_created_ = false;
    }
    bool context_created_{};
    bool platform_ready_{};
    bool renderer_ready_{};
};

class LibraryTextures final {
public:
    LibraryTextures(const std::vector<LibraryTile>& tiles, core::Logger& logger) {
        for (const auto& tile : tiles) {
            if (tile.icon_png.empty()) continue;
            const auto decoded = runtime::DecodeImageToArgb(tile.icon_png);
            if (!decoded.has_value()) {
                logger.Write(core::LogLevel::warn, "frontend.gui.icon",
                             "cached icon could not be decoded", {},
                             {{"package", tile.key}});
                continue;
            }
            std::vector<std::uint8_t> rgba(decoded->argb.size() * 4U);
            for (std::size_t index = 0; index < decoded->argb.size(); ++index) {
                const auto argb = decoded->argb[index];
                rgba[index * 4U] = static_cast<std::uint8_t>(argb >> 16U);
                rgba[index * 4U + 1U] = static_cast<std::uint8_t>(argb >> 8U);
                rgba[index * 4U + 2U] = static_cast<std::uint8_t>(argb);
                rgba[index * 4U + 3U] = static_cast<std::uint8_t>(argb >> 24U);
            }
            GLuint texture{};
            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, decoded->width, decoded->height,
                         0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
            if (glGetError() != GL_NO_ERROR) {
                glDeleteTextures(1, &texture);
                logger.Write(core::LogLevel::warn, "frontend.gui.icon",
                             "cached icon texture upload failed", {},
                             {{"package", tile.key}});
                continue;
            }
            textures_.push_back({tile.key, texture});
        }
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    ~LibraryTextures() {
        for (const auto& item : textures_) glDeleteTextures(1, &item.texture);
    }

    LibraryTextures(const LibraryTextures&) = delete;
    LibraryTextures& operator=(const LibraryTextures&) = delete;

    [[nodiscard]] GLuint Find(const std::string_view key) const noexcept {
        for (const auto& item : textures_) {
            if (item.key == key) return item.texture;
        }
        return 0;
    }

private:
    struct Item final {
        std::string key;
        GLuint texture{};
    };
    std::vector<Item> textures_;
};

[[nodiscard]] std::string FitLabel(std::string text, const float width) {
    if (ImGui::CalcTextSize(text.c_str()).x <= width) return text;
    constexpr std::string_view suffix = "...";
    while (!text.empty()) {
        auto begin = text.size() - 1U;
        while (begin > 0 &&
               (static_cast<unsigned char>(text[begin]) & 0xc0U) == 0x80U) {
            --begin;
        }
        text.resize(begin);
        const auto candidate = text + std::string(suffix);
        if (ImGui::CalcTextSize(candidate.c_str()).x <= width) return candidate;
    }
    return std::string(suffix);
}

struct Badge final {
    const char* label{};
    ImU32 color{};
};

[[nodiscard]] Badge TileBadge(const LibraryTileStatus status) {
    switch (status) {
    case LibraryTileStatus::damaged:
        return {"条目损坏", IM_COL32(239, 91, 91, 255)};
    case LibraryTileStatus::profile_catalog_unavailable:
        return {"Profile 不可用", IM_COL32(239, 91, 91, 255)};
    case LibraryTileStatus::missing_profile:
        return {"缺 Profile", IM_COL32(243, 166, 42, 255)};
    case LibraryTileStatus::missing_external:
        return {"缺数据包", IM_COL32(243, 166, 42, 255)};
    case LibraryTileStatus::running:
        return {"运行中", IM_COL32(67, 209, 122, 255)};
    case LibraryTileStatus::setup_required:
        return {"需要设置", IM_COL32(243, 166, 42, 255)};
    case LibraryTileStatus::ready:
        return {"可启动", IM_COL32(67, 209, 122, 255)};
    }
    return {};
}

struct RowAction final {
    bool selected{};
    bool launch_requested{};
    bool delete_requested{};
};

[[nodiscard]] RowAction DrawLibraryRow(const LibraryTile& tile,
                                      const bool selected,
                                      const LibraryTextures& textures) {
    constexpr float icon_size = 64.0F;
    constexpr float row_height = 84.0F;
    ImGui::PushID(tile.key.c_str());
    const auto position = ImGui::GetCursorScreenPos();
    const auto width = ImGui::GetContentRegionAvail().x;
    const auto clicked = ImGui::InvisibleButton("library_row", {width, row_height});
    const auto hovered = ImGui::IsItemHovered();
    auto* draw = ImGui::GetWindowDrawList();
    const auto background = selected
                                ? IM_COL32(17, 63, 48, 255)
                                : hovered ? IM_COL32(18, 35, 47, 255)
                                          : IM_COL32(12, 27, 38, 255);
    const auto border = selected ? IM_COL32(67, 209, 122, 255)
                                 : IM_COL32(39, 55, 70, 255);
    draw->AddRectFilled(position, {position.x + width, position.y + row_height},
                        background, 10.0F);
    draw->AddRect(position, {position.x + width, position.y + row_height}, border,
                  10.0F, 0, selected ? 2.0F : 1.0F);

    const ImVec2 image_position{position.x + 10.0F, position.y + 10.0F};
    const auto texture = textures.Find(tile.key);
    if (texture != 0) {
        draw->AddImageRounded(static_cast<ImTextureID>(texture), image_position,
                              {image_position.x + icon_size,
                               image_position.y + icon_size},
                              {0.0F, 0.0F}, {1.0F, 1.0F}, IM_COL32_WHITE, 9.0F);
    } else {
        draw->AddRectFilled(image_position,
                            {image_position.x + icon_size,
                             image_position.y + icon_size},
                            IM_COL32(48, 58, 76, 255), 9.0F);
        const auto marker = ImGui::CalcTextSize("?");
        draw->AddText({image_position.x + (icon_size - marker.x) * 0.5F,
                       image_position.y + (icon_size - marker.y) * 0.5F},
                      IM_COL32(190, 198, 212, 255), "?");
    }
    const auto badge = TileBadge(tile.status);
    const auto text_left = image_position.x + icon_size + 14.0F;
    const auto text_width = std::max(24.0F, width - icon_size - 38.0F);
    const auto fitted = FitLabel(tile.display_name, text_width);
    draw->AddText({text_left, position.y + 17.0F}, IM_COL32(238, 242, 247, 255),
                  fitted.c_str());
    draw->AddCircleFilled({text_left + 5.0F, position.y + 57.0F}, 5.0F,
                          badge.color);
    draw->AddText({text_left + 17.0F, position.y + 47.0F}, badge.color,
                  badge.label);

    RowAction action{
        .selected = clicked,
        .launch_requested = tile.can_launch && hovered &&
                            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left),
    };
    if (ImGui::BeginPopupContextItem("条目操作")) {
        const auto deletable = !tile.running;
        action.delete_requested =
            ImGui::MenuItem("删除游戏", nullptr, false, deletable);
        ImGui::EndPopup();
    }
    if (hovered && ImGui::CalcTextSize(tile.display_name.c_str()).x > text_width) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(tile.display_name.c_str());
        if (!tile.detail.empty()) ImGui::TextWrapped("%s", tile.detail.c_str());
        ImGui::EndTooltip();
    }
    ImGui::PopID();
    return action;
}

struct LibraryAction final {
    bool import_requested{};
    bool settings_requested{};
    std::optional<std::string> selected_package;
    std::optional<std::string> launch_package;
    std::optional<std::string> delete_package;
};

void DrawBrand() {
    const auto position = ImGui::GetCursorScreenPos();
    constexpr float size = 34.0F;
    std::array<ImVec2, 6> points{};
    for (std::size_t index = 0; index < points.size(); ++index) {
        constexpr float pi = 3.14159265358979323846F;
        const auto angle = pi / 3.0F * static_cast<float>(index) - pi / 6.0F;
        points[index] = {position.x + size * 0.5F + std::cos(angle) * size * 0.5F,
                         position.y + size * 0.5F + std::sin(angle) * size * 0.5F};
    }
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddConvexPolyFilled(points.data(), static_cast<int>(points.size()),
                              IM_COL32(67, 209, 122, 255));
    const std::array<ImVec2, 3> play{{
        {position.x + 13.0F, position.y + 10.0F},
        {position.x + 13.0F, position.y + 24.0F},
        {position.x + 24.0F, position.y + 17.0F},
    }};
    draw->AddConvexPolyFilled(play.data(), static_cast<int>(play.size()),
                              IM_COL32(7, 27, 30, 255));
    ImGui::Dummy({size, size});
    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.0F);
    ImGui::TextUnformatted("OGPlay");
}

[[nodiscard]] ImVec4 ConditionColor(const LibraryConditionStatus status) {
    switch (status) {
    case LibraryConditionStatus::ready:
    case LibraryConditionStatus::not_required:
        return {0.26F, 0.82F, 0.48F, 1.0F};
    case LibraryConditionStatus::missing:
        return {0.95F, 0.65F, 0.16F, 1.0F};
    case LibraryConditionStatus::unavailable:
        return {0.94F, 0.36F, 0.36F, 1.0F};
    }
    return {0.52F, 0.57F, 0.63F, 1.0F};
}

[[nodiscard]] const char* ConditionStatusLabel(
    const char* label, const LibraryConditionStatus status) {
    switch (status) {
    case LibraryConditionStatus::ready:
        if (std::string_view(label) == "精确 Profile") return "已匹配";
        if (std::string_view(label) == "Android 系统库") return "已配置";
        return "已就绪";
    case LibraryConditionStatus::missing:
        if (std::string_view(label) == "精确 Profile") return "未匹配";
        if (std::string_view(label) == "Android 系统库") return "未配置";
        return "缺失";
    case LibraryConditionStatus::not_required:
        return "不需要";
    case LibraryConditionStatus::unavailable:
        return "无法判断";
    }
    return "未知";
}

void DrawConditionRow(const char* label, const LibraryCondition& condition) {
    ImGui::TableNextRow(ImGuiTableRowFlags_None, 42.0F);
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::AlignTextToFramePadding();
    const auto fitted = FitLabel(condition.value,
                                 ImGui::GetContentRegionAvail().x - 6.0F);
    ImGui::TextUnformatted(fitted.c_str());
    if (ImGui::IsItemHovered() &&
        (fitted != condition.value || !condition.detail.empty())) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(condition.value.c_str());
        if (!condition.detail.empty()) {
            ImGui::Separator();
            ImGui::TextWrapped("%s", condition.detail.c_str());
        }
        ImGui::EndTooltip();
    }
    ImGui::TableSetColumnIndex(2);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ConditionColor(condition.status), "●  %s",
                       ConditionStatusLabel(label, condition.status));
}

void DrawDetail(const LibraryDetail& detail, const LibraryTextures& textures,
                LibraryAction& action) {
    const auto texture = textures.Find(detail.key);
    if (texture != 0) {
        ImGui::Image(static_cast<ImTextureID>(texture), {128.0F, 128.0F});
    } else {
        const auto position = ImGui::GetCursorScreenPos();
        ImGui::Dummy({128.0F, 128.0F});
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(position, {position.x + 128.0F, position.y + 128.0F},
                            IM_COL32(48, 58, 76, 255), 12.0F);
        const auto marker = ImGui::CalcTextSize("?");
        draw->AddText({position.x + (128.0F - marker.x) * 0.5F,
                       position.y + (128.0F - marker.y) * 0.5F},
                      IM_COL32(190, 198, 212, 255), "?");
    }
    ImGui::SameLine(0.0F, 24.0F);
    ImGui::BeginGroup();
    ImGui::TextUnformatted(detail.display_name.c_str());
    ImGui::TextDisabled("%s", detail.package.c_str());
    ImGui::TextDisabled("%s", detail.version.c_str());
    const auto badge = TileBadge(detail.status);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(badge.color), "●  %s",
                       badge.label);
    ImGui::EndGroup();

    ImGui::Spacing();
    const auto launch_width = std::min(420.0F, ImGui::GetContentRegionAvail().x);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15F, 0.61F, 0.31F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImVec4(0.19F, 0.72F, 0.38F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImVec4(0.12F, 0.52F, 0.26F, 1.0F));
    ImGui::BeginDisabled(!detail.can_launch);
    if (GuiButton("启动游戏##detail", {launch_width, 48.0F})) {
        action.launch_package = detail.key;
    }
    ImGui::EndDisabled();
    ImGui::PopStyleColor(3);
    if (!detail.detail.empty()) {
        ImGui::TextWrapped("%s", detail.detail.c_str());
    }
    if (detail.status == LibraryTileStatus::setup_required &&
        GuiButton("打开设置##detail")) {
        action.settings_requested = true;
    }

    ImGui::Spacing();
    ImGui::SeparatorText("运行条件");
    if (ImGui::BeginTable("运行条件", 3,
                          ImGuiTableFlags_BordersInnerH |
                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("项目", ImGuiTableColumnFlags_WidthStretch, 0.32F);
        ImGui::TableSetupColumn("值", ImGuiTableColumnFlags_WidthStretch, 0.43F);
        ImGui::TableSetupColumn("状态", ImGuiTableColumnFlags_WidthStretch, 0.25F);
        DrawConditionRow("精确 Profile", detail.profile);
        DrawConditionRow("外部数据", detail.external);
        DrawConditionRow("Android 系统库", detail.system);
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::SeparatorText("管理");
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24F, 0.075F, 0.085F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImVec4(0.36F, 0.09F, 0.10F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImVec4(0.44F, 0.11F, 0.12F, 1.0F));
    ImGui::BeginDisabled(!detail.can_delete);
    if (GuiButton("删除游戏##detail", {220.0F, 44.0F})) {
        action.delete_package = detail.key;
    }
    ImGui::EndDisabled();
    ImGui::PopStyleColor(3);
}

[[nodiscard]] LibraryAction DrawLibrary(
    const std::vector<LibraryTile>& tiles, const LibraryDetail* detail,
    const std::optional<std::string>& selected_key,
    const LibraryTextures& textures) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                       ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("OGPlay launcher", nullptr, flags);
    DrawBrand();
    const auto buttons_width = ImGui::CalcTextSize("导入游戏").x +
                               ImGui::CalcTextSize("设置").x + 92.0F;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(),
                             ImGui::GetWindowWidth() - buttons_width));
    const auto import_requested = GuiButton("导入游戏##toolbar");
    ImGui::SameLine();
    const auto settings_requested = GuiButton("设置##toolbar");
    ImGui::Separator();
    ImGui::Spacing();
    LibraryAction action{.import_requested = import_requested,
                         .settings_requested = settings_requested};
    const auto available = ImGui::GetContentRegionAvail();
    const auto left_width = std::clamp(available.x * 0.31F, 280.0F, 390.0F);
    ImGui::BeginChild("游戏库", {left_width, available.y},
                      ImGuiChildFlags_Borders);
    ImGui::Text("游戏库  %zu", tiles.size());
    ImGui::Separator();
    if (tiles.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped("游戏库为空。导入游戏后会显示在这里。");
        if (GuiButton("导入游戏##empty_library")) {
            action.import_requested = true;
        }
    } else {
        for (const auto& tile : tiles) {
            const auto row = DrawLibraryRow(
                tile, selected_key.has_value() && *selected_key == tile.key,
                textures);
            if (row.selected) action.selected_package = tile.key;
            if (row.launch_requested) action.launch_package = tile.key;
            if (row.delete_requested) action.delete_package = tile.key;
            ImGui::Spacing();
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("游戏详情", {0.0F, available.y},
                      ImGuiChildFlags_Borders);
    if (detail == nullptr) {
        const auto remaining = ImGui::GetContentRegionAvail();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + remaining.y * 0.35F);
        ImGui::TextDisabled("选择或导入一个游戏以查看详情。");
    } else {
        DrawDetail(*detail, textures, action);
    }
    ImGui::EndChild();
    ImGui::End();
    return action;
}

[[nodiscard]] const LibraryTile* FindTile(
    const std::vector<LibraryTile>& tiles, const std::string_view package) {
    const auto found = std::find_if(tiles.begin(), tiles.end(),
                                    [package](const LibraryTile& tile) {
                                        return tile.key == package;
                                    });
    return found == tiles.end() ? nullptr : &*found;
}

[[nodiscard]] const LibraryEntry* FindEntry(
    const std::vector<LibraryEntry>& entries, const std::string_view package) {
    const auto found = std::find_if(entries.begin(), entries.end(),
                                    [package](const LibraryEntry& entry) {
                                        return entry.key == package;
                                    });
    return found == entries.end() ? nullptr : &*found;
}

[[nodiscard]] std::optional<std::string> LoadSystemDirectoryError(
    const std::filesystem::path& library_root) {
    try {
        return GuiSystemDirectoryError(LoadGuiConfig(library_root));
    } catch (const std::exception& error) {
        return "现有设置不可读：" + std::string(error.what()) +
               "。请打开设置并保存有效目录。";
    }
}

int RunShell(const GuiOptions& options, core::Logger& logger) {
    PrepareLog(logger, options.library_root);
    logger.Write(core::LogLevel::info, "frontend.gui", "GUI shell starting", {},
                 {{"library_root", PathUtf8(options.library_root)}});

    SdlVideo video;
    const auto* vendor_bytes = glGetString(GL_VENDOR);
    const auto* renderer_bytes = glGetString(GL_RENDERER);
    const auto* version_bytes = glGetString(GL_VERSION);
    if (vendor_bytes == nullptr || renderer_bytes == nullptr || version_bytes == nullptr) {
        throw std::runtime_error("GLES2 context did not publish renderer identity");
    }
    const std::string vendor(reinterpret_cast<const char*>(vendor_bytes));
    const std::string renderer(reinterpret_cast<const char*>(renderer_bytes));
    const std::string version(reinterpret_cast<const char*>(version_bytes));
    if (renderer.find("ANGLE") == std::string::npos) {
        throw std::runtime_error("GUI GLES2 context is not backed by ANGLE: " + renderer);
    }
    logger.Write(core::LogLevel::info, "frontend.gui", "ANGLE context ready", {},
                 {{"vendor", vendor}, {"renderer", renderer}, {"version", version}});
    ImGuiSession imgui(video, logger);
    LibraryStore store(options.library_root);
    GuiImportUi import_ui(video.Window(), store, logger);
    GuiSettingsUi settings_ui(video.Window(), options.library_root, logger);
    GuiManagementUi management_ui(store, logger);
    GuiProcessManager processes(logger);
    std::vector<LibraryEntry> entries;
    std::vector<std::string> required_external;
    LibraryViewContext view_context;
    std::vector<LibraryTile> tiles;
    LibrarySelection selection;
    std::unique_ptr<LibraryTextures> textures;
    const auto reload_library = [&] {
        entries = store.LoadEntries();
        required_external = import_ui.ExternalRequiredPackages(entries);
        view_context = {
            .running_packages = processes.RunningPackages(),
            .external_required_packages = required_external,
            .profile_catalog_error = import_ui.ProfileCatalogError(),
            .system_directory_error =
                LoadSystemDirectoryError(options.library_root),
        };
        tiles = BuildLibraryTiles(entries, view_context);
        selection.Reconcile(tiles);
        textures = std::make_unique<LibraryTextures>(tiles, logger);
    };
    reload_library();
    GuiMessageQueue messages;
    bool exit_confirmation_requested{};
    bool running = true;
    std::uint64_t rendered_frames{};
    while (running) {
        const auto wait = GuiEventWaitMilliseconds(
            options.smoke_frames.has_value());
        if (rendered_frames != 0 && wait != 0) {
            static_cast<void>(SDL_WaitEventTimeout(
                nullptr, static_cast<Sint32>(wait)));
        }
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            static_cast<void>(import_ui.HandleEvent(event));
            static_cast<void>(settings_ui.HandleEvent(event));
            if (event.type == SDL_EVENT_QUIT ||
                event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                if (processes.RunningPackages().empty()) {
                    running = false;
                } else {
                    exit_confirmation_requested = true;
                }
            }
        }
        if (!running) break;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        const auto exits = processes.Poll();
        if (!exits.empty()) {
            reload_library();
            for (const auto& exit : exits) {
                if (exit.exit_code == 0) continue;
                auto message = "进程退出码：" + std::to_string(exit.exit_code) +
                               "\n完整日志：" + PathUtf8(exit.log_path);
                const auto tail = ReadLogTail(exit.log_path);
                if (!tail.empty()) message += "\n\n日志末尾：\n" + tail;
                message +=
                    "\n\n这是游戏兼容性问题的诊断证据，可随 issue 一并提交。";
                messages.Push("游戏运行失败", std::move(message));
            }
        }
        std::optional<LibraryDetail> detail;
        if (selection.Key().has_value()) {
            const auto* selected_tile = FindTile(tiles, *selection.Key());
            const auto* selected_entry = FindEntry(entries, *selection.Key());
            if (selected_tile != nullptr && selected_entry != nullptr) {
                detail = BuildLibraryDetail(*selected_entry, *selected_tile,
                                            view_context);
            }
        }
        const auto action = DrawLibrary(
            tiles, detail.has_value() ? &*detail : nullptr, selection.Key(),
            *textures);
        if (action.selected_package.has_value()) {
            selection.Select(*action.selected_package, tiles);
        }
        if (action.import_requested) import_ui.OpenApkDialog();
        if (action.settings_requested) settings_ui.Open();
        if (action.delete_package.has_value()) {
            const auto* entry = FindEntry(entries, *action.delete_package);
            if (entry != nullptr) {
                management_ui.OpenDelete(
                    *entry, processes.IsRunning(*action.delete_package));
            }
        }
        if (action.launch_package.has_value()) {
            const auto* tile = FindTile(tiles, *action.launch_package);
            const auto* entry = FindEntry(entries, *action.launch_package);
            try {
                if (tile == nullptr || entry == nullptr) {
                    throw GuiModelError(GuiModelErrorCode::not_found,
                                        "library tile disappeared before launch");
                }
                if (!tile->can_launch) {
                    messages.Push("暂时无法启动",
                                  tile->detail.empty()
                                      ? "该游戏当前不可启动。"
                                      : tile->detail);
                } else {
                    const auto plan = BuildLaunchPlan(
                        FindSiblingCliExecutable(), store.Root(), *entry,
                        LoadGuiConfig(options.library_root));
                    processes.Launch(plan);
                    reload_library();
                }
            } catch (const std::exception& error) {
                std::string message = error.what();
                const auto* model_error =
                    dynamic_cast<const GuiModelError*>(&error);
                if (model_error != nullptr && !model_error->Path().empty()) {
                    message += "\n路径：" + PathUtf8(model_error->Path());
                }
                message +=
                    "\n\n下一步：检查设置中的系统/Profile 目录，或重新导入游戏。";
                if (model_error != nullptr) {
                    logger.Write(
                        core::LogLevel::error, "frontend.gui.launch",
                        "launch request failed", {},
                        {{"package", *action.launch_package},
                         {"reason", std::string(error.what())},
                         {"code", static_cast<std::uint64_t>(
                                      model_error->Code())},
                         {"path", PathUtf8(model_error->Path())}});
                } else {
                    logger.Write(core::LogLevel::error,
                                 "frontend.gui.launch",
                                 "launch request failed", {},
                                 {{"package", *action.launch_package},
                                  {"reason", std::string(error.what())}});
                }
                messages.Push("启动失败", std::move(message));
            }
        }
        if (import_ui.Draw()) {
            reload_library();
        }
        if (settings_ui.Draw()) {
            const auto profile_error = import_ui.ReloadProfiles();
            reload_library();
            if (profile_error.has_value()) {
                messages.Push(
                    "Profile 目录不可用",
                    *profile_error +
                        "\n\n下一步：打开设置，选择有效的 Profile 目录或使用内置默认。");
            }
        }
        if (management_ui.Draw()) reload_library();
        if (exit_confirmation_requested) ImGui::OpenPopup("退出主面板？");
        if (ImGui::BeginPopupModal("退出主面板？", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(
                "仍有游戏正在运行。退出主面板不会关闭这些游戏。是否继续？");
            if (GuiButton("继续运行主面板##exit_confirmation")) {
                exit_confirmation_requested = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (GuiButton("退出但保留游戏##exit_confirmation")) {
                exit_confirmation_requested = false;
                running = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (messages.ActivateNext(
                ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup))) {
            ImGui::OpenPopup(messages.Active()->title.c_str());
        }
        if (const auto* message = messages.Active();
            message != nullptr && ImGui::BeginPopupModal(
                message->title.c_str(), nullptr,
                ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("%s", message->message.c_str());
            if (GuiButton("关闭##result")) {
                ImGui::CloseCurrentPopup();
                messages.DismissActive();
            }
            ImGui::EndPopup();
        }
        ImGui::Render();

        int width{};
        int height{};
        if (!SDL_GetWindowSizeInPixels(video.Window(), &width, &height)) {
            ThrowSdl("SDL_GetWindowSizeInPixels");
        }
        glViewport(0, 0, width, height);
        glClearColor(0.055F, 0.067F, 0.09F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (!SDL_GL_SwapWindow(video.Window())) ThrowSdl("SDL_GL_SwapWindow");

        ++rendered_frames;
        if (options.smoke_frames.has_value() &&
            rendered_frames >= *options.smoke_frames) {
            running = false;
        }
    }

    logger.Write(core::LogLevel::info, "frontend.gui", "GUI shell stopped", {},
                 {{"rendered_frames", rendered_frames}});
    return 0;
}

}  // namespace

int RunGuiCommand(const int argc, const char* const argv[], core::Logger& logger) {
    return RunShell(ParseOptions(argc, argv), logger);
}

int RunGuiStandalone() {
    core::Logger logger;
    try {
        const std::array<const char*, 2> argv{"ogplay", "gui"};
        return RunGuiCommand(static_cast<int>(argv.size()), argv.data(), logger);
    } catch (const std::exception& error) {
        static_cast<void>(SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR, "OGPlay could not start", error.what(), nullptr));
        return 1;
    }
}

}  // namespace ogplay::frontend
