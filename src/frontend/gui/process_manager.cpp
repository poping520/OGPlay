#include "process_manager.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_properties.h>

#include <stdexcept>
#include <algorithm>
#include "ogplay/hal/clock.h"
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

// The child gets the launcher's environment plus the key editor token; SDL takes the
// environment as the process's entire environment.
class ChildEnvironment final {
public:
    explicit ChildEnvironment(const std::string& control_token) : value_(SDL_CreateEnvironment(true)) {
        if (value_ == nullptr) ThrowSdl("SDL_CreateEnvironment");
        if (!control_token.empty() &&
            !SDL_SetEnvironmentVariable(value_, "OGPLAY_CONTROL_TOKEN", control_token.c_str(), true)) {
            SDL_DestroyEnvironment(value_);
            ThrowSdl("SDL_SetEnvironmentVariable");
        }
    }
    ~ChildEnvironment() { SDL_DestroyEnvironment(value_); }
    ChildEnvironment(const ChildEnvironment&) = delete;
    ChildEnvironment& operator=(const ChildEnvironment&) = delete;
    [[nodiscard]] SDL_Environment* Get() const noexcept { return value_; }
private:
    SDL_Environment* value_{};
};

}  // namespace

GuiProcessManager::GuiProcessManager(core::Logger& logger) : logger_(logger) {}

GuiProcessManager::~GuiProcessManager() {
    for (const auto& active : active_) SDL_DestroyProcess(active.process);
}

void GuiProcessManager::Launch(const LaunchPlan& plan) {
    if (plan.mcp_port && std::any_of(active_.begin(), active_.end(), [&](const auto& active) {
        return active.dashboard.port == plan.mcp_port;
    })) throw std::runtime_error("MCP 端口已被其他运行实例使用，请在游戏设置中选择其他端口。");
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
        const ChildEnvironment environment(plan.control_token);
        SetPointer(properties.Get(), SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, environment.Get());
        auto* process = SDL_CreateProcessWithProperties(properties.Get());
        static_cast<void>(SDL_CloseIO(error_stream));
        error_stream = nullptr;
        if (process == nullptr) ThrowSdl("SDL_CreateProcessWithProperties");
        Active active;
        active.package = plan.package; active.process = process;
        active.dashboard.installation_id = plan.package; active.dashboard.port = plan.mcp_port;
        active.dashboard.process_id = static_cast<std::uint64_t>(SDL_GetNumberProperty(SDL_GetProcessProperties(process), SDL_PROP_PROCESS_PID_NUMBER, 0));
        active.auto_open = plan.dashboard_auto_open;
        if (plan.mcp_port) { active.dashboard.status = "starting"; active.dashboard.detail = "等待 Dashboard 服务启动。"; }
        active.editor_port = plan.key_editor_port; active.control_token = plan.control_token;
        if (plan.key_editor_port && !plan.control_token.empty()) {
            active.dashboard.editor_status = "starting"; active.dashboard.editor_detail = "等待按键设置服务启动。";
        }
        active_.push_back(std::move(active));
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
    std::erase_if(retired_probes_, [](auto& probe) { return probe.wait_for(std::chrono::seconds(0)) == std::future_status::ready; });
    std::erase_if(retired_editor_probes_, [](auto& probe) { return probe.wait_for(std::chrono::seconds(0)) == std::future_status::ready; });
    std::vector<GameExit> exits;
    for (auto iterator = active_.begin(); iterator != active_.end();) {
        int exit_code{};
        if (!SDL_WaitProcess(iterator->process, false, &exit_code)) {
            auto& active = *iterator;
            const auto now = hal::Clock::SteadyTimestampNs();
            if (active.probe.valid() && active.probe.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                const auto result = active.probe.get();
                active.dashboard.status = result.ready ? "ready" : "unavailable";
                active.dashboard.detail = result.detail;
                active.next_probe = now + 1000000000ULL;
            }
            if (active.dashboard.port && !active.probe.valid() && now >= active.next_probe) {
                const auto port = *active.dashboard.port; const auto pid = active.dashboard.process_id;
                active.probe = std::async(std::launch::async, [port, pid] { return ProbeDashboard(port, pid); });
            }
            // The editor poll doubles as the launcher heartbeat that routes in-game F2 here.
            if (active.editor_probe.valid() && active.editor_probe.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                const auto result = active.editor_probe.get();
                active.dashboard.editor_status = result.ready ? "ready" : "unavailable";
                active.dashboard.editor_detail = result.detail;
                if (result.ready && result.edit_requests > active.edit_requests) active.editor_requested = true;
                if (result.ready) active.edit_requests = result.edit_requests;
                active.next_editor_probe = now + 500000000ULL;
            }
            if (active.editor_port && !active.control_token.empty() && !active.editor_probe.valid() &&
                now >= active.next_editor_probe) {
                const auto port = *active.editor_port; const auto token = active.control_token;
                active.editor_probe = std::async(std::launch::async, [port, token] { return ProbeKeyEditor(port, token); });
            }
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
        if (iterator->probe.valid()) retired_probes_.push_back(std::move(iterator->probe));
        if (iterator->editor_probe.valid()) retired_editor_probes_.push_back(std::move(iterator->editor_probe));
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

std::vector<GuiDashboard> GuiProcessManager::Dashboards() const {
    std::vector<GuiDashboard> result;
    for (const auto& active : active_) result.push_back(active.dashboard);
    return result;
}
std::uint16_t GuiProcessManager::DashboardPort(std::string_view instance) const {
    const auto found = std::find_if(active_.begin(), active_.end(), [&](const auto& active) { return active.package == instance; });
    if (found == active_.end()) throw std::runtime_error("实例已退出，请重新启动。");
    if (found->dashboard.status != "ready" || !found->dashboard.port) throw std::runtime_error(found->dashboard.detail);
    return *found->dashboard.port;
}
std::vector<std::string> GuiProcessManager::TakeAutoOpen() {
    std::vector<std::string> result;
    for (auto& active : active_) if (active.auto_open && active.dashboard.status == "ready") {
        active.auto_open = false; result.push_back(active.package);
    }
    return result;
}

GuiProcessManager::KeyEditorAddress GuiProcessManager::KeyEditor(const std::string_view instance) const {
    const auto found = std::find_if(active_.begin(), active_.end(), [&](const auto& active) { return active.package == instance; });
    if (found == active_.end()) throw std::runtime_error("实例已退出，请重新启动。");
    if (found->dashboard.editor_status != "ready" || !found->editor_port) throw std::runtime_error(found->dashboard.editor_detail);
    return {*found->editor_port, found->control_token};
}
std::vector<std::string> GuiProcessManager::TakeKeyEditorRequests() {
    std::vector<std::string> result;
    for (auto& active : active_) if (active.editor_requested) {
        active.editor_requested = false; result.push_back(active.package);
    }
    return result;
}

}  // namespace ogplay::frontend
