#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/runtime/dexvm/guest_memory.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"
#include "ogplay/runtime/integration/api19_guest_process.h"

namespace {

constexpr ogplay::memory::GuestAddress kLibcAddress{0x10000000U};
constexpr ogplay::memory::GuestAddress kPropertyExportAddress{0x10000080U};

[[nodiscard]] std::uint32_t Read32(
    ogplay::memory::MemoryBus& bus,
    const ogplay::memory::GuestAddress address) {
    return bus.Read32(address, 1);
}

[[nodiscard]] ogplay::loader::Elf32LinkNamespace MakeNamespace() {
    ogplay::loader::Elf32LinkModule libc;
    libc.name = "libc.so";
    libc.load_bias = kLibcAddress;
    libc.symbols.symbols.push_back(
        {"", ogplay::memory::GuestAddress{0}, 0, 0, 0, 0, 0});
    libc.symbols.symbols.push_back(
        {"__system_property_area__", ogplay::memory::GuestAddress{0x80U},
         4, 1, 1, 0, 1});
    ogplay::loader::Elf32LinkNamespace result;
    result.modules.push_back(std::move(libc));
    result.load_order.push_back(0);
    result.lookup_scope.push_back(0);
    return result;
}

void MapLibcExport(ogplay::memory::AddressSpace& address_space) {
    address_space.Map(
        {kLibcAddress, address_space.PageSize()},
        ogplay::memory::PageProtection::read |
            ogplay::memory::PageProtection::write);
}

[[nodiscard]] std::string ReadCString(
    const ogplay::memory::AddressSpace& address_space,
    const ogplay::memory::GuestAddress address) {
    const auto length = address_space.CStringLength(address, 4096U, 1U);
    std::vector<std::byte> bytes(length);
    address_space.Read(address, bytes, 1U);
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

}  // namespace

TEST_CASE("API 19 guest process publishes complete reusable startup memory") {
    ogplay::memory::AddressSpace address_space;
    ogplay::memory::CheckedMemoryBus bus(address_space);
    MapLibcExport(address_space);
    bus.Write32(kPropertyExportAddress, 0xaabbccddU, 1);

    const auto process = ogplay::runtime::InitializeApi19GuestProcess(
        address_space, bus, MakeNamespace(), {1, "ogplay-profile"});

    CHECK(process.root_thread_id == 1);
    CHECK(process.thread_pointer == ogplay::runtime::kApi19GuestTlsAddress);
    CHECK(process.stack_top ==
          ogplay::runtime::kApi19GuestStackAddress.Add(
              ogplay::runtime::kApi19GuestStackSize - 64U));
    CHECK(process.return_trap ==
          ogplay::runtime::kApi19GuestReturnAddress);
    CHECK(process.environment ==
          ogplay::runtime::kApi19GuestEnvironmentAddress);
    CHECK(Read32(bus, ogplay::runtime::kApi19GuestPreinitAddress.Add(8)) ==
          process.environment.Value());
    CHECK(ReadCString(
              address_space,
              ogplay::memory::GuestAddress{Read32(bus, process.environment)}) ==
          "PATH=/sbin:/vendor/bin:/system/sbin:/system/bin:/system/xbin");
    CHECK(Read32(bus, ogplay::runtime::kApi19GuestThreadInfoAddress.Add(12)) ==
          ogplay::runtime::kApi19GuestStackAddress.Value());
    CHECK(Read32(bus, ogplay::runtime::kApi19GuestThreadInfoAddress.Add(16)) ==
          ogplay::runtime::kApi19GuestStackSize);
    CHECK(Read32(bus, ogplay::runtime::kApi19GuestThreadInfoAddress.Add(32)) ==
          1U);
    CHECK(Read32(bus, ogplay::runtime::kApi19GuestThreadInfoAddress.Add(60)) ==
          ogplay::runtime::kApi19GuestTlsAddress.Value());
    CHECK(Read32(bus, ogplay::runtime::kApi19GuestTlsAddress) ==
          ogplay::runtime::kApi19GuestTlsAddress.Value());
    CHECK(Read32(bus, ogplay::runtime::kApi19GuestTlsAddress.Add(4)) ==
          ogplay::runtime::kApi19GuestThreadInfoAddress.Value());
    CHECK(Read32(bus, ogplay::runtime::kApi19GuestTlsAddress.Add(12)) ==
          ogplay::runtime::kApi19GuestPreinitAddress.Value());
    CHECK(Read32(bus, kPropertyExportAddress) ==
          ogplay::runtime::kApi19GuestPropertyAreaAddress.Value());
    CHECK(Read32(bus, ogplay::runtime::kApi19GuestPropertyAreaAddress) == 20U);
    CHECK(Read32(bus,
                 ogplay::runtime::kApi19GuestPropertyAreaAddress.Add(8)) ==
          0x504f5250U);

    std::array<std::byte, 4> trap{};
    address_space.Fetch(ogplay::runtime::kApi19GuestReturnAddress, trap, 1);
    CHECK(trap == std::array<std::byte, 4>{
                      std::byte{0x01}, std::byte{0x00},
                      std::byte{0x00}, std::byte{0xef}});
    const std::array<std::byte, 1> value{std::byte{0xff}};
    CHECK_THROWS_AS(
        address_space.Write(ogplay::runtime::kApi19GuestReturnAddress,
                            value, 1),
        ogplay::memory::MemoryFault);

    std::array<std::byte, 14> name{};
    address_space.Read(
        ogplay::runtime::kApi19GuestPreinitAddress.Add(0xa0), name, 1);
    CHECK(std::string(reinterpret_cast<const char*>(name.data()),
                      name.size()) == "ogplay-profile");
}

TEST_CASE("API 19 guest process serializes one validated OGPlay environment") {
    ogplay::memory::AddressSpace address_space;
    ogplay::memory::CheckedMemoryBus bus(address_space);
    MapLibcExport(address_space);
    const auto environment = ogplay::runtime::GuestProcessEnvironment::Api19(
        "/storage/emulated/0");

    const auto process = ogplay::runtime::InitializeApi19GuestProcess(
        address_space, bus, MakeNamespace(), {1, "ogplay", &environment});
    std::vector<std::string> entries;
    for (std::size_t index = 0;; ++index) {
        const auto pointer = Read32(
            bus, process.environment.Add(index * sizeof(std::uint32_t)));
        if (pointer == 0U) break;
        entries.push_back(ReadCString(
            address_space, ogplay::memory::GuestAddress{pointer}));
    }
    CHECK(entries == std::vector<std::string>{"PATH=/sbin:/vendor/bin:/system/sbin:/system/bin:/system/xbin",
                                              "LD_LIBRARY_PATH=/vendor/lib:/system/lib", "ANDROID_ROOT=/system",
                                              "ANDROID_DATA=/data", "TZ=UTC", "EXTERNAL_STORAGE=/storage/emulated/0"});
    CHECK_FALSE(environment.Find("HOME").has_value());
    REQUIRE(environment.Find("EXTERNAL_STORAGE").has_value());
    CHECK(*environment.Find("EXTERNAL_STORAGE") == "/storage/emulated/0");
}

TEST_CASE("guest process environment rejects invalid and ambiguous entries") {
    using Entry = ogplay::runtime::GuestProcessEnvironmentEntry;
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::runtime::GuestProcessEnvironment(
            std::vector<Entry>{{"", "value"}})),
        std::invalid_argument);
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::runtime::GuestProcessEnvironment(
            std::vector<Entry>{{"A=B", "value"}})),
        std::invalid_argument);
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::runtime::GuestProcessEnvironment(
            std::vector<Entry>{{"A", "1"}, {"A", "2"}})),
        std::invalid_argument);
    CHECK_THROWS_AS(
        static_cast<void>(
            ogplay::runtime::GuestProcessEnvironment::Api19("relative")),
        std::invalid_argument);
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::runtime::GuestProcessEnvironment(
            std::vector<Entry>{{"A", std::string(4096U, 'x')}})),
        std::invalid_argument);
}

