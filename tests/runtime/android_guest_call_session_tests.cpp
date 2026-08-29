#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>
#include <thread>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/audio/java_sound_pool.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"
#include "ogplay/runtime/framework/framework_lifecycle.h"
#include "ogplay/runtime/framework/framework_locale.h"
#include "ogplay/runtime/jni/jni_array.h"
#include "ogplay/runtime/jni/jni_class_registry.h"
#include "ogplay/runtime/jni/jni_environment.h"
#include "ogplay/runtime/jni/jni_invocation.h"
#include "ogplay/runtime/jni/jni_object.h"
#include "runtime/boundary/modules/module_catalog.h"
#include "runtime/boundary/core/boundary_symbols.h"

namespace {

void Put16(std::vector<std::byte>& bytes, const std::size_t offset,
           const std::uint16_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1] = static_cast<std::byte>(value >> 8U);
}

void Put32(std::vector<std::byte>& bytes, const std::size_t offset,
           const std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        bytes[offset + index] = static_cast<std::byte>(
            (value >> static_cast<unsigned>(index * 8U)) & 0xffU);
    }
}

[[nodiscard]] std::vector<std::byte> MinimalLibcElf() {
    std::vector<std::byte> bytes(0x300, std::byte{});
    bytes[0] = std::byte{0x7f};
    bytes[1] = std::byte{'E'};
    bytes[2] = std::byte{'L'};
    bytes[3] = std::byte{'F'};
    bytes[4] = std::byte{1};
    bytes[5] = std::byte{1};
    bytes[6] = std::byte{1};
    Put16(bytes, 16, 3);
    Put16(bytes, 18, 40);
    Put32(bytes, 20, 1);
    Put32(bytes, 28, 52);
    Put32(bytes, 36, 0x05000400U);
    Put16(bytes, 40, 52);
    Put16(bytes, 42, 32);
    Put16(bytes, 44, 2);
    Put32(bytes, 52, ogplay::loader::kElfProgramLoad);
    Put32(bytes, 60, 0x10000U);
    Put32(bytes, 68, 0x300);
    Put32(bytes, 72, 0x300);
    Put32(bytes, 76, 6);
    Put32(bytes, 80, 0x1000);
    Put32(bytes, 84, ogplay::loader::kElfProgramDynamic);
    Put32(bytes, 88, 0x100);
    Put32(bytes, 92, 0x10100U);
    Put32(bytes, 100, 56);
    Put32(bytes, 104, 56);
    Put32(bytes, 108, 6);
    Put32(bytes, 112, 4);
    Put32(bytes, 0x100, ogplay::loader::kElfDynamicStringTable);
    Put32(bytes, 0x104, 0x10160U);
    Put32(bytes, 0x108, ogplay::loader::kElfDynamicStringTableSize);
    Put32(bytes, 0x10c, 34);
    Put32(bytes, 0x110, ogplay::loader::kElfDynamicSoname);
    Put32(bytes, 0x114, 1);
    Put32(bytes, 0x118, ogplay::loader::kElfDynamicHash);
    Put32(bytes, 0x11c, 0x10190U);
    Put32(bytes, 0x120, ogplay::loader::kElfDynamicSymbolTable);
    Put32(bytes, 0x124, 0x101b0U);
    Put32(bytes, 0x128, ogplay::loader::kElfDynamicSymbolEntrySize);
    Put32(bytes, 0x12c, 16);
    const char strings[] = "\0libc.so\0__system_property_area__\0";
    for (std::size_t index = 0; index < sizeof(strings); ++index) {
        bytes[0x160 + index] = static_cast<std::byte>(strings[index]);
    }
    Put32(bytes, 0x190, 1);
    Put32(bytes, 0x194, 2);
    Put32(bytes, 0x198, 1);
    Put32(bytes, 0x19c, 0);
    Put32(bytes, 0x1c0, 9);
    Put32(bytes, 0x1c4, 0x10200U);
    Put32(bytes, 0x1c8, 4);
    bytes[0x1cc] = std::byte{0x11};
    Put16(bytes, 0x1ce, 1);
    return bytes;
}

constexpr std::uint32_t kOpenSlesFixtureBase = 0x10010000U;
constexpr std::uint32_t kOpenSlesCallbackOffset = 0x1000U;
constexpr std::uint32_t kOpenSlesRead32Offset = 0x1080U;
constexpr std::uint32_t kOpenSlesWrite32Offset = 0x1090U;
constexpr std::uint32_t kOpenSlesSourceOffset = 0x500U;
constexpr std::uint32_t kOpenSlesBufferQueueLocatorOffset = 0x510U;
constexpr std::uint32_t kOpenSlesPcmFormatOffset = 0x520U;
constexpr std::uint32_t kOpenSlesSinkOffset = 0x540U;
constexpr std::uint32_t kOpenSlesOutputMixLocatorOffset = 0x550U;
constexpr std::uint32_t kOpenSlesEngineResultOffset = 0x560U;
constexpr std::uint32_t kOpenSlesMixResultOffset = 0x564U;
constexpr std::uint32_t kOpenSlesPlayerResultOffset = 0x568U;
constexpr std::uint32_t kOpenSlesFirstPcmOffset = 0x580U;
constexpr std::uint32_t kOpenSlesSecondPcmOffset = 0x590U;
constexpr std::uint32_t kOpenSlesCallbackMarkerOffset = 0x5a0U;
constexpr std::uint32_t kOpenSlesCallbackTlsOffset = 0x5a4U;

[[nodiscard]] std::uint32_t OpenSlesFixtureAddress(
    const std::uint32_t offset) {
    return kOpenSlesFixtureBase + offset;
}

[[nodiscard]] std::uint32_t OpenSlesThunk(const std::string_view name) {
    const auto* module = ogplay::runtime::AndroidBoundaryCatalog(
                             ogplay::runtime::AndroidApi::api19)
                             .FindModule("libOpenSLES.so");
    if (module == nullptr) {
        throw std::runtime_error("OpenSL fixture cannot find module catalog");
    }
    const auto found = std::ranges::find(module->exports, name,
                                         &ogplay::runtime::BoundaryExportDescriptor::name);
    if (found == module->exports.end() ||
        found->kind == ogplay::runtime::BoundaryExportKind::public_data) {
        throw std::runtime_error("OpenSL fixture cannot find callable " +
                                 std::string(name));
    }
    return found->address.Value();
}

constexpr std::uint32_t kLibdlFixtureBase = 0x10011000U;
constexpr std::uint32_t kLibdlLibhglOffset = 0x700U;
constexpr std::uint32_t kLibdlGlCreateShaderOffset = 0x720U;
constexpr std::uint32_t kLibdlEglGetProcAddressOffset = 0x740U;
constexpr std::uint32_t kLibdlMemcpyOffset = 0x770U;
constexpr std::uint32_t kLibdlPropertyAreaOffset = 0x790U;
constexpr std::uint32_t kLibdlMissingOffset = 0x7c0U;
constexpr std::uint32_t kLibdlGles1Offset = 0x7e0U;
constexpr std::uint32_t kLibdlGlVertexPointerOffset = 0x810U;

[[nodiscard]] std::uint32_t LibdlFixtureAddress(
    const std::uint32_t offset) {
    return kLibdlFixtureBase + offset;
}

[[nodiscard]] std::vector<std::byte> LibdlDefaultLibcElf() {
    auto bytes = MinimalLibcElf();
    bytes.resize(0x2000U, std::byte{});
    Put16(bytes, 44U, 3U);
    Put32(bytes, 68U, 0x1000U);
    Put32(bytes, 72U, 0x1000U);
    Put32(bytes, 76U, 6U);
    Put32(bytes, 116U, ogplay::loader::kElfProgramLoad);
    Put32(bytes, 120U, 0x1000U);
    Put32(bytes, 124U, 0x11000U);
    Put32(bytes, 128U, 0U);
    Put32(bytes, 132U, 0x1000U);
    Put32(bytes, 136U, 0x1000U);
    Put32(bytes, 140U, 5U);
    Put32(bytes, 144U, 0x1000U);
    const auto put_string = [&](const std::uint32_t offset,
                                const std::string_view text) {
        for (std::size_t index = 0; index < text.size(); ++index) {
            bytes[0x1000U + offset + index] =
                static_cast<std::byte>(static_cast<unsigned char>(text[index]));
        }
    };
    put_string(kLibdlLibhglOffset, "libhgl.so");
    put_string(kLibdlGlCreateShaderOffset, "glCreateShader");
    put_string(kLibdlEglGetProcAddressOffset, "eglGetProcAddress");
    put_string(kLibdlMemcpyOffset, "memcpy");
    put_string(kLibdlPropertyAreaOffset, "__system_property_area__");
    put_string(kLibdlMissingOffset, "missing_symbol");
    put_string(kLibdlGles1Offset, "libGLESv1_CM.so");
    put_string(kLibdlGlVertexPointerOffset, "glVertexPointer");
    return bytes;
}

