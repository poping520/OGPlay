#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/runtime/dexvm/guest_memory.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"
#include "ogplay/runtime/process/virtual_process.h"
#include "ogplay/runtime/syscall/syscall.h"
#include "ogplay/runtime/vfs/vfs.h"

namespace {

using ogplay::runtime::kVfsApplicationDescriptorTable;
using ogplay::runtime::VirtualProcessError;
using ogplay::runtime::VirtualProcessKernel;

struct KernelFixture final {
    ogplay::core::CapabilityLedger ledger;
    ogplay::runtime::VirtualFileSystem vfs;
    VirtualProcessKernel kernel{
        {.vfs = &vfs, .ledger = &ledger, .application_name = "com.example.game", .application_memory = [] {
             return ogplay::runtime::VirtualProcessMemory{64U << 20, 16U << 20};
         }}};

    KernelFixture() {
        ogplay::runtime::RegisterBuiltinCommands(kernel);
        const std::string keep = "x";
        vfs.PutFile("/data/local/tmp/.keep", std::as_bytes(std::span{keep.data(), keep.size()}), true);
    }

    struct Result final {
        std::int32_t status{};
        std::string output;
        std::string errors;
    };

    // Runs `path argv` as a child of the application with stdout and stderr
    // captured through pipes, then reaps it.
    [[nodiscard]] Result Run(const std::string& path, std::vector<std::string> arguments) {
        const auto out = vfs.CreatePipe();
        const auto err = vfs.CreatePipe();
        const auto pid = kernel.Spawn(kernel.ApplicationPid(), kVfsApplicationDescriptorTable, path,
                                      std::move(arguments), {"PATH=/system/bin"}, "/data/local/tmp", [&] {
                                          vfs.DuplicateTo(out.write_descriptor, 1);
                                          vfs.DuplicateTo(err.write_descriptor, 2);
                                          for (const auto fd : {out.read_descriptor, out.write_descriptor,
                                                                err.read_descriptor, err.write_descriptor})
                                              vfs.Close(fd);
                                      });
        vfs.Close(out.write_descriptor);
        vfs.Close(err.write_descriptor);
        Result result;
        result.output = Drain(out.read_descriptor);
        result.errors = Drain(err.read_descriptor);
        const auto waited = kernel.Wait(kernel.ApplicationPid(), pid, false);
        CHECK(waited.pid == pid);
        result.status = waited.status;
        return result;
    }

    [[nodiscard]] std::string Drain(const std::int32_t descriptor) {
        std::string text;
        std::array<std::byte, 512> buffer{};
        for (;;) {
            const auto count = vfs.Read(descriptor, buffer);
            if (count == 0) break;
            text.append(reinterpret_cast<const char*>(buffer.data()), count);
        }
        vfs.Close(descriptor);
        return text;
    }

    [[nodiscard]] std::string ReadFile(const std::string& path) {
        const auto descriptor = vfs.Open(path, {.read = true});
        return Drain(descriptor);
    }
};

[[nodiscard]] std::int32_t ErrorOf(const auto& operation) {
    try {
        operation();
    } catch (const VirtualProcessError& error) {
        return error.ErrorNumber();
    }
    return 0;
}

}  // namespace

TEST_CASE("virtual process ps prints the toolbox table of live processes") {
    KernelFixture fixture;
    const auto result = fixture.Run("/system/bin/ps", {"ps"});
    CHECK(result.status == 0);
    CHECK(result.errors.empty());
    CHECK(result.output.starts_with("USER     PID   PPID  VSIZE  RSS     WCHAN    PC         NAME\n"));
    CHECK(result.output.find("root      1     0     708    360   00000000 00000000 S /init\n") != std::string::npos);
    CHECK(result.output.find("u0_a0     1000  1     65536  16384 00000000 00000000 S com.example.game\n") !=
          std::string::npos);
    CHECK(result.output.find(" R ps\n") != std::string::npos);

    const auto filtered = fixture.Run("/system/bin/toolbox", {"toolbox", "ps", "com.example"});
    CHECK(filtered.status == 0);
    CHECK(filtered.output.find("/init") == std::string::npos);
    CHECK(filtered.output.find("com.example.game") != std::string::npos);
    CHECK(fixture.Run("/system/bin/toolbox", {"toolbox", "ls"}).status == (255 << 8));
}

