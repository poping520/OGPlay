#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <mutex>
#include <condition_variable>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "ogplay/runtime/vfs/vfs.h"

namespace {

std::vector<std::byte> ReadLease(
    const std::shared_ptr<const ogplay::runtime::VfsReadLease>& lease) {
    std::vector<std::byte> bytes(static_cast<std::size_t>(lease->Size()));
    REQUIRE(lease->ReadAt(0, bytes) == bytes.size());
    return bytes;
}

}  // namespace

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

TEST_CASE("VFS positioned IO preserves descriptor offsets and bounds reads") {
    ogplay::runtime::VirtualFileSystem vfs;
    std::size_t bytes_read{};
    std::size_t full_loads{};
    const std::array data{std::byte{1}, std::byte{2}, std::byte{3},
                          std::byte{4}, std::byte{5}};
    const std::vector<ogplay::runtime::VfsLazyMountEntry> entries{{
        "large.bin", data.size(),
        [&] {
            ++full_loads;
            return std::vector<std::byte>(data.begin(), data.end());
        },
        [&](const std::uint64_t offset, std::span<std::byte> destination) {
            const auto count = std::min<std::size_t>(
                destination.size(), data.size() - static_cast<std::size_t>(offset));
            std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(offset), count,
                        destination.begin());
            bytes_read += count;
            return count;
        },
    }};
    vfs.MountLazyReadOnly(ogplay::runtime::VfsSource::apk, "/apk", entries);
    const auto descriptor = vfs.Open("/apk/large.bin", {.read = true});
    std::array<std::byte, 2> positioned{};
    CHECK(vfs.ReadAt(descriptor, 2, positioned) == 2);
    CHECK(positioned == std::array{std::byte{3}, std::byte{4}});
    std::array<std::byte, 2> sequential{};
    CHECK(vfs.Read(descriptor, sequential) == 2);
    CHECK(sequential == std::array{std::byte{1}, std::byte{2}});
    CHECK(full_loads == 0);
    CHECK(bytes_read == 4);
    vfs.Close(descriptor);

    const auto writable = vfs.Open(
        "/data/data/example/file", {.read = true, .write = true, .create = true});
    CHECK(vfs.Write(writable, data) == data.size());
    CHECK(vfs.Seek(writable, 1, ogplay::runtime::VfsSeekWhence::begin) == 1);
    const std::array replacement{std::byte{9}};
    CHECK(vfs.WriteAt(writable, 3, replacement) == 1);
    CHECK(vfs.Seek(writable, 0, ogplay::runtime::VfsSeekWhence::current) == 1);
    std::array<std::byte, 5> updated{};
    CHECK(vfs.ReadAt(writable, 0, updated) == updated.size());
    CHECK(updated[3] == std::byte{9});
    vfs.Close(writable);
}

TEST_CASE("VFS blocked backing read does not hold the global index lock") {
    ogplay::runtime::VirtualFileSystem vfs;
    std::mutex barrier_mutex;
    std::condition_variable barrier;
    bool entered{};
    bool release{};
    const std::vector<ogplay::runtime::VfsLazyMountEntry> entries{{
        "blocked.bin", 1,
        [] { return std::vector<std::byte>{std::byte{1}}; },
        [&](std::uint64_t, std::span<std::byte> destination) {
            std::unique_lock lock(barrier_mutex);
            entered = true;
            barrier.notify_all();
            barrier.wait(lock, [&] { return release; });
            destination[0] = std::byte{1};
            return std::size_t{1};
        },
    }};
    vfs.MountLazyReadOnly(ogplay::runtime::VfsSource::apk, "/apk", entries);
    const auto descriptor = vfs.Open("/apk/blocked.bin", {.read = true});
    auto blocked = std::async(std::launch::async, [&] {
        std::array<std::byte, 1> byte{};
        return vfs.Read(descriptor, byte);
    });
    {
        std::unique_lock lock(barrier_mutex);
        REQUIRE(barrier.wait_for(lock, std::chrono::seconds(2),
                                 [&] { return entered; }));
    }
    auto independent = std::async(std::launch::async, [&] {
        CHECK(vfs.Stat("/apk/blocked.bin").size == 1);
        const auto other = vfs.Open("/apk/blocked.bin", {.read = true});
        vfs.Close(other);
    });
    CHECK(independent.wait_for(std::chrono::seconds(2)) ==
          std::future_status::ready);
    {
        std::scoped_lock lock(barrier_mutex);
        release = true;
    }
    barrier.notify_all();
    CHECK(blocked.get() == 1);
    independent.get();
    vfs.Close(descriptor);
}