[[nodiscard]] std::vector<std::byte> OpenSlesCallbackLibcElf() {
    auto bytes = MinimalLibcElf();
    bytes.resize(0x2000U, std::byte{});
    Put16(bytes, 44U, 3U);
    Put32(bytes, 68U, 0x1000U);
    Put32(bytes, 72U, 0x1000U);
    Put32(bytes, 76U, 6U);
    Put32(bytes, 116U, ogplay::loader::kElfProgramLoad);
    Put32(bytes, 120U, 0x1000U);
    Put32(bytes, 124U, 0x11000U);
    Put32(bytes, 128U, 0U);
    Put32(bytes, 132U, 0x1000U);
    Put32(bytes, 136U, 0x1000U);
    Put32(bytes, 140U, 5U);
    Put32(bytes, 144U, 0x1000U);

    const auto callback = kOpenSlesCallbackOffset;
    Put32(bytes, callback + 0U, 0xe92d4000U);   // push {lr}
    Put32(bytes, callback + 4U, 0xe59f1024U);   // ldr r1, [pc, #36]
    Put32(bytes, callback + 8U, 0xe3a02004U);   // mov r2, #4
    Put32(bytes, callback + 12U, 0xe59f3020U);  // ldr r3, [pc, #32]
    Put32(bytes, callback + 16U, 0xe12fff33U);  // blx r3
    Put32(bytes, callback + 20U, 0xee1d2f70U);  // mrc p15,0,r2,c13,c0,3
    Put32(bytes, callback + 24U, 0xe59f1018U);  // ldr r1, [pc, #24]
    Put32(bytes, callback + 28U, 0xe3a03001U);  // mov r3, #1
    Put32(bytes, callback + 32U, 0xe5813000U);  // str r3, [r1]
    Put32(bytes, callback + 36U, 0xe59f1010U);  // ldr r1, [pc, #16]
    Put32(bytes, callback + 40U, 0xe5812000U);  // str r2, [r1]
    Put32(bytes, callback + 44U, 0xe8bd8000U);  // pop {pc}
    Put32(bytes, callback + 48U,
          OpenSlesFixtureAddress(kOpenSlesSecondPcmOffset));
    Put32(bytes, callback + 52U, OpenSlesThunk("$BufferQueue.Enqueue"));
    Put32(bytes, callback + 56U,
          OpenSlesFixtureAddress(kOpenSlesCallbackMarkerOffset));
    Put32(bytes, callback + 60U,
          OpenSlesFixtureAddress(kOpenSlesCallbackTlsOffset));

    Put32(bytes, kOpenSlesRead32Offset + 0U, 0xe5900000U);  // ldr r0, [r0]
    Put32(bytes, kOpenSlesRead32Offset + 4U, 0xe12fff1eU);  // bx lr
    Put32(bytes, kOpenSlesWrite32Offset + 0U, 0xe5801000U); // str r1, [r0]
    Put32(bytes, kOpenSlesWrite32Offset + 4U, 0xe3a00000U); // mov r0, #0
    Put32(bytes, kOpenSlesWrite32Offset + 8U, 0xe12fff1eU); // bx lr

    Put32(bytes, kOpenSlesSourceOffset,
          OpenSlesFixtureAddress(kOpenSlesBufferQueueLocatorOffset));
    Put32(bytes, kOpenSlesSourceOffset + 4U,
          OpenSlesFixtureAddress(kOpenSlesPcmFormatOffset));
    Put32(bytes, kOpenSlesBufferQueueLocatorOffset, 0x800007bdU);
    Put32(bytes, kOpenSlesBufferQueueLocatorOffset + 4U, 1U);
    Put32(bytes, kOpenSlesPcmFormatOffset, 2U);
    Put32(bytes, kOpenSlesPcmFormatOffset + 4U, 1U);
    Put32(bytes, kOpenSlesPcmFormatOffset + 8U, 48000000U);
    Put32(bytes, kOpenSlesPcmFormatOffset + 12U, 16U);
    Put32(bytes, kOpenSlesPcmFormatOffset + 16U, 16U);
    Put32(bytes, kOpenSlesPcmFormatOffset + 20U, 4U);
    Put32(bytes, kOpenSlesPcmFormatOffset + 24U, 2U);
    Put32(bytes, kOpenSlesSinkOffset,
          OpenSlesFixtureAddress(kOpenSlesOutputMixLocatorOffset));
    Put32(bytes, kOpenSlesSinkOffset + 4U, 0U);
    Put32(bytes, kOpenSlesOutputMixLocatorOffset, 4U);
    Put32(bytes, kOpenSlesOutputMixLocatorOffset + 4U, 0U);
    Put16(bytes, kOpenSlesFirstPcmOffset + 0U, 1000U);
    Put16(bytes, kOpenSlesFirstPcmOffset + 2U, 2000U);
    Put16(bytes, kOpenSlesSecondPcmOffset + 0U, 3000U);
    Put16(bytes, kOpenSlesSecondPcmOffset + 2U, 4000U);
    return bytes;
}

[[nodiscard]] std::vector<std::byte> ReadAudioFixture() {
    const auto path = std::filesystem::path{OGPLAY_SOURCE_DIR} /
                      "tests/fixtures/audio/short-vorbis.ogg";
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open audio fixture");
    const std::vector<char> bytes{std::istreambuf_iterator<char>{input}, {}};
    std::vector<std::byte> result;
    result.reserve(bytes.size());
    for (const auto value : bytes) {
        result.push_back(static_cast<std::byte>(
            static_cast<unsigned char>(value)));
    }
    return result;
}

[[nodiscard]] std::string ReadVfsText(
    ogplay::runtime::VirtualFileSystem& filesystem,
    const std::string_view path) {
    const auto size = filesystem.Stat(path).size;
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    const auto descriptor = filesystem.Open(path, {.read = true});
    const auto read = filesystem.Read(descriptor, bytes);
    filesystem.Close(descriptor);
    return {reinterpret_cast<const char*>(bytes.data()), read};
}

[[nodiscard]] std::uint32_t ProcMemoryValue(
    const std::string_view meminfo, const std::string_view prefix) {
    auto position = meminfo.find(prefix);
    if (position == std::string_view::npos ||
        (position != 0U && meminfo[position - 1U] != '\n')) {
        throw std::runtime_error("proc memory line is missing");
    }
    position += prefix.size();
    while (position < meminfo.size() && meminfo[position] == ' ') ++position;
    const auto end = meminfo.find(' ', position);
    return static_cast<std::uint32_t>(std::stoul(
        std::string{meminfo.substr(position, end - position)}));
}

}  // namespace

TEST_CASE("Android guest process starts and stops without an application ELF") {
    auto libc = MinimalLibcElf();
    const ogplay::loader::Elf32ModuleInput module{
        "libc.so", libc, ogplay::memory::GuestAddress{0x10000000U}};
    ogplay::runtime::VirtualFileSystem filesystem;
    auto process = ogplay::runtime::AndroidGuestProcess::Start(
        {19, std::span{&module, 1}, {}, 64, 36,
         1000, 1, &filesystem, {}});

    CHECK(process->Running());
    CHECK(process->ApplicationModuleCount() == 0);
    CHECK(process->LoadedGuestModuleCount() == 1);
    CHECK(process->GuestEnvironment().Value() != 0);
    CHECK(process->GuestJavaVm().Value() != 0);
    CHECK(process->AttachedJniThreadCount() == 1);
    constexpr std::string_view expected_meminfo =
        "MemTotal:         524288 kB\n"
        "MemFree:          262144 kB\n"
        "Buffers:               0 kB\n"
        "Cached:           131072 kB\n"
        "SwapCached:            0 kB\n"
        "SwapTotal:             0 kB\n"
        "SwapFree:              0 kB\n";
    CHECK(ReadVfsText(filesystem, "/proc/meminfo") == expected_meminfo);
    CHECK_THROWS_AS(
        static_cast<void>(filesystem.Open("/proc/meminfo", {.write = true})),
        ogplay::runtime::VfsError);
    std::array<std::int16_t, 16> silent{};
    CHECK(process->RenderStereoAudio(silent, 48000U) == 8U);
    CHECK(std::ranges::all_of(silent, [](const auto sample) {
        return sample == 0;
    }));

    process->Stop();
    CHECK_FALSE(process->Running());
    CHECK(process->AttachedJniThreadCount() == 0);
    CHECK(process->LoadedGuestModuleCount() == 1);
    process->Stop();
}

TEST_CASE("Android guest proc memory values come from injected facts") {
    auto libc = MinimalLibcElf();
    const ogplay::loader::Elf32ModuleInput module{
        "libc.so", libc, ogplay::memory::GuestAddress{0x10000000U}};
    ogplay::runtime::VirtualFileSystem filesystem;
    ogplay::runtime::AndroidGuestProcessRequest request{
        19, std::span{&module, 1}, {}, 64, 36,
        1000, 1, &filesystem, {}};
    request.proc_facts = {1998156U, 966564U};
    auto process = ogplay::runtime::AndroidGuestProcess::Start(request);

    const auto meminfo = ReadVfsText(filesystem, "/proc/meminfo");
    CHECK(ProcMemoryValue(meminfo, "MemTotal:") ==
          request.proc_facts.memory_total_kb);
    CHECK(ProcMemoryValue(meminfo, "MemFree:") ==
          request.proc_facts.memory_free_kb);
    CHECK(ProcMemoryValue(meminfo, "Buffers:") == 0U);
    CHECK(ProcMemoryValue(meminfo, "Cached:") ==
          request.proc_facts.memory_total_kb / 4U);
    CHECK(ProcMemoryValue(meminfo, "SwapCached:") == 0U);
    CHECK(ProcMemoryValue(meminfo, "SwapTotal:") == 0U);
    CHECK(ProcMemoryValue(meminfo, "SwapFree:") == 0U);
    process->Stop();
}

