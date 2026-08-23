#include "ogplay/runtime/bionic/guest_symbol_override.h"

#include <array>

#include "ogplay/runtime/bionic/guest_symbol_override_metadata.h"

namespace ogplay::runtime {

std::span<const GuestSymbolOverrideDescriptor>
GuestSymbolOverrides() noexcept {
    static constexpr std::array overrides{
#define OGPLAY_OVERRIDE_DESCRIPTOR(library, symbol, id, count, method)         \
        GuestSymbolOverrideDescriptor{library, symbol, id, count},
        OGPLAY_GUEST_SYMBOL_OVERRIDE_EXPORTS(OGPLAY_OVERRIDE_DESCRIPTOR)
#undef OGPLAY_OVERRIDE_DESCRIPTOR
    };
    return overrides;
}

}  // namespace ogplay::runtime
