#pragma once

// Shared guest C-string path reader for the file syscall binders. Reads up to
// 4096 bytes from the guest address space and fails with ENAMETOOLONG when no
// NUL terminator is found, so every binder reports the identical errno.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "ogplay/memory/address_space.h"
#include "ogplay/runtime/vfs/vfs.h"

namespace ogplay::runtime::syscall_detail {

[[nodiscard]] inline std::string ReadGuestPath(
    memory::AddressSpace& address_space, const std::uint32_t raw_address) {
    constexpr std::int32_t kEnametoolong = 36;
    std::string path;
    path.reserve(128);
    auto address = memory::GuestAddress{raw_address};
    for (std::size_t index = 0; index < 4096; ++index) {
        std::array<std::byte, 1> byte{};
        address_space.Read(address, byte);
        const auto value = std::to_integer<std::uint8_t>(byte[0]);
        if (value == 0) return path;
        path.push_back(static_cast<char>(value));
        address = address.Add(1);
    }
    throw VfsError(kEnametoolong, "guest path is not null-terminated");
}

}  // namespace ogplay::runtime::syscall_detail
