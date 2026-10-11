#pragma once

#include <cstdint>
#include <array>
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

enum class GuestProcessMemoryBasis : std::uint8_t { injected_api19, virtual_private_committed };
// API19 order: PSS, swappable PSS, private/shared dirty, private/shared clean,
// swapped out. Units are KiB. Class algorithms stay in the original BootDex.
struct GuestProcessMemorySnapshot final {
    GuestProcessMemoryBasis basis{GuestProcessMemoryBasis::injected_api19};
    std::array<std::array<std::int32_t, 7>, 3> heaps{}; // dalvik, native, other
    std::array<std::int32_t, 147> other_stats{};
};
void ValidateGuestProcessMemorySnapshot(const GuestProcessMemorySnapshot& snapshot);
[[nodiscard]] GuestProcessMemorySnapshot MakeVirtualPrivateProcessMemorySnapshot(
    std::uint64_t managed_bytes, std::uint64_t committed_arm_bytes);
} // namespace ogplay::runtime
