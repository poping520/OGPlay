#include "ogplay/gles/atc.h"
#include "ogplay/gles/guest_transfer.h"

#include <array>
#include <limits>
#include <stdexcept>
#include "compressonator_tc.h"

namespace ogplay::gles {
namespace {
std::uint32_t ReadLE32(const std::span<const std::byte> bytes) {
    std::uint32_t value{};
    for (unsigned i = 0; i < 4U; ++i)
        value |= std::to_integer<std::uint32_t>(bytes[i]) << (8U * i);
    return value;
}
// Alpha formulas follow AMD codec_atc.cpp at the pinned vendor revision.
// Copyright (c) 2007-2024 Advanced Micro Devices, Inc.; MIT permission notice
// retained in third_party/compressonator/LICENSE.txt.
std::array<std::uint8_t, 16> DecodeAlpha(
    const std::span<const std::byte> block, const std::uint32_t format) {
    std::array<std::uint8_t, 16> alpha{};
    if (format == kAtcRgb) { alpha.fill(255U); return alpha; }
    if (format == kAtcRgbaExplicit) {
        for (unsigned i = 0; i < 16U; ++i) {
            const auto nibble = (std::to_integer<unsigned>(block[i / 2U]) >>
                                 ((i % 2U) * 4U)) & 15U;
            alpha[i] = static_cast<std::uint8_t>(nibble * 17U);
        }
        return alpha;
    }
    std::array<unsigned, 8> ramp{};
    ramp[0] = std::to_integer<unsigned>(block[0]);
    ramp[1] = std::to_integer<unsigned>(block[1]);
    const unsigned denominator = ramp[0] > ramp[1] ? 7U : 5U;
    for (unsigned i = 1U; i < denominator; ++i)
        ramp[i + 1U] = ((denominator - i) * ramp[0] + i * ramp[1] +
                       denominator / 2U) / denominator;
    if (denominator == 5U) { ramp[6] = 0U; ramp[7] = 255U; }
    std::uint64_t selectors{};
    for (unsigned i = 0; i < 6U; ++i)
        selectors |= std::to_integer<std::uint64_t>(block[i + 2U]) << (8U * i);
    for (unsigned i = 0; i < 16U; ++i)
        alpha[i] = static_cast<std::uint8_t>(ramp[(selectors >> (3U * i)) & 7U]);
    return alpha;
}
}  // namespace

std::size_t AtcImageBytes(const std::uint32_t width, const std::uint32_t height,
                          const std::uint32_t format) {
    if (!IsAtcFormat(format)) throw std::invalid_argument("ATC format is invalid");
    const auto blocks_x = (static_cast<std::uint64_t>(width) + 3U) / 4U;
    const auto blocks_y = (static_cast<std::uint64_t>(height) + 3U) / 4U;
    const auto blocks = blocks_x * blocks_y;
    const auto stride = format == kAtcRgb ? 8U : 16U;
    if (blocks > (std::numeric_limits<std::uint64_t>::max)() / stride)
        throw std::length_error("ATC compressed size overflows uint64_t");
    const auto bytes = blocks * stride;
    if (bytes > (std::numeric_limits<std::size_t>::max)())
        throw std::length_error("ATC compressed size overflows size_t");
    return static_cast<std::size_t>(bytes);
}

std::vector<std::byte> DecodeAtcRgba8(
    const std::uint32_t width, const std::uint32_t height,
    const std::uint32_t format, const std::span<const std::byte> compressed) {
    if (compressed.size() != AtcImageBytes(width, height, format))
        throw std::invalid_argument("ATC compressed size does not match dimensions");
    const auto pixels = static_cast<std::uint64_t>(width) * height;
    if (pixels > kDefaultGuestTransferLimit / 4U)
        throw std::length_error("ATC decoded image exceeds transfer budget");
    std::vector<std::byte> output(static_cast<std::size_t>(pixels * 4U));
    if (output.empty()) return output;
    const auto blocks_x = (static_cast<std::uint64_t>(width) + 3U) / 4U;
    const auto blocks_y = (static_cast<std::uint64_t>(height) + 3U) / 4U;
    const std::size_t block_bytes = format == kAtcRgb ? 8U : 16U;
    for (std::uint64_t y = 0; y < blocks_y; ++y) {
        for (std::uint64_t x = 0; x < blocks_x; ++x) {
            const auto block = compressed.subspan(
                static_cast<std::size_t>(y * blocks_x + x) * block_bytes, block_bytes);
            const auto colors = format == kAtcRgb ? block : block.subspan(8U);
            const auto endpoints = ReadLE32(colors);
            Color888_t rgb[4][4]{};
            atiDecodeRGBBlockATITC(&rgb, ReadLE32(colors.subspan(4U)),
                                   endpoints & 0xffffU, endpoints >> 16U);
            const auto alpha = DecodeAlpha(block, format);
            for (std::uint32_t row = 0; row < 4U; ++row) {
                for (std::uint32_t col = 0; col < 4U; ++col) {
                    const auto px = x * 4U + col, py = y * 4U + row;
                    if (px >= width || py >= height) continue;
                    const auto offset = static_cast<std::size_t>((py * width + px) * 4U);
                    output[offset] = static_cast<std::byte>(rgb[row][col].red);
                    output[offset + 1U] = static_cast<std::byte>(rgb[row][col].green);
                    output[offset + 2U] = static_cast<std::byte>(rgb[row][col].blue);
                    output[offset + 3U] = static_cast<std::byte>(alpha[row * 4U + col]);
                }
            }
        }
    }
    return output;
}
}  // namespace ogplay::gles
