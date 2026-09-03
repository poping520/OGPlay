#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "ogplay/frontend/data_directory.h"
#include "ogplay/hal/host_environment.h"

namespace {

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        static std::atomic<std::uint64_t> sequence{};
        path = std::filesystem::temp_directory_path() /
               ("ogplay-data-directory-" +
                std::to_string(std::chrono::steady_clock::now()
                                   .time_since_epoch().count()) +
                "-" + std::to_string(sequence.fetch_add(1)));
        std::filesystem::create_directories(path);
    }
    ~TemporaryDirectory() {
        std::error_code error;
        static_cast<void>(std::filesystem::remove_all(path, error));
    }
    std::filesystem::path path;
};

void WritePayload(const std::filesystem::path& root) {
    std::filesystem::create_directories(root / "profiles");
    std::ofstream(root / "quirks.toml", std::ios::binary) << "schema = 1\n";
    const auto libraries = root / "android" / "19" / "lib";
    std::filesystem::create_directories(libraries);
    for (const auto* name : {"libc.so", "libdl.so", "libm.so",
                             "libstdc++.so", "libz.so"}) {
        std::ofstream(libraries / name, std::ios::binary) << "elf";
    }
}

}  // namespace

TEST_CASE("bundled data prefers executable and macOS resource payloads") {
    TemporaryDirectory temporary;
    const auto executable = temporary.path / "app" / "bin";
    const auto source = temporary.path / "source";
    std::filesystem::create_directories(executable);
    WritePayload(source / "data");

    auto paths = ogplay::frontend::ResolveBundledDataPaths(executable, source);
    CHECK(paths.root == std::filesystem::absolute(source / "data"));

    const auto resources = executable.parent_path() / "Resources" / "data";
    WritePayload(resources);
    paths = ogplay::frontend::ResolveBundledDataPaths(executable, source);
    CHECK(paths.root == std::filesystem::absolute(resources));

    WritePayload(executable / "data");
    paths = ogplay::frontend::ResolveBundledDataPaths(executable, source);
    CHECK(paths.root == std::filesystem::absolute(executable / "data"));
    CHECK(paths.profiles_directory == paths.root / "profiles");
    CHECK(paths.quirk_registry == paths.root / "quirks.toml");
}

TEST_CASE("bundled data rejects payloads without Android libraries") {
    TemporaryDirectory temporary;
    const auto executable = temporary.path / "bin";
    const auto source = temporary.path / "source";
    std::filesystem::create_directories(executable / "data" / "profiles");
    std::ofstream(executable / "data" / "quirks.toml", std::ios::binary)
        << "schema = 1\n";
    WritePayload(source / "data");

    const auto paths =
        ogplay::frontend::ResolveBundledDataPaths(executable, source);
    CHECK(paths.root == std::filesystem::absolute(source / "data"));
}

TEST_CASE("build stages a complete runtime data payload") {
    const auto paths = ogplay::frontend::HostBundledDataPaths();
    CHECK(paths.root ==
          ogplay::hal::HostExecutableDirectory() / "data");
    CHECK(std::filesystem::is_directory(paths.profiles_directory));
    CHECK(std::filesystem::is_regular_file(paths.quirk_registry));
    const auto android = paths.root / "android" / "19";
    CHECK(std::filesystem::is_regular_file(android / "manifest.json"));
    CHECK(std::filesystem::is_regular_file(android / "source-manifest.xml"));
    for (const auto* name : {"libc.so", "libdl.so", "libm.so",
                             "libstdc++.so", "libz.so"}) {
        CHECK(std::filesystem::is_regular_file(android / "lib" / name));
    }
}
