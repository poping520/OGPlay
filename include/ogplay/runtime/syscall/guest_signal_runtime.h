#pragma once

#include <cstdint>
#include <memory>
#include "ogplay/cpu/cpu.h"

namespace ogplay::runtime {
class A32SyscallDispatcher;
class GuestThreadLifecycle;
struct A32SyscallFrame;
struct A32SyscallOutcome;

// Process-owned ARM signal state. Only the owning host thread may Deliver.
// Sending a signal never reads or writes another thread's CPU.
class GuestSignalRuntime final {
public:
    GuestSignalRuntime(memory::AddressSpace&, GuestThreadLifecycle*, std::uint32_t process_id, std::uint32_t user_id);
    ~GuestSignalRuntime();
    void Bind(A32SyscallDispatcher&);
    void Deliver(cpu::Cpu&);
    [[nodiscard]] bool Pending(std::uint64_t thread_id) const;
    void Inherit(std::uint64_t parent, std::uint64_t child);
    void Retire(std::uint64_t thread_id);
    void InterruptedWait(const A32SyscallFrame&);
    static constexpr std::uint64_t kPollTicks = 50000;
private:
    class Impl;
    std::shared_ptr<Impl> impl_;
};
} // namespace ogplay::runtime