TEST_CASE("virtual process sh -c runs redirections lists and reports missing commands") {
    KernelFixture fixture;
    const auto redirected = fixture.Run("/system/bin/sh", {"sh", "-c", "ps > /data/local/tmp/temp"});
    CHECK(redirected.status == 0);
    CHECK(redirected.output.empty());
    const auto file = fixture.ReadFile("/data/local/tmp/temp");
    CHECK(file.starts_with("USER "));
    CHECK(file.find(" S com.example.game\n") != std::string::npos);
    // The shell and its ps child were both alive while ps ran.
    CHECK(file.find(" S sh\n") != std::string::npos);

    const auto relative = fixture.Run("/system/bin/sh", {"sh", "-c", "echo one >out; echo two >>out"});
    CHECK(relative.status == 0);
    CHECK(fixture.ReadFile("/data/local/tmp/out") == "one\ntwo\n");

    const auto list = fixture.Run("/system/bin/sh", {"sh", "-c", "X='a b'; echo \"$X\" && false || echo $? ok"});
    CHECK(list.errors == "");
    CHECK(list.output == "a b\n1 ok\n");

    const auto missing = fixture.Run("/system/bin/sh", {"sh", "-c", "su -c id"});
    CHECK(missing.status == (127 << 8));
    CHECK(missing.errors == "sh: su: not found\n");

    const auto pipeline = fixture.Run("/system/bin/sh", {"sh", "-c", "echo hidden | echo shown 2>&1; exit 3"});
    CHECK(pipeline.output == "shown\n");
    CHECK(pipeline.status == (3 << 8));

    const auto unsupported = fixture.Run("/system/bin/sh", {"sh", "-c", "echo $(id)"});
    CHECK(unsupported.status == (2 << 8));
    CHECK(unsupported.errors.find("syntax error") != std::string::npos);

    const auto blocked = fixture.Run("/system/bin/sh", {"sh", "-c", "echo x > /system/bin/x"});
    CHECK(blocked.status == (1 << 8));
    CHECK(blocked.errors.find("can't create /system/bin/x") != std::string::npos);
}

TEST_CASE("virtual process fork exec exit and wait follow Linux statuses") {
    KernelFixture fixture;
    auto& kernel = fixture.kernel;
    const auto app = kernel.ApplicationPid();
    CHECK(ErrorOf([&] { static_cast<void>(kernel.Wait(app, -1, false)); }) == 10);
    CHECK(kernel.ResolveExecutable("ps", ogplay::runtime::kVirtualDefaultPath) == "/system/bin/ps");
    CHECK_FALSE(kernel.ResolveExecutable("su", ogplay::runtime::kVirtualDefaultPath));

    const auto child = kernel.Fork(app, kVfsApplicationDescriptorTable);
    CHECK(child >= 2000);
    CHECK(kernel.ParentOf(child) == app);
    CHECK(ErrorOf([&] { kernel.Exec(child, "/system/xbin/su", {"su"}, {}, "/"); }) == 2);
    CHECK(kernel.Wait(app, child, true).pid == 0);
    kernel.Exit(child, 127);
    const auto exited = kernel.Wait(app, -1, false);
    CHECK(exited.pid == child);
    CHECK(exited.status == (127 << 8));
    CHECK_FALSE(kernel.Exists(child));

    // A command blocked on its empty stdin pipe is stopped by SIGTERM.
    const auto input = fixture.vfs.CreatePipe();
    const auto reader = kernel.SpawnCommand(
        app, kVfsApplicationDescriptorTable,
        [](ogplay::runtime::VirtualCommandContext& context) {
            std::array<std::byte, 1> byte{};
            static_cast<void>(context.Read(0, byte));
            return 0;
        },
        {"reader"}, {}, "/",
        [&] {
            fixture.vfs.DuplicateTo(input.read_descriptor, 0);
            fixture.vfs.Close(input.read_descriptor);
            fixture.vfs.Close(input.write_descriptor);
        });
    CHECK(kernel.Wait(app, reader, true).pid == 0);
    CHECK(ErrorOf([&] { kernel.Kill(1, 15); }) == 1);
    CHECK(ErrorOf([&] { kernel.Kill(31000, 0); }) == 3);
    kernel.Kill(reader, 0);
    kernel.Kill(reader, 17);
    kernel.Kill(reader, 15);
    const auto killed = kernel.Wait(app, reader, false);
    CHECK(killed.status == 15);
    std::atomic_bool interrupt{true};
    const auto again = kernel.Fork(app, kVfsApplicationDescriptorTable);
    CHECK(ErrorOf([&] { static_cast<void>(kernel.Wait(app, again, false, [&] { return interrupt.load(); })); }) == 4);
    kernel.Exit(again, 0);
    CHECK(kernel.Wait(app, again, false).status == 0);
}