TEST_CASE("Android guest proc installation preserves an existing snapshot") {
    auto libc = MinimalLibcElf();
    const ogplay::loader::Elf32ModuleInput module{
        "libc.so", libc, ogplay::memory::GuestAddress{0x10000000U}};
    ogplay::runtime::VirtualFileSystem filesystem;
    constexpr std::string_view existing = "explicit existing snapshot\n";
    filesystem.PutFile(
        "/proc/meminfo",
        std::as_bytes(std::span{existing.data(), existing.size()}), false);
    auto process = ogplay::runtime::AndroidGuestProcess::Start(
        {19, std::span{&module, 1}, {}, 64, 36,
         1000, 1, &filesystem, {}});
    CHECK(ReadVfsText(filesystem, "/proc/meminfo") == existing);
    process->Stop();
}

TEST_CASE("Android guest process rejects invalid proc facts") {
    auto libc = MinimalLibcElf();
    const ogplay::loader::Elf32ModuleInput module{
        "libc.so", libc, ogplay::memory::GuestAddress{0x10000000U}};
    ogplay::runtime::VirtualFileSystem zero_total_filesystem;
    ogplay::runtime::AndroidGuestProcessRequest zero_total{
        19, std::span{&module, 1}, {}, 64, 36,
        1000, 1, &zero_total_filesystem, {}};
    zero_total.proc_facts = {0U, 0U};
    CHECK_THROWS_WITH_AS(
        static_cast<void>(
            ogplay::runtime::AndroidGuestProcess::Start(zero_total)),
        "Android guest proc facts are invalid",
        ogplay::runtime::AndroidGuestProcessError);

    ogplay::runtime::VirtualFileSystem excess_free_filesystem;
    auto excess_free = zero_total;
    excess_free.filesystem = &excess_free_filesystem;
    excess_free.proc_facts = {1024U, 1025U};
    CHECK_THROWS_WITH_AS(
        static_cast<void>(
            ogplay::runtime::AndroidGuestProcess::Start(excess_free)),
        "Android guest proc facts are invalid",
        ogplay::runtime::AndroidGuestProcessError);
}

