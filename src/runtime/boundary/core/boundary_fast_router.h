#pragma once

#include <span>
#include <vector>

#include "runtime/boundary/core/boundary_binding.h"
#include "runtime/boundary/core/boundary_thunk_arena.h"

namespace ogplay::runtime {

class BoundaryFastRouter final {
public:
    explicit BoundaryFastRouter(BoundaryThunkArena& arena) noexcept;

    void Resize(std::size_t size);
    [[nodiscard]] BoundaryHotEntry& Entry(std::size_t slot);
    [[nodiscard]] const BoundaryHotEntry& Entry(std::size_t slot) const;
    [[nodiscard]] std::span<const BoundaryHotEntry> Entries() const noexcept;
    [[nodiscard]] cpu::HostCallHook Hook() noexcept;
    [[nodiscard]] cpu::HostCallResult TryFastCall(
        cpu::A32HostCallContext& call) noexcept;

private:
    static cpu::HostCallResult Dispatch(
        void* userdata, std::uint32_t svc,
        cpu::A32HostCallContext& call) noexcept;

    BoundaryThunkArena& arena_;
    std::vector<BoundaryHotEntry> hot_;
};

}  // namespace ogplay::runtime
