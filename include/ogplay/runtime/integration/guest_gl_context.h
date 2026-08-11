#pragma once

#include <cstdint>

#include "ogplay/gles/gles_transfer_state.h"

namespace ogplay::runtime {

using GuestGlContextId = std::uint32_t;

struct SharedGlState final {
    gles::GlesTransferState transfer;
    std::uint32_t active_texture{0x84C0U};

    void Reset() noexcept;
};

class GuestGlContext final {
public:
    explicit GuestGlContext(GuestGlContextId id = 1U);

    [[nodiscard]] GuestGlContextId Id() const noexcept;
    [[nodiscard]] SharedGlState& Shared() noexcept;
    [[nodiscard]] const SharedGlState& Shared() const noexcept;
    void Reset() noexcept;

private:
    GuestGlContextId id_{};
    SharedGlState shared_;
};

}  // namespace ogplay::runtime
