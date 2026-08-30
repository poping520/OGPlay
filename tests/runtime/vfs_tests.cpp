#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/runtime/vfs/vfs.h"

TEST_CASE("VFS indexes Android paths case insensitively and isolates offsets") {
    ogplay::runtime::VirtualFileSystem vfs;
    const std::array contents{std::byte{1}, std::byte{2}, std::byte{3}};
    vfs.PutFile("/Assets/Data/Save.BIN", contents, true);
    CHECK(vfs.Stat("/assets/data/save.bin").size == 3);
    const auto first = vfs.Open("/ASSETS/data/SAVE.bin", {.read = true});
    const auto second = vfs.Open("/assets/data/save.bin", {.read = true});
    std::array<std::byte, 2> output{};
    CHECK(vfs.Read(first, output) == 2);
    CHECK(output[0] == std::byte{1});
    CHECK(vfs.Read(second, output) == 2);
    CHECK(output[0] == std::byte{1});
    CHECK(vfs.Seek(first, -1, ogplay::runtime::VfsSeekWhence::current) == 1);
    CHECK(vfs.Read(first, output) == 2);
    CHECK(output[0] == std::byte{2});
    vfs.Close(first);
    vfs.Close(second);
}

TEST_CASE("VFS creates writes truncates and rejects unsafe paths") {
    ogplay::runtime::VirtualFileSystem vfs;
    const auto descriptor = vfs.Open(
        "/data/data/sample/save.dat",
        {.read = true, .write = true, .create = true});
    const std::array contents{std::byte{0x41}, std::byte{0x42}};
    CHECK(vfs.Write(descriptor, contents) == 2);
    CHECK(vfs.Seek(descriptor, 0, ogplay::runtime::VfsSeekWhence::begin) == 0);
    std::array<std::byte, 2> output{};
    CHECK(vfs.Read(descriptor, output) == 2);
    CHECK(output == contents);
    vfs.Close(descriptor);
    CHECK(vfs.Stat("/DATA/DATA/SAMPLE/SAVE.DAT").size == 2);

    CHECK_THROWS_AS(static_cast<void>(
                        vfs.Open("/data/../escape", {.read = true})),
                    ogplay::runtime::VfsError);
    CHECK_THROWS_AS(static_cast<void>(vfs.Open("relative", {.read = true})),
                    ogplay::runtime::VfsError);
    CHECK_THROWS_AS(vfs.PutFile("/data/data/sample/SAVE.dat", contents, true),
                    ogplay::runtime::VfsError);
    CHECK_THROWS_AS(vfs.Close(99), ogplay::runtime::VfsError);
}

TEST_CASE("VFS resolves relative paths from an explicit working directory") {
    ogplay::runtime::VirtualFileSystem vfs;
    const std::array contents{std::byte{0x31}, std::byte{0x32}};
    vfs.PutFile("/sdcard/game/data/config.bin", contents, false);
    CHECK_FALSE(vfs.WorkingDirectory().has_value());
    CHECK_THROWS_AS(
        static_cast<void>(vfs.Open("./data/config.bin", {.read = true})),
        ogplay::runtime::VfsError);

    vfs.SetWorkingDirectory("/SDCARD/game/./");
    CHECK(vfs.WorkingDirectory() == "/sdcard/game");
    const auto descriptor = vfs.Open("./data/CONFIG.bin", {.read = true});
    std::array<std::byte, 2> output{};
    CHECK(vfs.Read(descriptor, output) == output.size());
    CHECK(output == contents);
    vfs.Close(descriptor);
    CHECK_THROWS_AS(vfs.SetWorkingDirectory("relative"),
                    ogplay::runtime::VfsError);
    CHECK_THROWS_AS(
        static_cast<void>(vfs.Open("../escape.bin", {.read = true})),
        ogplay::runtime::VfsError);
}

