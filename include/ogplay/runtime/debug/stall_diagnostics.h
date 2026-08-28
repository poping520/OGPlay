#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/cpu/futex.h"
#include "ogplay/runtime/common/supervisor_call_progress.h"

namespace ogplay::core { class Logger; }

namespace ogplay::runtime::debug {

enum class DiagnosticSectionStatus : std::uint8_t {
    complete,
    partial,
    unavailable,
};

struct DiagnosticSectionInfo final {
    std::string name;
    DiagnosticSectionStatus status{DiagnosticSectionStatus::complete};
    std::uint64_t captured_at_steady_ns{};
    std::uint64_t generation{};
    std::string reason;
};

struct DiagnosticSyscallEvent final {
    std::uint64_t sequence{};
    std::uint64_t steady_ns{};
    std::uint64_t guest_tid{};
    std::uint32_t syscall_nr{};
    std::int32_t result{};
    SupervisorCallProgress progress{SupervisorCallProgress::handled_idle};
};

enum class DiagnosticNativePhase : std::uint8_t { enter, returned, threw };

struct DiagnosticNativeEvent final {
    std::uint64_t sequence{};
    std::uint64_t steady_ns{};
    std::uint64_t context_token{};
    std::uint64_t guest_tid{};
    std::uint64_t call_id{};
    std::uint64_t method_id{};
    DiagnosticNativePhase phase{DiagnosticNativePhase::enter};
    std::string method;
};

struct DiagnosticDexVmEvent final {
    std::uint64_t sequence{};
    std::uint64_t context_token{};
    std::uint64_t tick{};
    std::uint32_t dex_pc{};
    std::string kind;
    std::string method;
};

struct DiagnosticJavaThread final {
    std::uint64_t guest_tid{};
    std::uint64_t context_token{};
    std::string name;
    std::string status;
    std::string wait_state;
    std::uint64_t ticks{};
    std::string pending_exception;
    struct Frame final {
        std::string method;
        std::uint32_t dex_pc{};
    };
    std::vector<Frame> frames;
};

struct DiagnosticMonitor final {
    std::uint32_t object{};
    std::uint64_t owner_context{};
    std::uint32_t recursion{};
    std::vector<std::uint64_t> entry_waiters;
    std::vector<std::uint64_t> notify_wait_set;
};

struct DiagnosticPacer final {
    bool attached{};
    bool driver_blocked{};
    bool shutdown{};
    bool surface_retired{};
    std::uint64_t generation{};
};

struct DiagnosticGlesEvent final {
    std::string call;
    std::uint32_t r0{};
    std::uint32_t r1{};
    std::uint32_t r2{};
    std::uint32_t r3{};
    std::optional<std::uint32_t> error;
};

struct DiagnosticDexVmSnapshot final {
    std::vector<DiagnosticDexVmEvent> events;
    std::vector<DiagnosticJavaThread> threads;
};

struct DiagnosticExecution final {
    std::uint64_t sequence{};
    std::uint64_t host_tid{};
    std::uint64_t guest_tid{};
    std::uint64_t context_token{};
    std::uint64_t entered_at_steady_ns{};
    std::uint64_t last_progress_steady_ns{};
    std::uint32_t arm_pc{};
    std::string path;
    std::string name;
    bool active{};
};

struct GuestStallSnapshot final {
    static constexpr std::uint32_t kSchemaVersion = 1U;

    std::uint64_t sequence{};
    std::uint64_t captured_at_steady_ns{};
    std::string trigger;
    std::string lifecycle_phase{"not_started"};
    std::uint64_t lifecycle_generation{};
    std::uint64_t lifecycle_unchanged_ns{};
    bool teardown_active{};
    std::vector<DiagnosticSectionInfo> sections;
    std::vector<DiagnosticExecution> executions;
    std::vector<DiagnosticSyscallEvent> syscalls;
    std::vector<DiagnosticNativeEvent> native_calls;
    std::vector<DiagnosticDexVmEvent> dexvm_events;
    std::vector<DiagnosticJavaThread> java_threads;
    std::vector<DiagnosticMonitor> monitors;
    std::vector<std::vector<std::uint64_t>> confirmed_cycles;
    std::optional<DiagnosticPacer> pacer;
    std::vector<DiagnosticGlesEvent> gles_events;
    cpu::FutexTableSnapshot futexes;
    std::uint64_t syscalls_dropped{};
    std::uint64_t native_calls_dropped{};
};

class DiagnosticState final {
public:
    explicit DiagnosticState(std::size_t ring_capacity = 64U,
                             std::size_t tombstone_capacity = 32U);
    ~DiagnosticState();
    DiagnosticState(const DiagnosticState&) = delete;
    DiagnosticState& operator=(const DiagnosticState&) = delete;

