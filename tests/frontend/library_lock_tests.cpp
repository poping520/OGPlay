#include <doctest/doctest.h>
#include "ogplay/app/library.h"
#include "ogplay/app/host.h"
#include <fstream>
#include <future>
using namespace ogplay::app;
namespace {
struct Fixture {
    std::filesystem::path root = std::filesystem::temp_directory_path() / ("ogplay-library-lock-" + std::to_string(MonotonicClock::NowNs()));
    LibraryStore store{root};
    Fixture() {
        std::filesystem::create_directories(root);
        std::ofstream(root / "source.apk") << "apk";
        store.Import({root / "source.apk", {"org.example.lock", "Lock", 1, "1", "2026-10-15", {}, {}}, {}}, false);
    }
    ~Fixture() { std::filesystem::remove_all(root); }
};
}
TEST_CASE("Library running lock prevents concurrent launch and remove and releases") {
    Fixture f;
    {
        RunningLease active(f.root, "org.example.lock");
        CHECK(f.store.IsRunning("org.example.lock"));
        auto rejected = std::async(std::launch::async, [&] {
            try { f.store.Remove("org.example.lock"); }
            catch (const LibraryError& e) { return e.Code() == LibraryErrorCode::running; }
            return false;
        });
        CHECK(rejected.get());
        CHECK_THROWS_AS(RunningLease(f.root, "org.example.lock"), LibraryError);
    }
    CHECK_FALSE(f.store.IsRunning("org.example.lock"));
    f.store.Remove("org.example.lock");
    CHECK(f.store.LoadEntries().empty());
    CHECK(std::filesystem::is_directory(f.root / "sandbox/org.example.lock"));
}
TEST_CASE("Library import duplicate policy is checked atomically") {
    Fixture f;
    LibraryImport request{f.root / "source.apk", {"org.example.lock", "Lock", 1, "1", "2026-10-15", {}, {}}, {}};
    CHECK_THROWS_AS(f.store.Import(request, false), LibraryError);
    CHECK(f.store.Import(request, true) == "org.example.lock-2");
}
TEST_CASE("Library launch resolution shares settings inheritance and parameter codec") {
    Fixture f;
    GameSettings settings;
    settings.values["cpu_cores"] = std::uint32_t(4);
    settings.values["ephemeral_sandbox"] = true;
    settings.values["device_preset"] = std::string("reserved-value");
    SaveGameSettings(f.store.EntriesRoot() / "org.example.lock", settings);
    const auto resolved = ResolveLaunch(f.store, "org.example.lock", LaunchMode::preflight);
    CHECK(resolved.options.cpu_config.cores == 4);
    CHECK(resolved.options.ephemeral_sandbox);
    CHECK_FALSE(resolved.options.sandbox_directory);
    CHECK(resolved.options.library_root == f.root);
    CHECK(resolved.options.preflight);
    CHECK_FALSE(resolved.mcp_port);
    const auto encoded = FormatGameRunArgs(resolved.options);
    const std::vector<std::string_view> views(encoded.begin(), encoded.end());
    CHECK(ParseGameRunArgs(views) == resolved.options);
    settings.values["unknown"] = true;
    CHECK_THROWS_AS(SaveGameSettings(f.store.EntriesRoot() / "org.example.lock", settings), std::invalid_argument);
}
