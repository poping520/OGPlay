#include "ogplay/frontend/gui.h"

#include <SDL3/SDL.h>
#include <GLES2/gl2.h>

#include <algorithm>
#include <array>
#include <charconv>
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
#include "process_manager.h"

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

void PrepareLog(core::Logger& logger, const std::filesystem::path& root) {
    std::filesystem::create_directories(root);
    const auto path = root / "gui.log";
    {
        std::ofstream truncate(path, std::ios::binary | std::ios::trunc);
        if (!truncate) throw std::runtime_error("cannot create GUI log: " + path.string());
    }
    logger.AddSink(std::make_shared<core::FileSink>(path), core::LogLevel::info);
}

[[noreturn]] void ThrowSdl(const std::string_view operation) {
    throw std::runtime_error(std::string(operation) + " failed: " + SDL_GetError());
}

[[nodiscard]] std::string PathUtf8(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
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

            window_ = SDL_CreateWindow("OGPlay", 960, 640,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
                                           SDL_WINDOW_HIGH_PIXEL_DENSITY);
            if (window_ == nullptr) ThrowSdl("SDL_CreateWindow");
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
    static void ConfigureFonts(core::Logger& logger) {
        auto& io = ImGui::GetIO();
        ImFontConfig default_config;
        default_config.SizePixels = 16.0F;
        static_cast<void>(io.Fonts->AddFontDefault(&default_config));
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
            logger.Write(core::LogLevel::warn, "frontend.gui.font",
                         "CJK host font unavailable; using ASCII fallback", {});
            return;
        }
        ImFontConfig config;
        config.MergeMode = true;
        config.PixelSnapH = true;
        const auto path = font.string();
        if (io.Fonts->AddFontFromFileTTF(
                path.c_str(), 16.0F, &config,
                io.Fonts->GetGlyphRangesChineseSimplifiedCommon()) == nullptr) {
            logger.Write(core::LogLevel::warn, "frontend.gui.font",
                         "CJK host font could not be loaded; using ASCII fallback", {},
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
        return {"条目损坏", IM_COL32(184, 52, 52, 235)};
    case LibraryTileStatus::missing_profile:
        return {"缺 Profile", IM_COL32(193, 118, 30, 235)};
    case LibraryTileStatus::missing_external:
        return {"缺数据包", IM_COL32(193, 118, 30, 235)};
    case LibraryTileStatus::running:
        return {"运行中", IM_COL32(38, 142, 82, 235)};
    case LibraryTileStatus::ready:
        return {};
    }
    return {};
}

[[nodiscard]] bool DrawTile(const LibraryTile& tile,
                            const LibraryTextures& textures) {
    constexpr float icon_size = 128.0F;
    constexpr float tile_width = 152.0F;
    ImGui::PushID(tile.key.c_str());
    ImGui::BeginGroup();
    const auto left = ImGui::GetCursorPosX();
    ImGui::SetCursorPosX(left + (tile_width - icon_size) * 0.5F);
    const auto image_position = ImGui::GetCursorScreenPos();
    const auto texture = textures.Find(tile.key);
    if (texture != 0) {
        ImGui::Image(static_cast<ImTextureID>(texture), {icon_size, icon_size});
    } else {
        ImGui::Dummy({icon_size, icon_size});
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(image_position,
                            {image_position.x + icon_size,
                             image_position.y + icon_size},
                            IM_COL32(48, 58, 76, 255), 12.0F);
        const auto marker = ImGui::CalcTextSize("?");
        draw->AddText({image_position.x + (icon_size - marker.x) * 0.5F,
                       image_position.y + (icon_size - marker.y) * 0.5F},
                      IM_COL32(190, 198, 212, 255), "?");
    }
    const auto badge = TileBadge(tile.status);
    if (badge.label != nullptr) {
        auto* draw = ImGui::GetWindowDrawList();
        const auto text = ImGui::CalcTextSize(badge.label);
        const ImVec2 minimum{image_position.x + icon_size - text.x - 14.0F,
                             image_position.y + icon_size - text.y - 10.0F};
        draw->AddRectFilled(minimum,
                            {image_position.x + icon_size,
                             image_position.y + icon_size},
                            badge.color, 6.0F, ImDrawFlags_RoundCornersTopLeft);
        draw->AddText({minimum.x + 7.0F, minimum.y + 4.0F},
                      IM_COL32_WHITE, badge.label);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(tile.display_name.c_str());
        if (badge.label != nullptr) ImGui::TextUnformatted(badge.label);
        if (!tile.detail.empty()) ImGui::TextWrapped("%s", tile.detail.c_str());
        ImGui::EndTooltip();
    }
    const auto fitted = FitLabel(tile.display_name, tile_width);
    const auto label_width = ImGui::CalcTextSize(fitted.c_str()).x;
    ImGui::SetCursorPosX(left + std::max(0.0F, (tile_width - label_width) * 0.5F));
    ImGui::TextUnformatted(fitted.c_str());
    ImGui::EndGroup();
    const auto selected = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    ImGui::PopID();
    return selected;
}

struct LibraryAction final {
    bool import_requested{};
    std::optional<std::string> selected_package;
};

[[nodiscard]] LibraryAction DrawLibrary(
    const std::vector<LibraryTile>& tiles,
    const LibraryTextures& textures) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                       ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("OGPlay launcher", nullptr, flags);
    ImGui::TextUnformatted("OGPlay");
    const auto buttons_width = ImGui::CalcTextSize("导入游戏").x +
                               ImGui::CalcTextSize("设置").x + 54.0F;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(),
                             ImGui::GetWindowWidth() - buttons_width));
    const auto import_requested = ImGui::Button("导入游戏");
    ImGui::SameLine();
    ImGui::BeginDisabled();
    ImGui::Button("设置");
    ImGui::EndDisabled();
    ImGui::Separator();
    ImGui::Spacing();
    if (tiles.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped("游戏库为空。导入游戏后会显示在这里。");
        if (ImGui::Button("导入游戏")) {
            ImGui::End();
            return {.import_requested = true};
        }
    } else {
        constexpr float tile_width = 152.0F;
        constexpr float spacing = 18.0F;
        const auto available = ImGui::GetContentRegionAvail().x;
        const auto columns = std::max(
            1, static_cast<int>((available + spacing) / (tile_width + spacing)));
        for (std::size_t index = 0; index < tiles.size(); ++index) {
            if (index % static_cast<std::size_t>(columns) != 0) ImGui::SameLine();
            if (DrawTile(tiles[index], textures)) {
                ImGui::End();
                return {.selected_package = tiles[index].key};
            }
        }
    }
    ImGui::End();
    return {.import_requested = import_requested};
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

