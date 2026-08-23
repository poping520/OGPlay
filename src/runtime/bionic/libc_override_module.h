#pragma once

#include <cstdint>

#include "ogplay/runtime/bionic/guest_symbol_override_metadata.h"
#include "runtime/boundary/core/a32_call_frame.h"
#include "runtime/boundary/core/boundary_binding.h"

namespace ogplay::runtime {

// These handlers remain GuestSymbolOverride implementations for the real
// guest libc.so; they only share the direct Fast Host Call binding transport.
class LibcOverrideModule final {
public:
    explicit LibcOverrideModule(BoundaryCallServices& calls) noexcept
        : calls_(calls) {}
    [[nodiscard]] BoundaryCallServices& CallServices() noexcept { return calls_; }

#define OGPLAY_DECLARE_OVERRIDE(library, symbol, id, count, method) \
    std::uint32_t method(const A32CallFrame& call) { \
        return ExecuteBionicMemoryIntercept( \
            calls_.address_space, \
            {symbol, call.RegisterArguments(), call.ThreadId()}); \
    }
    OGPLAY_GUEST_SYMBOL_OVERRIDE_EXPORTS(OGPLAY_DECLARE_OVERRIDE)
#undef OGPLAY_DECLARE_OVERRIDE

private:
    BoundaryCallServices& calls_;
};

}  // namespace ogplay::runtime