TEST_CASE("VFS enforces read only files and descriptor access modes") {
    ogplay::runtime::VirtualFileSystem vfs;
    const std::array contents{std::byte{7}};
    vfs.PutFile("/assets/read-only.bin", contents, false);
    CHECK_THROWS_AS(static_cast<void>(
                        vfs.Open("/assets/read-only.bin", {.write = true})),
                    ogplay::runtime::VfsError);
    const auto descriptor =
        vfs.Open("/assets/read-only.bin", {.read = true});
    CHECK_THROWS_AS(static_cast<void>(vfs.Write(descriptor, contents)),
                    ogplay::runtime::VfsError);
    vfs.Close(descriptor);
}

TEST_CASE("VFS mounts APK OBB and external files through one index") {
    ogplay::runtime::VirtualFileSystem vfs;
    const std::vector<ogplay::runtime::VfsMountEntry> apk{
        {"Textures/Hero.bin", {std::byte{1}}}};
    const std::vector<ogplay::runtime::VfsMountEntry> obb{
        {"Levels/One.bin", {std::byte{2}}}};
    const std::vector<ogplay::runtime::VfsMountEntry> external{
        {"Save/Profile.dat", {std::byte{3}}}};
    vfs.Mount(ogplay::runtime::VfsSource::apk, "/apk", apk);
    vfs.Mount(ogplay::runtime::VfsSource::obb, "/obb/main", obb);
    vfs.Mount(ogplay::runtime::VfsSource::external, "/sdcard/game",
              external);
    CHECK(vfs.Stat("/APK/textures/HERO.BIN").source ==
          ogplay::runtime::VfsSource::apk);
    CHECK(vfs.Stat("/obb/main/levels/one.bin").source ==
          ogplay::runtime::VfsSource::obb);
    CHECK_FALSE(vfs.Stat("/apk/textures/hero.bin").writable);
    CHECK(vfs.Stat("/SDCARD/GAME/save/profile.dat").writable);
    const auto writable = vfs.Open(
        "/sdcard/game/save/profile.dat", {.write = true});
    const std::array replacement{std::byte{9}};
    CHECK(vfs.Write(writable, replacement) == 1);
    vfs.Close(writable);
}

TEST_CASE("VFS path aliases share mounted nodes") {
    ogplay::runtime::VirtualFileSystem vfs;
    const std::array contents{std::byte{0x41}};
    vfs.PutFile("/sdcard/game/data.bin", contents, true);
    vfs.AddPathAlias("/storage/emulated/0", "/sdcard");

    CHECK(vfs.Stat("/storage/emulated/0/game/data.bin").size == 1);
    const auto writer = vfs.Open(
        "/storage/emulated/0/game/data.bin", {.write = true});
    const std::array replacement{std::byte{0x52}};
    CHECK(vfs.Write(writer, replacement) == 1);
    vfs.Close(writer);
    const auto descriptor = vfs.Open("/sdcard/game/data.bin", {.read = true});
    std::array<std::byte, 1> read{};
    CHECK(vfs.Read(descriptor, read) == 1);
    CHECK(read == replacement);
    vfs.Close(descriptor);
}

TEST_CASE("VFS mount validation is transactional") {
    ogplay::runtime::VirtualFileSystem vfs;
    const std::vector<ogplay::runtime::VfsMountEntry> invalid{
        {"valid.bin", {std::byte{1}}},
        {"../escape.bin", {std::byte{2}}}};
    CHECK_THROWS_AS(
        vfs.Mount(ogplay::runtime::VfsSource::apk, "/apk", invalid),
        ogplay::runtime::VfsError);
    CHECK_THROWS_AS(static_cast<void>(vfs.Stat("/apk/valid.bin")),
                    ogplay::runtime::VfsError);
}

