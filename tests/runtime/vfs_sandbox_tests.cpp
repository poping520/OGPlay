// Sandbox overlay on the VFS (SBX-3, ADR-0020). The load-bearing case is
// the last one: two sessions over one host directory, where session 2 must
// see exactly what session 1 left behind. That is the user problem — saves
// disappearing on restart — expressed as an assertion.

#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/runtime/vfs/sandbox_store.h"
#include "ogplay/runtime/vfs/vfs.h"

namespace {

using namespace ogplay::runtime;

struct TemporaryRoot final {
    std::filesystem::path path;

    explicit TemporaryRoot(const std::string& name)
        : path(std::filesystem::temp_directory_path() /
               ("ogplay-vfs-sandbox-" + name)) {
        std::error_code error;
        std::filesystem::remove_all(path, error);
        std::filesystem::create_directories(path, error);
        REQUIRE_FALSE(error);
    }
    ~TemporaryRoot() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    TemporaryRoot(const TemporaryRoot&) = delete;
    TemporaryRoot& operator=(const TemporaryRoot&) = delete;
};

constexpr const char* kPackage = "com.example.game";
const std::vector<std::string> kWritableRoots{"/data/data/com.example.game",
                                              "/sdcard"};

[[nodiscard]] std::vector<std::byte> Bytes(const std::string_view text) {
    std::vector<std::byte> out;
    out.reserve(text.size());
    for (const auto character : text) {
        out.push_back(static_cast<std::byte>(character));
    }
    return out;
}

void WriteThrough(VirtualFileSystem& vfs, const std::string_view path,
                  const std::string_view text) {
    const auto descriptor =
        vfs.Open(path, {.read = true, .write = true, .create = true,
                        .truncate = true});
    const auto bytes = Bytes(text);
    CHECK(vfs.Write(descriptor, bytes) == bytes.size());
    vfs.Close(descriptor);  // close is a flush point
}

[[nodiscard]] std::string ReadAll(VirtualFileSystem& vfs,
                                  const std::string_view path) {
    const auto descriptor = vfs.Open(path, {.read = true});
    std::array<std::byte, 256> buffer{};
    const auto count = vfs.Read(descriptor, buffer);
    vfs.Close(descriptor);
    std::string text;
    for (std::size_t index = 0; index < count; ++index) {
        text.push_back(static_cast<char>(buffer[index]));
    }
    return text;
}

[[nodiscard]] std::string Text(const std::vector<std::byte>& bytes) {
    std::string out;
    out.reserve(bytes.size());
    for (const auto value : bytes) {
        out.push_back(static_cast<char>(value));
    }
    return out;
}

[[nodiscard]] std::int32_t ErrnoOf(const std::function<void()>& action) {
    try {
        action();
    } catch (const VfsError& error) {
        return error.ErrorNumber();
    }
    return 0;
}

}  // namespace

TEST_CASE("VFS sandbox keeps guest writes across two sessions") {
    const TemporaryRoot root("crosssession");
    {
        auto store = SandboxStore::Open(root.path, kPackage);
        VirtualFileSystem vfs;
        vfs.AttachSandbox(*store, kWritableRoots);
        CHECK(vfs.SandboxAttached());

        vfs.CreateDirectory("/sdcard/game");
        vfs.CreateDirectory("/sdcard/game/saves");
        WriteThrough(vfs, "/sdcard/game/saves/slot0.sav", "progress-42");
        WriteThrough(vfs, "/data/data/com.example.game/files/prefs.bin", "ok");
        vfs.FlushAll();  // clean shutdown
    }

    // Session two: a fresh store and a fresh VFS over the same directory.
    auto store = SandboxStore::Open(root.path, kPackage);
    VirtualFileSystem vfs;
    vfs.AttachSandbox(*store, kWritableRoots);
    CHECK(ReadAll(vfs, "/sdcard/game/saves/slot0.sav") == "progress-42");
    CHECK(ReadAll(vfs, "/data/data/com.example.game/files/prefs.bin") == "ok");
    CHECK(vfs.Stat("/sdcard/game/saves/slot0.sav").size == 11);
    CHECK(vfs.Stat("/sdcard/game/saves").is_directory);
    CHECK(vfs.Stat("/sdcard/game/saves/slot0.sav").source ==
          VfsSource::sandbox);
    CHECK(vfs.ListDirectory("/sdcard/game") ==
          std::vector<VfsDirectoryEntry>{{"saves", true}});
}

