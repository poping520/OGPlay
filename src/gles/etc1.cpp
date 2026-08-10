#include "ogplay/gles/etc1.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace ogplay::gles {
namespace {

constexpr std::array<std::array<std::int32_t, 4>, 8> kModifiers{{
    {{2, 8, -2, -8}},
    {{5, 17, -5, -17}},
    {{9, 29, -9, -29}},
    {{13, 42, -13, -42}},
    {{18, 60, -18, -60}},
    {{24, 80, -24, -80}},
    {{33, 106, -33, -106}},
    {{47, 183, -47, -183}},
}};

struct Color final {
    std::int32_t red{};
    std::int32_t green{};
    std::int32_t blue{};
};

[[nodiscard]] std::uint32_t ReadBigEndian32(
    const std::span<const std::byte, 4> bytes) noexcept {
    std::uint32_t result{};
    for (const auto byte : bytes) {
        result = (result << 8U) | std::to_integer<std::uint32_t>(byte);
    }
    return result;
}

[[nodiscard]] std::int32_t SignedThreeBits(const std::uint32_t value) noexcept {
    const auto narrow = static_cast<std::int32_t>(value & 7U);
    return (narrow & 4) != 0 ? narrow - 8 : narrow;
}

[[nodiscard]] std::int32_t ExtendFourBits(const std::uint32_t value) noexcept {
    return static_cast<std::int32_t>((value << 4U) | value);
}

[[nodiscard]] std::int32_t ExtendFiveBits(const std::int32_t value) {
    if (value < 0 || value > 31) {
        throw std::invalid_argument("ETC1 differential color is out of range");
    }
    return (value << 3) | (value >> 2);
}

[[nodiscard]] std::byte ClampColor(const std::int32_t value) noexcept {
    if (value <= 0) return std::byte{};
    if (value >= 255) return std::byte{255};
    return static_cast<std::byte>(value);
}

void DecodeBlock(const std::span<const std::byte, 8> block,
                 const std::uint32_t block_x,
                 const std::uint32_t block_y,
                 const std::uint32_t width,
                 const std::uint32_t height,
                 std::span<std::byte> output) {
    const auto high = ReadBigEndian32(block.first<4>());
    const auto low = ReadBigEndian32(block.last<4>());
    std::array<Color, 2> bases{};
    if ((high & 2U) == 0U) {
        bases[0] = {
            ExtendFourBits((high >> 28U) & 15U),
            ExtendFourBits((high >> 20U) & 15U),
            ExtendFourBits((high >> 12U) & 15U),
        };
        bases[1] = {
            ExtendFourBits((high >> 24U) & 15U),
            ExtendFourBits((high >> 16U) & 15U),
            ExtendFourBits((high >> 8U) & 15U),
        };
    } else {
        const auto red = static_cast<std::int32_t>((high >> 27U) & 31U);
        const auto green = static_cast<std::int32_t>((high >> 19U) & 31U);
        const auto blue = static_cast<std::int32_t>((high >> 11U) & 31U);
        bases[0] = {ExtendFiveBits(red), ExtendFiveBits(green),
                    ExtendFiveBits(blue)};
        bases[1] = {
            ExtendFiveBits(red + SignedThreeBits(high >> 24U)),
            ExtendFiveBits(green + SignedThreeBits(high >> 16U)),
            ExtendFiveBits(blue + SignedThreeBits(high >> 8U)),
        };
    }

    const std::array tables{
        static_cast<std::size_t>((high >> 5U) & 7U),
        static_cast<std::size_t>((high >> 2U) & 7U),
    };
    const bool flipped = (high & 1U) != 0U;
    for (std::uint32_t x = 0; x < 4U; ++x) {
        for (std::uint32_t y = 0; y < 4U; ++y) {
            const auto destination_x = block_x * 4U + x;
            const auto destination_y = block_y * 4U + y;
            if (destination_x >= width || destination_y >= height) continue;

            const std::uint32_t pixel = x * 4U + y;
            const auto selector = static_cast<std::size_t>(
                ((low >> (pixel + 15U)) & 2U) | ((low >> pixel) & 1U));
            const std::size_t subblock = flipped ? (y >= 2U) : (x >= 2U);
            const auto modifier = kModifiers[tables[subblock]][selector];
            const auto offset = static_cast<std::size_t>(
                (destination_y * width + destination_x) * 4U);
            output[offset] = ClampColor(bases[subblock].red + modifier);
            output[offset + 1U] = ClampColor(bases[subblock].green + modifier);
            output[offset + 2U] = ClampColor(bases[subblock].blue + modifier);
            output[offset + 3U] = std::byte{255};
        }
    }
}

}  // namespace

std::vector<std::byte> DecodeEtc1Rgba8(
    const std::uint32_t width,
    const std::uint32_t height,
    const std::span<const std::byte> compressed) {
    if (width == 0U || height == 0U) {
        throw std::invalid_argument("ETC1 dimensions must be positive");
    }
    const auto blocks_wide = (static_cast<std::uint64_t>(width) + 3U) / 4U;
    const auto blocks_high = (static_cast<std::uint64_t>(height) + 3U) / 4U;
    const auto block_count = blocks_wide * blocks_high;
    if (block_count > std::numeric_limits<std::size_t>::max() / 8U ||
        compressed.size() != block_count * 8U) {
        throw std::invalid_argument("ETC1 data size does not match dimensions");
    }
    const auto pixel_count = static_cast<std::uint64_t>(width) * height;
    if (pixel_count > std::numeric_limits<std::size_t>::max() / 4U) {
        throw std::length_error("ETC1 decoded image exceeds host limits");
    }

    std::vector<std::byte> output(static_cast<std::size_t>(pixel_count * 4U));
    for (std::uint64_t block_y = 0; block_y < blocks_high; ++block_y) {
        for (std::uint64_t block_x = 0; block_x < blocks_wide; ++block_x) {
            const auto index = static_cast<std::size_t>(
                (block_y * blocks_wide + block_x) * 8U);
            DecodeBlock(std::span<const std::byte, 8>{compressed.subspan(index, 8U)},
                        static_cast<std::uint32_t>(block_x),
                        static_cast<std::uint32_t>(block_y), width, height, output);
        }
    }
    return output;
}

}  // namespace ogplay::gles
