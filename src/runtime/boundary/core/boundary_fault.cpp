#include "runtime/boundary/core/boundary_fault.h"

#include <algorithm>

namespace ogplay::runtime {

void BoundaryFaultStore::RecordCurrent(
    const cpu::A32HostCallContext& context) noexcept {
    try {
        std::scoped_lock lock(mutex_);
        const auto found = std::find_if(
            pending_.begin(), pending_.end(), [&](const auto& fault) {
                return fault.thread_id == context.thread_id &&
                       fault.pc == context.pc;
            });
        if (found != pending_.end()) {
            found->exception = std::current_exception();
        } else {
            pending_.push_back(
                {context.thread_id, context.pc, std::current_exception()});
        }
    } catch (...) {
    }
}

std::exception_ptr BoundaryFaultStore::Take(
    const std::uint64_t thread_id, const memory::GuestAddress pc) noexcept {
    try {
        std::scoped_lock lock(mutex_);
        const auto found = std::find_if(
            pending_.begin(), pending_.end(), [&](const auto& fault) {
                return fault.thread_id == thread_id && fault.pc == pc;
            });
        if (found == pending_.end()) return {};
        auto result = std::move(found->exception);
        pending_.erase(found);
        return result;
    } catch (...) {
        return {};
    }
}

}  // namespace ogplay::runtime
