#pragma once

#include <string_view>

#include "ogplay/loader/link_namespace.h"
#include "ogplay/memory/address_space.h"
#include "ogplay/runtime/common/guest_cpu_config.h"
#include "ogplay/runtime/vfs/vfs.h"

namespace ogplay::cpu {
class ExecutionBudget;
}

namespace ogplay::runtime {

// Process-owned immutable CPU facts, with revocable VFS publications.
class GuestCpuEnvironment final {
public:
    explicit GuestCpuEnvironment(GuestCpuConfig config);

    [[nodiscard]] const GuestCpuConfig& Config() const noexcept { return config_; }

    [[nodiscard]] std::shared_ptr<cpu::ExecutionBudget> MakeExecutionBudget() const;
    void Publish(VirtualFileSystem& filesystem);
    void BindSysconf(loader::Elf32LinkNamespace& symbols, memory::AddressSpace& memory) const;

private:
    struct VirtualCpuIdentity {
        std::string_view name;
        std::string_view platform;
        std::uint32_t midr;
        std::uint32_t architecture;
        std::string_view features;
        std::string_view hardware;
        std::uint32_t board_revision;
        std::uint64_t serial;
    };

    // Cortex-A9 r0p0: a fixed virtual model matching the supported ARMv7 subset,
    // not a claim about the host CPU or a particular physical device/stepping.
    inline static constexpr VirtualCpuIdentity identity_{
        "ARMv7 Processor", "v7l", 0x410fc090U, 7,
        // Fixed Dynarmic execution surface; never ROM/host-configurable flags.
        "half thumb fastmult vfp edsp neon vfpv3 tls", "OGPlay virtual ARMv7", 0, 0};
    const GuestCpuConfig config_;
    std::vector<std::unique_ptr<VfsGeneratedFileRegistration>> files_;
};

}  // namespace ogplay::runtime