TEST_CASE("VFS close detaches a blocked open state before descriptor reuse") {
    ogplay::runtime::VirtualFileSystem vfs;
    std::mutex barrier_mutex;
    std::condition_variable barrier;
    bool entered{};
    bool release{};
    const std::vector<ogplay::runtime::VfsLazyMountEntry> entries{{
        "old.bin", 1,
        [] { return std::vector<std::byte>{std::byte{1}}; },
        [&](std::uint64_t, std::span<std::byte> destination) {
            std::unique_lock lock(barrier_mutex);
            entered = true;
            barrier.notify_all();
            barrier.wait(lock, [&] { return release; });
            destination[0] = std::byte{1};
            return std::size_t{1};
        },
    }};
    vfs.MountLazyReadOnly(ogplay::runtime::VfsSource::apk, "/apk", entries);
    const auto old_descriptor = vfs.Open("/apk/old.bin", {.read = true});
    std::array<std::byte, 1> old_byte{};
    auto reading = std::async(std::launch::async, [&] {
        return vfs.Read(old_descriptor, old_byte);
    });
    {
        std::unique_lock lock(barrier_mutex);
        REQUIRE(barrier.wait_for(lock, std::chrono::seconds(2),
                                 [&] { return entered; }));
    }
    std::promise<void> close_started;
    auto close_started_future = close_started.get_future();
    auto closing = std::async(std::launch::async, [&] {
        close_started.set_value();
        vfs.Close(old_descriptor);
    });
    close_started_future.wait();

    std::int32_t reused{-1};
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(2);
    do {
        const auto candidate = vfs.Open("/apk/old.bin", {.read = true});
        if (candidate == old_descriptor) {
            reused = candidate;
            break;
        }
        vfs.Close(candidate);
        std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < deadline);
    REQUIRE(reused == old_descriptor);
    CHECK(closing.wait_for(std::chrono::seconds(0)) ==
          std::future_status::timeout);

    {
        std::scoped_lock lock(barrier_mutex);
        release = true;
    }
    barrier.notify_all();
    CHECK(reading.get() == 1U);
    closing.get();
    CHECK(old_byte[0] == std::byte{1});
    std::array<std::byte, 1> new_byte{};
    CHECK(vfs.Read(reused, new_byte) == 1U);
    CHECK(new_byte[0] == std::byte{1});
    vfs.Close(reused);
}

TEST_CASE("VFS serializes same descriptor seek and keeps failed read offset") {
    ogplay::runtime::VirtualFileSystem vfs;
    std::mutex barrier_mutex;
    std::condition_variable barrier;
    bool entered{};
    bool release{};
    bool fail_first{true};
    const std::array data{std::byte{1}, std::byte{2}, std::byte{3}};
    const std::vector<ogplay::runtime::VfsLazyMountEntry> entries{{
        "file.bin", data.size(),
        [&] { return std::vector<std::byte>(data.begin(), data.end()); },
        [&](const std::uint64_t offset, const std::span<std::byte> destination) {
            if (fail_first) {
                fail_first = false;
                throw std::runtime_error("injected read failure");
            }
            {
                std::unique_lock lock(barrier_mutex);
                entered = true;
                barrier.notify_all();
                barrier.wait(lock, [&] { return release; });
            }
            std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(offset),
                        destination.size(), destination.begin());
            return destination.size();
        },
    }};
    vfs.MountLazyReadOnly(ogplay::runtime::VfsSource::apk, "/apk", entries);
    const auto descriptor = vfs.Open("/apk/file.bin", {.read = true});
    std::array<std::byte, 1> byte{};
    CHECK_THROWS(static_cast<void>(vfs.Read(descriptor, byte)));
    auto reading = std::async(std::launch::async, [&] {
        return vfs.Read(descriptor, byte);
    });
    {
        std::unique_lock lock(barrier_mutex);
        REQUIRE(barrier.wait_for(lock, std::chrono::seconds(2),
                                 [&] { return entered; }));
    }
    std::promise<void> seek_started;
    auto seek_started_future = seek_started.get_future();
    auto seeking = std::async(std::launch::async, [&] {
        seek_started.set_value();
        return vfs.Seek(descriptor, 0,
                        ogplay::runtime::VfsSeekWhence::current);
    });
    seek_started_future.wait();
    CHECK(seeking.wait_for(std::chrono::seconds(0)) ==
          std::future_status::timeout);
    {
        std::scoped_lock lock(barrier_mutex);
        release = true;
    }
    barrier.notify_all();
    CHECK(reading.get() == 1U);
    CHECK(byte[0] == std::byte{1});
    CHECK(seeking.get() == 1U);
    vfs.Close(descriptor);
}