TEST_CASE("API 19 guest process rolls back a late fixed-layout collision") {
    ogplay::memory::AddressSpace address_space;
    ogplay::memory::CheckedMemoryBus bus(address_space);
    MapLibcExport(address_space);
    bus.Write32(kPropertyExportAddress, 0xaabbccddU, 1);
    const ogplay::memory::GuestRange occupied{
        ogplay::runtime::kApi19GuestPropertyAreaAddress,
        address_space.PageSize()};
    address_space.Map(occupied, ogplay::memory::PageProtection::read);

    CHECK_THROWS(static_cast<void>(
        ogplay::runtime::InitializeApi19GuestProcess(
            address_space, bus, MakeNamespace(), {1, "ogplay-profile"})));
    CHECK(Read32(bus, kPropertyExportAddress) == 0xaabbccddU);
    address_space.ValidateMapped(occupied);
    CHECK_THROWS_AS(
        address_space.ValidateMapped(
            {ogplay::runtime::kApi19GuestThreadInfoAddress,
             address_space.PageSize()}),
        ogplay::memory::MemoryFault);
    CHECK_THROWS_AS(
        address_space.ValidateMapped(
            {ogplay::runtime::kApi19GuestTlsAddress,
             address_space.PageSize()}),
        ogplay::memory::MemoryFault);
    CHECK_THROWS_AS(
        address_space.ValidateMapped(
            {ogplay::runtime::kApi19GuestEnvironmentAddress,
             address_space.PageSize()}),
        ogplay::memory::MemoryFault);
    CHECK_THROWS_AS(
        address_space.ValidateMapped(
            {ogplay::runtime::kApi19GuestStackAddress,
             ogplay::runtime::kApi19GuestStackSize}),
        ogplay::memory::MemoryFault);
}

