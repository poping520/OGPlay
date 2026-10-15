#include <doctest/doctest.h>
#include "ogplay/app/library.h"
#include "ogplay/hal/file_lock.h"
#include <fstream>
#include <future>

TEST_CASE("Library lock rejects mutations and List preserves active importing directories") {
    using namespace ogplay::app;
    const auto root = std::filesystem::temp_directory_path() / ("ogplay-lock-contention-" + std::to_string(MonotonicClock::NowNs()));
    struct Cleanup { std::filesystem::path root; ~Cleanup() { std::filesystem::remove_all(root); } } cleanup{root};
    LibraryStore store(root);
    std::filesystem::create_directories(root);
    std::ofstream(root / "source.apk") << "apk";
    LibraryImport request{root / "source.apk", {"org.example.lock", "Lock", 1, "1", "2026-10-15", {}, {}}, {}};
    const auto id = store.Import(request, false);
    const auto temporary = store.EntriesRoot() / ".active.importing";
    {
        auto lock = ogplay::hal::FileLock::TryAcquire(store.EntriesRoot() / ".lock");
        REQUIRE(lock);
        std::filesystem::create_directory(temporary);
        auto competitor = std::async(std::launch::async, [&] {
            CHECK(store.LoadEntries().size() == 1);
            CHECK(std::filesystem::exists(temporary));
            for (const auto remove : {false, true}) {
                bool rejected = false;
                try { if (remove) store.Remove(id); else store.Import(request, true); }
                catch (const LibraryError& error) {
                    CHECK(error.Code() == LibraryErrorCode::busy);
                    CHECK(std::string(error.what()).find("retry") != std::string::npos);
                    rejected = true;
                }
                CHECK(rejected);
            }
        });
        competitor.get();
        CHECK(std::filesystem::exists(temporary));
    }
    CHECK(store.LoadEntries().size() == 1);
    CHECK_FALSE(std::filesystem::exists(temporary));
}
