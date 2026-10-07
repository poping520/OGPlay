#pragma once

#include <dynarmic/interface/exclusive_monitor.h>

// The pinned monitor grants these accessors friendship. They expose the same
// stable ARM64 storage; each processor owns a cache-line-isolated reservation.
namespace Dynarmic {
inline volatile int* GetExclusiveMonitorLockPointer(ExclusiveMonitor* monitor) {
    return &monitor->lock.storage;
}
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(sizeof(std::atomic<std::uint64_t>) == sizeof(std::uint64_t));
static_assert(std::atomic_ref<VAddr>::required_alignment <= alignof(VAddr));
static_assert(std::atomic_ref<VAddr>::is_always_lock_free);
inline std::atomic<std::uint64_t>* GetExclusiveMonitorEpochPointer(ExclusiveMonitor* monitor) {
    return &monitor->write_epoch;
}
inline size_t GetExclusiveMonitorReservationStride(ExclusiveMonitor* monitor) {
    return sizeof(monitor->reservations.front());
}
inline size_t GetExclusiveMonitorProcessorCount(ExclusiveMonitor* monitor) {
    return monitor->reservations.size();
}
inline VAddr* GetExclusiveMonitorAddressPointer(ExclusiveMonitor* monitor, size_t index) {
    return &monitor->reservations[index].address;
}
inline Vector* GetExclusiveMonitorValuePointer(ExclusiveMonitor* monitor, size_t index) {
    return &monitor->reservations[index].value;
}
}  // namespace Dynarmic
