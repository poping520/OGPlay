#pragma once
#include "ogplay/app/export.h"
#include <cstdint>
#include <filesystem>
#include <string>
namespace ogplay::app {
struct DataPaths final {
    std::filesystem::path root, profiles_directory, quirk_registry, input_templates_directory;
};
OGPLAYCORE_API DataPaths BundledDataPaths();
OGPLAYCORE_API std::filesystem::path ExecutablePath();
OGPLAYCORE_API std::filesystem::path DefaultLibraryRoot();
OGPLAYCORE_API std::string UtcTimestamp();
OGPLAYCORE_API std::string NewControlToken();
class OGPLAYCORE_API MonotonicClock final {
public:
    static std::uint64_t NowNs();
};
}
