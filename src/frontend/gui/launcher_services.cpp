#include "frontend/gui/launcher_services.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <map>
#include <utility>

#include "ogplay/app/demo_games.h"
#include "ogplay/app/host.h"
#include "ogplay/app/library.h"
#include "ogplay/app/logging.h"
#include "ogplay/app/profiles.h"

namespace ogplay::frontend {
using namespace ogplay::app;

namespace {

std::string PathUtf8(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

ProfileCatalog ConfiguredProfiles(const LibraryConfig& config) {
    return config.profiles_dir ? ProfileCatalog::Load(*config.profiles_dir) : ProfileCatalog::LoadBundled();
}

LibraryViewContext BuildContext(const LibraryStore& store, std::vector<std::string> running,
                                const std::vector<LibraryEntry>& entries) {
    LibraryViewContext context;
    context.running_packages = std::move(running);
    for (const auto& entry : entries)
        if (store.IsRunning(entry.key) && std::find(context.running_packages.begin(), context.running_packages.end(),
                                                    entry.key) == context.running_packages.end())
            context.running_packages.push_back(entry.key);
    try {
        const auto config = LoadLibraryConfig(store.Root());
        std::map<std::optional<std::filesystem::path>, ProfileCatalog> catalogs;
        for (const auto& entry : entries) {
            if (!entry.metadata || entry.Damaged()) continue;
            try {
                const auto effective = EffectiveGameConfig(LoadGameSettings(entry.directory), config);
                const auto& directory = effective.profiles_dir;
                if (!catalogs.contains(directory)) catalogs.emplace(directory, ConfiguredProfiles(effective));
                const auto& profiles = catalogs.at(directory);
                if (!entry.metadata->profile_id) continue;
                const auto summary = profiles.Find(*entry.metadata->profile_id);
                if (!summary) context.missing_profiles.insert(entry.key);
            } catch (const LibraryError& error) {
                if (error.Code() == LibraryErrorCode::busy) throw;
                context.profile_errors[entry.key] = error.what();
            } catch (const std::exception& error) {
                context.profile_errors[entry.key] = error.what();
            }
        }
    } catch (const LibraryError& error) {
        if (error.Code() == LibraryErrorCode::busy) throw;
        context.profile_catalog_error = error.what();
    } catch (const std::exception& error) {
        context.profile_catalog_error = error.what();
    }
    return context;
}

}  // namespace

LauncherServices::LauncherServices(LibraryStore& store, app::Logger& logger) : store_(store), logger_(logger) {
    // Bundled demos are read once; a broken one is logged and leaves the launcher without demos.
    try {
        demos_ = FindBundledDemos(BundledDataPaths().root / "demos", ProfileCatalog::LoadBundled());
        SeedDemos();
    } catch (const std::exception& error) {
        logger_.Write(LogLevel::warn, "frontend.gui.demo", "demo games unavailable", {},
                      {{"reason", std::string(error.what())}});
    }
}

void LauncherServices::SeedDemos() {
    for (const auto& id : SeedBundledDemos(store_, demos_, UtcTimestamp()))
        logger_.Write(LogLevel::info, "frontend.gui.demo", "demo game added to the library", {},
                      {{"installation_id", id}});
}

UiLanguage LauncherServices::Language() const {
    try {
        return ResolveUiLanguage(std::get<std::string>(Setting(LoadLibraryConfig(store_.Root()), "language")));
    } catch (const std::exception& error) {
        logger_.Write(LogLevel::warn, "frontend.gui", "UI language configuration unavailable", {},
                      {{"reason", std::string(error.what())}});
        return SystemUiLanguage();
    }
}

GuiRpcHost LauncherServices::Host(std::function<std::vector<std::string>()> running) {
    GuiRpcHost host;
    host.context = [this, running = std::move(running)](const std::vector<LibraryEntry>& entries) {
        return BuildContext(store_, running ? running() : std::vector<std::string>{}, entries);
    };
    host.analyze = [this](const std::filesystem::path& path) {
        return AnalyzeApkImportFile(path, ConfiguredProfiles(LoadLibraryConfig(store_.Root())));
    };
    host.timestamp = [] { return UtcTimestamp(); };
    host.settings_facts = [] {
        std::map<std::string, std::optional<std::string>> facts;
        facts["ogplay_version"] = OGPLAY_VERSION;
        const auto root = BundledDataPaths().root;
        // Web UI licenses are part of the open-source acknowledgements (about.credits).
        for (const auto& [key, path] :
             std::array<std::pair<const char*, const char*>, 1>{{{"android_manifest", "android/19/manifest.json"}}}) {
            std::error_code error;
            const auto size = std::filesystem::file_size(root / path, error);
            facts[key] = std::nullopt;  // missing, unreadable or over the read limit
            if (error || size > 65536) continue;
            std::ifstream file(root / path, std::ios::binary);
            std::string text(static_cast<std::size_t>(size), '\0');
            file.read(text.data(), static_cast<std::streamsize>(size));
            if (file) facts[key] = std::move(text);
        }
        return facts;
    };
    host.game_facts = [](const LibraryEntry& entry, const LibraryConfig& config) {
        std::map<std::string, std::string> facts;
        facts["profiles_dir"] = PathUtf8(config.profiles_dir.value_or(BundledDataPaths().profiles_directory));
        const auto profiles = ConfiguredProfiles(config);
        if (entry.metadata && entry.metadata->profile_id)
            facts["profile_status"] = profiles.Find(*entry.metadata->profile_id) ? "recorded" : "missing";
        return facts;
    };
    host.credits = [] { return LoadCredits(BundledDataPaths().root); };
    host.credit_notices = [](std::string_view id) { return ReadCreditNotices(BundledDataPaths().root, id); };
    host.demo_packages = [this] {
        std::vector<std::string> packages;
        for (const auto& demo : demos_)
            packages.push_back(demo.manifest.package);
        return packages;
    };
    host.seed_demos = [this] { SeedDemos(); };
    return host;
}

}  // namespace ogplay::frontend
