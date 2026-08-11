#include "ogplay/runtime/integration/guest_gl_context.h"

#include <stdexcept>

namespace ogplay::runtime {

void SharedGlState::Reset() noexcept {
    transfer = {};
    active_texture = 0x84C0U;
}

GuestGlContext::GuestGlContext(const GuestGlContextId id) : id_(id) {
    if (id_ == 0U) {
        throw std::invalid_argument("guest GL context identity must not be zero");
    }
}

GuestGlContextId GuestGlContext::Id() const noexcept { return id_; }

SharedGlState& GuestGlContext::Shared() noexcept { return shared_; }

const SharedGlState& GuestGlContext::Shared() const noexcept { return shared_; }

void GuestGlContext::Reset() noexcept { shared_.Reset(); }

}  // namespace ogplay::runtime
