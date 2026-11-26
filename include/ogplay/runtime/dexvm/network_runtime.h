#pragma once

#include "ogplay/runtime/common/api19_identity.h"

#include <optional>
#include <string>

namespace ogplay::runtime::net {
class GuestNetworkStack;
}

namespace ogplay::runtime::dexvm {

struct NetworkPolicy final {
    // Deterministic API 19 guest identity; never the host computer's identity.
    struct HostIdentity final {
        std::string sysname, nodename, release, version, machine;
    };

    std::optional<HostIdentity> host_identity{
        HostIdentity{kGuestSysname, kGuestNodename, kGuestRelease, kGuestVersion, kGuestMachine}};
};

// The VM's handle on the process socket stack (runtime/net). libcore.io.Posix
// socket natives use guest descriptor numbers of that stack, shared with ARM
// syscalls. Without an injected stack every socket operation fails.
class NetworkRuntime final {
public:
    // host_tls selects the OGPlayJSSE engine: the host TLS engine with host
    // roots, or guest libssl with the bundled CA set.
    void Configure(net::GuestNetworkStack* stack, const bool host_tls = true) noexcept {
        stack_ = stack;
        host_tls_ = host_tls;
    }

    [[nodiscard]] net::GuestNetworkStack* Stack() const noexcept { return stack_; }

    [[nodiscard]] bool HostTls() const noexcept { return host_tls_; }

    [[nodiscard]] const NetworkPolicy& Policy() const noexcept { return policy_; }

    void Shutdown() noexcept { stack_ = nullptr; }

private:
    NetworkPolicy policy_;
    net::GuestNetworkStack* stack_{};
    bool host_tls_{true};
};

}  // namespace ogplay::runtime::dexvm