TEST_CASE("API 19 guest process rejects invalid identity before mapping") {
    ogplay::memory::AddressSpace address_space;
    ogplay::memory::CheckedMemoryBus bus(address_space);
    MapLibcExport(address_space);
    const std::string oversized_name(32, 'x');

    CHECK_THROWS_AS(
        static_cast<void>(ogplay::runtime::InitializeApi19GuestProcess(
            address_space, bus, MakeNamespace(), {0, "ogplay"})),
        std::invalid_argument);
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::runtime::InitializeApi19GuestProcess(
            address_space, bus, MakeNamespace(), {1, oversized_name})),
        std::invalid_argument);
    CHECK_THROWS_AS(
        address_space.ValidateMapped(
            {ogplay::runtime::kApi19GuestThreadInfoAddress,
             address_space.PageSize()}),
        ogplay::memory::MemoryFault);
}

TEST_CASE("Published system properties are read by the real API19 libc trie lookup") {
    using namespace ogplay;
    std::vector<std::vector<std::byte>> images;
    std::vector<loader::Elf32ModuleInput> modules;
    for (const auto* name : {"libc.so", "libdl.so"}) {
        std::ifstream input(std::string(OGPLAY_SOURCE_DIR) + "/data/android/19/lib/" + name, std::ios::binary);
        REQUIRE(input.good());
        const std::vector<char> bytes{std::istreambuf_iterator<char>(input), {}};
        auto& image = images.emplace_back(bytes.size());
        std::transform(bytes.begin(), bytes.end(), image.begin(),
                       [](char value) { return static_cast<std::byte>(value); });
        modules.push_back(
            {name, image,
             memory::GuestAddress{0x10000000U + static_cast<std::uint32_t>(modules.size()) * 0x10000000U}});
    }
    runtime::VirtualFileSystem fs;
    auto process = runtime::AndroidGuestProcess::Start({19, modules, {}, 64, 36, 1000000, 1, &fs, {}});
    const auto call = [&](std::string_view name, std::array<std::uint32_t, 4> args) {
        return process->Invoke({process->FindModuleExport(0, name), args}).return_value;
    };
    const memory::GuestAddress scratch{call("malloc", {256})};
    REQUIRE(scratch.Value() != 0);
    const auto get = [&](const std::string& name) {
        process->GuestMemoryAccess().write(scratch, std::as_bytes(std::span{name.c_str(), name.size() + 1}));
        const auto length = call("__system_property_get", {scratch.Value(), scratch.Add(64).Value(), 0, 0});
        std::array<char, 92> value{};
        process->GuestMemoryAccess().read(scratch.Add(64), std::as_writable_bytes(std::span{value}));
        CHECK(std::strlen(value.data()) == length);
        return std::string(value.data());
    };
    // Siblings at every level exercise both binary-tree links and the
    // length-first ordering of cmp_prop_name.
    const std::string fingerprint(91, 'f');
    const std::vector<std::pair<std::string, std::string>> properties{
        {"ro.product.model", "SM-G900F"},       {"ro.product.brand", "samsung"},
        {"ro.product.cpu.abi", "armeabi-v7a"},  {"ro.build.fingerprint", fingerprint},
        {"ro.build.product", "klte"},           {"gsm.sim.state", "ABSENT"},
        {"ro.build.version.sdk", "19"},         {"ro.sf.lcd_density", "480"},
        {"config.disable_renderscript", "true"}};
    process->PublishSystemProperties(properties);
    for (const auto& [name, value] : properties) {
        CAPTURE(name);
        CHECK(get(name) == value);
    }
    CHECK(get("ro.product").empty());
    CHECK(get("ro.product.model.extra").empty());
    CHECK(get("ro.hardware").empty());
    // A later set replaces the earlier one entirely.
    process->PublishSystemProperties(
        std::vector<std::pair<std::string, std::string>>{{"ro.product.model", "GT-I9300"}});
    CHECK(get("ro.product.model") == "GT-I9300");
    CHECK(get("ro.product.brand").empty());
    for (const auto& invalid : std::vector<std::pair<std::string, std::string>>{
             {"ro.product.model", std::string(92, 'x')}, {std::string(32, 'n'), "1"}, {"ro..model", "1"}, {"", "1"}}) {
        CAPTURE(invalid.first);
        CHECK_THROWS_AS(process->PublishSystemProperties(std::span{&invalid, 1}), std::invalid_argument);
    }
    const std::vector<std::pair<std::string, std::string>> duplicate{{"ro.a", "1"}, {"ro.a", "2"}};
    CHECK_THROWS_AS(process->PublishSystemProperties(duplicate), std::invalid_argument);
    std::vector<std::pair<std::string, std::string>> oversized;
    for (int index = 0; index < 200; ++index)
        oversized.emplace_back("ro.oversized.p" + std::to_string(index), "v");
    CHECK_THROWS_AS(process->PublishSystemProperties(oversized), std::length_error);
    CHECK(get("ro.product.model") == "GT-I9300");  // rejected sets leave the area untouched
    static_cast<void>(call("free", {scratch.Value()}));
    process->Stop();
}
