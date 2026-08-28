#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace ogplay::core {

[[nodiscard]] constexpr bool RangeFits(const std::size_t total,
                                       const std::size_t offset,
                                       const std::size_t size) noexcept {
    return offset <= total && size <= total - offset;
}

template <typename Byte, std::size_t Extent>
[[nodiscard]] constexpr bool RangeFits(const std::span<Byte, Extent> bytes,
                                       const std::size_t offset,
                                       const std::size_t size) noexcept {
    return RangeFits(bytes.size(), offset, size);
}

template <typename Integer, typename Byte, std::size_t Extent>
[[nodiscard]] constexpr Integer ReadLittleEndian(
    const std::span<Byte, Extent> bytes, const std::size_t offset) noexcept {
    static_assert(std::is_unsigned_v<Integer>);
    static_assert(sizeof(Byte) == 1U);
    Integer value{};
    for (std::size_t index = 0; index < sizeof(Integer); ++index) {
        const auto byte = [&] {
            if constexpr (std::is_same_v<std::remove_cv_t<Byte>, std::byte>) {
                return std::to_integer<unsigned char>(bytes[offset + index]);
            } else {
                return static_cast<unsigned char>(bytes[offset + index]);
            }
        }();
        value |= static_cast<Integer>(byte)
                 << static_cast<unsigned>(index * 8U);
    }
    return value;
}

}  // namespace ogplay::core
