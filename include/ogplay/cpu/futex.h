#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "ogplay/memory/bus.h"

namespace ogplay::cpu {

enum class FutexWaitResult : std::uint8_t {
    awoken,
    value_mismatch,
    interrupted,
    interrupted_after_wait,
    timed_out,
};

struct FutexWaiterSnapshot final {
    std::uint64_t thread_id{};
    std::uint32_t expected{};
    bool timed{};
    std::uint64_t wait_since_steady_ns{};
};

struct FutexAddressSnapshot final {
    memory::GuestAddress address;
    std::size_t wake_tokens{};
    std::uint64_t wake_count{};
    std::vector<FutexWaiterSnapshot> waiters;
};

struct FutexTableSnapshot final {
    bool complete{true};
    std::vector<FutexAddressSnapshot> addresses;
};

class FutexTable final {
public:
    FutexTable();
    ~FutexTable();

    FutexTable(const FutexTable&) = delete;
    FutexTable& operator=(const FutexTable&) = delete;

    [[nodiscard]] FutexWaitResult Wait(memory::MemoryBus& memory_bus,
                                       memory::GuestAddress address,
                                       std::uint32_t expected,
                                       std::uint64_t thread_id,
                                       std::optional<std::chrono::nanoseconds>
                                           timeout = std::nullopt);
    [[nodiscard]] std::size_t Wake(memory::GuestAddress address,
                                   std::size_t maximum_count);
    [[nodiscard]] std::size_t WakeAll();
    [[nodiscard]] std::size_t InterruptAll();
    [[nodiscard]] std::size_t WaiterCount(memory::GuestAddress address) const;
    // Never waits for a lock held by a stalled guest. Busy queues are omitted
    // and complete=false records the partial result.
    [[nodiscard]] FutexTableSnapshot TrySnapshot() const;
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::cpu