int RunShell(const GuiOptions& options, core::Logger& logger) {
    PrepareLog(logger, options.library_root);
    logger.Write(core::LogLevel::info, "frontend.gui", "GUI shell starting", {},
                 {{"library_root", options.library_root.string()}});

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
    GuiImportUi import_ui(video.Window(), store,
                          std::filesystem::path(OGPLAY_SOURCE_DIR), logger);
    GuiProcessManager processes(logger);
    auto entries = store.LoadEntries();
    auto required_external = import_ui.ExternalRequiredPackages(entries);
    auto tiles = BuildLibraryTiles(
        entries, {.running_packages = processes.RunningPackages(),
                  .external_required_packages = required_external});
    auto textures = std::make_unique<LibraryTextures>(tiles, logger);
    const auto reload_library = [&] {
        entries = store.LoadEntries();
        required_external = import_ui.ExternalRequiredPackages(entries);
        tiles = BuildLibraryTiles(
            entries, {.running_packages = processes.RunningPackages(),
                      .external_required_packages = required_external});
        textures = std::make_unique<LibraryTextures>(tiles, logger);
    };
    std::string result_title;
    std::string result_message;
    bool exit_confirmation_requested{};
    bool running = true;
    std::uint64_t rendered_frames{};
    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            static_cast<void>(import_ui.HandleEvent(event));
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
                result_title = "游戏运行失败";
                result_message = "进程退出码：" + std::to_string(exit.exit_code) +
                                 "\n完整日志：" + PathUtf8(exit.log_path);
                const auto tail = ReadLogTail(exit.log_path);
                if (!tail.empty()) result_message += "\n\n日志末尾：\n" + tail;
            }
        }
        const auto action = DrawLibrary(tiles, *textures);
        if (action.import_requested) import_ui.OpenApkDialog();
        if (action.selected_package.has_value()) {
            const auto* tile = FindTile(tiles, *action.selected_package);
            const auto* entry = FindEntry(entries, *action.selected_package);
            try {
                if (tile == nullptr || entry == nullptr) {
                    throw GuiModelError(GuiModelErrorCode::not_found,
                                        "library tile disappeared before launch");
                }
                if (tile->status != LibraryTileStatus::ready) {
                    result_title = "暂时无法启动";
                    result_message = tile->detail.empty()
                                         ? "该游戏当前不可启动。"
                                         : tile->detail;
                } else {
                    const auto plan = BuildLaunchPlan(
                        FindSiblingCliExecutable(), *entry,
                        LoadGuiConfig(options.library_root));
                    processes.Launch(plan);
                    reload_library();
                }
            } catch (const std::exception& error) {
                result_title = "启动失败";
                result_message = error.what();
                const auto* model_error =
                    dynamic_cast<const GuiModelError*>(&error);
                if (model_error != nullptr && !model_error->Path().empty()) {
                    result_message += "\n路径：" + PathUtf8(model_error->Path());
                }
                result_message +=
                    "\n\n下一步：检查设置中的系统/Profile 目录，或重新导入游戏。";
                logger.Write(core::LogLevel::error, "frontend.gui.launch",
                             "launch request failed", {},
                             {{"package", *action.selected_package},
                              {"reason", std::string(error.what())}});
            }
        }
        if (import_ui.Draw()) {
            reload_library();
        }
        if (!result_title.empty()) {
            ImGui::OpenPopup(result_title.c_str());
        }
        if (!result_title.empty() && ImGui::BeginPopupModal(
                result_title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("%s", result_message.c_str());
            if (ImGui::Button("关闭")) {
                ImGui::CloseCurrentPopup();
                result_title.clear();
                result_message.clear();
            }
            ImGui::EndPopup();
        }
        if (exit_confirmation_requested) ImGui::OpenPopup("退出主面板？");
        if (ImGui::BeginPopupModal("退出主面板？", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(
                "仍有游戏正在运行。退出主面板不会关闭这些游戏。是否继续？");
            if (ImGui::Button("继续运行主面板")) {
                exit_confirmation_requested = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("退出但保留游戏")) {
                exit_confirmation_requested = false;
                running = false;
                ImGui::CloseCurrentPopup();
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