namespace {

[[nodiscard]] std::vector<std::vector<std::byte>>
LoadGuestLibc(std::vector<ogplay::loader::Elf32ModuleInput>& modules) {
    std::vector<std::vector<std::byte>> images;
    images.reserve(2);
    for (const auto* name : {"libc.so", "libdl.so"}) {
        std::ifstream input(std::string(OGPLAY_SOURCE_DIR) + "/data/android/19/lib/" + name, std::ios::binary);
        REQUIRE(input.good());
        const std::vector<char> bytes{std::istreambuf_iterator<char>(input), {}};
        auto& image = images.emplace_back(bytes.size());
        std::ranges::transform(bytes, image.begin(), [](const char value) { return static_cast<std::byte>(value); });
    }
    for (std::size_t index = 0; index < images.size(); ++index)
        modules.push_back(
            {index == 0 ? "libc.so" : "libdl.so", images[index],
             ogplay::memory::GuestAddress{0x10000000U + static_cast<std::uint32_t>(index) * 0x10000000U}});
    return images;
}

}  // namespace

TEST_CASE("virtual process real API19 libc system and popen run built-in commands") {
    std::vector<ogplay::loader::Elf32ModuleInput> modules;
    const auto images = LoadGuestLibc(modules);
    ogplay::runtime::VirtualFileSystem fs;
    const std::string keep = "x";
    fs.PutFile("/data/local/tmp/.keep", std::as_bytes(std::span{keep.data(), keep.size()}), true);
    auto process = ogplay::runtime::AndroidGuestProcess::Start({19, modules, {}, 64, 36, 1000000, 1, &fs, {}});
    process->VirtualProcesses().SetApplicationName("com.example.game");
    const auto call = [&](std::string_view name, std::array<std::uint32_t, 4> args) {
        return process->Invoke({process->FindModuleExport(0, name), args}).return_value;
    };
    const ogplay::memory::GuestAddress scratch{call("malloc", {4096})};
    REQUIRE(scratch.Value() != 0);
    const auto put = [&](const std::uint32_t offset, const std::string& text) {
        process->GuestMemoryAccess().write(scratch.Add(offset),
                                           std::as_bytes(std::span{text.c_str(), text.size() + 1}));
        return scratch.Add(offset).Value();
    };

    CHECK(call("system", {put(0, "ps > /data/local/tmp/temp"), 0}) == 0);
    const auto descriptor = fs.Open("/data/local/tmp/temp", {.read = true});
    std::string listing(4096, '\0');
    listing.resize(fs.Read(descriptor, std::as_writable_bytes(std::span{listing.data(), listing.size()})));
    fs.Close(descriptor);
    CHECK(listing.starts_with("USER     PID   PPID  VSIZE  RSS     WCHAN    PC         NAME\n"));
    CHECK(listing.find(" 1000  1     ") != std::string::npos);
    CHECK(listing.find(" S com.example.game\n") != std::string::npos);
    CHECK(listing.find(" R ps\n") != std::string::npos);

    CHECK(call("system", {put(0, "nosuchcmd"), 0}) == (127U << 8U));
    CHECK(call("system", {put(0, "exit 5"), 0}) == (5U << 8U));

    // popen reads the child's stdout through a pipe; pclose reaps it.
    const auto stream = call("popen", {put(0, "ps com.example"), put(64, "r")});
    REQUIRE(stream != 0);
    const auto count = call("fread", {scratch.Add(256).Value(), 1, 2048, stream});
    std::string output(count, '\0');
    process->GuestMemoryAccess().read(scratch.Add(256),
                                      std::as_writable_bytes(std::span{output.data(), output.size()}));
    CHECK(output.starts_with("USER "));
    CHECK(output.find(" S com.example.game\n") != std::string::npos);
    CHECK(output.find("/init") == std::string::npos);
    CHECK(call("pclose", {stream}) == 0);
    // Every child was reaped: nothing is left to wait for.
    CHECK(process->VirtualProcesses().Processes().size() == 2U);

    static_cast<void>(call("free", {scratch.Value()}));
    process->Stop();
}