TEST_CASE("OpenSL buffer callback runs on its guest thread and re-enqueues") {
    auto libc = OpenSlesCallbackLibcElf();
    const ogplay::loader::Elf32ModuleInput module{
        "libc.so", libc, ogplay::memory::GuestAddress{0x10000000U}};
    ogplay::runtime::VirtualFileSystem filesystem;
    auto process = ogplay::runtime::AndroidGuestProcess::Start(
        {19, std::span{&module, 1}, {}, 64, 36,
         100000, 1, &filesystem, {}});

    const auto invoke = [&](const std::uint32_t target,
                            const std::array<std::uint32_t, 4> registers,
                            const std::span<const std::uint32_t> stack) {
        return process
            ->Invoke({ogplay::memory::GuestAddress{target}, registers, stack})
            .return_value;
    };
    const auto read_guest = [&](const std::uint32_t address) {
        return invoke(OpenSlesFixtureAddress(kOpenSlesRead32Offset),
                      {address, 0U, 0U, 0U}, {});
    };
    const auto write_guest = [&](const std::uint32_t address,
                                 const std::uint32_t value) {
        CHECK(invoke(OpenSlesFixtureAddress(kOpenSlesWrite32Offset),
                     {address, value, 0U, 0U}, {}) == 0U);
    };

    const std::array<std::uint32_t, 2> create_engine_stack{};
    CHECK(invoke(OpenSlesThunk("slCreateEngine"),
                 {OpenSlesFixtureAddress(kOpenSlesEngineResultOffset),
                  0U, 0U, 0U},
                 create_engine_stack) == 0U);
    const auto engine =
        read_guest(OpenSlesFixtureAddress(kOpenSlesEngineResultOffset));
    REQUIRE(engine != 0U);
    CHECK(invoke(OpenSlesThunk("$Object.Realize"),
                 {engine, 0U, 0U, 0U}, {}) == 0U);

    const std::array<std::uint32_t, 2> create_mix_stack{};
    CHECK(invoke(OpenSlesThunk("$Engine.CreateOutputMix"),
                 {engine + 4U,
                  OpenSlesFixtureAddress(kOpenSlesMixResultOffset),
                  0U, 0U},
                 create_mix_stack) == 0U);
    const auto mix =
        read_guest(OpenSlesFixtureAddress(kOpenSlesMixResultOffset));
    REQUIRE(mix != 0U);
    CHECK(invoke(OpenSlesThunk("$Object.Realize"),
                 {mix, 0U, 0U, 0U}, {}) == 0U);
    write_guest(OpenSlesFixtureAddress(kOpenSlesOutputMixLocatorOffset) + 4U,
                mix);

    const std::array<std::uint32_t, 4> create_player_stack{};
    CHECK(invoke(OpenSlesThunk("$Engine.CreateAudioPlayer"),
                 {engine + 4U,
                  OpenSlesFixtureAddress(kOpenSlesPlayerResultOffset),
                  OpenSlesFixtureAddress(kOpenSlesSourceOffset),
                  OpenSlesFixtureAddress(kOpenSlesSinkOffset)},
                 create_player_stack) == 0U);
    const auto player =
        read_guest(OpenSlesFixtureAddress(kOpenSlesPlayerResultOffset));
    REQUIRE(player != 0U);
    CHECK(invoke(OpenSlesThunk("$Object.Realize"),
                 {player, 0U, 0U, 0U}, {}) == 0U);

    const auto queue = player + 8U;
    CHECK(invoke(OpenSlesThunk("$BufferQueue.RegisterCallback"),
                 {queue, OpenSlesFixtureAddress(kOpenSlesCallbackOffset),
                  0x51e5U, 0U}, {}) == 0U);
    CHECK(invoke(OpenSlesThunk("$BufferQueue.Enqueue"),
                 {queue, OpenSlesFixtureAddress(kOpenSlesFirstPcmOffset),
                  4U, 0U}, {}) == 0U);
    CHECK(invoke(OpenSlesThunk("$Play.SetPlayState"),
                 {player + 4U, 3U, 0U, 0U}, {}) == 0U);

    std::array<std::int16_t, 4> first{};
    CHECK(process->RenderStereoAudio(first, 48000U) == 2U);
    CHECK(first == std::array<std::int16_t, 4>{1000, 1000, 2000, 2000});

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(2);
    while (read_guest(OpenSlesFixtureAddress(kOpenSlesCallbackMarkerOffset)) ==
               0U &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    REQUIRE(read_guest(OpenSlesFixtureAddress(kOpenSlesCallbackMarkerOffset)) ==
            1U);
    CHECK(read_guest(OpenSlesFixtureAddress(kOpenSlesCallbackTlsOffset)) ==
          0x71a00000U);
    CHECK(process->AttachedJniThreadCount() == 1U);

    std::array<std::int16_t, 4> second{};
    CHECK(process->RenderStereoAudio(second, 48000U) == 2U);
    CHECK(second == std::array<std::int16_t, 4>{3000, 3000, 4000, 4000});
    process->Stop();
}

TEST_CASE("libdl process service exposes RTLD_DEFAULT and the host GL alias") {
    auto libc = LibdlDefaultLibcElf();
    const ogplay::loader::Elf32ModuleInput module{
        "libc.so", libc, ogplay::memory::GuestAddress{0x10000000U}};
    ogplay::runtime::VirtualFileSystem filesystem;
    auto process = ogplay::runtime::AndroidGuestProcess::Start(
        {19, std::span{&module, 1}, {}, 64, 36, 1000, 1, &filesystem, {}});

    const auto hle_symbols =
        ogplay::runtime::detail::BuildAndroidBoundarySymbols(
            ogplay::runtime::AndroidApi::api19);
    const auto hle_address = [&](const std::string_view library,
                                 const std::string_view symbol) {
        const auto found = std::ranges::find_if(
            hle_symbols, [&](const ogplay::runtime::BionicHleSymbol& entry) {
                return entry.library == library && entry.symbol == symbol;
            });
        REQUIRE(found != hle_symbols.end());
        return found->address.Value();
    };
    const auto invoke = [&](const std::string_view entry,
                            const std::array<std::uint32_t, 4> registers) {
        return process
            ->Invoke({ogplay::memory::GuestAddress{
                          hle_address("libdl.so", entry)},
                      registers, {}})
            .return_value;
    };

    // dlopen(nullptr) is the process-wide pseudo-handle, not a fresh open of
    // the root module.
    CHECK(invoke("dlopen", {0U, 2U, 0U, 0U}) == 0xffffffffU);
    // RTLD_DEFAULT resolves sealed HLE exports across module boundaries.
    CHECK(invoke("dlsym", {0xffffffffU,
                          LibdlFixtureAddress(kLibdlGlCreateShaderOffset),
                          0U, 0U}) ==
          hle_address("libGLESv2.so", "glCreateShader"));
    CHECK(invoke("dlsym", {0xffffffffU,
                          LibdlFixtureAddress(kLibdlEglGetProcAddressOffset),
                          0U, 0U}) ==
          hle_address("libEGL.so", "eglGetProcAddress"));
    CHECK(invoke("dlsym", {0xffffffffU,
                          LibdlFixtureAddress(kLibdlMemcpyOffset), 0U, 0U}) ==
          hle_address("libc.so", "memcpy"));
    // Names outside the HLE surface fall back to the guest ELF namespace.
    CHECK(invoke("dlsym", {0xffffffffU,
                          LibdlFixtureAddress(kLibdlPropertyAreaOffset),
                          0U, 0U}) == 0x10010200U);
    // Unknown names keep failing explicitly.
    CHECK(invoke("dlsym", {0xffffffffU,
                          LibdlFixtureAddress(kLibdlMissingOffset), 0U,
                          0U}) == 0U);
    // The host GL wrapper library opens the sealed GLES2 boundary.
    const auto hgl = invoke("dlopen",
                            {LibdlFixtureAddress(kLibdlLibhglOffset), 2U, 0U,
                             0U});
    REQUIRE(hgl != 0U);
    REQUIRE(hgl != 0xffffffffU);
    CHECK(invoke("dlsym", {hgl,
                          LibdlFixtureAddress(kLibdlGlCreateShaderOffset),
                          0U, 0U}) ==
          hle_address("libGLESv2.so", "glCreateShader"));
    // dlclose treats the pseudo-handle as a no-op and the alias as a normal
    // counted reference.
    CHECK(invoke("dlclose", {0xffffffffU, 0U, 0U, 0U}) == 0U);
    CHECK(invoke("dlclose", {hgl, 0U, 0U, 0U}) == 0U);
    process->Stop();
}

TEST_CASE("libdl boundary handle lookups fall back to the sealed surface") {
    auto libc = LibdlDefaultLibcElf();
    const ogplay::loader::Elf32ModuleInput module{
        "libc.so", libc, ogplay::memory::GuestAddress{0x10000000U}};
    ogplay::runtime::VirtualFileSystem filesystem;
    auto process = ogplay::runtime::AndroidGuestProcess::Start(
        {19, std::span{&module, 1}, {}, 64, 36, 1000, 1, &filesystem, {}});

    const auto hle_symbols =
        ogplay::runtime::detail::BuildAndroidBoundarySymbols(
            ogplay::runtime::AndroidApi::api19);
    const auto hle_address = [&](const std::string_view library,
                                 const std::string_view symbol) {
        const auto found = std::ranges::find_if(
            hle_symbols, [&](const ogplay::runtime::BionicHleSymbol& entry) {
                return entry.library == library && entry.symbol == symbol;
            });
        REQUIRE(found != hle_symbols.end());
        return found->address.Value();
    };
    const auto invoke = [&](const std::string_view entry,
                            const std::array<std::uint32_t, 4> registers) {
        return process
            ->Invoke({ogplay::memory::GuestAddress{
                          hle_address("libdl.so", entry)},
                      registers, {}})
            .return_value;
    };

    // Engines of the KitKat era query GLES2 entry points through the GLES1
    // handle; its own exports keep precedence and misses cross the sealed
    // surface instead of failing.
    const auto gles1 = invoke("dlopen",
                              {LibdlFixtureAddress(kLibdlGles1Offset), 2U, 0U,
                               0U});
    REQUIRE(gles1 != 0U);
    REQUIRE(gles1 != 0xffffffffU);
    CHECK(invoke("dlsym",
                 {gles1,
                  LibdlFixtureAddress(kLibdlGlVertexPointerOffset), 0U, 0U}) ==
          hle_address("libGLESv1_CM.so", "glVertexPointer"));
    CHECK(invoke("dlsym",
                 {gles1, LibdlFixtureAddress(kLibdlGlCreateShaderOffset), 0U,
                  0U}) == hle_address("libGLESv2.so", "glCreateShader"));
    CHECK(invoke("dlsym", {gles1,
                          LibdlFixtureAddress(kLibdlMissingOffset), 0U,
                          0U}) == 0U);
    CHECK(invoke("dlclose", {gles1, 0U, 0U, 0U}) == 0U);
    process->Stop();
}

TEST_CASE("Android guest process owns reusable DexVM native thread contexts") {
    auto libc = MinimalLibcElf();
    const ogplay::loader::Elf32ModuleInput module{
        "libc.so", libc, ogplay::memory::GuestAddress{0x10000000U}};
    ogplay::runtime::VirtualFileSystem filesystem;
    auto process = ogplay::runtime::AndroidGuestProcess::Start(
        {19, std::span{&module, 1}, {}, 64, 36,
         1000, 1, &filesystem, {}});

    CHECK_THROWS(process->PrepareDexVmThread(UINT64_C(0x40000004), 0U));
    CHECK_THROWS(process->PrepareDexVmThread(UINT64_C(0x4004), 31U));

    constexpr std::uint64_t first = UINT64_C(0x4000);
    constexpr std::uint64_t second = UINT64_C(0x4001);
    process->PrepareDexVmThread(first, 0);
    CHECK(process->AttachedJniThreadCount() == 2U);
    process->ReleaseDexVmThread(first);
    CHECK(process->AttachedJniThreadCount() == 1U);
    process->PrepareDexVmThread(second, 0);
    CHECK(process->AttachedJniThreadCount() == 2U);
    process->ReleaseDexVmThread(second);
    CHECK(process->AttachedJniThreadCount() == 1U);
    process->Stop();
}

TEST_CASE("legacy Android guest call session delegates process ownership") {
    auto libc = MinimalLibcElf();
    const ogplay::loader::Elf32ModuleInput module{
        "libc.so", libc, ogplay::memory::GuestAddress{0x10000000U}};
    ogplay::runtime::VirtualFileSystem filesystem;
    ogplay::runtime::AndroidGuestCallSessionRequest request{
        19, "libc.so", std::span{&module, 1}, {}, 64, 36,
        1000, 1, &filesystem, {}};
    request.proc_facts = {8192U, 4096U};
    auto session = ogplay::runtime::AndroidGuestCallSession::Start(request);
    CHECK(session->Running());
    CHECK(session->GuestJavaVm().Value() != 0);
    const auto meminfo = ReadVfsText(filesystem, "/proc/meminfo");
    CHECK(ProcMemoryValue(meminfo, "MemTotal:") == 8192U);
    CHECK(ProcMemoryValue(meminfo, "MemFree:") == 4096U);
    session->Stop();
    CHECK_FALSE(session->Running());
}

TEST_CASE("Android guest process teardown interrupts blocked AudioTrack write") {
    auto libc = MinimalLibcElf();
    const ogplay::loader::Elf32ModuleInput module{
        "libc.so", libc, ogplay::memory::GuestAddress{0x10000000U}};
    ogplay::runtime::VirtualFileSystem filesystem;
    auto process = ogplay::runtime::AndroidGuestProcess::Start(
        {19, std::span{&module, 1}, {}, 64, 36,
         1000, 1, &filesystem, {}});
    auto& mixer = process->PcmPlayback();
    const auto player = mixer.CreatePlayer({48000U, 1U, 16U}, 255U);
    const std::array pcm{
        std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}};
    REQUIRE(mixer.EnqueueBlocking(player, pcm, pcm.size()) ==
            ogplay::audio::OpenSlesEnqueueResult::enqueued);

    std::atomic result{ogplay::audio::OpenSlesEnqueueResult::enqueued};
    std::jthread writer([&] {
        result = mixer.EnqueueBlocking(player, pcm, pcm.size());
    });
    bool parked{};
    for (std::size_t attempt = 0; attempt < 100000U; ++attempt) {
        if (mixer.BlockingWriterCount() == 1U) {
            parked = true;
            break;
        }
        std::this_thread::yield();
    }
    process->BeginTeardown();
    writer.join();
    REQUIRE(parked);
    CHECK(result.load() == ogplay::audio::OpenSlesEnqueueResult::interrupted);
    process->Stop();
}

TEST_CASE("Android guest call session validates its complete launch request") {
    ogplay::runtime::VirtualFileSystem filesystem;
    const std::array<std::byte, 4> bytes{};
    const ogplay::loader::Elf32ModuleInput module{
        "root.so", bytes, ogplay::memory::GuestAddress{0x10000000U}};

    CHECK_THROWS_WITH_AS(
        static_cast<void>(ogplay::runtime::AndroidGuestCallSession::Start(
            {19, "", std::span{&module, 1}, {}, 64, 36,
             1000, 1, &filesystem, {}})),
        "Android guest call session request is incomplete",
        ogplay::runtime::AndroidGuestCallSessionError);
    CHECK_THROWS_WITH_AS(
        static_cast<void>(ogplay::runtime::AndroidGuestCallSession::Start(
            {19, "root.so", std::span{&module, 1}, {}, 64, 36,
             1000, 1, nullptr, {}})),
        "Android guest call session request is incomplete",
        ogplay::runtime::AndroidGuestCallSessionError);
}

