#include <doctest/doctest.h>
#include "ogplay/app/dex_run.h"
#include <array>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
std::filesystem::path FixtureDex() {
    return std::filesystem::path(OGPLAY_DEXVM_FIXTURE_DIR) / "run_dex.dex";
}

ogplay::app::DexRunOptions Options(const std::string& main_class) {
    ogplay::app::DexRunOptions options;
    options.input = FixtureDex();
    options.main_class = main_class;
    return options;
}
}  // namespace

TEST_CASE("run-dex parses dalvikvm-style options and main arguments") {
    using ogplay::app::ParseDexRunArgs;
    const std::array args{std::string_view{"t.jar"}, std::string_view{"--class"},
                          std::string_view{"a.b.Main"}, std::string_view{"--backend"},
                          std::string_view{"threaded"}, std::string_view{"-ea"},
                          std::string_view{"--ledger-json"}, std::string_view{"l.json"},
                          std::string_view{"--"}, std::string_view{"--x"}, std::string_view{"7"}};
    const auto options = ParseDexRunArgs(args);
    CHECK(options.input == "t.jar");
    CHECK(options.main_class == "a.b.Main");
    CHECK(options.backend == "threaded");
    CHECK(options.enable_assertions);
    CHECK(options.ledger_json == "l.json");
    CHECK(options.arguments == std::vector<std::string>{"--x", "7"});
    const std::array bad_backend{std::string_view{"t.dex"}, std::string_view{"--backend"},
                                 std::string_view{"jit"}};
    CHECK_THROWS_AS(static_cast<void>(ParseDexRunArgs(bad_backend)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(ParseDexRunArgs(std::span<const std::string_view>{})),
                    std::invalid_argument);
}

TEST_CASE("run-dex forwards System.out and System.err bytes exactly") {
    for (const auto* backend : {"switch", "threaded"}) {
        CAPTURE(backend);
        auto options = Options("rundex.Main");
        options.backend = backend;
        std::string console;
        const auto status = ogplay::app::RunDex(options, [&](std::string_view bytes) {
            console.append(bytes);
        });
        CHECK(status == 0);
        // print() flushes a fragment without a line end; an empty println stays a line.
        CHECK(console == "partial line\n\nerr\n");
    }
}

TEST_CASE("run-dex exit status follows dalvikvm for System.exit and uncaught exceptions") {
    auto options = Options("rundex.Main");
    options.arguments = {"7"};
    CHECK(ogplay::app::RunDex(options, [](std::string_view) {}) == 7);
    CHECK(ogplay::app::RunDex(Options("rundex.Thrower"), [](std::string_view) {}) == 1);
    CHECK_THROWS_AS(static_cast<void>(ogplay::app::RunDex(Options("rundex.Missing"),
                                                          [](std::string_view) {})),
                    std::runtime_error);
}

TEST_CASE("WU7-3 run-dex retains main and the actual native entry in uncaught stacks") {
    for (const auto* backend : {"switch", "threaded"}) {
        auto options = Options("rundex.Thrower");
        options.backend = backend;
        std::string output;
        CHECK(ogplay::app::RunDex(options, [&](std::string_view bytes) { output += bytes; }) == 1);
        CHECK(output == "Exception in thread \"main\" java.lang.IllegalStateException: boom\n"
                        "\tat rundex.Thrower.main(Unknown Source)\n"
                        "\tat dalvik.system.NativeStart.main(Native Method)\n");
    }
}

TEST_CASE("run-dex uses platform guest JNI and direct NIO memory on both backends") {
    for (const auto* backend : {"switch", "threaded"}) {
        CAPTURE(backend);
        auto options = Options("rundex.Platform");
        options.backend = backend;
        std::string output;
        CHECK(ogplay::app::RunDex(options, [&](std::string_view bytes) { output += bytes; }) == 0);
        CHECK(output == "1,234,567\n42\n");
    }
}

TEST_CASE("run-dex explicitly rejects application native libraries") {
    for (const auto* backend : {"switch", "threaded"}) {
        auto options = Options("rundex.AppNative");
        options.backend = backend;
        std::string output;
        CHECK(ogplay::app::RunDex(options, [&](std::string_view bytes) { output += bytes; }) == 1);
        CHECK(output.find("java.lang.UnsatisfiedLinkError") != std::string::npos);
        CHECK(output.find("rundex_application") != std::string::npos);
    }
}
