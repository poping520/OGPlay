#include "ogplay/gles/etc1.h"

#include <array>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <vector>

#include <doctest/doctest.h>

TEST_CASE("ETC1 decoder follows individual subblocks and selector order") {
    const std::array block{
        std::byte{0xF0}, std::byte{}, std::byte{}, std::byte{},
        std::byte{}, std::byte{}, std::byte{}, std::byte{},
    };
    const auto decoded = ogplay::gles::DecodeEtc1Rgba8(4U, 4U, block);
    REQUIRE(decoded.size() == 64U);
    for (std::size_t y = 0; y < 4U; ++y) {
        for (std::size_t x = 0; x < 4U; ++x) {
            const auto offset = (y * 4U + x) * 4U;
            CHECK(decoded[offset] == (x < 2U ? std::byte{255} : std::byte{2}));
            CHECK(decoded[offset + 1U] == std::byte{2});
            CHECK(decoded[offset + 2U] == std::byte{2});
            CHECK(decoded[offset + 3U] == std::byte{255});
        }
    }
}

TEST_CASE("ETC1 decoder clips partial blocks and rejects invalid input") {
    const std::array<std::byte, 8> block{};
    const auto decoded = ogplay::gles::DecodeEtc1Rgba8(2U, 1U, block);
    CHECK(decoded == std::vector{
                         std::byte{2}, std::byte{2}, std::byte{2}, std::byte{255},
                         std::byte{2}, std::byte{2}, std::byte{2}, std::byte{255},
                     });
    CHECK_THROWS_AS(static_cast<void>(
                        ogplay::gles::DecodeEtc1Rgba8(0U, 1U, block)),
                    std::invalid_argument);
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::gles::DecodeEtc1Rgba8(
            4U, 4U, std::span{block}.first<7>())),
        std::invalid_argument);

    auto invalid_differential = block;
    invalid_differential[0] = std::byte{0x04};
    invalid_differential[3] = std::byte{0x02};
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::gles::DecodeEtc1Rgba8(
            4U, 4U, invalid_differential)),
        std::invalid_argument);
}

TEST_CASE("ETC1 decoder applies selector bits and differential flip") {
    const std::array selectors{
        std::byte{0x88}, std::byte{0x88}, std::byte{0x88}, std::byte{},
        std::byte{}, std::byte{0x0C}, std::byte{}, std::byte{0x0A},
    };
    const auto selected = ogplay::gles::DecodeEtc1Rgba8(1U, 4U, selectors);
    CHECK(selected[0] == std::byte{138});
    CHECK(selected[4] == std::byte{144});
    CHECK(selected[8] == std::byte{134});
    CHECK(selected[12] == std::byte{128});

    const std::uint32_t high = (10U << 27U) | (2U << 24U) |
                               (10U << 19U) | (2U << 16U) |
                               (10U << 11U) | (2U << 8U) | 3U;
    const std::array differential{
        static_cast<std::byte>(high >> 24U),
        static_cast<std::byte>(high >> 16U),
        static_cast<std::byte>(high >> 8U),
        static_cast<std::byte>(high),
        std::byte{}, std::byte{}, std::byte{}, std::byte{},
    };
    const auto flipped = ogplay::gles::DecodeEtc1Rgba8(1U, 4U, differential);
    CHECK(flipped[0] == std::byte{84});
    CHECK(flipped[4] == std::byte{84});
    CHECK(flipped[8] == std::byte{101});
    CHECK(flipped[12] == std::byte{101});
}
