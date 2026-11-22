#include <doctest/doctest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <future>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "ogplay/runtime/vfs/vfs.h"

namespace {

using ogplay::runtime::VfsError;
using ogplay::runtime::VfsThreadBinding;
using ogplay::runtime::VirtualFileSystem;

[[nodiscard]] std::span<const std::byte> Bytes(const std::string& text) {
    return std::as_bytes(std::span{text.data(), text.size()});
}

[[nodiscard]] std::string ReadText(VirtualFileSystem& vfs, const std::int32_t descriptor, const std::size_t size) {
    std::string result(size, '\0');
    const auto count = vfs.Read(descriptor, std::as_writable_bytes(std::span{result.data(), result.size()}));
    result.resize(count);
    return result;
}

[[nodiscard]] std::int32_t ErrorOf(const auto& operation) {
    try {
        operation();
    } catch (const VfsError& error) {
        return error.ErrorNumber();
    }
    return 0;
}

}  // namespace

TEST_CASE("VFS forked descriptor tables share open states but not numbers") {
    VirtualFileSystem vfs;
    vfs.PutFile("/data/a.txt", Bytes("abcdef"), true);
    const auto shared = vfs.Open("/data/a.txt", {.read = true});
    const auto marked = vfs.Open("/data/a.txt", {.read = true});
    vfs.SetCloseOnExec(marked, true);
    const auto child = vfs.ForkDescriptorTable(ogplay::runtime::kVfsApplicationDescriptorTable);
    {
        VfsThreadBinding binding(vfs, child);
        CHECK(vfs.CurrentDescriptorTable() == child);
        CHECK(vfs.CloseOnExec(marked));
        // Same open description: the offset moves for both tables.
        CHECK(ReadText(vfs, shared, 2) == "ab");
        vfs.Close(shared);
        CHECK(ErrorOf([&] { static_cast<void>(ReadText(vfs, shared, 1)); }) == 9);
        const auto reopened = vfs.Open("/data/a.txt", {.read = true});
        CHECK(reopened == shared);
        vfs.CloseOnExecDescriptors();
        CHECK(ErrorOf([&] { static_cast<void>(vfs.CloseOnExec(marked)); }) == 9);
    }
    CHECK(vfs.CurrentDescriptorTable() == ogplay::runtime::kVfsApplicationDescriptorTable);
    CHECK(ReadText(vfs, shared, 2) == "cd");
    CHECK(vfs.CloseOnExec(marked));
    vfs.ReleaseDescriptorTable(child);
    CHECK(ErrorOf([&] { vfs.ReleaseDescriptorTable(child); }) == 9);
    CHECK(ErrorOf([&] { vfs.ReleaseDescriptorTable(ogplay::runtime::kVfsApplicationDescriptorTable); }) == 22);
    {
        VfsThreadBinding stale(vfs, child);
        CHECK(ErrorOf([&] { static_cast<void>(ReadText(vfs, shared, 1)); }) == 9);
    }
}

TEST_CASE("VFS dup2 replaces the target and keeps close-on-exec per number") {
    VirtualFileSystem vfs;
    vfs.PutFile("/data/a.txt", Bytes("first"), true);
    vfs.PutFile("/data/b.txt", Bytes("second"), true);
    const auto a = vfs.Open("/data/a.txt", {.read = true});
    const auto b = vfs.Open("/data/b.txt", {.read = true});
    vfs.SetCloseOnExec(b, true);
    vfs.DuplicateTo(a, b);
    CHECK_FALSE(vfs.CloseOnExec(b));
    CHECK(ReadText(vfs, b, 5) == "first");
    vfs.DuplicateTo(a, 40, true);
    CHECK(vfs.CloseOnExec(40));
    vfs.DuplicateTo(a, a);
    CHECK(ErrorOf([&] { vfs.DuplicateTo(99, 41); }) == 9);
    CHECK(ErrorOf([&] { vfs.DuplicateTo(a, -1); }) == 9);
}

