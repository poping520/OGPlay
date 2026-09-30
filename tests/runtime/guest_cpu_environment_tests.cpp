#include <doctest/doctest.h>
#include <array>
#include <algorithm>
#include <bit>
#include <fstream>
#include <iterator>
#include <string>
#include <cstring>

#include "ogplay/cpu/dynarmic.h"
#include "ogplay/memory/bus.h"
#include "ogplay/runtime/dexvm/nio_runtime.h"
#include "runtime/integration/guest_cpu_environment.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"

namespace {
using namespace ogplay;
std::string ReadCpuFile(runtime::VirtualFileSystem& fs, const std::string& path) {
    const auto fd = fs.Open(path, {.read = true});
    std::string text;
    std::array<std::byte, 31> buffer;
    for (;;) {
        const auto size = fs.Read(fd, buffer);
        if (size == 0) break;
        text.append(reinterpret_cast<const char*>(buffer.data()), size);
    }
    fs.Close(fd);
    return text;
}
}

TEST_CASE("BND-42 CPU facts publish isolated readonly topology and fixed frequency") {
    runtime::VirtualFileSystem fs;
    for (const auto cores : {1U, 32U, 4U}) {
        {
            runtime::GuestCpuEnvironment environment({cores, 1500});
            environment.Publish(fs);
            const auto info = ReadCpuFile(fs, "/proc/cpuinfo");
            for (std::uint32_t core = 0; core < cores; ++core) {
                CHECK(info.find("processor\t: " + std::to_string(core) + "\n") != std::string::npos);
                const auto root = "/sys/devices/system/cpu/cpu" + std::to_string(core) + "/cpufreq/";
                CHECK(ReadCpuFile(fs, root + "cpuinfo_max_freq") == "1500000\n");
                CHECK(ReadCpuFile(fs, root + "scaling_cur_freq") == "1500000\n");
            }
            CHECK(info.find("processor\t: " + std::to_string(cores) + "\n") == std::string::npos);
            CHECK(info.find("CPU architecture: 7") != std::string::npos);
            CHECK(info.find("vfpv4") == std::string::npos);
            CHECK(ReadCpuFile(fs, "/sys/devices/system/cpu/online") ==
                  (cores == 1 ? "0\n" : "0-" + std::to_string(cores - 1) + "\n"));
            CHECK(ReadCpuFile(fs, "/sys/devices/system/cpu/present") ==
                  ReadCpuFile(fs, "/sys/devices/system/cpu/possible"));
            const auto entries = fs.ListDirectory("/sys/devices/system/cpu");
            CHECK(std::count_if(entries.begin(), entries.end(), [](const auto& entry) {
                return entry.is_directory && entry.name.starts_with("cpu");
            }) == cores);
            CHECK_THROWS_AS(static_cast<void>(fs.Stat("/sys/devices/system/cpu/cpu" + std::to_string(cores))), runtime::VfsError);
            CHECK_THROWS_AS(static_cast<void>(fs.Open("/proc/cpuinfo", {.write = true})), runtime::VfsError);
            CHECK_THROWS_AS(static_cast<void>(fs.Open("/sys/devices/system/cpu/online", {.write = true})), runtime::VfsError);
            runtime::VirtualFileSystem other_fs;
            runtime::GuestCpuEnvironment other({2, 800});
            other.Publish(other_fs);
            CHECK(ReadCpuFile(other_fs, "/sys/devices/system/cpu/online") == "0-1\n");
            CHECK(ReadCpuFile(other_fs, "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq") == "800000\n");
        }
        CHECK_THROWS_AS(static_cast<void>(fs.Stat("/proc/cpuinfo")), runtime::VfsError);
        CHECK_THROWS_AS(static_cast<void>(fs.Stat("/sys/devices/system/cpu/cpu0")), runtime::VfsError);
    }
}

