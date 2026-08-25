#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

#include "ogplay/memory/bus.h"

namespace ogplay::cpu {

enum class FutexWaitResult : std::uint8_t {
    awoken,
    value_mismatch,
    interrupted,
    timed_out,
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
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::cpu