TEST_CASE("Android guest platform handlers publish facts state and explicit failures") {
    using namespace ogplay::runtime;
    JniClassRegistry classes;
    const auto device = classes.RegisterClass(
        {"fixture/Device", {},
         {{"a", "()[B", "device.identifier_bytes", true},
          {"d", "()[B", "application.version_bytes", true},
          {"IsWifiEnable", "()Z", "network.wifi_enabled", true},
          {"e", "(I)V", "device.set_unique_code", true}}, {}});
    const auto game = classes.RegisterClass(
        {"fixture/Game", {},
         {{"sendAppToBackground", "()V", "activity.send_to_background", true},
          {"setFullyLoaded", "()V", "activity.set_fully_loaded", true},
          {"IsInternetAvaliable", "()I", "network.internet_available", true},
          {"showIAPDialog", "(I)V", "platform.unavailable", true}}, {}});
    const auto renderer = classes.RegisterClass(
        {"fixture/Renderer", {},
         {{"getKeyboardText", "()[B", "keyboard.text_bytes", true},
          {"setKeyboard", "(ILjava/lang/String;I)V", "keyboard.set", true},
          {"isKeyboardVisible", "()I", "keyboard.visible", true},
          {"swapEGLBuffers", "()V", "display.swap_managed_surface", true}}, {}});
    JniInvocationEngine invocations{classes};
    JniEnvironment environment;
    environment.AttachThread(7U);
    JniStringStore strings;
    JniPrimitiveArrayStore arrays;
    AndroidGuestPlatformState state;
    BindAndroidGuestJavaPlatformHandlers(
        invocations, environment, strings, arrays, state,
        {.installation_id = "fixture-id", .version_name = "1.2.3"});

    const auto invoke = [&](const JniObjectIdentity java_class,
                            const char* name, const char* signature,
                            const std::span<const JniValue> arguments = {}) {
        const auto method = classes.GetMethodId(
            java_class, name, signature, true);
        REQUIRE(method.has_value());
        return invocations.InvokeStatic(
            7U, java_class, *method, arguments,
            JniArgumentSource::value_array);
    };
    const auto identifier = std::get<JniReference>(
        invoke(device, "a", "()[B"));
    const auto identifier_object = environment.ResolveObjectForHle(
        7U, identifier);
    REQUIRE(identifier_object.has_value());
    CHECK(std::get<std::vector<JniByte>>(arrays.Region(
              *identifier_object, 0, arrays.Length(*identifier_object))) ==
          std::vector<JniByte>{'f', 'i', 'x', 't', 'u', 'r', 'e', '-', 'i', 'd'});
    CHECK(std::get<JniBoolean>(
              invoke(device, "IsWifiEnable", "()Z")) == 0U);
    const std::array<JniValue, 1> unique{JniInt{53412}};
    static_cast<void>(invoke(device, "e", "(I)V", unique));
    REQUIRE(state.UniqueCode().has_value());
    CHECK(*state.UniqueCode() == 53412);
    CHECK(std::get<JniInt>(invoke(
              game, "IsInternetAvaliable", "()I")) == 0);
    static_cast<void>(invoke(game, "sendAppToBackground", "()V"));
    static_cast<void>(invoke(game, "setFullyLoaded", "()V"));
    CHECK(state.BackgroundRequested());
    CHECK(state.FullyLoaded());

    const std::array<std::uint8_t, 4> keyboard_utf8{'t', 'e', 's', 't'};
    const auto keyboard_object = strings.CreateModifiedUtf8(keyboard_utf8);
    const auto keyboard = environment.PublishLocalObject(7U, keyboard_object);
    const std::array<JniValue, 3> set_keyboard{
        JniInt{1}, keyboard, JniInt{32}};
    static_cast<void>(invoke(
        renderer, "setKeyboard", "(ILjava/lang/String;I)V", set_keyboard));
    CHECK(std::get<JniInt>(invoke(
              renderer, "isKeyboardVisible", "()I")) == 1);
    CHECK(state.KeyboardText() == std::vector<JniChar>{'t', 'e', 's', 't'});
    static_cast<void>(invoke(renderer, "swapEGLBuffers", "()V"));
    CHECK(state.ManagedSwapRequests() == 1U);

    const std::array<JniValue, 1> dialog{JniInt{1}};
    CHECK_THROWS_WITH_AS(
        static_cast<void>(invoke(
            game, "showIAPDialog", "(I)V", dialog)),
        "Android guest platform callback is unavailable: showIAPDialog(I)V",
        AndroidGuestCallSessionError);
    environment.DetachThread(7U);
}

TEST_CASE("Android guest Java display handler records screen sleep policy") {
    ogplay::runtime::JniClassRegistry classes;
    const auto java_class = classes.RegisterClass(
        {"fixture/JavaDisplay", {},
         {{"changeDisplayMode", "(I)V", "display.change_mode", true}}, {}});
    const auto method = classes.GetMethodId(
        java_class, "changeDisplayMode", "(I)V", true);
    REQUIRE(method.has_value());
    ogplay::runtime::JniInvocationEngine invocations{classes};
    ogplay::runtime::FrameworkScreenPolicyState screen_policy;
    ogplay::runtime::BindAndroidGuestJavaDisplayHandlers(
        invocations, screen_policy);

    const std::array<ogplay::runtime::JniValue, 1> allow_sleep{
        ogplay::runtime::JniInt{1}};
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *method, allow_sleep,
        ogplay::runtime::JniArgumentSource::variadic));
    REQUIRE(screen_policy.SleepAllowed().has_value());
    CHECK(*screen_policy.SleepAllowed());

    const std::array<ogplay::runtime::JniValue, 1> keep_awake{
        ogplay::runtime::JniInt{0}};
    static_cast<void>(invocations.InvokeStatic(
        2U, java_class, *method, keep_awake,
        ogplay::runtime::JniArgumentSource::value_array));
    REQUIRE(screen_policy.SleepAllowed().has_value());
    CHECK_FALSE(*screen_policy.SleepAllowed());
    CHECK(screen_policy.RequestCount() == 2U);
}

TEST_CASE("Android guest Java process exit requests session shutdown") {
    ogplay::runtime::JniClassRegistry classes;
    const auto java_class = classes.RegisterClass(
        {"fixture/JavaProcess", {},
         {{"Exit", "()V", "process.exit", true}}, {}});
    const auto method = classes.GetMethodId(java_class, "Exit", "()V", true);
    REQUIRE(method.has_value());
    ogplay::runtime::JniInvocationEngine invocations{classes};
    ogplay::runtime::AndroidGuestProcessState process_state;
    ogplay::runtime::BindAndroidGuestJavaProcessHandlers(
        invocations, process_state);

    CHECK_FALSE(process_state.ExitRequested());
    CHECK(process_state.ExitRequestCount() == 0U);
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *method, {},
        ogplay::runtime::JniArgumentSource::variadic));
    CHECK(process_state.ExitRequested());
    CHECK(process_state.ExitRequestCount() == 1U);
}

TEST_CASE("Android guest Java locale handler returns legacy language index") {
    ogplay::runtime::JniClassRegistry classes;
    const auto java_class = classes.RegisterClass(
        {"fixture/JavaLocale", {},
         {{"detectPhoneLang", "()I", "locale.detect_phone_language", true}},
         {}});
    const auto method = classes.GetMethodId(
        java_class, "detectPhoneLang", "()I", true);
    REQUIRE(method.has_value());
    ogplay::runtime::JniInvocationEngine invocations{classes};
    ogplay::runtime::BindAndroidGuestJavaLocaleHandlers(
        invocations, {"fra", "FR"});

    const auto result = invocations.InvokeStatic(
        1U, java_class, *method, {},
        ogplay::runtime::JniArgumentSource::value_array);
    CHECK(std::get<ogplay::runtime::JniInt>(result) == 0);
    CHECK(ogplay::runtime::LegacyPhoneLanguageIndex({"de", "DE"}) == 1);
    CHECK(ogplay::runtime::LegacyPhoneLanguageIndex({"ita", "IT"}) == 2);
    CHECK(ogplay::runtime::LegacyPhoneLanguageIndex({"es", "ES"}) == 3);
    CHECK(ogplay::runtime::LegacyPhoneLanguageIndex({"jpn", "JP"}) == 4);
    CHECK(ogplay::runtime::LegacyPhoneLanguageIndex({"eng", "US"}) == 5);
    CHECK(ogplay::runtime::LegacyPhoneLanguageIndex({"pt", "BR"}) == 6);
    CHECK(ogplay::runtime::LegacyPhoneLanguageIndex({"zh", "CN"}) == 5);
    CHECK_THROWS_AS(
        static_cast<void>(
            ogplay::runtime::LegacyPhoneLanguageIndex({"EN", "US"})),
        ogplay::runtime::FrameworkLocaleError);
}

