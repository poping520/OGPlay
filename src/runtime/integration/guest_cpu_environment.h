#pragma once

#include "ogplay/loader/link_namespace.h"
#include "ogplay/memory/address_space.h"
#include "ogplay/runtime/common/guest_cpu_config.h"
#include "ogplay/runtime/vfs/vfs.h"

namespace ogplay::cpu { class ExecutionBudget; }

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
    const GuestCpuConfig config_;
    std::vector<std::unique_ptr<VfsGeneratedFileRegistration>> files_;
};

}  // namespace ogplay::runtime
