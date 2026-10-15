#include <doctest/doctest.h>
#include "ogplay/app/library.h"
#include "ogplay/hal/file_lock.h"
#include "frontend/gui/rpc.h"
#include <thread>
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
        const auto deadline = MonotonicClock::NowNs() + 2000000000ULL;
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