TEST_CASE("BND-42 CPU configuration rejects invalid values and rolls back collisions") {
    for (const auto config : {runtime::GuestCpuConfig{0, 1000}, {33, 1000}, {1, 0}, {1, 10001}})
        CHECK_THROWS_AS(static_cast<void>(runtime::GuestCpuEnvironment{config}), std::invalid_argument);
    runtime::VirtualFileSystem fs;
    const std::array contents{std::byte{'x'}};
    fs.PutFile("/sys/devices/system/cpu/online", contents, false);
    runtime::GuestCpuEnvironment environment({4, 1200});
    CHECK_THROWS_AS(environment.Publish(fs), runtime::VfsError);
    CHECK_THROWS_AS(static_cast<void>(fs.Stat("/proc/cpuinfo")), runtime::VfsError);
    CHECK(ReadCpuFile(fs, "/sys/devices/system/cpu/online") == "x");
}

TEST_CASE("BND-42 real API19 Bionic sysconf shares process CPU facts and retains other selectors") {
    std::vector<std::vector<std::byte>> images;
    std::vector<loader::Elf32ModuleInput> modules;
    for (const auto* name : {"libc.so", "libdl.so"}) {
        std::ifstream input(std::string(OGPLAY_SOURCE_DIR) + "/data/android/19/lib/" + name, std::ios::binary);
        REQUIRE(input.good());
        const std::vector<char> bytes{std::istreambuf_iterator<char>(input), {}};
        auto& image = images.emplace_back(bytes.size());
        std::transform(bytes.begin(), bytes.end(), image.begin(), [](char value) { return static_cast<std::byte>(value); });
        modules.push_back({name, image, memory::GuestAddress{0x10000000U + static_cast<std::uint32_t>(modules.size()) * 0x10000000U}});
    }
    runtime::VirtualFileSystem fs;
    runtime::AndroidGuestProcessRequest request{19, modules, {}, 64, 36, 1000000, 1, &fs, {}};
    request.proc_facts.cpu = {4, 1500};
    SUBCASE("unrestricted") {}
    SUBCASE("BND-43 execution limits") { request.proc_facts.cpu.execution = {true, 1}; }
    auto process = runtime::AndroidGuestProcess::Start(request);
    const auto function = process->FindModuleExport(0, "sysconf");
    CHECK(process->Invoke({function, {96}}).return_value == 4);
    CHECK(process->Invoke({function, {97}}).return_value == 4);
    CHECK(process->Invoke({function, {39}}).return_value == 4096);
    CHECK(process->Invoke({function, {0xffffffffU}}).return_value == 0xffffffffU);
    CHECK(ReadCpuFile(fs, "/sys/devices/system/cpu/online") == "0-3\n");
    process->Stop();
}

TEST_CASE("BND-42 published VFP and NEON basic arithmetic executes on Dynarmic") {
    for (const bool neon : {false, true}) {
        memory::AddressSpace memory;
        memory::CheckedMemoryBus bus(memory);
        constexpr memory::GuestAddress code{0x10000};
        memory.Map({code, memory.PageSize()}, memory::PageProtection::read | memory::PageProtection::write);
        memory.Write32(code, neon ? 0xf2200840U : 0xee300a00U); // vadd.i32 q0,q0,q0 / vadd.f32 s0,s0,s0
        memory.Write32(code.Add(4), 0xef000000U);
        memory.Protect({code, memory.PageSize()}, memory::PageProtection::read | memory::PageProtection::execute);
        cpu::DynarmicCpu cpu(bus);
        cpu::A32State state;
        state.SetRegister(cpu::CoreRegister::pc, code.Value());
        for (std::uint8_t lane = 0; lane < 4; ++lane)
            state.SetExtendedRegister(lane, neon ? lane + 1U : std::bit_cast<std::uint32_t>(1.5F));
        cpu.SetState(state);
        CHECK(cpu.Run(10).reason == cpu::RunStopReason::supervisor_call);
        const auto after = cpu.GetState();
        if (neon) {
            for (std::uint8_t lane = 0; lane < 4; ++lane)
                CHECK(after.ExtendedRegisters()[lane] == (lane + 1U) * 2U);
        } else CHECK(std::bit_cast<float>(after.ExtendedRegisters()[0]) == 3.0F);
    }
}

