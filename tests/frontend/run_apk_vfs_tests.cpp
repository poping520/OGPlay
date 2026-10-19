#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <stop_token>
#include <string_view>
#include <vector>

#include "ogplay/audio/encoded_audio_stream.h"
#include "../../src/frontend/game/run_game_vfs.h"

namespace {

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        static std::atomic_uint64_t next{};
        path = std::filesystem::temp_directory_path() /
               ("ogplay-run-apk-vfs-" +
                std::to_string(next.fetch_add(1U, std::memory_order_relaxed)));
        std::filesystem::remove_all(path);
    }
    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    std::filesystem::path path;
};

}  // namespace

TEST_CASE("run-apk attaches a persistent sandbox with a UTF-8 host path") {
    TemporaryDirectory temporary;
    const auto root = temporary.path / u8"存档 目录";
    auto sandbox = ogplay::frontend::OpenSandbox(
        {.directory = root}, "org.example.game", 1);
    ogplay::runtime::VirtualFileSystem filesystem;
    ogplay::core::Logger logger;
    CHECK_NOTHROW(ogplay::frontend::AttachSandbox(sandbox, "org.example.game", filesystem, logger));
    CHECK(sandbox.root == root);
    CHECK(sandbox.Describe().find("\xe5\xad\x98\xe6\xa1\xa3") != std::string::npos);
}

TEST_CASE("direct run-apk allocates and reuses an unambiguous installation") {
    TemporaryDirectory tree;
    const auto root = tree.path / "sandbox";
    {
        auto sandbox = ogplay::frontend::OpenSandbox(
            {.directory = root}, "org.example.game", 1U);
        CHECK(sandbox.installation_id == "org.example.game");
        CHECK(std::filesystem::is_directory(root / "org.example.game"));
    }
    {
        auto sandbox = ogplay::frontend::OpenSandbox(
            {.directory = root}, "org.example.game", 2U);
        CHECK(sandbox.installation_id == "org.example.game");
    }
}

TEST_CASE("direct run-apk reuses a unique suffixed installation") {
    TemporaryDirectory tree;
    const auto root = tree.path / "sandbox";
    std::filesystem::create_directories(root);
    auto created = ogplay::runtime::SandboxStore::Create(
        root, "org.example.game-2", "org.example.game");
    created.reset();
    auto sandbox = ogplay::frontend::OpenSandbox(
        {.directory = root}, "org.example.game", 1U);
    CHECK(sandbox.installation_id == "org.example.game-2");
}

TEST_CASE("direct run-apk requires disambiguation for multiple installations") {
    TemporaryDirectory tree;
    const auto root = tree.path / "sandbox";
    std::filesystem::create_directories(root);
    auto first = ogplay::runtime::SandboxStore::Create(
        root, "org.example.game", "org.example.game");
    auto second = ogplay::runtime::SandboxStore::Create(
        root, "org.example.game-2", "org.example.game");
    first.reset();
    second.reset();
    CHECK_THROWS_WITH(
        static_cast<void>(ogplay::frontend::OpenSandbox(
            {.directory = root}, "org.example.game", 1U)),
        "multiple save sandbox installations exist for package "
        "org.example.game; pass --installation-id <id>");
}

