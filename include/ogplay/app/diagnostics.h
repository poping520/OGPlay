#pragma once
#include "ogplay/app/export.h"
#include <cstdint>
#include <filesystem>
#include <string>
namespace ogplay::app {
OGPLAYCORE_API std::string RequestDiagnosticSnapshot(std::uint64_t pid, const std::filesystem::path& directory);
}
