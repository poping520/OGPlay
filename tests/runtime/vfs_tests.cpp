#include <doctest/doctest.h>

#include <array>
#include <cstddef>

#include "ogplay/runtime/vfs.h"

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