TEST_CASE("run-apk mounts an external directory without a Profile") {
    TemporaryDirectory tree;
    std::filesystem::create_directories(tree.path);
    {
        std::ofstream file(tree.path / "data.bin", std::ios::binary);
        file << "external-data";
    }
    ogplay::runtime::VirtualFileSystem vfs;
    ogplay::frontend::MountExternalDirectory(tree.path, vfs);
    CHECK(vfs.Stat("/mnt/sdcard/data.bin").size == vfs.Stat("/sdcard/data.bin").size);
    const auto legacy = vfs.Open("/mnt/sdcard/data.bin", {.read = true});
    const auto canonical = vfs.Open("/sdcard/data.bin", {.read = true});
    CHECK(vfs.TryDescriptorNode(legacy) == vfs.TryDescriptorNode(canonical));
    vfs.Close(legacy);
    vfs.Close(canonical);
    CHECK(vfs.ListDirectory("/mnt/sdcard") == vfs.ListDirectory("/sdcard"));
    const auto writer = vfs.Open("/mnt/sdcard/data.bin", {.write = true});
    const std::array replacement{std::byte{'E'}};
    CHECK(vfs.Write(writer, replacement) == 1);
    vfs.Close(writer);
    const auto reader = vfs.Open("/sdcard/data.bin", {.read = true});
    std::array<std::byte, 1> shared{};
    CHECK(vfs.Read(reader, shared) == 1);
    CHECK(shared == replacement);
    vfs.Close(reader);
    const auto descriptor = vfs.Open("/storage/emulated/0/data.bin", {.read = true});
    std::array<std::byte, 8> output{};
    CHECK(vfs.Read(descriptor, output) == output.size());
    CHECK(std::string(reinterpret_cast<const char*>(output.data()), output.size()) ==
          "External");
    CHECK(vfs.DescriptorInfo(descriptor).source ==
          ogplay::runtime::VfsSource::external);
    vfs.Close(descriptor);

    ogplay::runtime::VirtualFileSystem explicit_vfs;
    ogplay::frontend::MountExternalDirectory(
        tree.path, explicit_vfs,
        std::string{"/sdcard/Android/data/org.example.game/files"});
    CHECK_NOTHROW(static_cast<void>(explicit_vfs.Stat(
        "/sdcard/Android/data/org.example.game/files/data.bin")));
    CHECK_NOTHROW(static_cast<void>(explicit_vfs.Stat(
        "/mnt/sdcard/Android/data/org.example.game/files/data.bin")));
    CHECK_NOTHROW(static_cast<void>(explicit_vfs.Stat(
        "/SDCARD/android/DATA/org.example.GAME/files/DATA.BIN")));
    const auto mixed = explicit_vfs.Open(
        "/SDCARD/android/DATA/org.example.GAME/files/DATA.BIN", {.read = true});
    std::array<std::byte, 13> mixed_bytes{};
    CHECK(explicit_vfs.Read(mixed, mixed_bytes) == mixed_bytes.size());
    CHECK(std::string(reinterpret_cast<const char*>(mixed_bytes.data()), mixed_bytes.size()) == "external-data");
    explicit_vfs.Close(mixed);
    CHECK_THROWS(static_cast<void>(explicit_vfs.Stat("/mnt/sdcard-other/data.bin")));
    CHECK_THROWS(static_cast<void>(explicit_vfs.Stat("/sdcard/data.bin")));
    ogplay::runtime::VirtualFileSystem invalid_vfs;
    CHECK_THROWS(ogplay::frontend::MountExternalDirectory(
        tree.path, invalid_vfs, std::string{"/data/data/org.example.game"}));


}

TEST_CASE("run-apk mounts the original OBB file with bounded reads") {
    TemporaryDirectory tree;
    std::filesystem::create_directories(tree.path);
    const auto file = tree.path / "main.1.org.example.game.obb";
    {
        std::ofstream output(file, std::ios::binary);
        output << "original-obb-bytes";
    }
    ogplay::runtime::VirtualFileSystem vfs;
    vfs.MountHostFile(ogplay::runtime::VfsSource::obb,
                      "/sdcard/Android/obb/org.example.game", file);
    vfs.AddPathAlias("/storage/emulated/0", "/sdcard");
    const auto descriptor = vfs.Open(
        "/storage/emulated/0/Android/obb/org.example.game/main.1.org.example.game.obb",
        {.read = true});
    CHECK(vfs.DescriptorInfo(descriptor).source == ogplay::runtime::VfsSource::obb);
    CHECK(vfs.Seek(descriptor, 9, ogplay::runtime::VfsSeekWhence::begin) == 9U);
    std::array<std::byte, 3> output{};
    CHECK(vfs.Read(descriptor, output) == output.size());
    CHECK(std::string(reinterpret_cast<const char*>(output.data()), output.size()) ==
          "obb");
    CHECK(vfs.IoStatistics().full_materialized_bytes == 0U);
    vfs.Close(descriptor);
    CHECK_THROWS(vfs.MountHostFile(ogplay::runtime::VfsSource::obb,
                                  "/sdcard/Android/obb/org.example.game", file));
}
