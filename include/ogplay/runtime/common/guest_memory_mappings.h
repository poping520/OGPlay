#pragma once

#include <cstdint>
#include <functional>

#include "ogplay/memory/address.h"

namespace ogplay::runtime {

inline constexpr std::uint32_t kApi19MmapPageSize = 4096;

struct GuestMmapRequest final {
    memory::GuestAddress address;
    std::uint32_t size, protection, flags;
    std::int32_t fd;
    std::uint32_t page_offset;
};

// Linux results: a zero-extended guest address or a negative errno for map,
// zero or a negative errno for unmap and sync.
struct GuestMemoryMappings final {
    std::function<std::int64_t(const GuestMmapRequest&)> map;
    std::function<std::int32_t(memory::GuestAddress, std::uint32_t)> unmap;
    std::function<std::int32_t(memory::GuestAddress, std::uint32_t, std::uint32_t, std::uint64_t)> sync;
};

}  // namespace ogplay::runtime
