#pragma once

#include <array>
#include <cstdint>

namespace ogplay::runtime {

// API19 StructStatVfs order. Units are blocks/entries, never host pointers.
struct GuestFileSystemStats final {
    std::array<std::uint64_t, 11> values{};
    // bsize, frsize, blocks, bfree, bavail, files, ffree, favail,
    // fsid, flag, namemax. Provenance/support is owned by the provider.
};

} // namespace ogplay::runtime
