#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace ogplay::runtime {

// Immutable guest kernel interface facts. No host handles or network transport.
struct GuestNetworkInterface final {
    std::string name;
    std::uint32_t index{}, flags{}, mtu{};
    std::string hardware_address;
    std::optional<std::array<std::byte, 4>> ipv4;
    std::array<std::byte, 4> netmask{}, broadcast{};
    std::array<std::byte, 16> ipv6{};
    std::uint32_t ipv6_prefix{}, ipv6_scope{}, ipv6_flags{};
};

}  // namespace ogplay::runtime
