#include "ogplay/frontend/gui.h"

#include <SDL3/SDL.h>
#include <GLES2/gl2.h>

#include <array>
#include <charconv>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl3.h"

#include "ogplay/core/logger.h"
#include "ogplay/frontend/user_data_dir.h"

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
    explicit ImGuiSession(SdlVideo& video) {
        try {
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            context_created_ = true;
            ImGui::StyleColorsDark();
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

void DrawEmptyLibrary() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                       ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("OGPlay launcher", nullptr, flags);
    ImGui::TextUnformatted("OGPlay");
    ImGui::SameLine();
    ImGui::BeginDisabled();
    ImGui::Button("Import Game");
    ImGui::SameLine();
    ImGui::Button("Settings");
    ImGui::EndDisabled();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextWrapped("Your game library is empty. Import a game to get started.");
    ImGui::End();
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
    ImGuiSession imgui(video);
    bool running = true;
    std::uint64_t rendered_frames{};
    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT ||
                event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                running = false;
            }
        }
        if (!running) break;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        DrawEmptyLibrary();
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
