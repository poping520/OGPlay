#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ogplay/runtime/dexvm/interpreter.h"

namespace ogplay::runtime::dexvm {

// Guest Java threads on real host threads (docs/design/dexvm/04-integration.md
// §3). One guest Java thread is one hal host thread with its own interpreter
// execution context; the linker, object model and JNI identities stay shared,
// so a child thread sees exactly the same object world as the root thread.
//
// Bytecode execution itself is serialized by Interpreter::ExecutionLock():
// threads start, block, wake and join for real, but only one interprets at a
// time. That is a deliberate, recorded limitation, not concurrency.

enum class VmThreadStatus : std::uint8_t {
    created,   // start() has not been called yet
    running,   // host thread exists and run() has not returned
    finished,  // run() returned normally
    stopped,   // unwound at teardown after RequestStop
    failed,    // run() ended with an uncaught Java exception or a VM error
};

enum class VmThreadWaitState : std::uint8_t {
    none,
    sleeping,
    joining,
    monitor,
};

struct VmThreadSnapshot final {
    std::uint64_t id{};
    std::uint64_t context_token{};
    std::uint32_t object{};
    VmThreadStatus status{VmThreadStatus::created};
    VmThreadWaitState wait_state{VmThreadWaitState::none};
    bool interrupted{};
    std::string name;
};

class VmThreadRuntime final {
public:
    explicit VmThreadRuntime(Interpreter& vm);
    ~VmThreadRuntime();
    VmThreadRuntime(const VmThreadRuntime&) = delete;
    VmThreadRuntime& operator=(const VmThreadRuntime&) = delete;

    // Per-VM Java Thread identity. ID 1 belongs to the root lifecycle thread;
    // constructed guest Thread objects allocate from 2 before start().
    [[nodiscard]] std::uint64_t AllocateThreadId();
    void SetRootThreadObject(VmObjectRef thread_object);

    // Starts virtual this.run() on a new host thread (Thread.start ->
    // VMThread.create -> dvmCreateInterpThread). The supplied id was allocated
    // when the Java object was constructed and remains stable after death.
    void Start(VmObjectRef thread_object, std::string name,
               std::uint64_t thread_id);

    [[nodiscard]] bool IsAlive(VmObjectRef thread_object) const;
    [[nodiscard]] std::uint64_t ThreadId(VmObjectRef thread_object) const;

    // Parks the calling VM thread until the target finishes. Releases the
    // execution lock while parked so the target can actually run.
    void Join(VmObjectRef thread_object);
    void Join(VmObjectRef thread_object, std::int64_t timeout_millis);

    // VMThread.sleep / dvmThreadSleep. This parks the current execution
    // context without releasing any Java monitor owned by the caller.
    void Sleep(std::int64_t timeout_millis);

    // Raises the interrupt flag and wakes the target if it is parked
    // (AOSP vm/Thread.cpp dvmThreadInterrupt).
    void Interrupt(VmObjectRef thread_object);
    [[nodiscard]] bool IsInterrupted(VmObjectRef thread_object) const;
    // Reads and clears the calling thread's interrupt flag.
    [[nodiscard]] bool ClearCurrentInterrupt();

    // Updates live diagnostics only; the Java-visible name stays in the
    // Thread object's declared field.
    void Rename(VmObjectRef thread_object, std::string name);

    // A guest caller briefly releases the execution lock. A host caller waits
    // for one observable progress handoff from the runnable worker set.
    void Yield();

    // Runtime-internal observable park state, keyed by execution context.
    // Monitor waits use this bridge so Snapshot() has one coherent surface
    // for sleep, join and monitor blocking.
    void SetWaitState(std::uint64_t context_token,
                      VmThreadWaitState wait_state);

    // Uncaught exceptions and VM errors are recorded here instead of being
    // thrown at whoever happens to call join(): the lifecycle driver drains
    // this at frame boundaries, matching the process-fatal default handler.
    [[nodiscard]] std::optional<std::string> TakeFailure();

    [[nodiscard]] VmObjectRef CurrentThreadObject() const;
    [[nodiscard]] std::size_t LiveCount() const;
    [[nodiscard]] std::vector<VmThreadSnapshot> Snapshot() const;
    void VisitThreadRoots(const std::function<void(VmObjectRef)>& visitor) const;

    // Teardown: request every live thread to unwind, wake every parked
    // thread, then join all host threads. Idempotent.
    void Shutdown();
    [[nodiscard]] bool ShuttingDown() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime::dexvm
