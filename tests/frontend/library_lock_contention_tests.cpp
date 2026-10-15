#include <doctest/doctest.h>
#include "ogplay/app/library.h"
#include "ogplay/hal/file_lock.h"
#include "frontend/gui/rpc.h"
#include <thread>
#include <fstream>
#include <future>
#include <chrono>
#include <optional>

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
        // An asynchronous GUI commit must preserve the typed busy error too.
        ogplay::frontend::GuiRpcService rpc(store, root / "ogplay", {
            [](const auto&) { return ogplay::frontend::LibraryViewContext{}; },
            [](const auto&) {}, [](const auto&) {},
            [](const auto& source) {
                ApkImportAnalysis analysis;
                analysis.source_apk = source; analysis.manifest.package = "org.example.other";
                analysis.display_name = "Other"; return analysis;
            }, {}, [] { return "2026-10-15"; }, {}, {}, {}, {}, {}, {}});
        JsonWriter writer;
        const auto params = writer.Object(), envelope = writer.Object();
        writer.AddString(params, "path", (root / "source.apk").generic_string());
        writer.AddString(envelope, "jsonrpc", "2.0"); writer.AddInteger(envelope, "id", 1);
        writer.AddString(envelope, "method", "library.analyze"); writer.Add(envelope, "params", params);
        JsonParseError parse_error;
        auto started = JsonDocument::ParseStrict(rpc.Handle(writer.Serialize(envelope)), parse_error);
        REQUIRE(started); REQUIRE(started->Root().Member("result"));
        const std::string job(*started->Root().Member("result")->Member("job")->String());
        const auto call = [&](std::string_view method, bool confirm) {
            return rpc.Handle("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"" + std::string(method) +
                "\",\"params\":{\"job\":\"" + job + "\"" + (confirm ? ",\"new_instance\":true" : "") + "}}");
        };
        bool ready = false;
        const auto deadline = MonotonicClock::NowNs() + 10000000000ULL;
        while (MonotonicClock::NowNs() < deadline) {
            auto response = JsonDocument::ParseStrict(call("library.job.poll", false), parse_error);
            if (response && response->Root().Member("result")->Member("state")->String() == "ready") { ready = true; break; }
            std::this_thread::yield();
        }
        REQUIRE(ready);
        static_cast<void>(call("library.import", true));
        bool busy = false;
        while (MonotonicClock::NowNs() < deadline) {
            auto response = JsonDocument::ParseStrict(call("library.job.poll", false), parse_error);
            if (response && response->Root().Member("error")) {
                busy = response->Root().Member("error")->Member("code")->Integer() == -32002;
                break;
            }
            std::this_thread::yield();
        }
        CHECK(busy);
    }
    CHECK(store.LoadEntries().size() == 1);
    CHECK_FALSE(std::filesystem::exists(temporary));
}

TEST_CASE("Library lock waits out brief holders, shares reads, and reports long holds as busy") {
    using namespace ogplay::app;
    using ogplay::hal::FileLock;
    using ogplay::hal::FileLockMode;
    const auto root = std::filesystem::temp_directory_path() / ("ogplay-lock-wait-" + std::to_string(MonotonicClock::NowNs()));
    struct Cleanup { std::filesystem::path root; ~Cleanup() { std::filesystem::remove_all(root); } } cleanup{root};
    LibraryStore store(root);
    std::filesystem::create_directories(root);
    std::ofstream(root / "source.apk") << "apk";
    const auto id = store.Import({root / "source.apk", {"org.example.wait", "Wait", 1, "1", "2026-10-15", {}, {}}, {}}, false);
    const auto directory = store.EntriesRoot() / id;
    SaveGameSettings(directory, {{{"cpu_cores", std::uint32_t{2}}}});
    const auto lock_path = store.EntriesRoot() / ".lock";

    {
        // Settings, config and running probes only need a shared lock; writers still wait.
        auto reader = FileLock::TryAcquire(lock_path, FileLockMode::shared);
        REQUIRE(reader);
        CHECK(LoadGameSettings(directory).values.size() == 1);
        CHECK_NOTHROW(static_cast<void>(LoadLibraryConfig(root)));
        CHECK_FALSE(store.IsRunning(id));
        CHECK(FileLock::TryAcquire(lock_path, FileLockMode::shared));
        CHECK_FALSE(FileLock::TryAcquire(lock_path));
    }
    {
        // A list refresh holding the lock briefly must not fail a starting game.
        std::optional<FileLock> holder = FileLock::TryAcquire(lock_path);
        REQUIRE(holder);
        auto release = std::async(std::launch::async, [&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            holder.reset();
        });
        RunningLease lease(root, id);
        release.get();
        CHECK(store.IsRunning(id));
    }
    {
        auto holder = FileLock::TryAcquire(lock_path);
        REQUIRE(holder);
        auto read = std::async(std::launch::async, [&] {
            try { static_cast<void>(LoadGameSettings(directory)); }
            catch (const LibraryError& error) { return error.Code() == LibraryErrorCode::busy; }
            return false;
        });
        CHECK(read.get());
        // GUI list reports retryable busy instead of marking the entry damaged.
        ogplay::frontend::GuiRpcService rpc(store, root / "ogplay", {
            [](const auto&) { return ogplay::frontend::LibraryViewContext{}; },
            [](const auto&) {}, [](const auto&) {}, {}, {}, [] { return "2026-10-15"; },
            {}, {}, {}, {}, {}, {}});
        JsonParseError parse_error;
        const auto response = JsonDocument::ParseStrict(
            rpc.Handle(R"({"jsonrpc":"2.0","id":1,"method":"library.list"})"), parse_error);
        REQUIRE(response);
        REQUIRE(response->Root().Member("error"));
        CHECK(response->Root().Member("error")->Member("code")->Integer() == -32002);
    }
}