TEST_CASE("VFS lazy read-only mounts materialize once on first read") {
    ogplay::runtime::VirtualFileSystem vfs;
    std::size_t loads{};
    const std::vector<ogplay::runtime::VfsLazyMountEntry> apk{{
        "assets/data.bin",
        3,
        [&loads] {
            ++loads;
            return std::vector<std::byte>{
                std::byte{1}, std::byte{2}, std::byte{3}};
        },
    }};
    vfs.MountLazyReadOnly(ogplay::runtime::VfsSource::apk, "/apk", apk);
    std::size_t obb_loads{};
    const std::vector<ogplay::runtime::VfsLazyMountEntry> obb{{
        "levels/one.bin",
        1,
        [&obb_loads] {
            ++obb_loads;
            return std::vector<std::byte>{std::byte{4}};
        },
    }};
    vfs.MountLazyReadOnly(
        ogplay::runtime::VfsSource::obb, "/obb/main", obb);
    CHECK(vfs.Stat("/apk/assets/data.bin").size == 3);
    CHECK(vfs.Stat("/obb/main/levels/one.bin").source ==
          ogplay::runtime::VfsSource::obb);
    CHECK(obb_loads == 0);
    CHECK_THROWS_AS(
        vfs.MountLazyReadOnly(
            ogplay::runtime::VfsSource::external, "/sdcard/game", obb),
        ogplay::runtime::VfsError);
    const auto first = vfs.Open("/apk/assets/data.bin", {.read = true});
    CHECK(vfs.Seek(first, 0, ogplay::runtime::VfsSeekWhence::end) == 3);
    CHECK(loads == 0);
    CHECK(vfs.Seek(first, 0, ogplay::runtime::VfsSeekWhence::begin) == 0);
    std::array<std::byte, 3> output{};
    CHECK(vfs.Read(first, output) == output.size());
    CHECK(loads == 1);
    CHECK(output == std::array{std::byte{1}, std::byte{2}, std::byte{3}});
    vfs.Close(first);

    const auto second = vfs.Open("/apk/assets/data.bin", {.read = true});
    CHECK(vfs.Read(second, output) == output.size());
    CHECK(loads == 1);
    vfs.Close(second);
}

TEST_CASE("VFS lazy mount retries explicit backing failures") {
    ogplay::runtime::VirtualFileSystem vfs;
    std::size_t loads{};
    const std::vector<ogplay::runtime::VfsLazyMountEntry> apk{{
        "assets/wrong.bin",
        2,
        [&loads] {
            ++loads;
            return std::vector<std::byte>{std::byte{1}};
        },
    }};
    vfs.MountLazyReadOnly(ogplay::runtime::VfsSource::apk, "/apk", apk);
    const auto descriptor = vfs.Open(
        "/apk/assets/wrong.bin", {.read = true});
    std::array<std::byte, 2> output{};
    CHECK_THROWS_AS(static_cast<void>(vfs.Read(descriptor, output)),
                    ogplay::runtime::VfsError);
    CHECK_THROWS_AS(static_cast<void>(vfs.Read(descriptor, output)),
                    ogplay::runtime::VfsError);
    CHECK(loads == 2);
    vfs.Close(descriptor);
}

TEST_CASE("VFS host directory mount lazily reads and preserves external writes") {
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto root = std::filesystem::temp_directory_path() /
                      ("ogplay-vfs-" + unique);
    const auto nested = root / "Data";
    std::filesystem::create_directories(nested);
    const auto backing = nested / "Game.bin";
    {
        std::ofstream output(backing, std::ios::binary);
        output.write("abc", 3);
    }

    ogplay::runtime::VirtualFileSystem vfs;
    vfs.MountHostDirectory("/sdcard/game", root);
    CHECK(vfs.Stat("/SDCARD/GAME/data/game.bin").size == 3);
    CHECK(vfs.Stat("/sdcard/game/data/game.bin").writable);

    {
        std::ofstream output(backing, std::ios::binary | std::ios::trunc);
        output.write("xyz", 3);
    }
    const auto descriptor = vfs.Open(
        "/sdcard/game/data/game.bin", {.read = true, .write = true});
    const std::array replacement{std::byte{'Q'}};
    CHECK(vfs.Write(descriptor, replacement) == 1);
    CHECK(vfs.Seek(descriptor, 0,
                   ogplay::runtime::VfsSeekWhence::begin) == 0);
    std::array<std::byte, 3> contents{};
    CHECK(vfs.Read(descriptor, contents) == contents.size());
    CHECK(contents == std::array{std::byte{'Q'}, std::byte{'y'},
                                 std::byte{'z'}});
    vfs.Close(descriptor);

    std::filesystem::remove_all(root);
}