TEST_CASE("BND-42 sysconf shim returns topology and tailcalls ARM or Thumb libc") {
    for (const bool thumb : {false, true}) {
        memory::AddressSpace memory;
        memory::CheckedMemoryBus bus(memory);
        constexpr memory::GuestAddress base{0x10000000};
        memory.Map({base, memory.PageSize()}, memory::PageProtection::read | memory::PageProtection::write);
        if (thumb) {
            memory.Write16(base, 0x3007); // adds r0, #7
            memory.Write16(base.Add(2), 0x4770); // bx lr
        } else {
            memory.Write32(base, 0xe2800007); // add r0, r0, #7
            memory.Write32(base.Add(4), 0xe12fff1e); // bx lr
        }
        memory.Write32(base.Add(0x100), 0xef000000); // return stop
        memory.Protect({base, memory.PageSize()}, memory::PageProtection::read | memory::PageProtection::execute);
        loader::Elf32LinkModule libc;
        libc.name = "libc.so";
        libc.load_bias = base;
        libc.symbols.symbols.push_back({"sysconf", memory::GuestAddress{thumb ? 1U : 0U}, 8, 1, 2, 0, 1});
        loader::Elf32LinkNamespace symbols;
        symbols.modules.push_back(libc);
        runtime::GuestCpuEnvironment environment({4, 1500});
        environment.BindSysconf(symbols, memory);
        const auto address = symbols.modules[0].symbols.symbols[0].value;
        REQUIRE(address != base);
        CHECK_THROWS(memory.Write32(address, 0));
        for (const auto selector : {96U, 97U, 39U, 0xffffffffU}) {
            cpu::DynarmicCpu cpu(bus);
            cpu::A32State state;
            state.SetRegister(cpu::CoreRegister::pc, address.Value());
            state.SetRegister(cpu::CoreRegister::lr, base.Add(0x100).Value());
            state.SetRegister(cpu::CoreRegister::sp, 0x12340000);
            state.SetRegister(cpu::CoreRegister::r0, selector);
            state.SetRegister(cpu::CoreRegister::r1, 123);
            state.SetRegister(cpu::CoreRegister::r4, 456);
            cpu.SetState(state);
            const auto result = cpu.Run(100);
            CHECK(result.reason == cpu::RunStopReason::supervisor_call);
            const auto after = cpu.GetState();
            CHECK(after.Register(cpu::CoreRegister::r0) == (selector == 96 || selector == 97 ? 4 : selector + 7));
            CHECK(after.Register(cpu::CoreRegister::r1) == 123);
            CHECK(after.Register(cpu::CoreRegister::r4) == 456);
            CHECK(after.Register(cpu::CoreRegister::sp) == 0x12340000);
        }
    }
}

TEST_CASE("ARM tkill real API19 libc wrapper preserves syscall errno") {
    std::vector<std::vector<std::byte>> images;
    std::vector<loader::Elf32ModuleInput> modules;
    for (const auto* name : {"libc.so", "libdl.so"}) {
        std::ifstream input(std::string(OGPLAY_SOURCE_DIR) + "/data/android/19/lib/" + name, std::ios::binary);
        REQUIRE(input.good());
        const std::vector<char> bytes{std::istreambuf_iterator<char>(input), {}};
        auto& image = images.emplace_back(bytes.size());
        std::transform(bytes.begin(), bytes.end(), image.begin(), [](char value) { return static_cast<std::byte>(value); });
        modules.push_back({name, image, memory::GuestAddress{0x10000000U + static_cast<std::uint32_t>(modules.size()) * 0x10000000U}});
    }
    runtime::VirtualFileSystem fs;
    auto process = runtime::AndroidGuestProcess::Start({19, modules, {}, 64, 36, 1000000, 1, &fs, {}});
    const auto tkill = process->FindModuleExport(0, "tkill");
    const auto error_pointer = process->FindModuleExport(0, "__errno");
    CHECK(process->Invoke({tkill, {1, 0}}).return_value == 0);
    for (const auto& sample : {std::array<std::uint32_t, 3>{999, 0, 3},
                               std::array<std::uint32_t, 3>{1, 65, 22}}) {
        CHECK(process->Invoke({tkill, {sample[0], sample[1]}}).return_value == 0xffffffffU);
        const memory::GuestAddress address{process->Invoke({error_pointer, {}}).return_value};
        std::array<std::byte, 4> bytes{};
        process->GuestMemoryAccess().read(address, bytes);
        CHECK(std::to_integer<std::uint32_t>(bytes[0]) == sample[2]);
        CHECK(bytes[1] == std::byte{});
        CHECK(bytes[2] == std::byte{});
        CHECK(bytes[3] == std::byte{});
    }
    process->Stop();
}

