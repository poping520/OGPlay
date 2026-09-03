#include "ogplay/frontend/data_directory.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include "ogplay/hal/host_environment.h"

namespace ogplay::frontend {
namespace {

[[nodiscard]] std::string PathUtf8(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

void AppendCandidate(std::vector<std::filesystem::path>& candidates,
                     std::filesystem::path candidate) {
    candidate = std::filesystem::absolute(candidate).lexically_normal();
    if (std::find(candidates.begin(), candidates.end(), candidate) ==
        candidates.end()) {
        candidates.push_back(std::move(candidate));
    }
}

[[nodiscard]] bool CompletePayload(const std::filesystem::path& root) {
    std::error_code error;
    if (!std::filesystem::is_directory(root / "profiles", error) || error) {
        return false;
    }
    error.clear();
    if (!std::filesystem::is_regular_file(root / "quirks.toml", error) ||
        error) {
        return false;
    }
    for (const auto* name : {"libc.so", "libdl.so", "libm.so",
                             "libstdc++.so", "libz.so"}) {
        error.clear();
        if (!std::filesystem::is_regular_file(
                root / "android" / "19" / "lib" / name, error) ||
            error) {
            return false;
        }
    }
    return true;
}

}  // namespace

BundledDataPaths ResolveBundledDataPaths(
    const std::filesystem::path& executable_directory,
    std::optional<std::filesystem::path> development_source_root) {
    if (executable_directory.empty()) {
        throw std::invalid_argument("executable directory must not be empty");
    }
    const auto executable =
        std::filesystem::absolute(executable_directory).lexically_normal();
    std::vector<std::filesystem::path> candidates;
    AppendCandidate(candidates, executable / "data");
    AppendCandidate(candidates,
                    executable.parent_path() / "Resources" / "data");
    if (development_source_root.has_value()) {
        AppendCandidate(candidates, *development_source_root / "data");
    }
    for (const auto& root : candidates) {
        if (CompletePayload(root)) {
            return {.root = root,
                    .profiles_directory = root / "profiles",
                    .quirk_registry = root / "quirks.toml"};
        }
    }
    throw std::runtime_error(
        "bundled runtime data is unavailable beside executable: " +
        PathUtf8(executable));
}

BundledDataPaths HostBundledDataPaths() {
    return ResolveBundledDataPaths(
        hal::HostExecutableDirectory(),
        std::filesystem::path(OGPLAY_SOURCE_DIR));
}

}  // namespace ogplay::frontend
