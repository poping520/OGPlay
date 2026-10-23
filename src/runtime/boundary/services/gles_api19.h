#pragma once

#include <limits>
#include <string>
#include <string_view>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"
#include "runtime/boundary/core/boundary_binding.h"

namespace ogplay::runtime {

inline constexpr std::size_t kNoGlesAlias = (std::numeric_limits<std::size_t>::max)();

// Only ABI-identical OES/IMG aliases of an existing core entry are forwarded.
// Export availability does not advertise the corresponding extension.
template <typename Functions>
constexpr std::size_t GlesCoreAlias(const std::string_view name, const Functions& functions) {
    if (!name.ends_with("OES") && !name.ends_with("IMG")) return kNoGlesAlias;
    const auto core = name.substr(0, name.size() - 3U);
    for (std::size_t index = 0; index < functions.size(); ++index)
        if (functions[index].name == core) return index;
    return kNoGlesAlias;
}

inline std::uint32_t UnimplementedGl(BoundaryCallServices& services, const A32CallFrame& call,
                                     const std::string_view library, const std::string_view symbol) {
    if (services.ledger) services.ledger->RecordUnimplemented("runtime.gles_unimplemented", call.LinkRegister());
    if (services.logger)
        services.logger->Write(core::LogLevel::error, "guest.gles", "called unimplemented OpenGL ES API",
                               {.guest_thread = call.ThreadId()},
                               {{"library", std::string(library)},
                                {"symbol", std::string(symbol)},
                                {"lr", core::GuestAddress{call.LinkRegister()}}});
    // Android 4.4 Loader.cpp/gl_unimplemented: no guest memory access or GL
    // state mutation. Return a deterministic zero for non-void A32 entries.
    return 0U;
}

}  // namespace ogplay::runtime
