#pragma once
#include "ogplay/app/export.h"
#include <cstdint>
#include <exception>
#include <filesystem>
#include <string>
namespace ogplay::app {
// Preserve known non-standard runtime failures without exposing kernel types.
OGPLAYCORE_API std::string DescribeUnhandledException(std::exception_ptr failure);
OGPLAYCORE_API std::string DumpDexSurface(const std::filesystem::path& boot_archive_path);
OGPLAYCORE_API std::string RequestDiagnosticSnapshot(std::uint64_t pid, const std::filesystem::path& directory);
}
