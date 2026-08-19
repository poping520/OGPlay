#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include "ogplay/runtime/dexvm/interpreter.h"

namespace ogplay::runtime::dexvm {

// Object monitors with owner, recursion and wait set (design 04 §4). The
// state machine mirrors AOSP vm/Sync.cpp lockMonitor/waitMonitor/
// notifyMonitor at the pinned baseline; the thin-lock/fat-lock inflation
// optimisation is deliberately not reproduced.
//
// Owners are interpreter execution context tokens, so "the current thread"
// means "the context this call is running on" whether that is the root
// lifecycle thread or a VmThreadRuntime host thread.

enum class VmWaitOutcome : std::uint8_t {
    notified,     // notify/notifyAll moved us out of the wait set
    timed_out,    // the Clock deadline passed
    interrupted,  // Thread.interrupt: throw InterruptedException
    shut_down,    // teardown woke every waiter
};

// Monotonic milliseconds from the unified Clock. Timed waits refuse to run
// without one rather than reading a host wall clock.
using VmMonotonicMillis = std::function<std::int64_t()>;

struct VmMonitorSnapshot final {
    std::uint64_t owner{};
    std::size_t recursion{};
    std::size_t waiting{};
    bool shutting_down{};
};

class VmMonitorTable final {
public:
    explicit VmMonitorTable(Interpreter& vm);
    ~VmMonitorTable();
    VmMonitorTable(const VmMonitorTable&) = delete;
    VmMonitorTable& operator=(const VmMonitorTable&) = delete;

    void SetTimeSource(VmMonotonicMillis source);
    // Thread.sleep/join copy this same session clock. Host waits may schedule
    // rechecks, but only this source decides whether a guest deadline passed.
    [[nodiscard]] VmMonotonicMillis TimeSource() const;

    // monitor-enter / monitor-exit. Enter parks (releasing the execution
    // lock) while another context owns the monitor.
    void Enter(VmObjectRef object, std::uint64_t owner);
    void Exit(VmObjectRef object, std::uint64_t owner);
    [[nodiscard]] bool IsOwner(VmObjectRef object, std::uint64_t owner) const;
    [[nodiscard]] std::size_t HeldCount(std::uint64_t owner) const;

    // Object.wait: validates ownership, saves the recursion depth, releases
    // the monitor completely, joins the wait set, and only returns after it
    // has re-acquired the monitor and restored that depth.
    // timeout_millis == 0 waits without a deadline.
    [[nodiscard]] VmWaitOutcome Wait(VmObjectRef object, std::uint64_t owner,
                                     std::int64_t timeout_millis);
    void Notify(VmObjectRef object, std::uint64_t owner);
    void NotifyAll(VmObjectRef object, std::uint64_t owner);

    // Thread.interrupt (AOSP vm/Thread.cpp dvmThreadInterrupt): raise the
    // flag, then wake the target if it is parked.
    void Interrupt(std::uint64_t owner);
    [[nodiscard]] bool Interrupted(std::uint64_t owner) const;
    [[nodiscard]] bool ClearInterrupt(std::uint64_t owner);

    // A finished thread must not keep other threads out of its monitors.
    void ReleaseAll(std::uint64_t owner);

    // Teardown: wake every waiter with shut_down and refuse new parking.
    void Shutdown();
    [[nodiscard]] bool ShuttingDown() const;

    // Diagnostics: how many contexts sit in the wait set of this object.
    [[nodiscard]] std::size_t WaitingCount(VmObjectRef object) const;
    [[nodiscard]] VmMonitorSnapshot Snapshot(VmObjectRef object) const;
    void ReleaseObjectForGc(VmObjectRef object);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime::dexvm
