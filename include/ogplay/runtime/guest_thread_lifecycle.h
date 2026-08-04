#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

#include "ogplay/memory/address.h"
#include "ogplay/cpu/futex.h"

namespace ogplay::runtime {

enum class GuestThreadStatus : std::uint8_t {
    running,
    exit_requested,
    exited,
};

struct GuestThreadRuntimeState final {
    std::uint64_t thread_id{};
    memory::GuestAddress thread_pointer{0};
    memory::GuestAddress clear_child_tid{0};
    std::int32_t exit_code{};
    GuestThreadStatus status{GuestThreadStatus::running};

    bool operator==(const GuestThreadRuntimeState&) const = default;
};

enum class GuestThreadCleanupStatus : std::uint8_t {
    not_requested,
    cleared,
    invalid_address,
    memory_fault,
};

struct GuestThreadExitCompletion final {
    GuestThreadRuntimeState state;
    GuestThreadCleanupStatus cleanup{GuestThreadCleanupStatus::not_requested};
    std::size_t futex_wake_count{};
};

class GuestThreadLifecycleError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class GuestThreadLifecycle final {
public:
    GuestThreadLifecycle();
    ~GuestThreadLifecycle();
    GuestThreadLifecycle(GuestThreadLifecycle&&) noexcept;
    GuestThreadLifecycle& operator=(GuestThreadLifecycle&&) noexcept;
    GuestThreadLifecycle(const GuestThreadLifecycle&) = delete;
    GuestThreadLifecycle& operator=(const GuestThreadLifecycle&) = delete;

    void Register(std::uint64_t thread_id,
                  memory::GuestAddress thread_pointer =
                      memory::GuestAddress{0});
    void RegisterChild(std::uint64_t parent_thread_id,
                       std::uint64_t child_thread_id,
                       memory::GuestAddress thread_pointer,
                       memory::GuestAddress clear_child_tid);
    void SetThreadPointer(std::uint64_t thread_id,
                          memory::GuestAddress thread_pointer);
    void SetClearChildTid(std::uint64_t thread_id,
                          memory::GuestAddress address);
    void RequestExit(std::uint64_t thread_id, std::int32_t exit_code);
    void RequestExitGroup(std::uint64_t requesting_thread_id,
                          std::int32_t exit_code);
    [[nodiscard]] GuestThreadRuntimeState CompleteExit(
        std::uint64_t thread_id);
    [[nodiscard]] GuestThreadExitCompletion CompleteExit(
        std::uint64_t thread_id, memory::MemoryBus& memory_bus,
        cpu::FutexTable& futex_table);
    [[nodiscard]] GuestThreadRuntimeState Reap(std::uint64_t thread_id);
    [[nodiscard]] GuestThreadRuntimeState State(std::uint64_t thread_id) const;
    [[nodiscard]] std::vector<GuestThreadRuntimeState> States() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime
