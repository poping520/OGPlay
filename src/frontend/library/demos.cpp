#include "ogplay/app/demo_games.h"

#include <algorithm>
#include <set>

namespace ogplay::app {

std::vector<ApkImportAnalysis> FindBundledDemos(const std::filesystem::path& directory,
                                                const ProfileCatalog& profiles) {
    std::vector<std::filesystem::path> apks;
    if (!std::filesystem::is_directory(directory)) return {};
    for (const auto& item : std::filesystem::directory_iterator(directory))
        if (item.is_regular_file() && item.path().extension() == ".apk") apks.push_back(item.path());
    std::sort(apks.begin(), apks.end());
    std::vector<ApkImportAnalysis> demos;
    for (const auto& apk : apks) demos.push_back(AnalyzeApkImportFile(apk, profiles));
    return demos;
}

bool IsBundledDemo(const std::span<const ApkImportAnalysis> demos, const std::string_view package) {
    return std::any_of(demos.begin(), demos.end(), [&](const auto& demo) { return demo.manifest.package == package; });
}

std::vector<std::string> SeedBundledDemos(LibraryStore& store, const std::span<const ApkImportAnalysis> demos,
                                          const std::string& imported_at) {
    if (demos.empty() || !std::get<bool>(Setting(LoadLibraryConfig(store.Root()), "show_demo_games"))) return {};
    std::set<std::string, std::less<>> present;
    for (const auto& entry : store.LoadEntries())
        if (entry.metadata) present.insert(entry.metadata->package);
    std::vector<std::string> imported;
    for (const auto& demo : demos)
        if (!present.contains(demo.manifest.package))
            imported.push_back(store.Import(BuildLibraryImport(demo, std::nullopt, imported_at)));
    return imported;
}

}  // namespace ogplay::app