TEST_CASE("Android guest Java movie handler publishes the exact request") {
    ogplay::runtime::JniClassRegistry classes;
    const auto java_class = classes.RegisterClass(
        {"fixture/JavaMovie", {},
         {{"loadMovie", "(Ljava/lang/String;)V", "audio.load_movie", true}},
         {}});
    const auto method = classes.GetMethodId(
        java_class, "loadMovie", "(Ljava/lang/String;)V", true);
    REQUIRE(method.has_value());

    ogplay::runtime::JniEnvironment environment;
    environment.AttachThread(7U);
    ogplay::runtime::JniStringStore strings;
    const std::array<ogplay::runtime::JniChar, 9> name{
        'i', 'n', 't', 'r', 'o', '.', 'm', 'p', '4'};
    const auto identity = strings.Create(name);
    const auto reference = environment.PublishLocalObject(7U, identity);
    ogplay::runtime::JniInvocationEngine invocations{classes};
    ogplay::runtime::AndroidGuestMovieState movie_state;
    ogplay::runtime::BindAndroidGuestJavaMovieHandlers(
        invocations, environment, strings, movie_state);

    const std::array<ogplay::runtime::JniValue, 1> arguments{reference};
    static_cast<void>(invocations.InvokeStatic(
        7U, java_class, *method, arguments,
        ogplay::runtime::JniArgumentSource::variadic));
    const auto latest = movie_state.Latest();
    REQUIRE(latest.has_value());
    CHECK(latest->sequence == 1U);
    CHECK(latest->thread_id == 7U);
    CHECK(latest->name == std::vector<ogplay::runtime::JniChar>{
                              name.begin(), name.end()});
    CHECK(movie_state.RequestCount() == 1U);

    const std::array<ogplay::runtime::JniValue, 1> null_arguments{
        ogplay::runtime::JniReference{}};
    CHECK_THROWS_WITH_AS(
        static_cast<void>(invocations.InvokeStatic(
            7U, java_class, *method, null_arguments,
            ogplay::runtime::JniArgumentSource::value_array)),
        "Android guest movie name cannot be null",
        ogplay::runtime::AndroidGuestCallSessionError);
    CHECK(movie_state.RequestCount() == 1U);

    const auto non_string = environment.PublishLocalObject(
        7U, ogplay::runtime::AllocateJniHostObjectIdentity());
    const std::array<ogplay::runtime::JniValue, 1> non_string_arguments{
        non_string};
    CHECK_THROWS_WITH_AS(
        static_cast<void>(invocations.InvokeStatic(
            7U, java_class, *method, non_string_arguments,
            ogplay::runtime::JniArgumentSource::value_array)),
        "JNI object is not a string in this store",
        ogplay::runtime::JniStringError);
    CHECK(movie_state.RequestCount() == 1U);

    const std::vector<ogplay::runtime::JniChar> oversized(4097U, 'x');
    CHECK_THROWS_WITH_AS(
        movie_state.Request(7U, oversized),
        "Android guest movie request is invalid",
        ogplay::runtime::AndroidGuestCallSessionError);
    CHECK(movie_state.RequestCount() == 1U);
}

