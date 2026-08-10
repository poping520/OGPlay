#include "ogplay/gles/pvrtc.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

#include "PVRTDecompress.h"

namespace ogplay::gles {
namespace {

[[nodiscard]] bool IsPowerOfTwo(const std::uint32_t value) noexcept {
    return value != 0U && (value & (value - 1U)) == 0U;
}

[[nodiscard]] std::size_t CheckedSize(const std::uint32_t width,
                                      const std::uint32_t height,
                                      const std::uint8_t bits_per_pixel) {
    const auto true_width = std::max(
        width, bits_per_pixel == 2U ? std::uint32_t{16U} : std::uint32_t{8U});
    const auto true_height = std::max(height, std::uint32_t{8U});
    if (true_width > (std::numeric_limits<std::uint64_t>::max)() / true_height) {
        throw std::length_error("PVRTC compressed size overflows uint64_t");
    }
    const auto pixels = static_cast<std::uint64_t>(true_width) * true_height;
    if (pixels > (std::numeric_limits<std::uint64_t>::max)() / bits_per_pixel) {
        throw std::length_error("PVRTC compressed size overflows uint64_t");
    }
    const auto bits = pixels * bits_per_pixel;
    const auto bytes = bits / 8U;
    if (bytes > (std::numeric_limits<std::size_t>::max)()) {
        throw std::length_error("PVRTC compressed size overflows size_t");
    }
    return static_cast<std::size_t>(bytes);
}

[[nodiscard]] std::size_t CheckedOutputSize(const std::uint32_t width,
                                            const std::uint32_t height) {
    if (width > (std::numeric_limits<std::uint64_t>::max)() / height) {
        throw std::length_error("PVRTC output size overflows uint64_t");
    }
    const auto pixels = static_cast<std::uint64_t>(width) * height;
    if (pixels > (std::numeric_limits<std::uint64_t>::max)() / 4U) {
        throw std::length_error("PVRTC output size overflows uint64_t");
    }
    const auto bytes = pixels * 4U;
    if (bytes > (std::numeric_limits<std::size_t>::max)()) {
        throw std::length_error("PVRTC output size overflows size_t");
    }
    return static_cast<std::size_t>(bytes);
}

}  // namespace

std::vector<std::byte> DecodePvrtc1Rgba8(
    const std::uint32_t width, const std::uint32_t height,
    const std::uint8_t bits_per_pixel, const bool opaque,
    const std::span<const std::byte> compressed) {
    if (!IsPowerOfTwo(width) || !IsPowerOfTwo(height)) {
        throw std::invalid_argument("PVRTC dimensions must be powers of two");
    }
    if (bits_per_pixel != 2U && bits_per_pixel != 4U) {
        throw std::invalid_argument("PVRTC bits per pixel must be 2 or 4");
    }

    const auto compressed_size = CheckedSize(width, height, bits_per_pixel);
    if (compressed.size() != compressed_size) {
        throw std::invalid_argument("PVRTC compressed size does not match dimensions");
    }

    // The upstream implementation reads uint32_t words directly. Copy the guest
    // payload into aligned storage before crossing that API boundary.
    std::vector<std::uint32_t> aligned_words(compressed_size / sizeof(std::uint32_t));
    std::memcpy(aligned_words.data(), compressed.data(), compressed_size);

    std::vector<std::byte> result(CheckedOutputSize(width, height));
    const auto decoded_size = pvr::PVRTDecompressPVRTC(
        aligned_words.data(), bits_per_pixel == 2U ? 1U : 0U, width, height,
        reinterpret_cast<std::uint8_t*>(result.data()));
    if (decoded_size != compressed_size) {
        throw std::runtime_error("PowerVR PVRTC decoder returned an unexpected size");
    }

    if (opaque) {
        for (std::size_t offset = 3U; offset < result.size(); offset += 4U) {
            result[offset] = std::byte{0xff};
        }
    }
    return result;
}

}  // namespace ogplay::gles
