#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/cpu/futex.h"
#include "ogplay/hal/clock.h"
#include "ogplay/memory/address_space.h"
#include "ogplay/runtime/guest_thread_lifecycle.h"
#include "ogplay/runtime/vfs.h"

namespace ogplay::runtime {

inline constexpr std::int32_t kLinuxEnosys = 38;

enum class SyscallGroup : std::uint8_t {
    memory,
    file,
    thread,
    time,
    signal,
    process,
    poll,
    network,
    arm_private,
};

struct A32SyscallFrame final {
    std::uint32_t number{};
    std::array<std::uint32_t, 7> arguments{};
    std::uint32_t program_counter{};
    std::uint32_t link_register{};
    std::uint64_t thread_id{};
};

struct SyscallCoverage final {
    std::size_t declared{};
    std::size_t implemented{};
    std::map<SyscallGroup, std::size_t> declared_by_group;
    std::map<SyscallGroup, std::size_t> implemented_by_group;
};

struct AndroidProcessIdentity final {
    std::uint32_t process_id{1000};
    std::uint32_t user_id{10000};
    std::uint32_t group_id{10000};
};

using GuestThreadPointerSetter =
    std::function<bool(std::uint64_t, memory::GuestAddress)>;

class SyscallError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class A32SyscallDispatcher final {
public:
    using Handler = std::function<std::int32_t(const A32SyscallFrame&)>;

    explicit A32SyscallDispatcher(core::CapabilityLedger& ledger);
    void Declare(std::uint32_t number, std::string name, SyscallGroup group);
    void Register(std::uint32_t number, std::string name, SyscallGroup group,
                  Handler handler);
    void Implement(std::uint32_t number, Handler handler);
    [[nodiscard]] std::int32_t Dispatch(const A32SyscallFrame& frame);
    [[nodiscard]] SyscallCoverage Coverage() const;

private:
    struct Entry final {
        std::string name;
        SyscallGroup group{};
        Handler handler;
    };

    core::CapabilityLedger& ledger_;
    std::map<std::uint32_t, Entry> entries_;
};

[[nodiscard]] A32SyscallDispatcher CreateAndroidArmSyscallDispatcher(
    core::CapabilityLedger& ledger, AndroidProcessIdentity identity = {});
void BindAndroidTimeSyscalls(A32SyscallDispatcher& dispatcher,
                             hal::Clock& clock,
                             memory::AddressSpace& address_space);
void BindAndroidMemorySyscalls(A32SyscallDispatcher& dispatcher,
                               memory::AddressSpace& address_space);
void BindAndroidThreadSyscalls(A32SyscallDispatcher& dispatcher,
                               cpu::FutexTable& futex_table,
                               memory::MemoryBus& memory_bus);
void BindAndroidArmPrivateSyscalls(
    A32SyscallDispatcher& dispatcher,
    GuestThreadPointerSetter thread_pointer_setter);
void BindAndroidThreadLifecycleSyscalls(
    A32SyscallDispatcher& dispatcher,
    GuestThreadLifecycle& thread_lifecycle);
void BindAndroidFileSyscalls(A32SyscallDispatcher& dispatcher,
                             VirtualFileSystem& vfs,
                             memory::AddressSpace& address_space);

}  // namespace ogplay::runtime