TEST_CASE("Android guest Java audio handlers own SoundPool lifecycle") {
    ogplay::runtime::JniClassRegistry classes;
    const auto java_class = classes.RegisterClass(
        {"fixture/JavaAudio", {},
         {{"destroySoundPool", "()V", "audio.destroy_sound_pool", true},
          {"initSoundPoolArray", "()V", "audio.init_sound_pool_array", true},
          {"stopAllSounds", "()V", "audio.stop_all_sounds", true},
          {"stopAllPool", "(I)V", "audio.stop_all_pool", true},
          {"stopAllBig", "(I)V", "audio.stop_all_big", true},
          {"isSoundLoaded", "(II)I", "audio.is_sound_loaded", true},
          {"isSoundLoadedBig", "(I)I", "audio.is_sound_loaded_big", true},
          {"unloadSound", "(II)V", "audio.unload_sound", true},
          {"unloadSoundBig", "(I)V", "audio.unload_sound_big", true},
          {"playSound", "(IIF)V", "audio.play_sound", true},
          {"playSoundBig", "(IF)V", "audio.play_sound_big", true},
          {"playSoundBigLooping", "(IFZ)V",
           "audio.play_sound_big_looping", true},
          {"loadSound", "(II)V", "audio.load_sound", true},
          {"loadSoundBig", "(I)V", "audio.load_sound_big", true},
          {"pauseSound", "(II)V", "audio.pause_sound", true},
          {"pauseSoundBig", "(I)V", "audio.pause_sound_big", true},
          {"pauseAllSoundBig", "()V", "audio.pause_all_big", true},
          {"resumeSound", "(II)V", "audio.resume_sound", true},
          {"resumeSoundBig", "(I)V", "audio.resume_sound_big", true},
          {"resumeAllSoundBig", "()V", "audio.resume_all_big", true},
          {"stopSound", "(II)V", "audio.stop_sound", true},
          {"stopSoundBig", "(I)V", "audio.stop_sound_big", true},
          {"setVolume", "(IIF)V", "audio.set_volume", true},
          {"setVolumeBig", "(IF)V", "audio.set_volume_big", true},
          {"setPitch", "(IIF)V", "audio.set_pitch", true},
          {"resetSound", "(I)V", "audio.reset_sound", true}},
         {}});
    const auto method = classes.GetMethodId(
        java_class, "destroySoundPool", "()V", true);
    REQUIRE(method.has_value());
    const auto initialize = classes.GetMethodId(
        java_class, "initSoundPoolArray", "()V", true);
    REQUIRE(initialize.has_value());
    const auto stop_all = classes.GetMethodId(
        java_class, "stopAllSounds", "()V", true);
    const auto stop_pool = classes.GetMethodId(
        java_class, "stopAllPool", "(I)V", true);
    const auto stop_big = classes.GetMethodId(
        java_class, "stopAllBig", "(I)V", true);
    const auto is_loaded = classes.GetMethodId(
        java_class, "isSoundLoaded", "(II)I", true);
    const auto is_loaded_big = classes.GetMethodId(
        java_class, "isSoundLoadedBig", "(I)I", true);
    const auto unload = classes.GetMethodId(
        java_class, "unloadSound", "(II)V", true);
    const auto unload_big = classes.GetMethodId(
        java_class, "unloadSoundBig", "(I)V", true);
    const auto play = classes.GetMethodId(
        java_class, "playSound", "(IIF)V", true);
    const auto play_big = classes.GetMethodId(
        java_class, "playSoundBig", "(IF)V", true);
    const auto play_big_looping = classes.GetMethodId(
        java_class, "playSoundBigLooping", "(IFZ)V", true);
    const auto load = classes.GetMethodId(
        java_class, "loadSound", "(II)V", true);
    const auto load_big = classes.GetMethodId(
        java_class, "loadSoundBig", "(I)V", true);
    const auto pause = classes.GetMethodId(
        java_class, "pauseSound", "(II)V", true);
    const auto pause_big = classes.GetMethodId(
        java_class, "pauseSoundBig", "(I)V", true);
    const auto pause_all_big = classes.GetMethodId(
        java_class, "pauseAllSoundBig", "()V", true);
    const auto resume = classes.GetMethodId(
        java_class, "resumeSound", "(II)V", true);
    const auto resume_big = classes.GetMethodId(
        java_class, "resumeSoundBig", "(I)V", true);
    const auto resume_all_big = classes.GetMethodId(
        java_class, "resumeAllSoundBig", "()V", true);
    const auto stop = classes.GetMethodId(
        java_class, "stopSound", "(II)V", true);
    const auto stop_big_voice = classes.GetMethodId(
        java_class, "stopSoundBig", "(I)V", true);
    const auto set_volume = classes.GetMethodId(
        java_class, "setVolume", "(IIF)V", true);
    const auto set_volume_big = classes.GetMethodId(
        java_class, "setVolumeBig", "(IF)V", true);
    const auto set_pitch = classes.GetMethodId(
        java_class, "setPitch", "(IIF)V", true);
    const auto reset = classes.GetMethodId(
        java_class, "resetSound", "(I)V", true);
    REQUIRE(stop_all.has_value());
    REQUIRE(stop_pool.has_value());
    REQUIRE(stop_big.has_value());
    REQUIRE(is_loaded.has_value());
    REQUIRE(is_loaded_big.has_value());
    REQUIRE(unload.has_value());
    REQUIRE(unload_big.has_value());
    REQUIRE(play.has_value());
    REQUIRE(play_big.has_value());
    REQUIRE(play_big_looping.has_value());
    REQUIRE(load.has_value());
    REQUIRE(load_big.has_value());
    REQUIRE(pause.has_value());
    REQUIRE(pause_big.has_value());
    REQUIRE(pause_all_big.has_value());
    REQUIRE(resume.has_value());
    REQUIRE(resume_big.has_value());
    REQUIRE(resume_all_big.has_value());
    REQUIRE(stop.has_value());
    REQUIRE(stop_big_voice.has_value());
    REQUIRE(set_volume.has_value());
    REQUIRE(set_volume_big.has_value());
    REQUIRE(set_pitch.has_value());
    REQUIRE(reset.has_value());
    ogplay::runtime::JniInvocationEngine invocations{classes};
    CHECK_THROWS_WITH_AS(
        static_cast<void>(invocations.InvokeStatic(
            1U, java_class, *method, {},
            ogplay::runtime::JniArgumentSource::variadic)),
        "JNI method implementation has no registered handler: "
        "audio.destroy_sound_pool",
        ogplay::runtime::JniInvocationError);

    ogplay::audio::JavaSoundPoolState sound_pool;
    ogplay::runtime::BindAndroidGuestJavaAudioHandlers(
        invocations, sound_pool);
    CHECK(sound_pool.Active());
    CHECK(std::holds_alternative<std::monostate>(
        invocations.InvokeStatic(
            1U, java_class, *method, {},
            ogplay::runtime::JniArgumentSource::variadic)));
    CHECK_FALSE(sound_pool.Active());
    CHECK(sound_pool.DestructionCount() == 1U);
    CHECK_FALSE(sound_pool.MarkLoaded(
        ogplay::audio::JavaSoundPoolKind::pool, 2));
    CHECK(sound_pool.LoadedResourceCount() == 0U);

    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *method, {},
        ogplay::runtime::JniArgumentSource::va_list));
    CHECK(sound_pool.DestructionCount() == 1U);

    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *initialize, {},
        ogplay::runtime::JniArgumentSource::variadic));
    CHECK(sound_pool.Active());
    CHECK(sound_pool.InitializationCount() == 1U);

    const std::array<ogplay::runtime::JniValue, 2> pool_resource{
        ogplay::runtime::JniInt{2}, ogplay::runtime::JniInt{7}};
    const std::array<ogplay::runtime::JniValue, 1> big_resource{
        ogplay::runtime::JniInt{7}};
    const std::array<ogplay::runtime::JniValue, 3> pool_play{
        ogplay::runtime::JniInt{2}, ogplay::runtime::JniInt{7},
        ogplay::runtime::JniFloat{0.5F}};
    const std::array<ogplay::runtime::JniValue, 2> big_play{
        ogplay::runtime::JniInt{7}, ogplay::runtime::JniFloat{0.75F}};
    const std::array<ogplay::runtime::JniValue, 3> big_looping_play{
        ogplay::runtime::JniInt{7}, ogplay::runtime::JniFloat{0.75F},
        ogplay::runtime::JniBoolean{1U}};
    CHECK(std::get<ogplay::runtime::JniInt>(invocations.InvokeStatic(
              1U, java_class, *is_loaded, pool_resource,
              ogplay::runtime::JniArgumentSource::value_array)) == -1);
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *play, pool_play,
        ogplay::runtime::JniArgumentSource::variadic));
    CHECK(sound_pool.ActiveVoiceCount() == 0U);
    CHECK(sound_pool.PendingLoadCount() == 1U);
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *load, pool_resource,
        ogplay::runtime::JniArgumentSource::value_array));
    CHECK(sound_pool.PendingLoadCount() == 1U);
    CHECK(sound_pool.RequestLoad(
        ogplay::audio::JavaSoundPoolKind::pool, 99));
    CHECK(sound_pool.PendingLoadCount() == 2U);
    CHECK(sound_pool.Unload(
        ogplay::audio::JavaSoundPoolKind::pool, 99));
    CHECK(sound_pool.PendingLoadCount() == 1U);
    CHECK(sound_pool.MarkLoaded(
        ogplay::audio::JavaSoundPoolKind::pool, 2));
    CHECK(sound_pool.PendingLoadCount() == 0U);
    CHECK(std::get<ogplay::runtime::JniInt>(invocations.InvokeStatic(
              1U, java_class, *is_loaded, pool_resource,
              ogplay::runtime::JniArgumentSource::variadic)) == 0);
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *play, pool_play,
        ogplay::runtime::JniArgumentSource::va_list));
    CHECK(sound_pool.ActiveVoiceCount() == 1U);
    CHECK(std::get<ogplay::runtime::JniInt>(invocations.InvokeStatic(
              1U, java_class, *is_loaded_big, big_resource,
              ogplay::runtime::JniArgumentSource::va_list)) == -1);
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *load_big, big_resource,
        ogplay::runtime::JniArgumentSource::variadic));
    CHECK(sound_pool.PendingLoadCount() == 1U);
    CHECK(sound_pool.MarkLoaded(
        ogplay::audio::JavaSoundPoolKind::big, 7));
    CHECK(sound_pool.PendingLoadCount() == 0U);
    CHECK(std::get<ogplay::runtime::JniInt>(invocations.InvokeStatic(
              1U, java_class, *is_loaded_big, big_resource,
              ogplay::runtime::JniArgumentSource::value_array)) == 0);
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *play_big, big_play,
        ogplay::runtime::JniArgumentSource::value_array));
    CHECK_FALSE(sound_pool.Snapshot(
                    ogplay::audio::JavaSoundPoolKind::big, 7, 0)->looping);
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *play_big_looping, big_looping_play,
        ogplay::runtime::JniArgumentSource::variadic));
    CHECK(sound_pool.Snapshot(
              ogplay::audio::JavaSoundPoolKind::big, 7, 0)->looping);
    CHECK(sound_pool.ActiveVoiceCount() == 2U);
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *pause, pool_resource,
        ogplay::runtime::JniArgumentSource::variadic));
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *pause_big, big_resource,
        ogplay::runtime::JniArgumentSource::va_list));
    const auto paused_pool = sound_pool.Snapshot(
        ogplay::audio::JavaSoundPoolKind::pool, 2, 7);
    const auto paused_big = sound_pool.Snapshot(
        ogplay::audio::JavaSoundPoolKind::big, 7, 0);
    REQUIRE(paused_pool.has_value());
    REQUIRE(paused_big.has_value());
    CHECK(paused_pool->status ==
          ogplay::audio::JavaSoundPoolVoiceStatus::paused);
    CHECK(paused_big->status ==
          ogplay::audio::JavaSoundPoolVoiceStatus::paused);
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *resume_all_big, {},
        ogplay::runtime::JniArgumentSource::value_array));
    CHECK(sound_pool.Snapshot(
              ogplay::audio::JavaSoundPoolKind::pool, 2, 7)->status ==
          ogplay::audio::JavaSoundPoolVoiceStatus::paused);
    CHECK(sound_pool.Snapshot(
              ogplay::audio::JavaSoundPoolKind::big, 7, 0)->status ==
          ogplay::audio::JavaSoundPoolVoiceStatus::playing);
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *pause_all_big, {},
        ogplay::runtime::JniArgumentSource::variadic));
    CHECK(sound_pool.Snapshot(
              ogplay::audio::JavaSoundPoolKind::big, 7, 0)->status ==
          ogplay::audio::JavaSoundPoolVoiceStatus::paused);
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *resume, pool_resource,
        ogplay::runtime::JniArgumentSource::value_array));
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *resume_big, big_resource,
        ogplay::runtime::JniArgumentSource::variadic));
    const std::array<ogplay::runtime::JniValue, 3> pool_properties{
        ogplay::runtime::JniInt{2}, ogplay::runtime::JniInt{7},
        ogplay::runtime::JniFloat{1.5F}};
    const std::array<ogplay::runtime::JniValue, 2> big_volume{
        ogplay::runtime::JniInt{7}, ogplay::runtime::JniFloat{0.5F}};
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *set_volume, pool_play,
        ogplay::runtime::JniArgumentSource::va_list));
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *set_volume_big, big_volume,
        ogplay::runtime::JniArgumentSource::value_array));
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *set_pitch, pool_properties,
        ogplay::runtime::JniArgumentSource::variadic));
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *reset, big_resource,
        ogplay::runtime::JniArgumentSource::va_list));
    const auto pool_snapshot = sound_pool.Snapshot(
        ogplay::audio::JavaSoundPoolKind::pool, 2, 7);
    const auto big_snapshot = sound_pool.Snapshot(
        ogplay::audio::JavaSoundPoolKind::big, 7, 0);
    REQUIRE(pool_snapshot.has_value());
    REQUIRE(big_snapshot.has_value());
    CHECK(pool_snapshot->status ==
          ogplay::audio::JavaSoundPoolVoiceStatus::playing);
    CHECK(pool_snapshot->volume == 0.5F);
    CHECK(pool_snapshot->pitch == 1.5F);
    CHECK(big_snapshot->status ==
          ogplay::audio::JavaSoundPoolVoiceStatus::playing);
    CHECK(big_snapshot->volume == 0.5F);
    CHECK(big_snapshot->reset_count == 1U);
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *stop, pool_resource,
        ogplay::runtime::JniArgumentSource::value_array));
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *stop_big_voice, big_resource,
        ogplay::runtime::JniArgumentSource::variadic));
    CHECK(sound_pool.ActiveVoiceCount() == 0U);
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *play_big, big_play,
        ogplay::runtime::JniArgumentSource::value_array));
    CHECK(sound_pool.ActiveVoiceCount() == 1U);
    CHECK(sound_pool.LoadedResourceCount() == 2U);
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *unload, pool_resource,
        ogplay::runtime::JniArgumentSource::variadic));
    CHECK(std::get<ogplay::runtime::JniInt>(invocations.InvokeStatic(
              1U, java_class, *is_loaded, pool_resource,
              ogplay::runtime::JniArgumentSource::va_list)) == -1);
    CHECK(sound_pool.ActiveVoiceCount() == 1U);
    CHECK(sound_pool.LoadedResourceCount() == 1U);
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *unload_big, big_resource,
        ogplay::runtime::JniArgumentSource::value_array));
    CHECK(sound_pool.LoadedResourceCount() == 0U);
    CHECK(sound_pool.ActiveVoiceCount() == 0U);
    CHECK(sound_pool.MarkLoaded(
        ogplay::audio::JavaSoundPoolKind::big, 7));
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *initialize, {},
        ogplay::runtime::JniArgumentSource::value_array));
    CHECK(sound_pool.InitializationCount() == 1U);

    CHECK(sound_pool.MarkLoaded(
        ogplay::audio::JavaSoundPoolKind::pool, 7));
    CHECK(sound_pool.MarkLoaded(
        ogplay::audio::JavaSoundPoolKind::pool, 8));
    CHECK(sound_pool.MarkLoaded(
        ogplay::audio::JavaSoundPoolKind::big, 9));
    CHECK(sound_pool.Play(
        ogplay::audio::JavaSoundPoolKind::pool, 7, 1, 1.0F));
    CHECK(sound_pool.Play(
        ogplay::audio::JavaSoundPoolKind::pool, 8, 2, 0.5F));
    CHECK(sound_pool.Play(
        ogplay::audio::JavaSoundPoolKind::big, 9, 0, 0.25F));
    CHECK(sound_pool.Play(
        ogplay::audio::JavaSoundPoolKind::pool, 7, 1, 0.25F));
    CHECK_FALSE(sound_pool.Play(
        ogplay::audio::JavaSoundPoolKind::pool, 7, 3, 1.25F));
    CHECK(sound_pool.ActiveVoiceCount() == 3U);
    const std::array<ogplay::runtime::JniValue, 1> keep_seven{
        ogplay::runtime::JniInt{7}};
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *stop_pool, keep_seven,
        ogplay::runtime::JniArgumentSource::variadic));
    CHECK(sound_pool.ActiveVoiceCount() == 2U);

    const std::array<ogplay::runtime::JniValue, 1> stop_every_big{
        ogplay::runtime::JniInt{-1}};
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *stop_big, stop_every_big,
        ogplay::runtime::JniArgumentSource::va_list));
    CHECK(sound_pool.ActiveVoiceCount() == 1U);
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *stop_all, {},
        ogplay::runtime::JniArgumentSource::value_array));
    CHECK(sound_pool.ActiveVoiceCount() == 0U);
    CHECK(sound_pool.LoadedResourceCount() == 4U);
    sound_pool.Destroy();
    CHECK(sound_pool.LoadedResourceCount() == 0U);
    CHECK(sound_pool.PendingLoadCount() == 0U);
    CHECK(std::get<ogplay::runtime::JniInt>(invocations.InvokeStatic(
              1U, java_class, *is_loaded_big, big_resource,
              ogplay::runtime::JniArgumentSource::variadic)) == -1);
}

