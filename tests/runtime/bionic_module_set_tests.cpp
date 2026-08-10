#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "ogplay/loader/elf.h"
#include "ogplay/runtime/bionic/bionic_module_set.h"

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

[[nodiscard]] std::vector<std::byte> DynamicElf(
    const std::string_view soname,
    const std::span<const std::string_view> needed = {}) {
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
    Put32(bytes, 76, 5);
    Put32(bytes, 80, 0x1000);
    Put32(bytes, 84, ogplay::loader::kElfProgramDynamic);
    Put32(bytes, 88, 0x100);
    Put32(bytes, 92, 0x10100U);
    Put32(bytes, 108, 6);
    Put32(bytes, 112, 4);

    std::vector<std::string_view> strings{soname};
    strings.insert(strings.end(), needed.begin(), needed.end());
    std::vector<std::uint32_t> offsets;
    std::size_t string_cursor = 1;
    for (const auto value : strings) {
        offsets.push_back(static_cast<std::uint32_t>(string_cursor));
        for (const char byte : value) {
            bytes[0x200 + string_cursor++] = static_cast<std::byte>(byte);
        }
        ++string_cursor;
    }
    const auto dynamic_count = 4U + needed.size();
    Put32(bytes, 100, static_cast<std::uint32_t>(dynamic_count * 8U));
    Put32(bytes, 104, static_cast<std::uint32_t>(dynamic_count * 8U));
    std::size_t dynamic = 0x100;
    const auto entry = [&](const std::int32_t tag, const std::uint32_t value) {
        Put32(bytes, dynamic, static_cast<std::uint32_t>(tag));
        Put32(bytes, dynamic + 4, value);
        dynamic += 8;
    };
    entry(ogplay::loader::kElfDynamicStringTable, 0x10200U);
    entry(ogplay::loader::kElfDynamicStringTableSize,
          static_cast<std::uint32_t>(string_cursor));
    entry(ogplay::loader::kElfDynamicSoname, offsets.front());
    for (std::size_t index = 0; index < needed.size(); ++index) {
        entry(ogplay::loader::kElfDynamicNeeded, offsets[index + 1]);
    }
    entry(ogplay::loader::kElfDynamicNull, 0);
    return bytes;
}

}  // namespace

TEST_CASE("Bionic module set discovers guest closure and excludes HLE boundaries") {
    const std::string_view root_needed[]{"libc.so", "libGLESv1_CM.so",
                                         "libstdc++.so"};
    const std::string_view stdcpp_needed[]{"libc.so", "libm.so"};
    const auto root = DynamicElf("game.so", root_needed);
    const auto libc = DynamicElf("libc.so");
    const auto libm = DynamicElf("libm.so");
    const auto libdl = DynamicElf("libdl.so");
    const auto stdcpp = DynamicElf("libstdc++.so", stdcpp_needed);
    const ogplay::runtime::BionicModuleSource sources[]{
        {"libc.so", libc}, {"libm.so", libm}, {"libdl.so", libdl},
        {"libstdc++.so", stdcpp}};

    const auto set = ogplay::runtime::BuildBionicModuleSet(
        ogplay::runtime::SelectBionicProfile(19), "game.so", root, sources);
    REQUIRE(set.Modules().size() == 4);
    CHECK(set.RootName() == "game.so");
    CHECK(set.Modules()[0].name == "game.so");
    CHECK(set.Modules()[1].name == "libc.so");
    CHECK(set.Modules()[2].name == "libstdc++.so");
    CHECK(set.Modules()[3].name == "libm.so");
    const auto inputs = set.Inputs();
    REQUIRE(inputs.size() == set.Modules().size());
    for (std::size_t index = 0; index < inputs.size(); ++index) {
        CHECK(inputs[index].name == set.Modules()[index].name);
        CHECK(inputs[index].bytes.size() == set.Modules()[index].image.size());
        CHECK(inputs[index].load_bias == set.Modules()[index].load_bias);
        CHECK(inputs[index].load_bias.Value() <
              ogplay::runtime::kBionicHleThunkBegin);
        if (index != 0) {
            CHECK(inputs[index - 1].load_bias < inputs[index].load_bias);
        }
    }
}

TEST_CASE("Bionic module set rejects unresolved and contradictory sources") {
    const auto profile = ogplay::runtime::SelectBionicProfile(19);
    SUBCASE("missing declared guest dependency") {
        const std::string_view needed[]{"libstdc++.so"};
        const auto root = DynamicElf("game.so", needed);
        CHECK_THROWS_WITH_AS(
            static_cast<void>(ogplay::runtime::BuildBionicModuleSet(
                profile, "game.so", root, {})),
            "required Bionic system library is missing: libstdc++.so",
            ogplay::runtime::BionicProfileError);
    }
    SUBCASE("dependency outside the Bionic profile") {
        const std::string_view needed[]{"libmystery.so"};
        const auto root = DynamicElf("game.so", needed);
        CHECK_THROWS_WITH_AS(
            static_cast<void>(ogplay::runtime::BuildBionicModuleSet(
                profile, "game.so", root, {})),
            "ELF requires undeclared Bionic library: libmystery.so",
            ogplay::runtime::BionicProfileError);
    }
    SUBCASE("source SONAME must agree with its catalog name") {
        const std::string_view needed[]{"libc.so"};
        const auto root = DynamicElf("game.so", needed);
        const auto wrong = DynamicElf("other.so");
        const ogplay::runtime::BionicModuleSource sources[]{{"libc.so", wrong}};
        CHECK_THROWS_WITH_AS(
            static_cast<void>(ogplay::runtime::BuildBionicModuleSet(
                profile, "game.so", root, sources)),
            "Bionic module SONAME does not match source name: libc.so",
            ogplay::runtime::BionicProfileError);
    }
}

TEST_CASE("Bionic module set preserves an APK root name with a SONAME alias") {
    const std::string_view needed[]{"libc.so"};
    const auto root = DynamicElf("libjni.so", needed);
    const auto libc = DynamicElf("libc.so");
    const ogplay::runtime::BionicModuleSource sources[]{{"libc.so", libc}};

    const auto set = ogplay::runtime::BuildBionicModuleSet(
        ogplay::runtime::SelectBionicProfile(19), "libgame.so", root,
        sources);

    REQUIRE(set.Modules().size() == 2);
    CHECK(set.RootName() == "libgame.so");
    CHECK(set.Modules()[0].name == "libgame.so");
    CHECK(set.Modules()[1].name == "libc.so");
}