TEST_CASE("VFS-06 real API19 ARM libc reads HAL secure random and closes duplicates") {
    std::vector<std::vector<std::byte>> images;
    std::vector<loader::Elf32ModuleInput> modules;
    for (const auto* name : {"libc.so", "libdl.so"}) {
        std::ifstream input(std::string(OGPLAY_SOURCE_DIR) + "/data/android/19/lib/" + name, std::ios::binary);
        REQUIRE(input.good());
        const std::vector<char> bytes{std::istreambuf_iterator<char>(input), {}};
        auto& image = images.emplace_back(bytes.size());
        std::transform(bytes.begin(), bytes.end(), image.begin(), [](char value) { return static_cast<std::byte>(value); });
        modules.push_back({name, image, memory::GuestAddress{0x10000000U + static_cast<std::uint32_t>(modules.size()) * 0x10000000U}});
    }
    runtime::VirtualFileSystem fs;
    auto process = runtime::AndroidGuestProcess::Start({19, modules, {}, 64, 36, 1000000, 1, &fs, {}});
    const auto call = [&](std::string_view name, std::array<std::uint32_t, 4> args) {
        return process->Invoke({process->FindModuleExport(0, name), args}).return_value;
    };
    const memory::GuestAddress scratch{call("malloc", {1024})};
    REQUIRE(scratch.Value() != 0);
    for (const auto* path : {"/dev/urandom", "/dev/random"}) {
        process->GuestMemoryAccess().write(scratch, std::as_bytes(std::span{path, std::strlen(path) + 1}));
        const auto fd = call("open", {scratch.Value(), 0x20900, 0});
        REQUIRE(fd >= 3);
        REQUIRE(fd != 0xffffffffU);
        std::array<std::byte, 256> first{}, second{};
        CHECK(call("read", {fd, scratch.Add(128).Value(), 256}) == 256);
        process->GuestMemoryAccess().read(scratch.Add(128), first);
        const auto dup = call("dup", {fd});
        CHECK(call("close", {fd}) == 0);
        CHECK(call("read", {dup, scratch.Add(128).Value(), 256}) == 256);
        process->GuestMemoryAccess().read(scratch.Add(128), second);
        CHECK(first != second);
        CHECK(std::ranges::any_of(first, [](auto b) { return b != std::byte{}; }));
        CHECK(call("read", {dup, scratch.Add(128).Value(), 0}) == 0);
        CHECK(call("close", {dup}) == 0);
        CHECK(call("read", {dup, scratch.Add(128).Value(), 1}) == 0xffffffffU);
        const memory::GuestAddress errno_pointer{call("__errno", {})};
        std::array<std::byte, 4> error{};
        process->GuestMemoryAccess().read(errno_pointer, error);
        CHECK(error[0] == std::byte{9});
    }
    static_cast<void>(call("free", {scratch.Value()}));
    process->Stop();
    process.reset();
    CHECK_THROWS_AS(static_cast<void>(fs.Stat("/dev/urandom")), runtime::VfsError);
}
