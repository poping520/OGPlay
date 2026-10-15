#include "frontend/gui/launch.h"
#include "frontend/gui/dashboard.h"
#include "ogplay/app/library.h"
#include "ogplay/app/encoding.h"
#include "ogplay/app/host.h"

#include <array>
#include <cstddef>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace ogplay::frontend {
using namespace ogplay::app;
namespace {

[[nodiscard]] std::string PathUtf8(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}


}  // namespace

LaunchPlan BuildLaunchPlan(const std::filesystem::path& game_executable,
                           const std::filesystem::path& library_root,
                           const LibraryEntry& entry, const LibraryConfig& config, LaunchMode mode) {
    if (!std::filesystem::is_regular_file(game_executable)) throw LibraryError(LibraryErrorCode::not_found, "OGPlay game executable is missing", game_executable);
    if (LibraryStore(library_root).IsRunning(entry.key)) throw LibraryError(LibraryErrorCode::running, "instance_running: game is already running; close it before launching again");
    auto resolved = ResolveLaunch(LibraryStore(library_root), entry, config, mode);
    LaunchPlan plan;
    plan.package = entry.key;
    plan.mcp_port = resolved.mcp_port;
    plan.dashboard_auto_open = resolved.dashboard_auto_open;
    if (mode != LaunchMode::preflight) {
        plan.control_token = app::NewControlToken();
        plan.key_editor_port = plan.mcp_port ? *plan.mcp_port : PickLoopbackPort();
        if (!plan.mcp_port) resolved.options.key_editor_port = plan.key_editor_port;
    }
    plan.argv = {PathUtf8(std::filesystem::absolute(game_executable).lexically_normal()), "--run-game"};
    const auto args = app::FormatGameRunArgs(resolved.options);
    plan.argv.insert(plan.argv.end(), args.begin(), args.end());
    plan.log_path = std::filesystem::absolute(entry.directory / "last-run.log").lexically_normal();
    return plan;
}

void LaunchTracker::Begin(std::string package,
                          std::filesystem::path log_path) {
    if (package.empty() || IsRunning(package)) {
        throw LibraryError(LibraryErrorCode::duplicate_package,
                            "this package is already running", std::move(log_path));
    }
    active_.push_back({std::move(package), std::move(log_path)});
}

GameExit LaunchTracker::Finish(const std::string_view package,
                               const int exit_code) {
    const auto found = std::find_if(active_.begin(), active_.end(),
                                    [package](const Active& active) {
                                        return active.package == package;
                                    });
    if (found == active_.end()) {
        throw LibraryError(LibraryErrorCode::not_found,
                            "running package is not tracked");
    }
    GameExit result{found->package, exit_code, found->log_path};
    active_.erase(found);
    return result;
}

bool LaunchTracker::IsRunning(const std::string_view package) const noexcept {
    return std::any_of(active_.begin(), active_.end(),
                       [package](const Active& active) {
                           return active.package == package;
                       });
}

std::vector<std::string> LaunchTracker::RunningPackages() const {
    std::vector<std::string> result;
    result.reserve(active_.size());
    for (const auto& active : active_) result.push_back(active.package);
    std::sort(result.begin(), result.end());
    return result;
}

std::string ReadLogTail(const std::filesystem::path& path,
                        const std::size_t maximum_lines,
                        const std::size_t maximum_bytes) {
    if (maximum_lines == 0 || maximum_bytes == 0) return {};
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end <= 0) return {};
    const auto available = static_cast<std::uintmax_t>(
        static_cast<std::streamoff>(end));
    const auto count = static_cast<std::size_t>(
        std::min<std::uintmax_t>(available, maximum_bytes));
    input.seekg(end - static_cast<std::streamoff>(count));
    std::string text(count, '\0');
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (!input) return {};
    std::size_t begin = 0;
    if (available > count) {
        while (begin < text.size() &&
               (static_cast<unsigned char>(text[begin]) & 0xc0U) == 0x80U) {
            ++begin;
        }
    }
    std::size_t lines{};
    for (std::size_t index = text.size(); index > begin; --index) {
        if (text[index - 1U] == '\n' && ++lines > maximum_lines) {
            begin = index;
            break;
        }
    }
    return text.substr(begin);
}

}  // namespace ogplay::frontend