TEST_CASE("VFS sandbox overlays and tombstones a read-only base layer") {
    const TemporaryRoot root("overlay");
    const std::array base{std::byte{'b'}, std::byte{'a'}, std::byte{'s'},
                          std::byte{'e'}};
    const std::vector<VfsMountEntry> entries{
        {"keep.dat", {base.begin(), base.end()}},
        {"shadowed.dat", {base.begin(), base.end()}},
        {"deleted.dat", {base.begin(), base.end()}}};

    {
        auto store = SandboxStore::Open(root.path, kPackage);
        VirtualFileSystem vfs;
        vfs.Mount(VfsSource::external, "/sdcard/data", entries);
        vfs.AttachSandbox(*store, kWritableRoots);

        CHECK(ReadAll(vfs, "/sdcard/data/shadowed.dat") == "base");
        WriteThrough(vfs, "/sdcard/data/shadowed.dat", "overlay");
        vfs.RemoveFile("/sdcard/data/deleted.dat");
        // Deleting the overlay copy must not resurrect the base file.
        CHECK(ErrnoOf([&] {
                  static_cast<void>(vfs.Stat("/sdcard/data/deleted.dat"));
              }) == 2);
        vfs.FlushAll();
    }

    auto store = SandboxStore::Open(root.path, kPackage);
    VirtualFileSystem vfs;
    // The base layer is mounted again exactly as before: it was never
    // written to.
    vfs.Mount(VfsSource::external, "/sdcard/data", entries);
    vfs.AttachSandbox(*store, kWritableRoots);

    CHECK(ReadAll(vfs, "/sdcard/data/keep.dat") == "base");
    CHECK(ReadAll(vfs, "/sdcard/data/shadowed.dat") == "overlay");
    CHECK(ErrnoOf([&] {
              static_cast<void>(vfs.Stat("/sdcard/data/deleted.dat"));
          }) == 2);
    // A tombstoned path must not show up in enumeration either.
    CHECK(vfs.ListDirectory("/sdcard/data") ==
          std::vector<VfsDirectoryEntry>{{"keep.dat", false},
                                         {"shadowed.dat", false}});
}

TEST_CASE("VFS sandbox deletion never resurrects a previously shadowed base file") {
    const TemporaryRoot root("rewrite-delete-restart");
    const auto base = Bytes("old-save");
    const std::vector<VfsMountEntry> entries{{"save.dat", base}};

    {
        auto store = SandboxStore::Open(root.path, kPackage);
        VirtualFileSystem vfs;
        vfs.Mount(VfsSource::external, "/sdcard/data", entries);
        vfs.AttachSandbox(*store, kWritableRoots);
        WriteThrough(vfs, "/sdcard/data/save.dat", "new-save");
    }
    {
        auto store = SandboxStore::Open(root.path, kPackage);
        VirtualFileSystem vfs;
        vfs.Mount(VfsSource::external, "/sdcard/data", entries);
        vfs.AttachSandbox(*store, kWritableRoots);
        CHECK(ReadAll(vfs, "/sdcard/data/save.dat") == "new-save");
        vfs.RemoveFile("/sdcard/data/save.dat");
    }

    auto store = SandboxStore::Open(root.path, kPackage);
    VirtualFileSystem vfs;
    vfs.Mount(VfsSource::external, "/sdcard/data", entries);
    vfs.AttachSandbox(*store, kWritableRoots);
    CHECK(ErrnoOf([&] {
              static_cast<void>(vfs.Stat("/sdcard/data/save.dat"));
          }) == 2);
    CHECK(vfs.ListDirectory("/sdcard/data").empty());
}