TEST_CASE("VFS stream endpoints follow dup close and fork without file metadata") {
    VirtualFileSystem vfs;
    std::string written;
    auto endpoint = std::make_shared<ogplay::runtime::VfsStreamEndpoint>();
    endpoint->name = "stdout";
    endpoint->kind = 7;
    endpoint->writable = true;
    endpoint->write = [&written](const std::span<const std::byte> bytes) {
        written.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    };
    vfs.InstallStreamEndpoint(1, endpoint);
    CHECK(vfs.Write(1, Bytes("hi")) == 2);
    const auto copy = vfs.Duplicate(1);
    CHECK(vfs.StreamEndpoint(copy)->kind == 7);
    CHECK(vfs.DescriptorStatus(copy).write);
    CHECK(ErrorOf([&] { static_cast<void>(vfs.DescriptorInfo(1)); }) == 9);
    CHECK(ErrorOf([&] { static_cast<void>(vfs.Seek(1, 0, ogplay::runtime::VfsSeekWhence::begin)); }) == 29);
    CHECK(ErrorOf([&] { static_cast<void>(ReadText(vfs, 1, 1)); }) == 9);
    const auto child = vfs.ForkDescriptorTable(ogplay::runtime::kVfsApplicationDescriptorTable);
    {
        VfsThreadBinding binding(vfs, child);
        CHECK(vfs.Write(1, Bytes("!")) == 1);
    }
    vfs.ReleaseDescriptorTable(child);
    vfs.Close(1);
    CHECK(vfs.StreamEndpoint(1) == nullptr);
    CHECK(vfs.Write(copy, Bytes("?")) == 1);
    CHECK(written == "hi!?");
}

TEST_CASE("VFS pipe read blocks until data or the last writer retires") {
    VirtualFileSystem vfs;
    const auto pipe = vfs.CreatePipe();
    const auto child = vfs.ForkDescriptorTable(ogplay::runtime::kVfsApplicationDescriptorTable);
    std::promise<void> started;
    auto reader = std::async(std::launch::async, [&] {
        started.set_value();
        return ReadText(vfs, pipe.read_descriptor, 16);
    });
    started.get_future().wait();
    CHECK(reader.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout);
    CHECK(vfs.Write(pipe.write_descriptor, Bytes("ps")) == 2);
    CHECK(reader.get() == "ps");

    // The application closes its write end; the forked table still holds one.
    vfs.Close(pipe.write_descriptor);
    auto eof = std::async(std::launch::async, [&] { return ReadText(vfs, pipe.read_descriptor, 16); });
    CHECK(eof.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout);
    {
        VfsThreadBinding binding(vfs, child);
        CHECK(vfs.Write(pipe.write_descriptor, Bytes("x")) == 1);
    }
    CHECK(eof.get() == "x");
    vfs.ReleaseDescriptorTable(child);
    CHECK(ReadText(vfs, pipe.read_descriptor, 16).empty());
    CHECK(vfs.PipePollEvents(pipe.read_descriptor) == 9U);
}

TEST_CASE("VFS pipe writes fail with EPIPE without readers and block when full") {
    VirtualFileSystem vfs;
    const auto pipe = vfs.CreatePipe();
    CHECK(vfs.PipePollEvents(pipe.write_descriptor) == 2U);
    std::vector<std::byte> full(ogplay::runtime::kVfsPipeCapacity, std::byte{0x5a});
    CHECK(vfs.Write(pipe.write_descriptor, full) == full.size());
    CHECK(vfs.PipePollEvents(pipe.write_descriptor) == 0U);
    auto blocked = std::async(std::launch::async, [&] { return vfs.Write(pipe.write_descriptor, Bytes("tail")); });
    CHECK(blocked.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout);
    std::vector<std::byte> drained(4096);
    CHECK(vfs.Read(pipe.read_descriptor, drained) == drained.size());
    CHECK(blocked.get() == 4U);
    vfs.Close(pipe.read_descriptor);
    CHECK(vfs.PipePollEvents(pipe.write_descriptor) == 4U);
    CHECK(ErrorOf([&] { static_cast<void>(vfs.Write(pipe.write_descriptor, Bytes("x"))); }) == 32);
}

TEST_CASE("VFS blocked pipe IO observes the thread binding interruption") {
    VirtualFileSystem vfs;
    const auto pipe = vfs.CreatePipe();
    std::atomic_bool interrupt{false};
    auto reader = std::async(std::launch::async, [&] {
        VfsThreadBinding binding(vfs, ogplay::runtime::kVfsApplicationDescriptorTable,
                                 [&interrupt] { return interrupt.load(); });
        return ErrorOf([&] { static_cast<void>(ReadText(vfs, pipe.read_descriptor, 1)); });
    });
    CHECK(reader.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout);
    interrupt = true;
    CHECK(reader.get() == 4);
}
