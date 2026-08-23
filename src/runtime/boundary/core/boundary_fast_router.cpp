#include "runtime/boundary/core/boundary_fast_router.h"

namespace ogplay::runtime {

BoundaryFastRouter::BoundaryFastRouter(BoundaryThunkArena& arena) noexcept
    : arena_(arena) {}

void BoundaryFastRouter::Resize(const std::size_t size) { hot_.resize(size); }
BoundaryHotEntry& BoundaryFastRouter::Entry(const std::size_t slot) {
    return hot_.at(slot);
}
const BoundaryHotEntry& BoundaryFastRouter::Entry(const std::size_t slot) const {
    return hot_.at(slot);
}
std::span<const BoundaryHotEntry> BoundaryFastRouter::Entries() const noexcept {
    return hot_;
}
cpu::HostCallHook BoundaryFastRouter::Hook() noexcept { return {&Dispatch, this}; }

cpu::HostCallResult BoundaryFastRouter::Dispatch(
    void* userdata, const std::uint32_t svc,
    cpu::A32HostCallContext& call) noexcept {
    if (userdata == nullptr || svc != 2U) return cpu::HostCallResult::unhandled;
    return static_cast<BoundaryFastRouter*>(userdata)->TryFastCall(call);
}

cpu::HostCallResult BoundaryFastRouter::TryFastCall(
    cpu::A32HostCallContext& call) noexcept {
    const auto slot = arena_.DecodeSlot(call.pc.Value());
    if (slot == BoundaryThunkArena::InvalidSlot() || slot >= hot_.size() ||
        hot_[slot].invoke == nullptr) {
        return cpu::HostCallResult::unhandled;
    }
    return hot_[slot].invoke(hot_[slot].self, call);
}

}  // namespace ogplay::runtime