TEST_CASE("VFS answers the backing host path only for host-mounted files") {
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto root = std::filesystem::temp_directory_path() /
                      ("ogplay-vfs-hostpath-" + unique);
    const auto nested = root / "Data";
    std::filesystem::create_directories(nested);
    const auto backing = nested / "Movie.mp4";
    {
        std::ofstream output(backing, std::ios::binary);
        output.write("abc", 3);
    }

    ogplay::runtime::VirtualFileSystem vfs;
    vfs.MountHostDirectory("/sdcard/game", root);
    const std::array memory{std::byte{'m'}};
    vfs.PutFile("/data/local/file.bin", memory, false);

    const auto host = vfs.HostPathFor("/SDCARD/Game/data/movie.mp4");
    REQUIRE(host.has_value());
    CHECK(*host == backing);
    CHECK_FALSE(vfs.HostPathFor("/data/local/file.bin").has_value());
    CHECK_FALSE(vfs.HostPathFor("/sdcard/game/data/absent.mp4").has_value());

    std::filesystem::remove_all(root);
}

TEST_CASE("VFS host directory mount rejects unsafe and ambiguous trees transactionally") {
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto root = std::filesystem::temp_directory_path() /
                      ("ogplay-vfs-invalid-" + unique);
    std::filesystem::create_directories(root);
    {
        std::ofstream first(root / "good.bin", std::ios::binary);
        first.put('1');
        std::ofstream second(root / "other.bin", std::ios::binary);
        second.put('2');
    }
    ogplay::runtime::VirtualFileSystem vfs;
    const std::array existing{std::byte{'x'}};
    vfs.PutFile("/sdcard/game/GOOD.BIN", existing, false);
    CHECK_THROWS_AS(vfs.MountHostDirectory("/sdcard/game", root),
                    ogplay::runtime::VfsError);
    CHECK(vfs.Stat("/sdcard/game/good.bin").size == 1U);
    CHECK_THROWS_AS(static_cast<void>(vfs.Stat("/sdcard/game/other.bin")),
                    ogplay::runtime::VfsError);
    std::filesystem::remove_all(root);

    std::filesystem::create_directories(root);
    std::error_code error;
    std::filesystem::create_symlink(root / "missing", root / "link", error);
    if (!error) {
        ogplay::runtime::VirtualFileSystem symlink_vfs;
        CHECK_THROWS_AS(symlink_vfs.MountHostDirectory("/sdcard/game", root),
                        ogplay::runtime::VfsError);
    }
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    ogplay::runtime::VirtualFileSystem empty_vfs;
    CHECK_THROWS_AS(empty_vfs.MountHostDirectory("/sdcard/game", root),
                    ogplay::runtime::VfsError);
    std::filesystem::remove_all(root);
}

TEST_CASE("VFS pipe connects isolated read and write descriptors") {
    ogplay::runtime::VirtualFileSystem vfs;
    const auto pipe = vfs.CreatePipe();
    CHECK(pipe.read_descriptor >= 3);
    CHECK(pipe.write_descriptor > pipe.read_descriptor);
    const std::array message{std::byte{0x41}, std::byte{0x42}};
    CHECK(vfs.Write(pipe.write_descriptor, message) == message.size());
    std::array<std::byte, 2> received{};
    CHECK(vfs.Read(pipe.read_descriptor, received) == received.size());
    CHECK(received == message);
    CHECK_THROWS_AS(static_cast<void>(
                        vfs.Read(pipe.write_descriptor, received)),
                    ogplay::runtime::VfsError);
    CHECK_THROWS_AS(static_cast<void>(
                        vfs.Write(pipe.read_descriptor, message)),
                    ogplay::runtime::VfsError);
    vfs.Close(pipe.read_descriptor);
    vfs.Close(pipe.write_descriptor);
}

