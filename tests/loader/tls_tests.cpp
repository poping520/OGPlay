#include <doctest/doctest.h>

#include <cstddef>
#include <vector>

#include "ogplay/loader/tls.h"

namespace {

[[nodiscard]] ogplay::loader::Elf32Image TlsImage() {
    ogplay::loader::Elf32Image image;
    image.program_headers.push_back(
        {ogplay::loader::kElfProgramLoad, 0,
         ogplay::memory::GuestAddress{0x1000}, 0x80, 0x100, 6, 0x1000});
    image.program_headers.push_back(
        {ogplay::loader::kElfProgramTls, 0x20,
         ogplay::memory::GuestAddress{0x1020}, 4, 12, 4, 4});
    return image;
}

}  // namespace

TEST_CASE("ELF32 TLS preserves the initial image BSS size and alignment") {
    std::vector<std::byte> bytes(0x80, std::byte{});
    bytes[0x20] = std::byte{0x11};
    bytes[0x21] = std::byte{0x22};
    bytes[0x22] = std::byte{0x33};
    bytes[0x23] = std::byte{0x44};
    const auto tls = ogplay::loader::ReadElf32TlsInfo(bytes, TlsImage());
    REQUIRE(tls.has_value());
    CHECK(tls->address == ogplay::memory::GuestAddress{0x1020});
    CHECK(tls->file_size == 4);
    CHECK(tls->memory_size == 12);
    CHECK(tls->alignment == 4);
    CHECK(tls->initial_image ==
          std::vector<std::byte>{std::byte{0x11}, std::byte{0x22},
                                 std::byte{0x33}, std::byte{0x44}});

    auto without_tls = TlsImage();
    without_tls.program_headers.pop_back();
    CHECK_FALSE(ogplay::loader::ReadElf32TlsInfo(bytes, without_tls).has_value());
}

TEST_CASE("ELF32 TLS rejects ambiguous malformed and unbacked templates") {
    const std::vector<std::byte> bytes(0x80, std::byte{});
    SUBCASE("multiple segments") {
        auto image = TlsImage();
        image.program_headers.push_back(image.program_headers.back());
        CHECK_THROWS_AS(static_cast<void>(
                            ogplay::loader::ReadElf32TlsInfo(bytes, image)),
                        ogplay::loader::ElfError);
    }
    SUBCASE("file size exceeds memory size") {
        auto image = TlsImage();
        image.program_headers.back().file_size = 16;
        CHECK_THROWS_AS(static_cast<void>(
                            ogplay::loader::ReadElf32TlsInfo(bytes, image)),
                        ogplay::loader::ElfError);
    }
    SUBCASE("alignment is not a power of two") {
        auto image = TlsImage();
        image.program_headers.back().alignment = 3;
        CHECK_THROWS_AS(static_cast<void>(
                            ogplay::loader::ReadElf32TlsInfo(bytes, image)),
                        ogplay::loader::ElfError);
    }
    SUBCASE("template is not file backed by load") {
        auto image = TlsImage();
        image.program_headers.back().virtual_address =
            ogplay::memory::GuestAddress{0x1080};
        CHECK_THROWS_AS(static_cast<void>(
                            ogplay::loader::ReadElf32TlsInfo(bytes, image)),
                        ogplay::loader::ElfError);
    }
}
