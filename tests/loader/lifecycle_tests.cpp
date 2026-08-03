#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ogplay/loader/lifecycle.h"

namespace {

void Put32(std::vector<std::byte>& bytes, const std::size_t offset,
           const std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        bytes[offset + index] = static_cast<std::byte>(
            (value >> static_cast<unsigned>(index * 8U)) & 0xffU);
    }
}

[[nodiscard]] ogplay::loader::Elf32Image LifecycleImage() {
    ogplay::loader::Elf32Image image;
    image.has_dynamic_segment = true;
    image.program_headers.push_back(
        {ogplay::loader::kElfProgramLoad, 0,
         ogplay::memory::GuestAddress{0x1000}, 0x80, 0x80, 5, 0x1000});
    image.program_headers.push_back(
        {ogplay::loader::kElfProgramArmExidx, 0x30,
         ogplay::memory::GuestAddress{0x1030}, 16, 16, 4, 4});
    image.dynamic_entries = {
        {ogplay::loader::kElfDynamicInit, 0x1101},
        {ogplay::loader::kElfDynamicFini, 0x1201},
        {ogplay::loader::kElfDynamicInitArray, 0x1010},
        {ogplay::loader::kElfDynamicInitArraySize, 8},
        {ogplay::loader::kElfDynamicFiniArray, 0x1020},
        {ogplay::loader::kElfDynamicFiniArraySize, 4},
    };
    return image;
}

}  // namespace

TEST_CASE("ELF lifecycle preserves init fini arrays and ARM exidx facts") {
    std::vector<std::byte> bytes(0x80);
    Put32(bytes, 0x10, 0x1301);
    Put32(bytes, 0x14, 0x1401);
    Put32(bytes, 0x20, 0x1501);
    const auto lifecycle = ogplay::loader::ReadElf32LifecycleInfo(
        bytes, LifecycleImage());
    CHECK(*lifecycle.init == ogplay::memory::GuestAddress{0x1101});
    CHECK(*lifecycle.fini == ogplay::memory::GuestAddress{0x1201});
    REQUIRE(lifecycle.init_array.size() == 2);
    CHECK(lifecycle.init_array[0] ==
          ogplay::memory::GuestAddress{0x1301});
    CHECK(lifecycle.init_array[1] ==
          ogplay::memory::GuestAddress{0x1401});
    REQUIRE(lifecycle.fini_array.size() == 1);
    CHECK(lifecycle.fini_array[0] ==
          ogplay::memory::GuestAddress{0x1501});
    REQUIRE(lifecycle.arm_exidx.has_value());
    CHECK(lifecycle.arm_exidx->address ==
          ogplay::memory::GuestAddress{0x1030});
    CHECK(lifecycle.arm_exidx->entry_count == 2);
}

TEST_CASE("ELF lifecycle rejects incomplete ambiguous and unbacked metadata") {
    std::vector<std::byte> bytes(0x80);
    SUBCASE("incomplete array pair") {
        auto image = LifecycleImage();
        image.dynamic_entries.erase(image.dynamic_entries.begin() + 3);
        CHECK_THROWS_AS(static_cast<void>(
                            ogplay::loader::ReadElf32LifecycleInfo(bytes, image)),
                        ogplay::loader::ElfError);
    }
    SUBCASE("array size is not word aligned") {
        auto image = LifecycleImage();
        image.dynamic_entries[3].value = 6;
        CHECK_THROWS_AS(static_cast<void>(
                            ogplay::loader::ReadElf32LifecycleInfo(bytes, image)),
                        ogplay::loader::ElfError);
    }
    SUBCASE("array is outside file backed load") {
        auto image = LifecycleImage();
        image.dynamic_entries[2].value = 0x1080;
        CHECK_THROWS_AS(static_cast<void>(
                            ogplay::loader::ReadElf32LifecycleInfo(bytes, image)),
                        ogplay::loader::ElfError);
    }
    SUBCASE("duplicate init tag") {
        auto image = LifecycleImage();
        image.dynamic_entries.push_back(
            {ogplay::loader::kElfDynamicInit, 0x1601});
        CHECK_THROWS_AS(static_cast<void>(
                            ogplay::loader::ReadElf32LifecycleInfo(bytes, image)),
                        ogplay::loader::ElfError);
    }
    SUBCASE("multiple exidx segments") {
        auto image = LifecycleImage();
        image.program_headers.push_back(image.program_headers[1]);
        CHECK_THROWS_AS(static_cast<void>(
                            ogplay::loader::ReadElf32LifecycleInfo(bytes, image)),
                        ogplay::loader::ElfError);
    }
    SUBCASE("exidx size is not entry aligned") {
        auto image = LifecycleImage();
        image.program_headers[1].file_size = 12;
        image.program_headers[1].memory_size = 12;
        CHECK_THROWS_AS(static_cast<void>(
                            ogplay::loader::ReadElf32LifecycleInfo(bytes, image)),
                        ogplay::loader::ElfError);
    }
}