TEST_CASE("virtual process real API19 libc fork keeps the parent stack across the child's exec") {
    std::vector<ogplay::loader::Elf32ModuleInput> modules;
    const auto images = LoadGuestLibc(modules);
    ogplay::runtime::VirtualFileSystem fs;
    auto process = ogplay::runtime::AndroidGuestProcess::Start({19, modules, {}, 64, 36, 1000000, 1, &fs, {}});
    const auto call = [&](std::string_view name, std::array<std::uint32_t, 4> args) {
        return process->Invoke({process->FindModuleExport(0, name), args}).return_value;
    };
    const auto put32 = [&](const ogplay::memory::GuestAddress address, const std::uint32_t value) {
        const std::array bytes{static_cast<std::byte>(value & 0xffU), static_cast<std::byte>((value >> 8U) & 0xffU),
                               static_cast<std::byte>((value >> 16U) & 0xffU),
                               static_cast<std::byte>((value >> 24U) & 0xffU)};
        process->GuestMemoryAccess().write(address, bytes);
    };
    const ogplay::memory::GuestAddress code{call("memalign", {4096, 4096})};
    REQUIRE(code.Value() != 0);
    REQUIRE(call("mprotect", {code.Value(), 4096, 7}) == 0);
    // spawn(argv, path): the child returns out of libc fork() and runs
    // execvp, whose frame lands where fork() saved its return address.
    const std::array<std::uint32_t, 17> spawn{0xe92d4030U,  // push {r4, r5, lr}
                                              0xe1a04000U,  // mov r4, r0
                                              0xe1a05001U,  // mov r5, r1
                                              0xe59f3024U,  // ldr r3, =fork
                                              0xe12fff33U,  // blx r3
                                              0xe3500000U,  // cmp r0, #0
                                              0x18bd8030U,  // popne {r4, r5, pc}
                                              0xe1a00005U,  // mov r0, r5
                                              0xe1a01004U,  // mov r1, r4
                                              0xe59f3010U,  // ldr r3, =execvp
                                              0xe12fff33U,  // blx r3
                                              0xe3a0007fU,  // mov r0, #127
                                              0xe59f3008U,  // ldr r3, =_exit
                                              0xe12fff33U,  // blx r3
                                              process->FindModuleExport(0, "fork").Value(),
                                              process->FindModuleExport(0, "execvp").Value(),
                                              process->FindModuleExport(0, "_exit").Value()};
    for (std::size_t index = 0; index < spawn.size(); ++index)
        put32(code.Add(index * 4U), spawn[index]);
    const auto data = code.Add(0x100);
    const auto put = [&](const std::uint32_t offset, const std::string& text) {
        process->GuestMemoryAccess().write(data.Add(offset), std::as_bytes(std::span{text.c_str(), text.size() + 1}));
        return data.Add(offset).Value();
    };
    put32(data, put(0x40, "sh"));
    put32(data.Add(4), put(0x48, "-c"));
    put32(data.Add(8), put(0x50, "exit 7"));
    put32(data.Add(12), 0);

    const auto child = std::bit_cast<std::int32_t>(
        process->Invoke({code, {data.Value(), put(0x60, "/system/bin/sh"), 0, 0}}).return_value);
    CHECK(child >= 2000);
    CHECK(call("waitpid", {static_cast<std::uint32_t>(child), data.Add(0x80).Value(), 0, 0}) ==
          static_cast<std::uint32_t>(child));
    std::array<std::byte, 4> status{};
    process->GuestMemoryAccess().read(data.Add(0x80), status);
    CHECK(std::to_integer<std::uint32_t>(status[1]) == 7U);
    CHECK(process->VirtualProcesses().Processes().size() == 2U);
    process->Stop();
}

