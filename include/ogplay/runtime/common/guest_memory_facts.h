#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include "ogplay/runtime/common/guest_cpu_config.h"

namespace ogplay::runtime {

// API19 ProcessList's high-memory buckets for the default 1 GiB device.
// This is a query policy, not an Android low-memory-killer implementation.
struct GuestMemoryPressurePolicy final {
    std::uint32_t home_kb{98304}, cached_kb{98304}, service_kb{98304};
    std::uint32_t visible_kb{61440}, foreground_kb{49152};
};

// Virtual-device facts; never observations of the host machine.
struct GuestProcFacts final {
    std::uint32_t memory_total_kb{1048576};
    std::uint32_t memory_free_kb{524288};
    GuestCpuConfig cpu;
    std::optional<std::uint32_t> memory_cached_kb;
    std::uint32_t memory_buffers_kb{};
    GuestMemoryPressurePolicy memory_pressure;
};

struct GuestMemorySnapshot final {
    std::uint64_t total_kb{}, free_kb{}, cached_kb{}, buffers_kb{};
    GuestMemoryPressurePolicy pressure;
    [[nodiscard]] std::uint64_t TotalBytes() const noexcept { return total_kb * 1024U; }
    // API19 Process.getFreeMemory sums MemFree and Cached, not Buffers.
    [[nodiscard]] std::uint64_t AvailableBytes() const noexcept { return (free_kb + cached_kb) * 1024U; }
    [[nodiscard]] std::string ProcText() const;
};

[[nodiscard]] GuestMemorySnapshot MakeGuestMemorySnapshot(const GuestProcFacts& facts);
} // namespace ogplay::runtime
