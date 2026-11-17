#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

#include "ogplay/runtime/syscall/syscall.h"
#include "ogplay/runtime/vfs/sandbox_store.h"
#include "ogplay/runtime/vfs/vfs.h"

namespace {

using namespace ogplay;
using namespace ogplay::runtime;
using memory::GuestAddress;

struct StatFsSyscallFixture final {
    static constexpr GuestAddress kPath{0x20000};
    static constexpr GuestAddress kOutput{0x20200};

    core::CapabilityLedger ledger;
    A32SyscallDispatcher dispatcher{CreateAndroidArmSyscallDispatcher(ledger)};
    VirtualFileSystem vfs;
    memory::AddressSpace memory;

    StatFsSyscallFixture() {
        memory.Map({kPath, memory.PageSize()}, memory::PageProtection::read | memory::PageProtection::write);
        BindAndroidFileSyscalls(dispatcher, vfs, memory);
        BindAndroidFileMetadataSyscalls(dispatcher, vfs, memory);
    }

    template <std::size_t Size>
    void WritePath(const char (&path)[Size]) {
        memory.Write(kPath, std::as_bytes(std::span{path}));
    }

    std::int32_t Call(std::uint32_t number, std::uint32_t argument, std::uint32_t size, GuestAddress output = kOutput) {
        A32SyscallFrame frame;
        frame.number = number;
        frame.thread_id = 1;
        frame.arguments = {argument, size, output.Value(), 0, 0, 0};
        return dispatcher.Dispatch(frame);
    }
};

std::uint64_t ReadField(std::span<const std::byte> bytes, std::size_t offset, std::size_t width) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < width; ++i) {
        value |= std::to_integer<std::uint64_t>(bytes[offset + i]) << (i * 8U);
    }
    return value;
}

struct StatFsTestRoot final {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ogplay-syscall-statfs";

    StatFsTestRoot() {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }

    ~StatFsTestRoot() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

}  // namespace

TEST_CASE("Android ARM statfs64 encodes quota and fstatfs64 returns the same 84 bytes") {
    const StatFsTestRoot root;
    constexpr auto blocks = (UINT64_C(1) << 32U) + 17;
    auto store =
        SandboxStore::Open(root.path, "fixture", "fixture", {.byte_quota = blocks * 4096, .maximum_files = 37});
    StatFsSyscallFixture fixture;
    constexpr auto path = "/sdcard/save";
    fixture.vfs.AttachSandbox(*store, std::array{std::string{"/sdcard"}});
    const auto fd = fixture.vfs.Open(path, {.read = true, .write = true, .create = true});
    REQUIRE(fixture.vfs.Write(fd, std::array{std::byte{'a'}}) == 1);
    const auto stats = fixture.vfs.StatVfs(path);
    fixture.WritePath("/sdcard/save");

    std::array<std::byte, 88> sentinel;
    sentinel.fill(std::byte{0xab});
    std::array<std::byte, 88> result;
    for (const auto size : {88U, 84U}) {
        CAPTURE(size);
        fixture.memory.Write(fixture.kOutput, sentinel);
        REQUIRE(fixture.Call(266, fixture.kPath.Value(), size) == 0);
        fixture.memory.Read(fixture.kOutput, result);
        CHECK(ReadField(result, 0, 4) == 0x65735546);
        CHECK(ReadField(result, 4, 4) == 4096);
        CHECK(ReadField(result, 8, 8) == blocks);
        CHECK(ReadField(result, 16, 8) == blocks - 1);
        CHECK(ReadField(result, 24, 8) == blocks - 1);
        CHECK(ReadField(result, 32, 8) == 37);
        CHECK(ReadField(result, 40, 8) == 36);
        CHECK(ReadField(result, 48, 4) == (stats.values[8] & UINT32_MAX));
        CHECK(ReadField(result, 52, 4) == (stats.values[8] >> 32U));
        CHECK(ReadField(result, 56, 4) == SandboxStore::MaximumEncodedNameBytes());
        CHECK(ReadField(result, 60, 4) == 4096);
        CHECK(ReadField(result, 64, 4) == 0);
        CHECK(ReadField(result, 68, 8) == 0);
        CHECK(ReadField(result, 76, 8) == 0);
        CHECK(ReadField(result, 84, 4) == 0xabababab);

        fixture.memory.Write(fixture.kOutput, sentinel);
        REQUIRE(fixture.Call(267, static_cast<std::uint32_t>(fd), size) == 0);
        std::array<std::byte, 88> fd_result;
        fixture.memory.Read(fixture.kOutput, fd_result);
        CHECK(fd_result == result);

        // The bionic-sized request also succeeds when only 84 bytes are mapped.
        const auto page_end = fixture.kPath.Add(fixture.memory.PageSize() - 84);
        CHECK(fixture.Call(266, fixture.kPath.Value(), size, page_end) == 0);
    }
    fixture.vfs.Close(fd);
    CHECK(fixture.ledger.Unimplemented().empty());
}

TEST_CASE("Android ARM statfs64 reports errno and fstatfs64 recognizes pipes") {
    StatFsSyscallFixture fixture;
    fixture.vfs.PutFile("/system/readonly", {}, false);
    fixture.WritePath("/system/readonly");
    REQUIRE(fixture.Call(266, fixture.kPath.Value(), 88) == 0);
    std::array<std::byte, 84> result;
    fixture.memory.Read(fixture.kOutput, result);
    CHECK(ReadField(result, 0, 4) == 0xEF53);
    CHECK(ReadField(result, 64, 4) == 1);
    CHECK(fixture.Call(266, fixture.kPath.Value(), 64) == -22);
    const auto short_buffer = fixture.kPath.Add(fixture.memory.PageSize() - 83);
    std::array<std::byte, 83> sentinel;
    sentinel.fill(std::byte{0xab});
    fixture.memory.Write(short_buffer, sentinel);
    CHECK(fixture.Call(266, fixture.kPath.Value(), 88, short_buffer) == -14);

    const auto pipe = fixture.vfs.CreatePipe();
    const auto fd = static_cast<std::uint32_t>(pipe.read_descriptor);
    CHECK(fixture.Call(267, fd, 64) == -22);
    CHECK(fixture.Call(267, fd, 84, short_buffer) == -14);
    std::array<std::byte, 83> untouched;
    fixture.memory.Read(short_buffer, untouched);
    CHECK(untouched == sentinel);
    REQUIRE(fixture.Call(267, fd, 84) == 0);
    fixture.memory.Read(fixture.kOutput, result);
    CHECK(ReadField(result, 0, 4) == 0x50495045);
    for (std::size_t offset = 8; offset < 48; offset += 8)
        CHECK(ReadField(result, offset, 8) == 0);
    fixture.vfs.Close(pipe.read_descriptor);
    fixture.vfs.Close(pipe.write_descriptor);
    CHECK(fixture.Call(267, fd, 84) == -9);
    CHECK(fixture.Call(267, UINT32_MAX, 84) == -9);
    fixture.WritePath("/system/missing");
    CHECK(fixture.Call(266, fixture.kPath.Value(), 84) == -2);
    CHECK(fixture.ledger.Unimplemented().empty());
}