// ---- directory and metadata operations (SBX-2, ADR-0020) -----------------

namespace {

using ogplay::runtime::VfsDirectoryEntry;
using ogplay::runtime::VfsError;
using ogplay::runtime::VirtualFileSystem;

[[nodiscard]] std::int32_t ErrnoOf(const std::function<void()>& action) {
    try {
        action();
    } catch (const VfsError& error) {
        return error.ErrorNumber();
    }
    return 0;
}

void PutText(VirtualFileSystem& vfs, const std::string_view path,
             const std::string_view text) {
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (const auto character : text) {
        bytes.push_back(static_cast<std::byte>(character));
    }
    vfs.PutFile(path, bytes, true);
}

}  // namespace

TEST_CASE("VFS mkdir requires a parent and refuses to collide") {
    VirtualFileSystem vfs;
    vfs.CreateDirectory("/data");
    vfs.CreateDirectory("/data/files");
    CHECK(vfs.Stat("/data/files").is_directory);
    CHECK(vfs.Stat("/data/files").size == 0);

    // A missing parent is -ENOENT, not an implicit mkdir -p.
    CHECK(ErrnoOf([&] { vfs.CreateDirectory("/data/files/a/b"); }) == 2);
    CHECK(ErrnoOf([&] { vfs.CreateDirectory("/data/files"); }) == 17);
    PutText(vfs, "/data/files/save.dat", "body");
    CHECK(ErrnoOf([&] { vfs.CreateDirectory("/data/files/save.dat"); }) == 17);
    // Directories implied by a mounted file are directories too.
    CHECK(vfs.Stat("/data").is_directory);
    CHECK_FALSE(vfs.Stat("/data/files/save.dat").is_directory);
    CHECK(ErrnoOf([&] { static_cast<void>(vfs.Stat("/data/missing")); }) == 2);
}

TEST_CASE("VFS enumerates explicit and implicit directories in one order") {
    VirtualFileSystem vfs;
    PutText(vfs, "/sdcard/game/b.dat", "b");
    PutText(vfs, "/sdcard/game/sub/c.dat", "c");
    vfs.CreateDirectory("/sdcard/game/a-empty");

    const auto entries = vfs.ListDirectory("/sdcard/game");
    const std::vector<VfsDirectoryEntry> expected{
        {"a-empty", true}, {"b.dat", false}, {"sub", true}};
    CHECK(entries == expected);
    // An empty directory really is empty, not absent.
    CHECK(vfs.ListDirectory("/sdcard/game/a-empty").empty());
}

TEST_CASE("VFS unlink and rmdir carry the platform errno contract") {
    VirtualFileSystem vfs;
    PutText(vfs, "/sdcard/keep.dat", "keep");
    vfs.CreateDirectory("/sdcard/dir");
    PutText(vfs, "/sdcard/dir/inner.dat", "inner");

    CHECK(ErrnoOf([&] { vfs.RemoveFile("/sdcard/missing"); }) == 2);
    CHECK(ErrnoOf([&] { vfs.RemoveFile("/sdcard/dir"); }) == 21);   // EISDIR
    CHECK(ErrnoOf([&] { vfs.RemoveDirectory("/sdcard/keep.dat"); }) == 20);
    CHECK(ErrnoOf([&] { vfs.RemoveDirectory("/sdcard/dir"); }) == 39);

    vfs.RemoveFile("/sdcard/dir/inner.dat");
    vfs.RemoveDirectory("/sdcard/dir");
    CHECK(ErrnoOf([&] { static_cast<void>(vfs.Stat("/sdcard/dir")); }) == 2);
    CHECK(ErrnoOf([&] { vfs.RemoveDirectory("/sdcard/dir"); }) == 2);
    CHECK(vfs.ListDirectory("/sdcard") ==
          std::vector<VfsDirectoryEntry>{{"keep.dat", false}});
}

