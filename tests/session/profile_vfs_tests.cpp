#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/session/profile_vfs.h"

namespace {

[[nodiscard]] ogplay::session::TitleProfile ProfileWithData() {
    ogplay::session::TitleProfile profile;
    profile.data = ogplay::session::ProfileData{
        .mounts = {{"/apk", ogplay::session::ProfileSource::apk, true},
                   {"/obb/main", ogplay::session::ProfileSource::obb, false},
                   {"/sdcard/game", ogplay::session::ProfileSource::external,
                    true}},
        .working_directory = "/sdcard/game",
        .manifest = {{"files/archive.dat", true},
                     {"files/optional.dat", false}},
    };
    return profile;
}

[[nodiscard]] std::vector<ogplay::session::ProfileVfsMountInput> Inputs() {
    return {
        {"/apk",
         ogplay::session::ProfileSource::apk,
         {{"assets/config.bin", {std::byte{1}}}}},
        {"/obb/main",
         ogplay::session::ProfileSource::obb,
         {{"levels/one.bin", {std::byte{4}}}}},
        {"/sdcard/game",
         ogplay::session::ProfileSource::external,
         {{"files/archive.dat", {std::byte{2}, std::byte{3}}}}},
    };
}

}  // namespace

TEST_CASE("Profile data assembles declared sources into a fresh VFS") {
    const auto profile = ProfileWithData();
    const auto inputs = Inputs();
    auto assembled = ogplay::session::AssembleProfileVfs(profile, inputs);

    REQUIRE(assembled.filesystem != nullptr);
    CHECK(assembled.filesystem->Stat("/APK/assets/CONFIG.bin").source ==
          ogplay::runtime::VfsSource::apk);
    CHECK(assembled.filesystem->Stat("/obb/main/levels/one.bin").source ==
          ogplay::runtime::VfsSource::obb);
    const auto external =
        assembled.filesystem->Stat("/sdcard/game/files/archive.dat");
    CHECK(external.source == ogplay::runtime::VfsSource::external);
    CHECK(external.writable);
    CHECK(external.size == 2);
    CHECK(assembled.working_directory == "/sdcard/game");
    REQUIRE(assembled.manifest.size() == 2);
    CHECK(assembled.manifest[0].present);
    CHECK_FALSE(assembled.manifest[1].present);
}

TEST_CASE("Profile VFS rejects missing extra duplicate and mismatched inputs") {
    const auto profile = ProfileWithData();

    auto optional_absent = Inputs();
    optional_absent.erase(optional_absent.begin() + 1);
    CHECK_NOTHROW(static_cast<void>(
        ogplay::session::AssembleProfileVfs(profile, optional_absent)));

    auto missing = Inputs();
    missing.pop_back();
    CHECK_THROWS_WITH_AS(
        static_cast<void>(ogplay::session::AssembleProfileVfs(profile, missing)),
        "required profile VFS mount is missing: /sdcard/game",
        ogplay::session::ProfileVfsError);

    auto extra = Inputs();
    extra.push_back({"/undeclared", ogplay::session::ProfileSource::external,
                     {{"file.bin", {std::byte{4}}}}});
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::session::AssembleProfileVfs(profile, extra)),
        ogplay::session::ProfileVfsError);

    auto duplicate = Inputs();
    duplicate.push_back(duplicate.front());
    duplicate.back().guest = "/APK";
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::session::AssembleProfileVfs(profile, duplicate)),
        ogplay::session::ProfileVfsError);

    auto mismatch = Inputs();
    mismatch.front().source = ogplay::session::ProfileSource::obb;
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::session::AssembleProfileVfs(profile, mismatch)),
        ogplay::session::ProfileVfsError);

    auto empty = Inputs();
    empty.front().entries.clear();
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::session::AssembleProfileVfs(profile, empty)),
        ogplay::session::ProfileVfsError);
}

TEST_CASE("Profile VFS enforces manifest and working directory coverage") {
    auto profile = ProfileWithData();
    auto inputs = Inputs();
    inputs.back().entries.front().path = "files/other.dat";
    CHECK_THROWS_WITH_AS(
        static_cast<void>(ogplay::session::AssembleProfileVfs(profile, inputs)),
        "required profile manifest file is missing: files/archive.dat",
        ogplay::session::ProfileVfsError);

    profile.data->manifest.clear();
    profile.data->working_directory = "/data/unmounted";
    CHECK_THROWS_WITH_AS(
        static_cast<void>(ogplay::session::AssembleProfileVfs(profile, Inputs())),
        "profile working directory is not covered by a mounted source",
        ogplay::session::ProfileVfsError);

    profile = {};
    auto empty = ogplay::session::AssembleProfileVfs(profile, {});
    REQUIRE(empty.filesystem != nullptr);
    CHECK_FALSE(empty.working_directory.has_value());
    CHECK(empty.manifest.empty());
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::session::AssembleProfileVfs(profile, Inputs())),
        ogplay::session::ProfileVfsError);
}

TEST_CASE("Profile lifecycle flush adapter persists every dirty VFS node") {
    const auto root = std::filesystem::temp_directory_path() /
                      "ogplay-profile-vfs-lifecycle-flush";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    REQUIRE_FALSE(error);

    auto store = ogplay::runtime::SandboxStore::Open(root, "com.example.game");
    ogplay::runtime::VirtualFileSystem vfs;
    const std::vector<std::string> roots{"/sdcard"};
    vfs.AttachSandbox(*store, roots);
    const auto descriptor = vfs.Open(
        "/sdcard/open.sav", {.write = true, .create = true});
    const std::vector<std::byte> bytes{std::byte{'o'}, std::byte{'k'}};
    REQUIRE(vfs.Write(descriptor, bytes) == 2);
    REQUIRE(store->UsedBytes() == 0);

    ogplay::session::FlushProfileVfsAtLifecycleBoundary(vfs);
    CHECK(store->UsedBytes() == 2);
    vfs.Close(descriptor);
    std::filesystem::remove_all(root, error);
}
