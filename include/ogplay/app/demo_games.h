#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/app/apk_inspect.h"

namespace ogplay::app {

// OGPlay's own demo games, shipped as APKs in the bundled data's demos/ directory. A library entry
// whose package matches one is that demo, whether it was seeded or imported by hand.
// Reads every *.apk there in name order; a missing directory has none, an unreadable APK throws.
[[nodiscard]] OGPLAYCORE_API std::vector<ApkImportAnalysis> FindBundledDemos(const std::filesystem::path& directory,
                                                                             const ProfileCatalog& profiles);
[[nodiscard]] OGPLAYCORE_API bool IsBundledDemo(std::span<const ApkImportAnalysis> demos, std::string_view package);
// With show_demo_games on, imports each demo whose package has no library entry yet (adopting saves
// a removed copy left); returns the new installation ids. With it off, the library is left alone.
OGPLAYCORE_API std::vector<std::string> SeedBundledDemos(LibraryStore& store, std::span<const ApkImportAnalysis> demos,
                                                         const std::string& imported_at);

}  // namespace ogplay::app
