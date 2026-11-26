#pragma once

#include "ogplay/runtime/common/guest_network_interface.h"
#include "ogplay/runtime/vfs/vfs.h"

#include <array>
#include <cstdint>
#include <optional>

namespace ogplay::runtime {
struct AndroidOfflineConnectivity;

class GuestNetworkEnvironment final {
public:
    // device_ipv4: the connected Wi-Fi address in online mode, adding wlan0 beside lo.
    explicit GuestNetworkEnvironment(const AndroidOfflineConnectivity& topology,
                                     std::optional<std::array<std::uint8_t, 4>> device_ipv4 = std::nullopt);
    [[nodiscard]] const std::vector<GuestNetworkInterface>& Interfaces() const { return interfaces_; }
    void Publish(VirtualFileSystem& filesystem);

private:
    std::vector<GuestNetworkInterface> interfaces_;
    std::vector<std::unique_ptr<VfsGeneratedFileRegistration>> files_;
};
}  // namespace ogplay::runtime
