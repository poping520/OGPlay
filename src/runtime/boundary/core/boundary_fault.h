#pragma once

#include <exception>
#include <mutex>
#include <vector>

#include "ogplay/cpu/cpu.h"

namespace ogplay::runtime {

class BoundaryFaultStore final {
public:
    void RecordCurrent(const cpu::A32HostCallContext& context) noexcept;
    [[nodiscard]] std::exception_ptr Take(
        std::uint64_t thread_id, memory::GuestAddress pc) noexcept;

private:
    struct PendingFault final {
        std::uint64_t thread_id{};
        memory::GuestAddress pc{};
        std::exception_ptr exception;
    };

    std::mutex mutex_;
    std::vector<PendingFault> pending_;
};

}  // namespace ogplay::runtime
