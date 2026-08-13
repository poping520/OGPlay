#include "process_manager.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_properties.h>

#include <stdexcept>
#include <string>
#include <utility>

#include "ogplay/core/logger.h"

namespace ogplay::frontend {
namespace {

[[nodiscard]] std::string PathUtf8(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

[[noreturn]] void ThrowSdl(const std::string_view operation) {
    throw std::runtime_error(std::string(operation) + " failed: " + SDL_GetError());
}

class Properties final {
public:
    Properties() : value_(SDL_CreateProperties()) {
        if (value_ == 0) ThrowSdl("SDL_CreateProperties");
    }
    ~Properties() { SDL_DestroyProperties(value_); }
    Properties(const Properties&) = delete;
    Properties& operator=(const Properties&) = delete;
    [[nodiscard]] SDL_PropertiesID Get() const noexcept { return value_; }
private:
    SDL_PropertiesID value_{};
};

void SetPointer(const SDL_PropertiesID properties, const char* name,
                void* value) {
    if (!SDL_SetPointerProperty(properties, name, value)) {
        ThrowSdl("SDL_SetPointerProperty");
    }
}

void SetNumber(const SDL_PropertiesID properties, const char* name,
               const Sint64 value) {
    if (!SDL_SetNumberProperty(properties, name, value)) {
        ThrowSdl("SDL_SetNumberProperty");
    }
}

}  // namespace

GuiProcessManager::GuiProcessManager(core::Logger& logger) : logger_(logger) {}

GuiProcessManager::~GuiProcessManager() {
    for (const auto& active : active_) SDL_DestroyProcess(active.process);
}

void GuiProcessManager::Launch(const LaunchPlan& plan) {
    tracker_.Begin(plan.package, plan.log_path);
    SDL_IOStream* error_stream{};
    try {
        const auto log_path = PathUtf8(plan.log_path);
        error_stream = SDL_IOFromFile(log_path.c_str(), "wb");
        if (error_stream == nullptr) ThrowSdl("SDL_IOFromFile");
        std::vector<const char*> arguments;
        arguments.reserve(plan.argv.size() + 1U);
        for (const auto& argument : plan.argv) arguments.push_back(argument.c_str());
        arguments.push_back(nullptr);
        Properties properties;
        SetPointer(properties.Get(), SDL_PROP_PROCESS_CREATE_ARGS_POINTER,
                   arguments.data());
        SetNumber(properties.Get(), SDL_PROP_PROCESS_CREATE_STDIN_NUMBER,
                  SDL_PROCESS_STDIO_NULL);
        SetNumber(properties.Get(), SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,
                  SDL_PROCESS_STDIO_INHERITED);
        SetNumber(properties.Get(), SDL_PROP_PROCESS_CREATE_STDERR_NUMBER,
                  SDL_PROCESS_STDIO_REDIRECT);
        SetPointer(properties.Get(), SDL_PROP_PROCESS_CREATE_STDERR_POINTER,
                   error_stream);
        auto* process = SDL_CreateProcessWithProperties(properties.Get());
        static_cast<void>(SDL_CloseIO(error_stream));
        error_stream = nullptr;
        if (process == nullptr) ThrowSdl("SDL_CreateProcessWithProperties");
        active_.push_back({plan.package, process});
        logger_.Write(core::LogLevel::info, "frontend.gui.launch",
                      "game process started", {},
                      {{"package", plan.package},
                       {"log_path", PathUtf8(plan.log_path)}});
    } catch (...) {
        if (error_stream != nullptr) static_cast<void>(SDL_CloseIO(error_stream));
        static_cast<void>(tracker_.Finish(plan.package, -255));
        throw;
    }
}

std::vector<GameExit> GuiProcessManager::Poll() {
    std::vector<GameExit> exits;
    for (auto iterator = active_.begin(); iterator != active_.end();) {
        int exit_code{};
        if (!SDL_WaitProcess(iterator->process, false, &exit_code)) {
            ++iterator;
            continue;
        }
        SDL_DestroyProcess(iterator->process);
        auto result = tracker_.Finish(iterator->package, exit_code);
        logger_.Write(exit_code == 0 ? core::LogLevel::info : core::LogLevel::error,
                      "frontend.gui.launch", "game process stopped", {},
                      {{"package", result.package},
                       {"exit_code", static_cast<std::int64_t>(exit_code)},
                       {"log_path", PathUtf8(result.log_path)}});
        exits.push_back(std::move(result));
        iterator = active_.erase(iterator);
    }
    return exits;
}

std::vector<std::string> GuiProcessManager::RunningPackages() const {
    return tracker_.RunningPackages();
}

bool GuiProcessManager::IsRunning(const std::string_view package) const noexcept {
    return tracker_.IsRunning(package);
}

std::filesystem::path FindSiblingCliExecutable() {
    const auto* base = SDL_GetBasePath();
    if (base == nullptr) ThrowSdl("SDL_GetBasePath");
    const auto root = std::filesystem::path(base);
    const std::filesystem::path candidates[]{
        root / "ogplay.exe", root / "ogplay",
        root / ".." / "MacOS" / "ogplay",
    };
    std::error_code error;
    for (const auto& candidate : candidates) {
        if (std::filesystem::is_regular_file(candidate, error) && !error) {
            return std::filesystem::absolute(candidate).lexically_normal();
        }
        error.clear();
    }
    throw GuiModelError(GuiModelErrorCode::not_found,
                        "OGPlay CLI executable is not installed beside the launcher",
                        root);
}

}  // namespace ogplay::frontend
