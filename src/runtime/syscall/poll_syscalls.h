#pragma once

#include <cstdint>
#include <functional>
#include <optional>

#include "ogplay/memory/address_space.h"
#include "ogplay/runtime/syscall/syscall.h"
#include "ogplay/runtime/vfs/vfs.h"

namespace ogplay::runtime {

// Access of a process diagnostic endpoint (stdio, /dev/log) that lives outside the VFS table.
struct GuestPollEndpointAccess final {
    bool read{};
    bool write{};
};

using GuestPollEndpointQuery = std::function<std::optional<GuestPollEndpointAccess>(std::int32_t)>;

// poll(168) and ppoll(336) over VFS pipes, other VFS descriptors and diagnostic endpoints.
void BindAndroidPollSyscalls(A32SyscallDispatcher& dispatcher, VirtualFileSystem& vfs,
                             memory::AddressSpace& address_space, GuestPollEndpointQuery endpoints,
                             GuestPollWaitHooks hooks);

}  // namespace ogplay::runtime
