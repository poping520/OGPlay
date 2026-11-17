#pragma once
#include "ogplay/app/export.h"
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace ogplay::app {
struct DataPaths final {
    std::filesystem::path root, profiles_directory;
};

OGPLAYCORE_API DataPaths BundledDataPaths();
OGPLAYCORE_API std::filesystem::path ExecutablePath();
OGPLAYCORE_API std::filesystem::path DefaultLibraryRoot();

struct LibraryRootLocation final {
    std::filesystem::path root;
    bool portable{};
    std::optional<std::filesystem::path> legacy_root;
};

// Includes the existing standard root for launcher diagnostics in portable mode.
OGPLAYCORE_API LibraryRootLocation DefaultLibraryLocation();
// The host OS as CMAKE_SYSTEM_NAME spells it: Windows, Darwin, Linux or Android.
OGPLAYCORE_API std::string HostPlatformName();
OGPLAYCORE_API std::string UtcTimestamp();
OGPLAYCORE_API std::string NewControlToken();

class OGPLAYCORE_API MonotonicClock final {
public:
    static std::uint64_t NowNs();
};
}  // namespace ogplay::app
