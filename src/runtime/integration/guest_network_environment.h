#pragma once

#include "ogplay/runtime/common/guest_network_interface.h"
#include "ogplay/runtime/vfs/vfs.h"

namespace ogplay::runtime {
struct AndroidOfflineConnectivity;

class GuestNetworkEnvironment final {
public:
    explicit GuestNetworkEnvironment(const AndroidOfflineConnectivity& topology);
    [[nodiscard]] const std::vector<GuestNetworkInterface>& Interfaces() const { return interfaces_; }
    void Publish(VirtualFileSystem& filesystem);

private:
    const std::vector<GuestNetworkInterface> interfaces_;
    std::vector<std::unique_ptr<VfsGeneratedFileRegistration>> files_;
};
}  // namespace ogplay::runtime