TEST_CASE("VFS leases retain source identity across close unlink and replacement") {
    ogplay::runtime::VirtualFileSystem vfs;
    const std::array original{std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
    vfs.PutFile("/data/file", original, true);
    const auto descriptor = vfs.Open("/data/file", {.read = true, .write = true});
    const auto lease = vfs.CaptureReadLease(descriptor, 1, 2);
    vfs.Close(descriptor);
    vfs.RemoveFile("/data/file");
    const auto replacement = vfs.Open(
        "/data/file", {.read = true, .write = true, .create = true});
    const std::array newer{std::byte{'x'}, std::byte{'y'}, std::byte{'z'}};
    CHECK(vfs.Write(replacement, newer) == newer.size());
    vfs.Close(replacement);
    CHECK(ReadLease(lease) ==
          std::vector<std::byte>{std::byte{'b'}, std::byte{'c'}});

    vfs.PutFile("/data/rename-source", newer, true);
    vfs.PutFile("/data/rename-target", original, true);
    const auto target = vfs.Open(
        "/data/rename-target", {.read = true, .write = true});
    const auto replaced_lease = vfs.CaptureReadLease(target, 0);
    vfs.Rename("/data/rename-source", "/data/rename-target");
    vfs.Close(target);
    CHECK(ReadLease(replaced_lease) ==
          std::vector<std::byte>(original.begin(), original.end()));
}

TEST_CASE("VFS writable lease snapshots are immutable") {
    ogplay::runtime::VirtualFileSystem vfs;
    const std::array original{std::byte{1}, std::byte{2}};
    vfs.PutFile("/data/snapshot", original, true);
    const auto descriptor = vfs.Open(
        "/data/snapshot", {.read = true, .write = true});
    const auto lease = vfs.CaptureReadLease(descriptor, 0);
    CHECK_THROWS_WITH(
        static_cast<void>(vfs.CaptureReadLease(descriptor, 3, 0)),
        "VFS lease offset is past EOF");
    CHECK_THROWS_WITH(
        static_cast<void>(vfs.CaptureReadLease(descriptor, 1, 2)),
        "VFS lease exceeds EOF");
    std::array<std::byte, 1> end{};
    CHECK(lease->ReadAt(lease->Size(), end) == 0U);
    CHECK(lease->ReadAt(0, {}) == 0U);
    const std::array changed{std::byte{9}, std::byte{9}};
    CHECK(vfs.WriteAt(descriptor, 0, changed) == changed.size());
    CHECK(ReadLease(lease) ==
          std::vector<std::byte>{std::byte{1}, std::byte{2}});
    vfs.Close(descriptor);
}

TEST_CASE("VFS writable lease budget is aggregate and released on destruction") {
    ogplay::runtime::VirtualFileSystem vfs({.resource_memory_budget_bytes = 3U});
    const std::array original{std::byte{1}, std::byte{2}, std::byte{3}};
    vfs.PutFile("/data/budget", original, true);
    const auto descriptor = vfs.Open(
        "/data/budget", {.read = true, .write = true});
    auto first = vfs.CaptureReadLease(descriptor, 0, 2);
    CHECK(vfs.IoStatistics().lease_snapshot_bytes == 2U);
    CHECK_THROWS_WITH(static_cast<void>(vfs.CaptureReadLease(descriptor, 1, 2)),
                      "VFS resource memory budget exhausted");
    first.reset();
    CHECK(vfs.IoStatistics().lease_snapshot_bytes == 0U);
    const auto second = vfs.CaptureReadLease(descriptor, 0, 3);
    CHECK(second->Size() == 3U);
    CHECK(vfs.IoStatistics().lease_snapshot_high_water == 3U);
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

TEST_CASE("VFS large host file small reads stay positioned and unmaterialized") {
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto root = std::filesystem::temp_directory_path() /
                      ("ogplay-vfs-large-" + unique);
    std::filesystem::create_directories(root);
    const auto backing = root / "large.bin";
    constexpr std::size_t size = 8U * 1024U * 1024U;
    {
        std::ofstream output(backing, std::ios::binary);
        std::array<char, 4096> block{};
        for (std::size_t offset = 0; offset < size; offset += block.size()) {
            block.fill(static_cast<char>((offset / block.size()) & 0x7fU));
            output.write(block.data(), static_cast<std::streamsize>(block.size()));
        }
    }
    {
        ogplay::runtime::VirtualFileSystem vfs;
        vfs.MountHostDirectory("/host", root);
        const auto descriptor = vfs.Open("/host/large.bin", {.read = true});
        std::array<std::byte, 4> bytes{};
        CHECK(vfs.ReadAt(descriptor, 0, bytes) == bytes.size());
        CHECK(vfs.ReadAt(descriptor, size / 2U, bytes) == bytes.size());
        CHECK(vfs.ReadAt(descriptor, size - bytes.size(), bytes) == bytes.size());
        CHECK(vfs.ReadAt(descriptor, size, bytes) == 0U);
        CHECK(vfs.ReadAt(descriptor, 0, {}) == 0U);
        const auto stats = vfs.IoStatistics();
        CHECK(stats.backing_read_bytes == 12U);
        CHECK(stats.full_materialized_bytes == 0U);
        vfs.Close(descriptor);
    }
    std::error_code error;
    std::filesystem::remove_all(root, error);
    CHECK_FALSE(error);
}

TEST_CASE("VFS large virtual source reads only requested windows") {
    constexpr std::uint64_t size = 64ULL * 1024ULL * 1024ULL;
    std::uint64_t source_bytes{};
    unsigned read_all_calls{};
    const std::vector<ogplay::runtime::VfsLazyMountEntry> entries{{
        "large.bin", size,
        [&] {
            ++read_all_calls;
            return std::vector<std::byte>(static_cast<std::size_t>(size));
        },
        [&](const std::uint64_t offset,
            const std::span<std::byte> destination) {
            source_bytes += destination.size();
            std::fill(destination.begin(), destination.end(),
                      static_cast<std::byte>(offset & 0xffU));
            return destination.size();
        },
    }};
    ogplay::runtime::VirtualFileSystem vfs;
    vfs.MountLazyReadOnly(ogplay::runtime::VfsSource::apk, "/apk", entries);
    const auto descriptor = vfs.Open("/apk/large.bin", {.read = true});
    std::array<std::byte, 3> bytes{};
    CHECK(vfs.ReadAt(descriptor, 0, bytes) == bytes.size());
    CHECK(vfs.ReadAt(descriptor, size / 2U, bytes) == bytes.size());
    CHECK(vfs.ReadAt(descriptor, size - bytes.size(), bytes) == bytes.size());
    CHECK(vfs.ReadAt(descriptor, size, bytes) == 0U);
    CHECK(vfs.ReadAt(descriptor, 0, {}) == 0U);
    CHECK(source_bytes == 9U);
    CHECK(read_all_calls == 0U);
    const auto statistics = vfs.IoStatistics();
    CHECK(statistics.backing_read_bytes == 9U);
    CHECK(statistics.full_materialized_bytes == 0U);
    CHECK(statistics.resource_memory_high_water == 0U);
    vfs.Close(descriptor);
}

TEST_CASE("VFS lease forwards cancellation into a backing read") {
    std::promise<void> entered;
    std::atomic_int error_number{};
    const std::vector<ogplay::runtime::VfsLazyMountEntry> entries{{
        "slow.bin", 1U,
        [] { return std::vector<std::byte>(1U); },
        [&](const std::uint64_t, const std::span<std::byte>,
            const std::stop_token stop) {
            entered.set_value();
            while (!stop.stop_requested()) std::this_thread::yield();
            return std::size_t{};
        },
    }};
    ogplay::runtime::VirtualFileSystem vfs;
    vfs.MountLazyReadOnly(ogplay::runtime::VfsSource::apk, "/apk", entries);
    const auto descriptor = vfs.Open("/apk/slow.bin", {.read = true});
    const auto lease = vfs.CaptureReadLease(descriptor, 0);
    std::stop_source cancellation;
    std::thread reader([&] {
        std::array<std::byte, 1> output{};
        try {
            static_cast<void>(lease->ReadAt(0, output,
                                            cancellation.get_token()));
        } catch (const ogplay::runtime::VfsError& error) {
            error_number.store(error.ErrorNumber());
        }
    });
    entered.get_future().wait();
    cancellation.request_stop();
    reader.join();
    CHECK(error_number.load() == 125);
    vfs.Close(descriptor);
}

TEST_CASE("VFS writable host materialization reserves memory before loading") {
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto root = std::filesystem::temp_directory_path() /
                      ("ogplay-vfs-materialize-budget-" + unique);
    std::filesystem::create_directories(root);
    {
        std::ofstream output(root / "save.bin", std::ios::binary);
        output.write("save", 4);
    }
    {
        ogplay::runtime::VirtualFileSystem vfs(
            {.resource_memory_budget_bytes = 3U});
        vfs.MountHostDirectory("/sdcard", root);
        const auto descriptor = vfs.Open(
            "/sdcard/save.bin", {.read = true, .write = true});
        const std::array replacement{std::byte{'x'}};
        CHECK_THROWS_WITH(
            static_cast<void>(vfs.WriteAt(descriptor, 0, replacement)),
            "VFS resource memory budget exhausted");
        CHECK(vfs.IoStatistics().resource_memory_bytes == 0U);
        vfs.Close(descriptor);
    }
    std::error_code error;
    std::filesystem::remove_all(root, error);
    CHECK_FALSE(error);
}

TEST_CASE("VFS writable materialization reserves later growth") {
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto root = std::filesystem::temp_directory_path() /
                      ("ogplay-vfs-growth-budget-" + unique);
    std::filesystem::create_directories(root);
    { std::ofstream(root / "save.bin", std::ios::binary) << "save"; }
    ogplay::runtime::VirtualFileSystem vfs(
        {.resource_memory_budget_bytes = 5U});
    vfs.MountHostDirectory("/sdcard", root);
    const auto descriptor = vfs.Open(
        "/sdcard/save.bin", {.read = true, .write = true});
    const std::array replacement{std::byte{'x'}};
    CHECK(vfs.WriteAt(descriptor, 0, replacement) == 1U);
    vfs.Truncate(descriptor, 1U);
    CHECK(vfs.WriteAt(descriptor, 3U, replacement) == 1U);
    const std::array growth{std::byte{'a'}, std::byte{'b'}};
    CHECK_THROWS_WITH(static_cast<void>(vfs.WriteAt(descriptor, 4, growth)),
                      "VFS resource memory budget exhausted");
    CHECK(vfs.Stat("/sdcard/save.bin").size == 4U);
    CHECK(vfs.IoStatistics().resource_memory_bytes == 4U);
    vfs.Close(descriptor);
    std::error_code error;
    std::filesystem::remove_all(root, error);
    CHECK_FALSE(error);
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
    CHECK_THROWS_WITH(
        static_cast<void>(vfs.ReadAt(pipe.read_descriptor, 0, received)),
        "VFS pipe does not support positioned IO");
    CHECK_THROWS_WITH(
        static_cast<void>(vfs.WriteAt(pipe.write_descriptor, 0, message)),
        "VFS pipe does not support positioned IO");
    CHECK_THROWS_WITH(
        static_cast<void>(vfs.Seek(
            pipe.read_descriptor, 0,
            ogplay::runtime::VfsSeekWhence::begin)),
        "VFS pipe is not seekable");
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

TEST_CASE("VFS concurrent read and truncate are ordered by node identity") {
    VirtualFileSystem vfs;
    const std::vector<std::byte> contents(256U * 1024U, std::byte{7});
    vfs.PutFile("/sdcard/ordered.dat", contents, true);
    const auto reader = vfs.Open("/sdcard/ordered.dat", {.read = true});
    const auto writer = vfs.Open("/sdcard/ordered.dat", {.write = true});
    std::mutex start_mutex;
    std::condition_variable start_barrier;
    unsigned ready{};
    bool start{};
    auto arrive = [&] {
        std::unique_lock lock(start_mutex);
        ++ready;
        start_barrier.notify_all();
        start_barrier.wait(lock, [&] { return start; });
    };
    std::vector<std::byte> output(contents.size());
    auto reading = std::async(std::launch::async, [&] {
        arrive();
        return vfs.Read(reader, output);
    });
    auto truncating = std::async(std::launch::async, [&] {
        arrive();
        vfs.Truncate(writer, 0);
    });
    {
        std::unique_lock lock(start_mutex);
        REQUIRE(start_barrier.wait_for(lock, std::chrono::seconds(2),
                                       [&] { return ready == 2U; }));
        start = true;
    }
    start_barrier.notify_all();
    const auto count = reading.get();
    truncating.get();
    CHECK((count == 0U || count == contents.size()));
    if (count == contents.size()) CHECK(output == contents);
    CHECK(vfs.Stat("/sdcard/ordered.dat").size == 0U);
    vfs.Close(reader);
    vfs.Close(writer);
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
