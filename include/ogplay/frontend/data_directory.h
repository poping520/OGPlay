#pragma once

#include <filesystem>
#include <optional>

namespace ogplay::frontend {

struct BundledDataPaths final {
    std::filesystem::path root;
    std::filesystem::path profiles_directory;
    std::filesystem::path quirk_registry;
};

// Resolves a complete launcher/runtime payload, including the bundled API 19
// Android libraries, beside the executable, then in a macOS bundle Resources
// directory, and finally in the development source tree when supplied.
// Incomplete candidates are never published.
[[nodiscard]] BundledDataPaths ResolveBundledDataPaths(
    const std::filesystem::path& executable_directory,
    std::optional<std::filesystem::path> development_source_root = std::nullopt);

[[nodiscard]] BundledDataPaths HostBundledDataPaths();

}  // namespace ogplay::frontend
