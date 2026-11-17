#pragma once

#include <filesystem>

namespace ogplay::app {

// True when the ZIP has a root AndroidManifest.xml, i.e. it is an APK rather than a game archive.
[[nodiscard]] bool ZipHasRootManifest(const std::filesystem::path& path);

}  // namespace ogplay::app
