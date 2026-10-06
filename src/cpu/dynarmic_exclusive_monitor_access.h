#pragma once

#include <dynarmic/interface/exclusive_monitor.h>

// The pinned monitor grants these accessors friendship. They expose the same
// stable storage used by its existing x64 inline-exclusive backend.
namespace Dynarmic {
inline volatile int* GetExclusiveMonitorLockPointer(ExclusiveMonitor* monitor) {
    return &monitor->lock.storage;
}
inline size_t GetExclusiveMonitorProcessorCount(ExclusiveMonitor* monitor) {
    return monitor->exclusive_addresses.size();
}
inline VAddr* GetExclusiveMonitorAddressPointer(ExclusiveMonitor* monitor, size_t index) {
    return monitor->exclusive_addresses.data() + index;
}
inline Vector* GetExclusiveMonitorValuePointer(ExclusiveMonitor* monitor, size_t index) {
    return monitor->exclusive_values.data() + index;
}
}  // namespace Dynarmic
