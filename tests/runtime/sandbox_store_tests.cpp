// Per-title persistent sandbox storage (SBX-1, ADR-0020).
// Everything here works in a test-owned temporary directory; no test ever
// touches a real user data directory.

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "ogplay/runtime/vfs/sandbox_store.h"
#include "ogplay/runtime/vfs/vfs.h"

namespace {

using namespace ogplay::runtime;

// Unique per test case so parallel CTest runs cannot collide.
struct TemporaryRoot final {
    std::filesystem::path path;

    explicit TemporaryRoot(const std::string& name)
        : path(std::filesystem::temp_directory_path() /
               ("ogplay-sandbox-" + name)) {
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

[[nodiscard]] std::vector<std::byte> Bytes(const std::string& text) {
    std::vector<std::byte> out;
    out.reserve(text.size());
    for (const auto character : text) {
        out.push_back(static_cast<std::byte>(character));
    }
    return out;
}

[[nodiscard]] std::string Text(const std::vector<std::byte>& bytes) {
    std::string out;
    out.reserve(bytes.size());
    for (const auto value : bytes) out.push_back(static_cast<char>(value));
    return out;
}

// Takes an lvalue on purpose: the returned pointer borrows from the caller's
// vector, so a temporary argument would dangle.
[[nodiscard]] const SandboxEntry* Find(const std::vector<SandboxEntry>& entries,
                                       const std::string& path) {
    const auto found = std::find_if(
        entries.begin(), entries.end(),
        [&path](const SandboxEntry& entry) { return entry.path == path; });
    return found == entries.end() ? nullptr : &*found;
}

constexpr const char* kPackage = "com.example.game";

}  // namespace

TEST_CASE("sandbox store round-trips its layout across two openings") {
    const TemporaryRoot root("roundtrip");
    {
        auto store = SandboxStore::Open(root.path, kPackage);
        store->WriteFileAtomic("/data/data/com.example.game/files/save0.dat",
                               Bytes("progress"));
        store->CreateDirectory("/sdcard/game/empty");
        store->WriteTombstone("/sdcard/game/deleted.dat");
        CHECK(store->UsedBytes() == 8);
    }

    // fs/ mirrors guest paths 1:1 so a user can find a save by hand.
    CHECK(std::filesystem::exists(
        root.path / kPackage / "fs" / "data" / "data" / "com.example.game" /
        "files" / "save0.dat"));

    auto reopened = SandboxStore::Open(root.path, kPackage);
    const auto entries = reopened->Entries();
    const auto* save = Find(entries, "/data/data/com.example.game/files/save0.dat");
    REQUIRE(save != nullptr);
    CHECK(save->size == 8);
    CHECK_FALSE(save->is_directory);
    const auto* empty = Find(entries, "/sdcard/game/empty");
    REQUIRE(empty != nullptr);
    CHECK(empty->is_directory);
    const auto* deleted = Find(entries, "/sdcard/game/deleted.dat");
    REQUIRE(deleted != nullptr);
    CHECK(deleted->is_tombstone);
    CHECK(Text(reopened->ReadFile(
              "/data/data/com.example.game/files/save0.dat")) == "progress");
    CHECK(reopened->UsedBytes() == 8);
    // Entries are ordered, so directory enumeration built on this is stable.
    CHECK(std::is_sorted(entries.begin(), entries.end(),
                         [](const SandboxEntry& left,
                            const SandboxEntry& right) {
                             return left.path < right.path;
                         }));
}

TEST_CASE("sandbox store replaces files atomically and clears crash residue") {
    const TemporaryRoot root("atomic");
    const auto target = root.path / kPackage / "fs" / "sdcard" / "save.dat";
    {
        auto store = SandboxStore::Open(root.path, kPackage);
        store->WriteFileAtomic("/sdcard/save.dat", Bytes("first"));
        store->WriteFileAtomic("/sdcard/save.dat", Bytes("second-longer"));
        CHECK(Text(store->ReadFile("/sdcard/save.dat")) == "second-longer");
        CHECK(store->UsedBytes() == 13);
        // No temporary survives a completed write.
        CHECK_FALSE(std::filesystem::exists(
            root.path / kPackage / "fs" / "sdcard" /
            "save.dat.__ogplay_tmp__"));
    }
    // Simulate a crash mid-write: the temporary is residue, the committed
    // file is intact.
    {
        std::ofstream residue(root.path / kPackage / "fs" / "sdcard" /
                              "save.dat.__ogplay_tmp__");
        residue << "half";
    }
    auto reopened = SandboxStore::Open(root.path, kPackage);
    CHECK(reopened->TemporaryFilesRemoved() == 1);
    CHECK(Text(reopened->ReadFile("/sdcard/save.dat")) == "second-longer");
    CHECK(std::filesystem::exists(target));
}

TEST_CASE("sandbox store escapes host-hostile names without losing bytes") {
    // Windows-illegal characters, the escape marker itself, a trailing dot
    // and a reserved device name all have to survive a round trip.
    const std::vector<std::string> segments{
        "plain.dat", "a:b", "q?mark", "star*", "pipe|", "quote\"",
        "less<greater>", "back\\slash", "percent%20", "trailing.",
        "trailing ", "con", "con.txt", "nul", "COM1", "tab\tchar"};
    for (const auto& segment : segments) {
        const auto escaped = SandboxStore::EscapeSegment(segment);
        CAPTURE(segment);
        CAPTURE(escaped);
        CHECK(SandboxStore::UnescapeSegment(escaped) == segment);
        // Nothing a host filesystem refuses may survive into the host name.
        CHECK(escaped.find('/') == std::string::npos);
        CHECK(escaped.find('\\') == std::string::npos);
        CHECK(escaped.find(':') == std::string::npos);
        CHECK(escaped.back() != '.');
        CHECK(escaped.back() != ' ');
    }
    // A reserved device name never reaches the host verbatim.
    CHECK(SandboxStore::EscapeSegment("con") != "con");
    CHECK(SandboxStore::EscapeSegment("console.dat") == "console.dat");
}

TEST_CASE("sandbox store persists escaped names across openings") {
    const TemporaryRoot root("escape");
    {
        auto store = SandboxStore::Open(root.path, kPackage);
        store->WriteFileAtomic("/sdcard/weird/a:b?c*/con", Bytes("x"));
    }
    auto reopened = SandboxStore::Open(root.path, kPackage);
    const auto entries = reopened->Entries();
    REQUIRE(Find(entries, "/sdcard/weird/a:b?c*/con") != nullptr);
    CHECK(Text(reopened->ReadFile("/sdcard/weird/a:b?c*/con")) == "x");
}

TEST_CASE("sandbox store refuses to escape its own root") {
    const TemporaryRoot root("traversal");
    auto store = SandboxStore::Open(root.path, kPackage);
    CHECK_THROWS_AS(store->WriteFileAtomic("/sdcard/../../escape", Bytes("x")),
                    VfsError);
    CHECK_THROWS_AS(store->WriteFileAtomic("relative/path", Bytes("x")),
                    VfsError);
    // The reserved suffixes are implementation names, not guest paths.
    CHECK_THROWS_AS(
        store->WriteFileAtomic("/sdcard/x.__ogplay_tombstone__", Bytes("x")),
        VfsError);
    CHECK_THROWS_AS(
        store->WriteFileAtomic("/sdcard/x.__ogplay_tmp__", Bytes("x")),
        VfsError);
}

TEST_CASE("sandbox store enforces byte and file quotas with -ENOSPC") {
    const TemporaryRoot root("quota");
    SandboxConfig config;
    config.byte_quota = 16;
    config.maximum_files = 3;
    auto store = SandboxStore::Open(root.path, kPackage, config);

    store->WriteFileAtomic("/sdcard/a", Bytes("0123456789"));
    // Rewriting the same path only counts its new size, not both.
    store->WriteFileAtomic("/sdcard/a", Bytes("0123456789ab"));
    CHECK(store->UsedBytes() == 12);
    try {
        store->WriteFileAtomic("/sdcard/b", Bytes("12345"));
        FAIL("quota overrun must not be accepted");
    } catch (const VfsError& error) {
        CHECK(error.ErrorNumber() == 28);  // -ENOSPC, like a full sdcard
    }
    CHECK(store->UsedBytes() == 12);

    store->WriteFileAtomic("/sdcard/b", Bytes("1234"));
    store->WriteFileAtomic("/sdcard/c", Bytes(""));
    try {
        store->WriteFileAtomic("/sdcard/d", Bytes(""));
        FAIL("file count overrun must not be accepted");
    } catch (const VfsError& error) {
        CHECK(error.ErrorNumber() == 28);
    }
}

TEST_CASE("sandbox tombstones do not consume the active file quota") {
    const TemporaryRoot root("tombstone-quota");
    SandboxConfig config;
    config.maximum_files = 1;
    auto store = SandboxStore::Open(root.path, kPackage, config);
    store->WriteFileAtomic("/sdcard/only", Bytes("x"));

    // Deleting a base-layer path must remain possible even while the
    // overlay's active-entry limit is full.
    CHECK_NOTHROW(store->WriteTombstone("/sdcard/base-only"));
    const auto entries = store->Entries();
    const auto* deleted = Find(entries, "/sdcard/base-only");
    REQUIRE(deleted != nullptr);
    CHECK(deleted->is_tombstone);
}

TEST_CASE("sandbox store tombstones shadow and then release a path") {
    const TemporaryRoot root("tombstone");
    auto store = SandboxStore::Open(root.path, kPackage);
    store->WriteFileAtomic("/sdcard/save.dat", Bytes("body"));
    CHECK(store->UsedBytes() == 4);

    store->WriteTombstone("/sdcard/save.dat");
    const auto shadowed_entries = store->Entries();
    const auto* shadowed = Find(shadowed_entries, "/sdcard/save.dat");
    REQUIRE(shadowed != nullptr);
    CHECK(shadowed->is_tombstone);
    // The overlay copy is gone, so nothing can read the old bytes back.
    CHECK_THROWS_AS(static_cast<void>(store->ReadFile("/sdcard/save.dat")),
                    VfsError);
    CHECK(store->UsedBytes() == 0);

    // Writing the path again lifts the tombstone.
    store->WriteFileAtomic("/sdcard/save.dat", Bytes("new"));
    const auto revived_entries = store->Entries();
    const auto* revived = Find(revived_entries, "/sdcard/save.dat");
    REQUIRE(revived != nullptr);
    CHECK_FALSE(revived->is_tombstone);
    CHECK_FALSE(std::filesystem::exists(
        root.path / kPackage / "fs" / "sdcard" /
        "save.dat.__ogplay_tombstone__"));
}

TEST_CASE("sandbox store removes only empty directories") {
    const TemporaryRoot root("remove");
    auto store = SandboxStore::Open(root.path, kPackage);
    store->CreateDirectory("/sdcard/dir");
    store->WriteFileAtomic("/sdcard/dir/file", Bytes("x"));
    try {
        store->Remove("/sdcard/dir");
        FAIL("a non-empty directory must not be removed");
    } catch (const VfsError& error) {
        CHECK(error.ErrorNumber() == 39);  // -ENOTEMPTY
    }
    store->Remove("/sdcard/dir/file");
    store->Remove("/sdcard/dir");
    CHECK(store->Entries().empty());
    CHECK_THROWS_AS(store->Remove("/sdcard/dir"), VfsError);
}

TEST_CASE("sandbox store rejects a meta.toml it did not write") {
    const TemporaryRoot root("meta");
    { const auto store = SandboxStore::Open(root.path, kPackage); }
    const auto meta = root.path / kPackage / "meta.toml";

    {
        std::ofstream output(meta, std::ios::trunc);
        output << "schema = 99\npackage = \"" << kPackage << "\"\n";
    }
    CHECK_THROWS_AS(static_cast<void>(SandboxStore::Open(root.path, kPackage)),
                    VfsError);

    {
        std::ofstream output(meta, std::ios::trunc);
        output << "schema = 1\npackage = \"other.package\"\n";
    }
    CHECK_THROWS_AS(static_cast<void>(SandboxStore::Open(root.path, kPackage)),
                    VfsError);

    {
        std::ofstream output(meta, std::ios::trunc);
        output << "schema = 1\npackage = \"" << kPackage
               << "\"\nfuture_key = 1\n";
    }
    CHECK_THROWS_AS(static_cast<void>(SandboxStore::Open(root.path, kPackage)),
                    VfsError);

    {
        std::ofstream output(meta, std::ios::trunc);
        output << "schema = not-a-number\npackage = \"" << kPackage << "\"\n";
    }
    CHECK_THROWS_AS(static_cast<void>(SandboxStore::Open(root.path, kPackage)),
                    VfsError);
}

TEST_CASE("sandbox store rejects ASCII case-folding conflicts on attach") {
    const TemporaryRoot root("case-conflict");
    { const auto store = SandboxStore::Open(root.path, kPackage); }
    const auto directory = root.path / kPackage / "fs" / "sdcard";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    REQUIRE_FALSE(error);
    // %53 decodes to uppercase S, so these host names are distinct even on
    // Windows but collapse to one guest path under the VFS ASCII rule.
    { std::ofstream(directory / "%53ave.dat") << "upper"; }
    { std::ofstream(directory / "save.dat") << "lower"; }

    CHECK_THROWS_AS(static_cast<void>(SandboxStore::Open(root.path, kPackage)),
                    VfsError);
}

TEST_CASE("sandbox store records the version code as a diagnostic fact") {
    const TemporaryRoot root("version");
    {
        auto store = SandboxStore::Open(root.path, kPackage);
        store->RecordVersionCode(132);
    }
    // Reopening accepts the recorded value: saves are shared across
    // versions, exactly like the platform keys them by package.
    auto reopened = SandboxStore::Open(root.path, kPackage);
    reopened->RecordVersionCode(133);
    CHECK(reopened->Package() == kPackage);
}

TEST_CASE("sandbox store rejects an unusable package key") {
    const TemporaryRoot root("package");
    CHECK_THROWS_AS(static_cast<void>(SandboxStore::Open(root.path, "")),
                    VfsError);
    CHECK_THROWS_AS(
        static_cast<void>(SandboxStore::Open(root.path, "../escape")),
        VfsError);
    CHECK_THROWS_AS(
        static_cast<void>(SandboxStore::Open(root.path, "with/slash")),
        VfsError);
    CHECK_THROWS_AS(
        static_cast<void>(SandboxStore::Open(root.path, ".leading")),
        VfsError);
}
