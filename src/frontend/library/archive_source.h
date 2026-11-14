#pragma once
#include "ogplay/app/archive_import.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace ogplay::app {

[[nodiscard]] std::unique_ptr<ImportSource> OpenZipArchive(const std::filesystem::path& path);
[[nodiscard]] std::unique_ptr<ImportSource> OpenRarArchive(const std::filesystem::path& path);
// True when the ZIP has a root AndroidManifest.xml, i.e. it is an APK rather than a game archive.
[[nodiscard]] bool ZipHasRootManifest(const std::filesystem::path& path);

}  // namespace ogplay::app
