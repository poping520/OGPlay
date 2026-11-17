#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>

#if !defined(_WIN32)
#include <unistd.h>
#endif

#include "ogplay/frontend/user_data_dir.h"

namespace {

namespace fs = std::filesystem;
using ogplay::frontend::EnsureWritableUserDataRoot;
using ogplay::frontend::ResolveUserDataLocation;

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        static std::atomic<std::uint64_t> sequence{};
        path = fs::temp_directory_path() /
               ("ogplay-user-data-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                "-" + std::to_string(sequence.fetch_add(1)));
        REQUIRE(fs::create_directory(path));
    }

    ~TemporaryDirectory() {
        std::error_code error;
        fs::remove_all(path, error);
    }

    fs::path path;
};

#if !defined(_WIN32)
class ReadOnlyDirectory {
public:
    explicit ReadOnlyDirectory(const fs::path& path) : path_(path), permissions_(fs::status(path).permissions()) {
        fs::permissions(path_, fs::perms::owner_read | fs::perms::owner_exec);
    }

    ~ReadOnlyDirectory() {
        std::error_code error;
        fs::permissions(path_, permissions_, error);
    }

private:
    fs::path path_;
    fs::perms permissions_;
};
#endif

}  // namespace

TEST_CASE_FIXTURE(TemporaryDirectory, "user data dir uses the platform root when the marker is absent or ignored") {
    const auto marker = path / "portable.txt";
    const auto platform_root = path / "platform";
    SUBCASE("absent") {}
    SUBCASE("directory") {
        REQUIRE(fs::create_directory(marker));
    }

    const auto location = ResolveUserDataLocation(path, true, platform_root);
    REQUIRE(location);
    CHECK(location->root == platform_root);
    CHECK_FALSE(location->portable);
    CHECK_FALSE(ResolveUserDataLocation(path, true, std::nullopt));
    CHECK_FALSE(fs::exists(platform_root));
}

TEST_CASE_FIXTURE(TemporaryDirectory, "user data dir uses a regular marker regardless of its contents") {
    const auto marker = path / "portable.txt";
    const auto platform_root = path / "platform";
    const auto portable_root = path / "userdata";
    SUBCASE("empty marker") {
        REQUIRE(std::ofstream(marker).good());
    }
    SUBCASE("nonempty marker") {
        REQUIRE((std::ofstream(marker) << "ignored").good());
    }

    const auto location = ResolveUserDataLocation(path, true, platform_root);
    REQUIRE(location);
    CHECK(location->root == portable_root);
    CHECK(location->portable);
    const auto without_platform = ResolveUserDataLocation(path, true, std::nullopt);
    REQUIRE(without_platform);
    CHECK(without_platform->root == portable_root);
    CHECK(without_platform->portable);
    CHECK_FALSE(fs::exists(portable_root));

    REQUIRE(fs::remove(marker));
    const auto after_removal = ResolveUserDataLocation(path, true, platform_root);
    REQUIRE(after_removal);
    CHECK(after_removal->root == platform_root);
    CHECK_FALSE(after_removal->portable);
}

TEST_CASE_FIXTURE(TemporaryDirectory, "user data dir ignores the marker on unsupported platforms") {
    REQUIRE(std::ofstream(path / "portable.txt").good());
    const auto platform_root = path / "platform";
    const auto location = ResolveUserDataLocation(path, false, platform_root);
    REQUIRE(location);
    CHECK(location->root == platform_root);
    CHECK_FALSE(location->portable);
    CHECK_FALSE(ResolveUserDataLocation(path, false, std::nullopt));
}

TEST_CASE_FIXTURE(TemporaryDirectory, "user data dir follows a marker symlink to a regular file") {
    const auto target = path / "marker-target";
    REQUIRE(std::ofstream(target).good());
    const auto marker = path / "portable.txt";
    std::error_code error;
    fs::create_symlink(target.filename(), marker, error);
#if defined(_WIN32)
    constexpr int kPrivilegeNotHeld = 1314;
    if (error == std::errc::permission_denied || error == std::errc::operation_not_permitted ||
        error.value() == kPrivilegeNotHeld) {
        MESSAGE("Creating a symlink requires Windows Developer Mode or the symlink privilege");
        return;
    }
#endif
    REQUIRE_FALSE(error);

    const auto location = ResolveUserDataLocation(path, true, std::nullopt);
    REQUIRE(location);
    CHECK(location->root == path / "userdata");
    CHECK(location->portable);
    REQUIRE(fs::remove(target));
    CHECK_FALSE(ResolveUserDataLocation(path, true, std::nullopt));
}

TEST_CASE_FIXTURE(TemporaryDirectory, "user data dir creates a writable root and removes its probe") {
    const auto root = path / "userdata";
    EnsureWritableUserDataRoot(root);
    CHECK(fs::is_directory(root));
    CHECK(fs::is_empty(root));

    const auto existing_file = root / ".ogplay-write-test-existing";
    std::ofstream(existing_file) << "keep";
    EnsureWritableUserDataRoot(root);
    CHECK(fs::is_regular_file(existing_file));
    CHECK(std::distance(fs::directory_iterator(root), fs::directory_iterator{}) == 1);
}

TEST_CASE_FIXTURE(TemporaryDirectory, "user data dir rejects a file in place of userdata") {
    const auto root = path / "userdata";
    REQUIRE(std::ofstream(root).good());
    try {
        EnsureWritableUserDataRoot(root);
        FAIL("expected a portable root error");
    } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        const auto root_utf8 = root.u8string();
        CHECK(message.find(std::string(root_utf8.begin(), root_utf8.end())) != std::string::npos);
        CHECK(message.find("writable folder") != std::string::npos);
        CHECK(message.find("delete portable.txt") != std::string::npos);
        CHECK(message.find("%APPDATA%\\OGPlay") != std::string::npos);
    }
    CHECK(fs::is_regular_file(root));
}

#if !defined(_WIN32)
TEST_CASE_FIXTURE(TemporaryDirectory, "user data dir rejects a read-only parent or root") {
    if (geteuid() == 0) {
        MESSAGE("Root can write through POSIX directory mode restrictions");
        return;
    }
    const auto root = path / "userdata";
    SUBCASE("read-only parent blocks directory creation") {
        ReadOnlyDirectory read_only(path);
        CHECK_THROWS_AS(EnsureWritableUserDataRoot(root), std::runtime_error);
        CHECK_FALSE(fs::exists(root));
    }
    SUBCASE("read-only userdata blocks the write probe") {
        REQUIRE(fs::create_directory(root));
        ReadOnlyDirectory read_only(root);
        CHECK_THROWS_AS(EnsureWritableUserDataRoot(root), std::runtime_error);
        CHECK(fs::is_empty(root));
    }
}
#endif