    void SetFutexProvider(std::function<cpu::FutexTableSnapshot()> provider);
    void SetNativeMethodResolver(
        std::function<std::string(std::uint64_t)> resolver);
    void SetDexVmProvider(
        std::function<std::optional<DiagnosticDexVmSnapshot>()> provider);
    void SetMonitorProvider(
        std::function<std::optional<std::vector<DiagnosticMonitor>>()> provider);
    void SetPacerProvider(
        std::function<std::optional<DiagnosticPacer>()> provider);
    void SetGlesProvider(
        std::function<std::optional<std::vector<DiagnosticGlesEvent>>()> provider);
    void RecordSyscall(std::uint64_t guest_tid, std::uint32_t syscall_nr,
                       std::int32_t result, SupervisorCallProgress progress);
    [[nodiscard]] std::uint64_t BeginNativeCall(
        std::uint64_t context_token, std::uint64_t guest_tid,
        std::uint64_t method_id);
    void EndNativeCall(std::uint64_t call_id, bool threw);
    [[nodiscard]] std::uint64_t EnterExecution(
        std::uint64_t guest_tid, std::uint64_t context_token,
        std::string_view path, std::string_view name, std::uint32_t arm_pc);
    void UpdateExecution(std::uint64_t execution_id, std::uint32_t arm_pc);
    void LeaveExecution(std::uint64_t execution_id);
    void SetLifecyclePhase(std::string_view phase, bool teardown_active);
    void RequestSnapshot(std::string_view trigger);
    [[nodiscard]] GuestStallSnapshot Collect(std::string_view trigger);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    friend class DiagCoordinator;
};

struct DiagCoordinatorConfig final {
    std::filesystem::path output_directory;
    std::optional<std::chrono::milliseconds> teardown_timeout;
    std::size_t maximum_snapshots{8U};
    std::size_t maximum_file_bytes{1024U * 1024U};
    std::size_t maximum_directory_bytes{8U * 1024U * 1024U};
    core::Logger* logger{};
};

struct DiagnosticSnapshotFiles final {
    std::filesystem::path text;
    std::filesystem::path json;
};

class DiagCoordinator final {
public:
    [[nodiscard]] static std::unique_ptr<DiagCoordinator> Start(
        std::shared_ptr<DiagnosticState> state, DiagCoordinatorConfig config);
    ~DiagCoordinator();
    DiagCoordinator(const DiagCoordinator&) = delete;
    DiagCoordinator& operator=(const DiagCoordinator&) = delete;

    void RequestSnapshot(std::string_view trigger);
    [[nodiscard]] std::optional<DiagnosticSnapshotFiles> RequestSnapshotAndWait(
        std::string_view trigger, std::chrono::milliseconds timeout);
    [[nodiscard]] std::filesystem::path ControlFile() const;
    [[nodiscard]] std::string ExternalTriggerName() const;

private:
    class Impl;
    explicit DiagCoordinator(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::uint64_t CurrentHostThreadId() noexcept;
[[nodiscard]] std::string RenderGuestStallSnapshotText(
    const GuestStallSnapshot& snapshot);
[[nodiscard]] std::string RenderGuestStallSnapshotJson(
    const GuestStallSnapshot& snapshot);

// Reads diag-control-<pid>.json from directory and activates the process-local
// OS trigger. Returns the event/signal description used for the request.
[[nodiscard]] std::string TriggerExternalDiagnostic(
    std::uint64_t process_id, const std::filesystem::path& directory);

}  // namespace ogplay::runtime::debug