TEST_CASE("VFS sandbox recreate clears a tombstone before close and persists read-only create") {
    const TemporaryRoot root("delete-recreate");
    const auto base = Bytes("base");
    const std::vector<VfsMountEntry> entries{{"save.dat", base}};

    {
        auto store = SandboxStore::Open(root.path, kPackage);
        VirtualFileSystem vfs;
        vfs.Mount(VfsSource::external, "/sdcard/data", entries);
        vfs.AttachSandbox(*store, kWritableRoots);
        vfs.RemoveFile("/sdcard/data/save.dat");

        const auto descriptor = vfs.Open(
            "/sdcard/data/save.dat", {.read = true, .create = true});
        CHECK(vfs.Stat("/sdcard/data/save.dat").size == 0);
        CHECK(vfs.ListDirectory("/sdcard/data") ==
              std::vector<VfsDirectoryEntry>{{"save.dat", false}});
        // A second delete must see the recreated node rather than the stale
        // in-memory tombstone.
        vfs.RemoveFile("/sdcard/data/save.dat");
        vfs.Close(descriptor);

        const auto recreated = vfs.Open(
            "/sdcard/data/save.dat", {.read = true, .create = true});
        vfs.Close(recreated);
    }

    auto store = SandboxStore::Open(root.path, kPackage);
    VirtualFileSystem vfs;
    vfs.Mount(VfsSource::external, "/sdcard/data", entries);
    vfs.AttachSandbox(*store, kWritableRoots);
    CHECK(vfs.Stat("/sdcard/data/save.dat").size == 0);
    CHECK(ReadAll(vfs, "/sdcard/data/save.dat").empty());
}

TEST_CASE("VFS sandbox refuses writes outside the writable namespace") {
    const TemporaryRoot root("namespace");
    auto store = SandboxStore::Open(root.path, kPackage);
    VirtualFileSystem vfs;
    const std::array base{std::byte{'x'}};
    const std::vector<VfsMountEntry> entries{
        {"asset.bin", {base.begin(), base.end()}}};
    vfs.Mount(VfsSource::apk, "/apk", entries);
    vfs.AttachSandbox(*store, kWritableRoots);

    CHECK(ErrnoOf([&] {
              static_cast<void>(vfs.Open(
                  "/etc/passwd", {.write = true, .create = true}));
          }) == 13);
    CHECK(ErrnoOf([&] { vfs.CreateDirectory("/etc/evil"); }) == 13);
    // The read-only base layer stays exactly as read-only as before.
    CHECK(ErrnoOf([&] {
              static_cast<void>(vfs.Open("/apk/asset.bin", {.write = true}));
          }) == 13);
    CHECK(ReadAll(vfs, "/apk/asset.bin") == "x");
}

TEST_CASE("VFS sandbox flushes at close and fsync but not before") {
    const TemporaryRoot root("flushpoints");
    auto store = SandboxStore::Open(root.path, kPackage);
    VirtualFileSystem vfs;
    vfs.AttachSandbox(*store, kWritableRoots);

    const auto descriptor = vfs.Open(
        "/sdcard/save.dat", {.read = true, .write = true, .create = true});
    const auto first = Bytes("one");
    CHECK(vfs.Write(descriptor, first) == first.size());
    // Nothing reached the host yet: the write is still an in-memory node.
    CHECK(store->UsedBytes() == 0);

    vfs.Flush(descriptor);  // fsync
    CHECK(store->UsedBytes() == 3);

    const auto second = Bytes("+two");
    CHECK(vfs.Write(descriptor, second) == second.size());
    CHECK(store->UsedBytes() == 3);
    vfs.Close(descriptor);  // close
    CHECK(store->UsedBytes() == 7);

    // Flushing a clean file is idempotent, not a rewrite.
    vfs.FlushAll();
    CHECK(store->UsedBytes() == 7);
}

TEST_CASE("VFS sandbox quota includes dirty bytes before close") {
    const TemporaryRoot root("dirty-quota");
    SandboxConfig config;
    config.byte_quota = 5;
    auto store = SandboxStore::Open(root.path, kPackage, config);
    VirtualFileSystem vfs;
    vfs.AttachSandbox(*store, kWritableRoots);

    const auto first = vfs.Open(
        "/sdcard/first", {.write = true, .create = true});
    CHECK(vfs.Write(first, Bytes("1234")) == 4);
    CHECK(store->UsedBytes() == 0);  // still dirty, but already quota-visible

    const auto second = vfs.Open(
        "/sdcard/second", {.write = true, .create = true});
    CHECK(ErrnoOf([&] { static_cast<void>(vfs.Write(second, Bytes("xx"))); }) ==
          28);
    vfs.Close(second);  // the empty creation itself is still valid
    vfs.Close(first);
    CHECK(store->UsedBytes() == 4);
}