TEST_CASE("VFS unlink refuses a read-only mounted file") {
    VirtualFileSystem vfs;
    const std::array contents{std::byte{7}};
    const std::array entries{
        ogplay::runtime::VfsMountEntry{"data.bin",
                                       {contents.begin(), contents.end()}}};
    vfs.Mount(ogplay::runtime::VfsSource::apk, "/apk", entries);
    CHECK(ErrnoOf([&] { vfs.RemoveFile("/apk/data.bin"); }) == 13);
}

TEST_CASE("VFS rename moves a file and leaves nothing behind") {
    VirtualFileSystem vfs;
    PutText(vfs, "/sdcard/old.sav", "body");
    vfs.CreateDirectory("/sdcard/dir");

    vfs.Rename("/sdcard/old.sav", "/sdcard/new.sav");
    CHECK(vfs.Stat("/sdcard/new.sav").size == 4);
    CHECK(ErrnoOf([&] { static_cast<void>(vfs.Stat("/sdcard/old.sav")); }) == 2);
    CHECK(ErrnoOf([&] { vfs.Rename("/sdcard/gone", "/sdcard/x"); }) == 2);
    CHECK(ErrnoOf([&] { vfs.Rename("/sdcard/gone", "/sdcard/gone"); }) == 2);
    CHECK(ErrnoOf([&] {
        vfs.Rename("/sdcard/new.sav", "/missing/new.sav");
    }) == 2);
    CHECK(vfs.Stat("/sdcard/new.sav").size == 4);
    vfs.Rename("/sdcard/new.sav", "/sdcard/new.sav");
    // Subtree moves have no caller yet and are refused rather than guessed.
    CHECK(ErrnoOf([&] { vfs.Rename("/sdcard/dir", "/sdcard/dir2"); }) == 22);
    CHECK(ErrnoOf([&] { vfs.Rename("/sdcard/new.sav", "/sdcard/dir"); }) == 21);
    // Renaming onto an existing file replaces it, as on the platform.
    PutText(vfs, "/sdcard/victim.sav", "old-and-longer");
    vfs.Rename("/sdcard/new.sav", "/sdcard/victim.sav");
    CHECK(vfs.Stat("/sdcard/victim.sav").size == 4);
}

TEST_CASE("VFS truncate shrinks and grows through the descriptor") {
    VirtualFileSystem vfs;
    PutText(vfs, "/sdcard/save.dat", "0123456789");
    const auto writer = vfs.Open("/sdcard/save.dat", {.read = true,
                                                      .write = true});
    vfs.Truncate(writer, 4);
    CHECK(vfs.Stat("/sdcard/save.dat").size == 4);
    vfs.Truncate(writer, 6);
    CHECK(vfs.Stat("/sdcard/save.dat").size == 6);
    std::array<std::byte, 6> output{};
    CHECK(vfs.Read(writer, output) == 6);
    CHECK(output[3] == std::byte{'3'});
    CHECK(output[4] == std::byte{0});  // grown tail reads as zeroes
    vfs.Close(writer);

    const auto reader = vfs.Open("/sdcard/save.dat", {.read = true});
    CHECK(ErrnoOf([&] { vfs.Truncate(reader, 0); }) == 9);  // EBADF
    vfs.Close(reader);
}

TEST_CASE("VFS flush validates its descriptor without a sandbox attached") {
    VirtualFileSystem vfs;
    PutText(vfs, "/sdcard/save.dat", "body");
    const auto writer = vfs.Open("/sdcard/save.dat", {.write = true});
    vfs.Flush(writer);   // no store yet: honest no-op, not a missing call
    vfs.FlushAll();
    vfs.Close(writer);
    CHECK(ErrnoOf([&] { vfs.Flush(writer); }) == 9);
}