TEST_CASE("Android guest SoundPool handlers commit decoded mixer voices") {
    ogplay::runtime::JniClassRegistry classes;
    const auto java_class = classes.RegisterClass(
        {"fixture/DecodedAudio", {},
         {{"loadSound", "(II)V", "audio.load_sound", true},
          {"loadSoundBig", "(I)V", "audio.load_sound_big", true},
          {"playSound", "(IIF)V", "audio.play_sound", true},
          {"playSoundBig", "(IF)V", "audio.play_sound_big", true},
          {"playSoundBigLooping", "(IFZ)V",
           "audio.play_sound_big_looping", true},
          {"pauseSound", "(II)V", "audio.pause_sound", true},
          {"pauseAllSoundBig", "()V", "audio.pause_all_big", true},
          {"resumeSound", "(II)V", "audio.resume_sound", true},
          {"resumeAllSoundBig", "()V", "audio.resume_all_big", true},
          {"stopSound", "(II)V", "audio.stop_sound", true},
          {"unloadSound", "(II)V", "audio.unload_sound", true}},
         {}});
    const auto load = classes.GetMethodId(
        java_class, "loadSound", "(II)V", true);
    const auto play = classes.GetMethodId(
        java_class, "playSound", "(IIF)V", true);
    const auto load_big = classes.GetMethodId(
        java_class, "loadSoundBig", "(I)V", true);
    const auto play_big_looping = classes.GetMethodId(
        java_class, "playSoundBigLooping", "(IFZ)V", true);
    const auto pause_all_big = classes.GetMethodId(
        java_class, "pauseAllSoundBig", "()V", true);
    const auto resume_all_big = classes.GetMethodId(
        java_class, "resumeAllSoundBig", "()V", true);
    const auto pause = classes.GetMethodId(
        java_class, "pauseSound", "(II)V", true);
    const auto resume = classes.GetMethodId(
        java_class, "resumeSound", "(II)V", true);
    const auto stop = classes.GetMethodId(
        java_class, "stopSound", "(II)V", true);
    const auto unload = classes.GetMethodId(
        java_class, "unloadSound", "(II)V", true);
    REQUIRE(load.has_value());
    REQUIRE(play.has_value());
    REQUIRE(load_big.has_value());
    REQUIRE(play_big_looping.has_value());
    REQUIRE(pause_all_big.has_value());
    REQUIRE(resume_all_big.has_value());
    REQUIRE(pause.has_value());
    REQUIRE(resume.has_value());
    REQUIRE(stop.has_value());
    REQUIRE(unload.has_value());

    const auto encoded = ReadAudioFixture();
    ogplay::audio::JavaSoundPoolMixer mixer{
        [&encoded](const ogplay::audio::EncodedAudioSource& source) {
            return source.resource == 7 ? encoded : std::vector<std::byte>{};
        }};
    ogplay::audio::JavaSoundPoolState state;
    ogplay::runtime::JniInvocationEngine invocations{classes};
    ogplay::runtime::BindAndroidGuestJavaAudioHandlers(
        invocations, state, &mixer);
    const std::array<ogplay::runtime::JniValue, 2> voice{
        ogplay::runtime::JniInt{7}, ogplay::runtime::JniInt{2}};
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *load, voice,
        ogplay::runtime::JniArgumentSource::variadic));
    CHECK(state.IsLoaded(ogplay::audio::JavaSoundPoolKind::pool, 7));
    CHECK(mixer.LoadedResourceCount() == 1U);

    const std::array<ogplay::runtime::JniValue, 3> play_voice{
        ogplay::runtime::JniInt{7}, ogplay::runtime::JniInt{2},
        ogplay::runtime::JniFloat{0.5F}};
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *play, play_voice,
        ogplay::runtime::JniArgumentSource::value_array));
    CHECK(state.ActiveVoiceCount() == 1U);
    CHECK(mixer.ActiveVoiceCount() == 1U);
    std::vector<std::int16_t> output(1024U * 2U);
    CHECK(mixer.RenderStereoPcm16(output, 48000U) == 1024U);
    CHECK(std::ranges::any_of(output, [](const auto sample) {
        return sample != 0;
    }));

    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *pause, voice,
        ogplay::runtime::JniArgumentSource::variadic));
    CHECK(mixer.RenderStereoPcm16(output, 48000U) == 1024U);
    CHECK(std::ranges::all_of(output, [](const auto sample) {
        return sample == 0;
    }));
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *resume, voice,
        ogplay::runtime::JniArgumentSource::variadic));
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *stop, voice,
        ogplay::runtime::JniArgumentSource::variadic));
    CHECK(state.ActiveVoiceCount() == 0U);
    CHECK(mixer.ActiveVoiceCount() == 0U);
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *unload, voice,
        ogplay::runtime::JniArgumentSource::variadic));
    CHECK(state.LoadedResourceCount() == 0U);
    CHECK(mixer.LoadedResourceCount() == 0U);

    const std::array<ogplay::runtime::JniValue, 1> big_resource{
        ogplay::runtime::JniInt{7}};
    const std::array<ogplay::runtime::JniValue, 3> big_looping_play{
        ogplay::runtime::JniInt{7}, ogplay::runtime::JniFloat{0.5F},
        ogplay::runtime::JniBoolean{1U}};
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *load_big, big_resource,
        ogplay::runtime::JniArgumentSource::variadic));
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *play_big_looping, big_looping_play,
        ogplay::runtime::JniArgumentSource::value_array));
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *pause_all_big, {},
        ogplay::runtime::JniArgumentSource::variadic));
    CHECK(mixer.RenderStereoPcm16(output, 48000U) == 1024U);
    CHECK(std::ranges::all_of(output, [](const auto sample) {
        return sample == 0;
    }));
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *resume_all_big, {},
        ogplay::runtime::JniArgumentSource::value_array));
    CHECK(mixer.RenderStereoPcm16(output, 48000U) == 1024U);
    CHECK(std::ranges::any_of(output, [](const auto sample) {
        return sample != 0;
    }));
    for (std::size_t chunk = 0; chunk < 256U; ++chunk) {
        static_cast<void>(mixer.RenderStereoPcm16(output, 48000U));
    }
    CHECK(mixer.ActiveVoiceCount() == 1U);
    CHECK(state.Snapshot(
              ogplay::audio::JavaSoundPoolKind::big, 7, 0)->looping);

    const std::array<ogplay::runtime::JniValue, 2> missing{
        ogplay::runtime::JniInt{99}, ogplay::runtime::JniInt{0}};
    static_cast<void>(invocations.InvokeStatic(
        1U, java_class, *load, missing,
        ogplay::runtime::JniArgumentSource::variadic));
    CHECK(state.PendingLoadCount() == 1U);
    REQUIRE(mixer.LoadFailure(99).has_value());
}