TEST_CASE("VFS sandbox does not rewrite a clean write-opened base file") {
    const TemporaryRoot root("clean-open");
    const auto base = Bytes("base");
    const std::vector<VfsMountEntry> entries{{"save.dat", base}};
    auto store = SandboxStore::Open(root.path, kPackage);
    VirtualFileSystem vfs;
    vfs.Mount(VfsSource::external, "/sdcard", entries);
    vfs.AttachSandbox(*store, kWritableRoots);

    const auto descriptor =
        vfs.Open("/sdcard/save.dat", {.read = true, .write = true});
    vfs.Close(descriptor);
    CHECK(store->Entries().empty());
    CHECK(vfs.Stat("/sdcard/save.dat").source == VfsSource::external);
}

TEST_CASE("VFS sandbox persists rename and rmdir metadata immediately") {
    const TemporaryRoot root("metadata");
    {
        auto store = SandboxStore::Open(root.path, kPackage);
        VirtualFileSystem vfs;
        vfs.AttachSandbox(*store, kWritableRoots);
        vfs.CreateDirectory("/sdcard/tmpdir");
        WriteThrough(vfs, "/sdcard/old.sav", "body");
        vfs.Rename("/sdcard/old.sav", "/sdcard/new.sav");
        vfs.RemoveDirectory("/sdcard/tmpdir");
    }
    auto store = SandboxStore::Open(root.path, kPackage);
    VirtualFileSystem vfs;
    vfs.AttachSandbox(*store, kWritableRoots);
    CHECK(ReadAll(vfs, "/sdcard/new.sav") == "body");
    CHECK(ErrnoOf([&] { static_cast<void>(vfs.Stat("/sdcard/old.sav")); }) == 2);
    CHECK(ErrnoOf([&] { static_cast<void>(vfs.Stat("/sdcard/tmpdir")); }) == 2);
}

TEST_CASE("VFS rename over an open target orphans the replaced node") {
    const TemporaryRoot root("rename-orphan");
    auto store = SandboxStore::Open(root.path, kPackage);
    VirtualFileSystem vfs;
    vfs.AttachSandbox(*store, kWritableRoots);

    WriteThrough(vfs, "/sdcard/victim.sav", "stale");
    WriteThrough(vfs, "/sdcard/source.sav", "fresh");

    // The victim stays open across the rename, exactly the descriptor a
    // game still holds while rotating save slots.
    const auto held = vfs.Open("/sdcard/victim.sav", {.read = true,
                                                      .write = true});
    vfs.Rename("/sdcard/source.sav", "/sdcard/victim.sav");
    CHECK(ReadAll(vfs, "/sdcard/victim.sav") == "fresh");

    // Closing the orphan is a flush point, but it must not publish the
    // replaced node's stale bytes under the renamed name.
    vfs.Close(held);
    CHECK(ReadAll(vfs, "/sdcard/victim.sav") == "fresh");
    CHECK(Text(store->ReadFile("/sdcard/victim.sav")) == "fresh");
}

TEST_CASE("VFS without a sandbox keeps its in-memory behaviour") {
    VirtualFileSystem vfs;
    CHECK_FALSE(vfs.SandboxAttached());
    // No writable-namespace restriction and no persistence: exactly the
    // behaviour every existing test and the ephemeral mode rely on.
    WriteThrough(vfs, "/anywhere/at/all.dat", "fine");
    CHECK(ReadAll(vfs, "/anywhere/at/all.dat") == "fine");
    vfs.FlushAll();
}

TEST_CASE("VFS refuses a second sandbox attachment") {
    const TemporaryRoot root("double");
    auto first = SandboxStore::Open(root.path, kPackage);
    auto second = SandboxStore::Open(root.path, "com.example.other");
    VirtualFileSystem vfs;
    vfs.AttachSandbox(*first, kWritableRoots);
    CHECK(ErrnoOf([&] { vfs.AttachSandbox(*second, kWritableRoots); }) == 17);
    CHECK(ErrnoOf([&] { vfs.AttachSandbox(*second, {}); }) == 17);
}
