#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "ogplay/gles/pvrtc.h"

namespace {

void Write32(std::span<std::byte> bytes, const std::size_t offset,
             const std::uint32_t value) {
    for (std::size_t index = 0; index < 4U; ++index) {
        bytes[offset + index] = static_cast<std::byte>(
            (value >> static_cast<unsigned>(index * 8U)) & 0xffU);
    }
}

std::vector<std::byte> UniformWords(const std::uint32_t color) {
    std::vector<std::byte> result(32U);
    for (std::size_t word = 0; word < 4U; ++word) {
        Write32(result, word * 8U, 0U);
        Write32(result, word * 8U + 4U, color);
    }
    return result;
}

std::vector<std::byte> NonUniformWords() {
    constexpr std::array<std::uint32_t, 4> kModulation = {
        0x00000000U, 0xffffffffU, 0xaaaaaaaaU, 0x55555555U};
    constexpr std::array<std::uint32_t, 4> kColor = {
        0xfc00fc00U, 0x83e083e0U, 0x801e801eU, 0xffffffffU};
    std::vector<std::byte> result(32U);
    for (std::size_t word = 0; word < kColor.size(); ++word) {
        Write32(result, word * 8U, kModulation[word]);
        Write32(result, word * 8U + 4U, kColor[word]);
    }
    return result;
}

std::uint64_t Fnv1a64(const std::span<const std::byte> bytes) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto value : bytes) {
        hash ^= std::to_integer<std::uint8_t>(value);
        hash *= 1099511628211ULL;
    }
    return hash;
}

void CheckSolidRed(const std::span<const std::byte> rgba,
                   const std::uint8_t alpha) {
    REQUIRE((rgba.size() & 3U) == 0U);
    for (std::size_t offset = 0; offset < rgba.size(); offset += 4U) {
        CHECK(rgba[offset] == std::byte{0xff});
        CHECK(rgba[offset + 1U] == std::byte{0x00});
        CHECK(rgba[offset + 2U] == std::byte{0x00});
        CHECK(rgba[offset + 3U] == static_cast<std::byte>(alpha));
    }
}

}  // namespace

TEST_CASE("PVRTC1 decoder expands uniform 2bpp and 4bpp blocks") {
    constexpr std::uint32_t kOpaqueRed = 0xfc00fc00U;
    const auto compressed = UniformWords(kOpaqueRed);

    const auto rgba4 = ogplay::gles::DecodePvrtc1Rgba8(
        8, 8, 4, false, compressed);
    CHECK(rgba4.size() == 8U * 8U * 4U);
    CheckSolidRed(rgba4, 0xffU);

    const auto rgba2 = ogplay::gles::DecodePvrtc1Rgba8(
        16, 8, 2, false, compressed);
    CHECK(rgba2.size() == 16U * 8U * 4U);
    CheckSolidRed(rgba2, 0xffU);
}

TEST_CASE("PVRTC1 decoder validates shape size and RGB opacity") {
    constexpr std::uint32_t kTransparentRed = 0x78007800U;
    const auto compressed = UniformWords(kTransparentRed);
    const auto transparent = ogplay::gles::DecodePvrtc1Rgba8(
        8, 8, 4, false, compressed);
    const auto opaque = ogplay::gles::DecodePvrtc1Rgba8(
        8, 8, 4, true, compressed);
    CHECK(transparent[3] != std::byte{0xff});
    CHECK(opaque[3] == std::byte{0xff});

    CHECK_THROWS_AS(static_cast<void>(ogplay::gles::DecodePvrtc1Rgba8(
                        7, 8, 4, false, compressed)),
                    std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(ogplay::gles::DecodePvrtc1Rgba8(
                        8, 8, 3, false, compressed)),
                    std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(ogplay::gles::DecodePvrtc1Rgba8(
                        8, 8, 4, false,
                        std::span<const std::byte>{compressed}.first(31))),
                    std::invalid_argument);
}

TEST_CASE("PowerVR PVRTC decoder preserves twiddled nonuniform words") {
    const auto compressed = NonUniformWords();
    const auto rgba4 = ogplay::gles::DecodePvrtc1Rgba8(
        8, 8, 4, false, compressed);
    const auto rgba2 = ogplay::gles::DecodePvrtc1Rgba8(
        16, 8, 2, false, compressed);

    CHECK(Fnv1a64(rgba4) == 17872741518131426047ULL);
    CHECK(Fnv1a64(rgba2) == 14019542757011266872ULL);
}
