#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <span>
#include <stdexcept>

#include "ogplay/memory/address_space.h"
#include "ogplay/runtime/syscall/syscall.h"
#include "ogplay/runtime/vfs/vfs.h"

namespace ogplay::runtime::syscall_detail {

// Bound host scratch, not the guest request. Stop on short IO and preserve
// completed bytes if a subsequent transfer fails.
inline std::int32_t TransferFile(VirtualFileSystem& vfs,
                                 memory::AddressSpace& memory,
                                 const A32SyscallFrame& frame, bool write) {
    const auto count = std::min(frame.arguments[2], UINT32_C(0x7ffff000));
    std::array<std::byte, 64U * 1024U> scratch;
    std::uint32_t completed{};
    try {
        while (completed < count) {
            const auto address = memory::GuestAddress{frame.arguments[1]}.Add(completed);
            const auto size = std::min<std::size_t>(scratch.size(), count - completed);
            auto bytes = std::span(scratch).first(size);
            memory.Validate({address, size}, write ? memory::AccessType::read
                                                  : memory::AccessType::write,
                            frame.thread_id);
            const auto fd = std::bit_cast<std::int32_t>(frame.arguments[0]);
            std::size_t actual{};
            if (write) {
                memory.Read(address, bytes, frame.thread_id);
                actual = vfs.Write(fd, bytes);
            } else {
                actual = vfs.Read(fd, bytes);
                memory.Write(address, bytes.first(actual), frame.thread_id);
            }
            completed += static_cast<std::uint32_t>(actual);
            if (actual < size) break;
        }
    } catch (const memory::MemoryFault&) {
        return completed != 0U ? static_cast<std::int32_t>(completed) : -14;
    } catch (const std::overflow_error&) {
        return completed != 0U ? static_cast<std::int32_t>(completed) : -14;
    } catch (const VfsError& error) {
        return completed != 0U ? static_cast<std::int32_t>(completed) : -error.ErrorNumber();
    }
    return static_cast<std::int32_t>(completed);
}
}  // namespace ogplay::runtime::syscall_detail
