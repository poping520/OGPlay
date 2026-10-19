#include <doctest/doctest.h>
#include "apk_fixture.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <thread>
#include "frontend/gui/rpc.h"
#include "ogplay/app/encoding.h"
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/app/apk_inspect.h"

namespace {

using ogplay::test::MinimalApk;

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        static std::atomic<std::uint64_t> sequence{};
        path = std::filesystem::temp_directory_path() /
               ("ogplay-gui-import-" + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count()) +
                "-" + std::to_string(sequence.fetch_add(1)));
        std::filesystem::create_directories(path);
    }
    ~TemporaryDirectory() {
        std::error_code error;
        static_cast<void>(std::filesystem::remove_all(path, error));
    }
    std::filesystem::path path;
};

ogplay::app::ApkImportAnalysis Analysis() {
    ogplay::app::ApkImportAnalysis analysis;
    analysis.source_apk = std::filesystem::absolute("fixture.apk");
    analysis.manifest.package = "org.example.game";
    analysis.manifest.version_code = 42;
    analysis.manifest.version_name = "1.2.3";
    analysis.display_name = "示例游戏";
    analysis.icon_png = {std::byte{1}, std::byte{2}};
    return analysis;
}

}  // namespace

TEST_CASE("GUI import modal stays alive while a host dialog is pending") {
    CHECK_FALSE(ogplay::frontend::CanDismissImportModal(true));
    CHECK(ogplay::frontend::CanDismissImportModal(false));
}

TEST_CASE("GUI import builds exact metadata without inventing a Profile") {
    const auto request = ogplay::app::BuildLibraryImport(
        Analysis(), std::nullopt, "2026-08-13T00:00:00Z");
    CHECK(request.metadata.package == "org.example.game");
    CHECK(request.metadata.display_name == "示例游戏");
    CHECK(request.metadata.version_code == 42);
    CHECK(request.metadata.version_name == "1.2.3");
    CHECK_FALSE(request.metadata.profile_id.has_value());
    CHECK_FALSE(request.metadata.external_dir.has_value());
    CHECK(request.icon_png == std::vector<std::byte>{std::byte{1}, std::byte{2}});
}

TEST_CASE("GUI import analyzes a valid unmatched APK without native libraries") {
    const ogplay::app::ProfileCatalog catalog;
    const auto analysis = ogplay::app::AnalyzeApkImport(
        MinimalApk(), "selected.apk", catalog);
    CHECK(analysis.manifest.package == "org.example.game");
    CHECK(analysis.manifest.version_code == 9);
    CHECK(analysis.display_name == "org.example.game");
    CHECK_FALSE(analysis.profile.has_value());
    CHECK(analysis.icon_png.empty());
    CHECK(analysis.visual_fallbacks.size() == 2);
}

TEST_CASE("GUI import accepts optional external data only as a real directory") {
    TemporaryDirectory temporary;
    const auto external = temporary.path / "external";
    std::filesystem::create_directories(external);
    auto analysis = Analysis();
    analysis.profile = ogplay::app::ProfileSummary{
        "org.example.game"};

    const auto skipped = ogplay::app::BuildLibraryImport(
        analysis, std::nullopt, "2026-08-13T00:00:00Z");
    CHECK(skipped.metadata.profile_id == "org.example.game");
    CHECK_FALSE(skipped.metadata.external_dir.has_value());
    CHECK(skipped.metadata.external_guest_dir == "/sdcard");

    const auto selected = ogplay::app::BuildLibraryImport(
        analysis, external, "2026-08-13T00:00:00Z", "/sdcard/Games/Example");
    CHECK(selected.metadata.external_guest_dir == "/sdcard/Games/Example");
    CHECK_THROWS(static_cast<void>(ogplay::app::BuildLibraryImport(analysis, external, "now", "/data")));
    CHECK(selected.metadata.external_dir ==
          std::filesystem::absolute(external).lexically_normal());
    CHECK_THROWS_AS(static_cast<void>(ogplay::app::BuildLibraryImport(
                        analysis, temporary.path / "missing",
                        "2026-08-13T00:00:00Z")),
                    ogplay::app::LibraryError);
}

