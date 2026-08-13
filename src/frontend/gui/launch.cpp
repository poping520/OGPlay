#include "ogplay/frontend/gui_launch.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace ogplay::frontend {
namespace {

[[nodiscard]] std::string PathUtf8(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

void RequireDirectory(const std::filesystem::path& path,
                      const std::string_view name) {
    std::error_code error;
    if (!std::filesystem::is_directory(path, error) || error) {
        throw GuiModelError(GuiModelErrorCode::not_found,
                            std::string(name) + " is unavailable", path);
    }
}

}  // namespace

std::filesystem::path LauncherSandboxRoot(
    const std::filesystem::path& library_root) {
    if (library_root.empty()) {
        throw GuiModelError(GuiModelErrorCode::invalid_argument,
                            "library root must not be empty");
    }
    return (std::filesystem::absolute(library_root).lexically_normal() /
            "sandbox").lexically_normal();
}

LaunchPlan BuildLaunchPlan(const std::filesystem::path& cli_executable,
                           const std::filesystem::path& library_root,
                           const LibraryEntry& entry,
                           const GuiConfig& config) {
    if (entry.Damaged() || !entry.metadata.has_value()) {
        throw GuiModelError(GuiModelErrorCode::corrupt_config,
                            "damaged library entry cannot be launched",
                            entry.directory);
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(cli_executable, error) || error) {
        throw GuiModelError(GuiModelErrorCode::not_found,
                            "OGPlay CLI executable is missing", cli_executable);
    }
    if (!config.system_dir.has_value()) {
        throw GuiModelError(GuiModelErrorCode::not_found,
                            "Android system library directory is not configured");
    }
    RequireDirectory(*config.system_dir, "Android system library directory");
    if (config.profiles_dir.has_value()) {
        RequireDirectory(*config.profiles_dir, "Profile directory");
    }
    const auto apk = std::filesystem::absolute(entry.directory / "game.apk")
                         .lexically_normal();
    if (!std::filesystem::is_regular_file(apk, error) || error) {
        throw GuiModelError(GuiModelErrorCode::not_found,
                            "library APK copy is missing", apk);
    }
    if (entry.metadata->external_dir.has_value()) {
        RequireDirectory(*entry.metadata->external_dir,
                         "external data directory");
    }

    LaunchPlan plan;
    plan.package = entry.key;
    plan.argv = {PathUtf8(std::filesystem::absolute(cli_executable).lexically_normal()),
                 "run-apk", PathUtf8(apk), "--system-dir",
                 PathUtf8(*config.system_dir)};
    if (config.profiles_dir.has_value()) {
        plan.argv.push_back("--profiles-dir");
        plan.argv.push_back(PathUtf8(*config.profiles_dir));
    }
    if (entry.metadata->external_dir.has_value()) {
        plan.argv.push_back("--external-dir");
        plan.argv.push_back(PathUtf8(*entry.metadata->external_dir));
    }
    plan.argv.push_back("--sandbox-dir");
    plan.argv.push_back(PathUtf8(LauncherSandboxRoot(library_root)));
    plan.log_path = std::filesystem::absolute(entry.directory / "last-run.log")
                        .lexically_normal();
    return plan;
}

void LaunchTracker::Begin(std::string package,
                          std::filesystem::path log_path) {
    if (package.empty() || IsRunning(package)) {
        throw GuiModelError(GuiModelErrorCode::duplicate_package,
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
        throw GuiModelError(GuiModelErrorCode::not_found,
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