TEST_CASE("virtual process vfork child mode resumes the parent on exit and kill reaches children") {
    ogplay::core::CapabilityLedger ledger;
    auto dispatcher = ogplay::runtime::CreateAndroidArmSyscallDispatcher(ledger);
    ogplay::runtime::VirtualFileSystem vfs;
    ogplay::memory::AddressSpace memory;
    memory.Map({ogplay::memory::GuestAddress{0x10000}, memory.PageSize()},
               ogplay::memory::PageProtection::read | ogplay::memory::PageProtection::write);
    ogplay::runtime::BindAndroidFileSyscalls(dispatcher, vfs, memory);
    // Stand-ins for the thread lifecycle exit handlers the binding decorates.
    dispatcher.Implement(1, [](const ogplay::runtime::A32SyscallFrame&) { return -99; });
    dispatcher.Implement(248, [](const ogplay::runtime::A32SyscallFrame&) { return -98; });
    VirtualProcessKernel kernel({.vfs = &vfs, .ledger = &ledger, .application_name = "game"});
    ogplay::runtime::RegisterBuiltinCommands(kernel);
    ogplay::runtime::BindAndroidProcessSpawnSyscalls(dispatcher, memory, kernel);
    ogplay::cpu::A32State parent;
    parent.SetRegister(ogplay::cpu::CoreRegister::pc, 0x4000);
    parent.SetRegister(ogplay::cpu::CoreRegister::r4, 0x1234);
    const auto call = [&](std::uint32_t number, std::array<std::uint32_t, 7> args) {
        ogplay::runtime::A32SyscallFrame frame;
        frame.number = number;
        frame.arguments = args;
        frame.thread_id = 7;
        frame.cpu_state = parent;
        return dispatcher.DispatchOutcome(frame);
    };
    CHECK(call(1, {0}).return_value == -99);
    CHECK(call(2, {}).return_value == 0);  // fork(2) behaves as vfork
    const auto child = call(20, {}).return_value;
    CHECK(child >= 2000);
    CHECK(call(64, {}).return_value == 1000);
    CHECK(vfs.CurrentDescriptorTable() == ogplay::runtime::kVfsApplicationDescriptorTable);
    CHECK(call(11, {0}).return_value == -14);  // bad path pointer: the child keeps running
    const auto resumed = call(248, {3});
    REQUIRE(resumed.restored_state.has_value());
    CHECK(resumed.restored_state->Register(ogplay::cpu::CoreRegister::r0) == static_cast<std::uint32_t>(child));
    CHECK(resumed.restored_state->Register(ogplay::cpu::CoreRegister::pc) == 0x4000);
    CHECK(resumed.restored_state->Register(ogplay::cpu::CoreRegister::r4) == 0x1234);
    CHECK(call(20, {}).return_value == 1000);
    CHECK(call(114, {static_cast<std::uint32_t>(child), 0x10000, 0, 0x10010}).return_value == child);
    CHECK(memory.Read32(ogplay::memory::GuestAddress{0x10000}) == (3U << 8U));
    CHECK(call(114, {0xffffffffU, 0, 0, 0}).return_value == -10);
    CHECK(std::ranges::any_of(ledger.Unimplemented(),
                              [](const auto& hit) { return hit.id == "syscall.fork.shared_memory"; }));
    CHECK(call(37, {1, 15}).return_value == -1);
    CHECK(call(37, {31000, 0}).return_value == -3);
    CHECK(call(37, {0xffffffffU, 9}).return_value == -1);
    CHECK(call(11, {0x10000, 0, 0}).return_value == -38);  // execve without a child is unsupported
}

TEST_CASE("virtual process getprop prints the shared system property set") {
    KernelFixture fixture;
    std::atomic_int reads{};
    fixture.kernel.SetSystemPropertySource([&reads] {
        ++reads;
        return std::vector<std::pair<std::string, std::string>>{
            {"ro.product.cpu.abi", "armeabi"}, {"ro.build.version.sdk", "19"}, {"gsm.sim.state", "ABSENT"}};
    });
    const auto all = fixture.Run("/system/bin/getprop", {"getprop"});
    CHECK(all.status == 0);
    CHECK(all.output == "[gsm.sim.state]: [ABSENT]\n[ro.build.version.sdk]: [19]\n[ro.product.cpu.abi]: [armeabi]\n");
    CHECK(fixture.Run("/system/bin/getprop", {"getprop", "ro.product.cpu.abi"}).output == "armeabi\n");
    CHECK(fixture.Run("/system/bin/getprop", {"getprop", "ro.missing", "fallback"}).output == "fallback\n");
    CHECK(fixture.Run("/system/bin/getprop", {"getprop", "ro.missing"}).output == "\n");
    CHECK(fixture.Run("/system/bin/toolbox", {"toolbox", "getprop", "ro.build.version.sdk"}).output == "19\n");
    const auto shell = fixture.Run("/system/bin/sh", {"sh", "-c", "getprop ro.build.version.sdk > sdk; echo $?"});
    CHECK(shell.output == "0\n");
    CHECK(fixture.ReadFile("/data/local/tmp/sdk") == "19\n");
    CHECK(reads.load() == 6);
    fixture.kernel.SetSystemPropertySource({});
    CHECK(fixture.Run("/system/bin/getprop", {"getprop"}).output.empty());
}