TEST_CASE("GUI import rejects missing APK and empty timestamp explicitly") {
    const ogplay::app::ProfileCatalog catalog;
    CHECK_THROWS_AS(static_cast<void>(ogplay::app::AnalyzeApkImportFile(
                        "missing.apk", catalog)),
                    ogplay::app::LibraryError);
    CHECK_THROWS_AS(static_cast<void>(ogplay::app::BuildLibraryImport(
                        Analysis(), std::nullopt, "")),
                    ogplay::app::LibraryError);
}

TEST_CASE("GUI RPC imports a real minimal APK from the analyzed snapshot") {
    using namespace ogplay;
    using namespace ogplay::app;
    TemporaryDirectory temporary;
    ogplay::app::LibraryStore store(temporary.path);
    frontend::GuiRpcService rpc(store, temporary.path / "ogplay.exe", {
        [](const auto&) { return frontend::LibraryViewContext{}; }, [](const auto&) {}, [](const auto&) {},
        [](const auto& source) { return ogplay::app::AnalyzeApkImportFile(source, app::ProfileCatalog{}); }, {},
        [] { return std::string("2026-09-22T00:00:00Z"); }});
    const auto call = [&](std::string_view method, std::string_view params) {
        app::JsonParseError error;
        auto document = app::JsonDocument::ParseStrict(rpc.Handle("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"" + std::string(method) + "\",\"params\":" + std::string(params) + "}"), error);
        REQUIRE(document);
        REQUIRE_FALSE(document->Root().Member("error"));
        return std::move(*document);
    };
    const auto poll = [&] {
        for (unsigned i = 0; i < 100000; ++i) {
            auto result = call("library.job.poll", R"({"job":"1"})");
            const auto state = result.Root().Member("result")->Member("state")->String();
            if (state != "analyzing" && state != "importing") return result;
            std::this_thread::yield();
        }
        throw std::runtime_error("job did not settle");
    };
    const auto bytes = MinimalApk();
    call("library.upload.begin", "{\"name\":\"fixture.apk\",\"size\":" + std::to_string(bytes.size()) + "}");
    call("library.upload.chunk", "{\"job\":\"1\",\"offset\":0,\"data\":\"" + app::EncodeBase64(bytes) + "\"}");
    call("library.upload.finish", R"({"job":"1"})");
    const auto ready = poll();
    CHECK(ready.Root().Member("result")->Member("state")->String() == "ready");
    CHECK(ready.Root().Member("result")->Member("summary")->Member("version_code")->UnsignedInteger() == 9);
    call("library.import", R"({"job":"1","new_instance":true,"external_guest_dir":"/sdcard/Games/Example"})");
    CHECK(poll().Root().Member("result")->Member("state")->String() == "imported");
    const auto entries = store.LoadEntries();
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].metadata->package == "org.example.game");
    CHECK(entries[0].metadata->version_code == 9);
    CHECK(entries[0].metadata->external_guest_dir == "/sdcard/Games/Example");
    const auto analyzed = ogplay::app::AnalyzeApkImportFile(entries[0].directory / "game.apk", app::ProfileCatalog{});
    CHECK(analyzed.manifest.version_code == 9);
    CHECK_FALSE(std::filesystem::exists(temporary.path / ".gui-import-1"));
    call("library.upload.begin", R"({"name":"broken.apk","size":1})");
    call("library.upload.chunk", R"({"job":"2","offset":0,"data":"YQ=="})");
    call("library.upload.finish", R"({"job":"2"})");
    bool failed = false;
    for (unsigned i = 0; i < 100000; ++i) {
        const auto status = call("library.job.poll", R"({"job":"2"})");
        if (status.Root().Member("result")->Member("state")->String() == "failed") {
            CHECK(status.Root().Member("result")->Member("message")); failed = true; break;
        }
        std::this_thread::yield();
    }
    CHECK(failed);
    CHECK(store.LoadEntries().size() == 1);
    CHECK_FALSE(std::filesystem::exists(temporary.path / ".gui-import-2"));
}
