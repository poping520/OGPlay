#include "ogplay/runtime/bionic/guest_symbol_override.h"

#include <array>

namespace ogplay::runtime {

std::span<const GuestSymbolOverrideDescriptor>
GuestSymbolOverrides() noexcept {
    static constexpr std::array overrides{
        GuestSymbolOverrideDescriptor{"libc.so", "memcpy", 0, 3},
        GuestSymbolOverrideDescriptor{"libc.so", "memmove", 1, 3},
        GuestSymbolOverrideDescriptor{"libc.so", "memset", 2, 3},
        GuestSymbolOverrideDescriptor{"libc.so", "memcmp", 3, 3},
        GuestSymbolOverrideDescriptor{"libc.so", "strlen", 4, 1},
    };
    return overrides;
}

}  // namespace ogplay::runtime
