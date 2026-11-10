#pragma once
#include "ogplay/loader/resources.h"
#include "ogplay/core/byte_order.h"

namespace ogplay::loader::resources::detail {
inline void require(bool valid, std::string_view message) {
    if (!valid) throw ResourceError(std::string(message));
}

inline Bytes range(Bytes b, std::size_t off, std::size_t len) {
    require(off <= b.size() && len <= b.size() - off, "resource offset outside chunk");
    return b.subspan(off, len);
}

inline std::uint16_t u16(Bytes b, std::size_t off) {
    return core::ReadLittleEndian<std::uint16_t>(range(b, off, 2), 0);
}

inline std::uint32_t u32(Bytes b, std::size_t off) {
    return core::ReadLittleEndian<std::uint32_t>(range(b, off, 4), 0);
}

inline std::uint8_t u8(Bytes b, std::size_t off) {
    return std::to_integer<std::uint8_t>(range(b, off, 1)[0]);
}

inline Bytes chunk(Bytes b, std::size_t off) {
    auto h = range(b, off, 8);
    auto n = u32(h, 4);
    require(u16(h, 2) >= 8 && n >= u16(h, 2) && n % 4 == 0, "invalid resource chunk header");
    return range(b, off, n);
}

inline Value value(Bytes b, std::size_t off) {
    auto v = range(b, off, 8);
    require(u16(v, 0) >= 8 && u8(v, 2) == 0, "invalid typed value");
    return {u8(v, 3), u32(v, 4)};
}

inline void stringIndex(const ResStringPool& pool, std::uint32_t index, bool optional = false) {
    require((optional && index == NoIndex) || index < pool.size(), "resource string index outside pool");
}
}  // namespace ogplay::loader::resources::detail
