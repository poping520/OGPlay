#include "ogplay/runtime/debug/stall_diagnostics.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

#include "ogplay/core/json.h"
#include "ogplay/core/logger.h"
#include "ogplay/hal/diagnostic_trigger.h"

namespace ogplay::runtime::debug {
namespace {

[[nodiscard]] std::uint64_t SteadyNowNs() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

[[nodiscard]] std::string_view SectionStatusName(
    const DiagnosticSectionStatus status) {
    switch (status) {
        case DiagnosticSectionStatus::complete: return "complete";
        case DiagnosticSectionStatus::partial: return "partial";
        case DiagnosticSectionStatus::unavailable: return "unavailable";
    }
    return "unavailable";
}

[[nodiscard]] std::string_view NativePhaseName(
    const DiagnosticNativePhase phase) {
    switch (phase) {
        case DiagnosticNativePhase::enter: return "enter";
        case DiagnosticNativePhase::returned: return "return";
        case DiagnosticNativePhase::threw: return "throw";
    }
    return "throw";
}

[[nodiscard]] std::string_view ProgressName(
    const SupervisorCallProgress progress) {
    switch (progress) {
        case SupervisorCallProgress::not_handled: return "not_handled";
        case SupervisorCallProgress::handled_idle: return "idle";
        case SupervisorCallProgress::handled_advanced: return "advanced";
    }
    return "idle";
}

[[nodiscard]] std::string BoundedText(const std::string_view value,
                                      const std::size_t maximum = 256U) {
    return std::string(value.substr(0U, std::min(value.size(), maximum)));
}

[[nodiscard]] bool IsSensitiveText(const std::string_view value) {
    if (value.find("://") != std::string_view::npos) return true;
    if (value.starts_with('/') || value.starts_with('\\')) return true;
    return value.size() >= 3U &&
           ((value[0] >= 'A' && value[0] <= 'Z') ||
            (value[0] >= 'a' && value[0] <= 'z')) &&
           value[1] == ':' && (value[2] == '/' || value[2] == '\\');
}

[[nodiscard]] std::string SafeText(const std::string_view value,
                                   const std::size_t maximum = 256U) {
    if (IsSensitiveText(value)) return "<redacted>";
    auto result = BoundedText(value, maximum);
    for (auto& character : result) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 0x20U || byte == 0x7fU) character = '?';
    }
    return result;
}

[[nodiscard]] std::vector<std::vector<std::uint64_t>> FindMonitorCycles(
    const std::vector<DiagnosticMonitor>& monitors) {
    std::map<std::uint64_t, std::uint64_t> waits_for;
    for (const auto& monitor : monitors) {
        if (monitor.owner_context == 0U) continue;
        for (const auto waiter : monitor.entry_waiters) {
            if (waiter != 0U && waiter != monitor.owner_context) {
                waits_for.emplace(waiter, monitor.owner_context);
            }
        }
    }
    std::vector<std::vector<std::uint64_t>> cycles;
    for (const auto& [start, owner] : waits_for) {
        static_cast<void>(owner);
        std::vector<std::uint64_t> path;
        auto current = start;
        for (std::size_t step = 0; step <= waits_for.size(); ++step) {
            path.push_back(current);
            const auto next = waits_for.find(current);
            if (next == waits_for.end()) break;
            current = next->second;
            if (current == start) {
                if (start == *std::ranges::min_element(path)) {
                    cycles.push_back(std::move(path));
                }
                break;
            }
            if (std::ranges::find(path, current) != path.end()) break;
        }
    }
    return cycles;
}

template <typename T>
class FixedRing final {
public:
    struct Snapshot final {
        std::vector<T> values;
        std::uint64_t dropped{};
    };

    explicit FixedRing(const std::size_t capacity) : values_(capacity) {}

    void Push(T value) {
        if (values_.empty()) return;
        std::scoped_lock lock(mutex_);
        values_[write_] = std::move(value);
        write_ = (write_ + 1U) % values_.size();
        if (size_ < values_.size()) ++size_;
        else ++dropped_;
    }

    [[nodiscard]] std::optional<Snapshot> TryCopy() const {
        std::unique_lock lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock()) return std::nullopt;
        Snapshot snapshot;
        if (values_.empty()) return snapshot;
        snapshot.values.reserve(size_);
        const auto begin = (write_ + values_.size() - size_) % values_.size();
        for (std::size_t offset = 0; offset < size_; ++offset) {
            snapshot.values.push_back(values_[(begin + offset) % values_.size()]);
        }
        snapshot.dropped = dropped_;
        return snapshot;
    }

private:
    mutable std::mutex mutex_;
    std::vector<T> values_;
    std::size_t write_{};
    std::size_t size_{};
    std::uint64_t dropped_{};
};

void WriteAtomic(const std::filesystem::path& path,
                 const std::string_view contents) {
    std::filesystem::create_directories(path.parent_path());
    auto temporary = path;
    temporary += ".partial";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            throw std::runtime_error("cannot open diagnostic output: " +
                                     temporary.string());
        }
        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        output.flush();
        if (!output) {
            throw std::runtime_error("cannot write diagnostic output: " +
                                     temporary.string());
        }
    }
    std::error_code error;
    hal::RestrictDiagnosticFile(temporary);
    std::filesystem::remove(path, error);
    error.clear();
    std::filesystem::rename(temporary, path, error);
    if (error) {
        throw std::runtime_error("cannot publish diagnostic output: " +
                                 error.message());
    }
}

[[nodiscard]] std::string RandomNonce() {
    std::random_device random;
    const auto value = (static_cast<std::uint64_t>(random()) << 32U) ^
                       static_cast<std::uint64_t>(random()) ^ SteadyNowNs();
    std::ostringstream text;
    text << std::hex << std::setw(16) << std::setfill('0') << value;
    return text.str();
}

}  // namespace

class DiagnosticState::Impl final {
public:
    Impl(const std::size_t ring_capacity,
         const std::size_t tombstone_capacity_value)
        : syscall_ring(ring_capacity), native_ring(ring_capacity),
          native_active(ring_capacity),
          tombstone_capacity(tombstone_capacity_value) {}

    FixedRing<DiagnosticSyscallEvent> syscall_ring;
    FixedRing<DiagnosticNativeEvent> native_ring;
    std::atomic_uint64_t next_event_sequence{1U};
    std::atomic_uint64_t next_call_id{1U};
    std::atomic_uint64_t next_execution_id{1U};
    std::atomic_uint64_t next_snapshot_sequence{1U};

    mutable std::mutex native_active_mutex;
    std::vector<DiagnosticNativeEvent> native_active;

    mutable std::mutex executions_mutex;
    std::map<std::uint64_t, DiagnosticExecution> executions;
    std::deque<DiagnosticExecution> tombstones;
    std::size_t tombstone_capacity{};

    mutable std::mutex futex_provider_mutex;
    std::function<cpu::FutexTableSnapshot()> futex_provider;

    mutable std::mutex supplemental_provider_mutex;
    std::function<std::string(std::uint64_t)> native_method_resolver;
    std::function<std::optional<DiagnosticDexVmSnapshot>()> dexvm_provider;
    std::function<std::optional<std::vector<DiagnosticMonitor>>()> monitor_provider;
    std::function<std::optional<DiagnosticPacer>()> pacer_provider;
    std::function<std::optional<std::vector<DiagnosticGlesEvent>>()> gles_provider;
    mutable std::mutex control_mutex;
    std::condition_variable control_changed;
    std::deque<std::string> requests;
    std::string lifecycle_phase{"not_started"};
    std::uint64_t lifecycle_generation{};
    std::uint64_t lifecycle_changed_ns{SteadyNowNs()};
    std::uint64_t teardown_started_ns{};
    std::uint64_t teardown_epoch{};
    bool teardown_active{};
    bool stopping{};
};

DiagnosticState::DiagnosticState(const std::size_t ring_capacity,
                                 const std::size_t tombstone_capacity)
    : impl_(std::make_unique<Impl>(ring_capacity, tombstone_capacity)) {}

DiagnosticState::~DiagnosticState() = default;

void DiagnosticState::SetFutexProvider(
    std::function<cpu::FutexTableSnapshot()> provider) {
    std::scoped_lock lock(impl_->futex_provider_mutex);
    impl_->futex_provider = std::move(provider);
}

void DiagnosticState::SetNativeMethodResolver(
    std::function<std::string(std::uint64_t)> resolver) {
    std::scoped_lock lock(impl_->supplemental_provider_mutex);
    impl_->native_method_resolver = std::move(resolver);
}

void DiagnosticState::SetDexVmProvider(
    std::function<std::optional<DiagnosticDexVmSnapshot>()> provider) {
    std::scoped_lock lock(impl_->supplemental_provider_mutex);
    impl_->dexvm_provider = std::move(provider);
}

void DiagnosticState::SetMonitorProvider(
    std::function<std::optional<std::vector<DiagnosticMonitor>>()> provider) {
    std::scoped_lock lock(impl_->supplemental_provider_mutex);
    impl_->monitor_provider = std::move(provider);
}

void DiagnosticState::SetPacerProvider(
    std::function<std::optional<DiagnosticPacer>()> provider) {
    std::scoped_lock lock(impl_->supplemental_provider_mutex);
    impl_->pacer_provider = std::move(provider);
}

void DiagnosticState::SetGlesProvider(
    std::function<std::optional<std::vector<DiagnosticGlesEvent>>()> provider) {
    std::scoped_lock lock(impl_->supplemental_provider_mutex);
    impl_->gles_provider = std::move(provider);
}

void DiagnosticState::RecordSyscall(
    const std::uint64_t guest_tid, const std::uint32_t syscall_nr,
    const std::int32_t result, const SupervisorCallProgress progress) {
    const auto now = SteadyNowNs();
    impl_->syscall_ring.Push(
        {impl_->next_event_sequence.fetch_add(1U), now, guest_tid,
         syscall_nr, result, progress});
    std::unique_lock lock(impl_->executions_mutex, std::try_to_lock);
    if (!lock.owns_lock()) return;
    for (auto& [id, execution] : impl_->executions) {
        static_cast<void>(id);
        if (execution.guest_tid == guest_tid) {
            execution.last_progress_steady_ns = now;
        }
    }
}

std::uint64_t DiagnosticState::BeginNativeCall(
    const std::uint64_t context_token, const std::uint64_t guest_tid,
    const std::uint64_t method_id) {
    const auto call_id = impl_->next_call_id.fetch_add(1U);
    DiagnosticNativeEvent event{
        impl_->next_event_sequence.fetch_add(1U), SteadyNowNs(),
        context_token, guest_tid, call_id, method_id,
        DiagnosticNativePhase::enter, {}};
    {
        std::scoped_lock lock(impl_->native_active_mutex);
        if (!impl_->native_active.empty()) {
            auto available = std::ranges::find(
                impl_->native_active, 0U,
                &DiagnosticNativeEvent::call_id);
            if (available == impl_->native_active.end()) {
                available = impl_->native_active.begin() +
                    static_cast<std::ptrdiff_t>(
                        call_id % impl_->native_active.size());
            }
            *available = event;
        }
    }
    impl_->native_ring.Push(std::move(event));
    return call_id;
}

void DiagnosticState::EndNativeCall(const std::uint64_t call_id,
                                    const bool threw) {
    DiagnosticNativeEvent event;
    {
        std::scoped_lock lock(impl_->native_active_mutex);
        const auto found = std::ranges::find(
            impl_->native_active, call_id,
            &DiagnosticNativeEvent::call_id);
        if (found == impl_->native_active.end()) return;
        event = *found;
        *found = {};
    }
    event.sequence = impl_->next_event_sequence.fetch_add(1U);
    event.steady_ns = SteadyNowNs();
    event.phase = threw ? DiagnosticNativePhase::threw
                        : DiagnosticNativePhase::returned;
    impl_->native_ring.Push(std::move(event));
}

std::uint64_t DiagnosticState::EnterExecution(
    const std::uint64_t guest_tid, const std::uint64_t context_token,
    const std::string_view path, const std::string_view name,
    const std::uint32_t arm_pc) {
    const auto id = impl_->next_execution_id.fetch_add(1U);
    const auto now = SteadyNowNs();
    DiagnosticExecution execution{
        id, CurrentHostThreadId(), guest_tid, context_token, now, now,
        arm_pc, SafeText(path, 64U), SafeText(name), true};
    std::scoped_lock lock(impl_->executions_mutex);
    impl_->executions.emplace(id, std::move(execution));
    return id;
}

void DiagnosticState::UpdateExecution(const std::uint64_t execution_id,
                                      const std::uint32_t arm_pc) {
    std::scoped_lock lock(impl_->executions_mutex);
    const auto found = impl_->executions.find(execution_id);
    if (found == impl_->executions.end()) return;
    found->second.arm_pc = arm_pc;
    found->second.last_progress_steady_ns = SteadyNowNs();
}

void DiagnosticState::LeaveExecution(const std::uint64_t execution_id) {
    std::scoped_lock lock(impl_->executions_mutex);
    const auto found = impl_->executions.find(execution_id);
    if (found == impl_->executions.end()) return;
    auto completed = found->second;
    completed.active = false;
    completed.last_progress_steady_ns = SteadyNowNs();
    impl_->executions.erase(found);
    if (impl_->tombstone_capacity == 0U) return;
    if (impl_->tombstones.size() == impl_->tombstone_capacity) {
        impl_->tombstones.pop_front();
    }
    impl_->tombstones.push_back(std::move(completed));
}

void DiagnosticState::SetLifecyclePhase(const std::string_view phase,
                                        const bool teardown_active) {
    std::scoped_lock lock(impl_->control_mutex);
    const auto now = SteadyNowNs();
    impl_->lifecycle_phase = BoundedText(phase, 96U);
    ++impl_->lifecycle_generation;
    impl_->lifecycle_changed_ns = now;
    if (teardown_active && !impl_->teardown_active) {
        impl_->teardown_started_ns = now;
        ++impl_->teardown_epoch;
    }
    impl_->teardown_active = teardown_active;
    impl_->control_changed.notify_all();
}

void DiagnosticState::RequestSnapshot(const std::string_view trigger) {
    std::scoped_lock lock(impl_->control_mutex);
    if (impl_->requests.size() < 16U) {
        impl_->requests.push_back(SafeText(trigger, 96U));
    }
    impl_->control_changed.notify_all();
}

GuestStallSnapshot DiagnosticState::Collect(const std::string_view trigger) {
    GuestStallSnapshot snapshot;
    snapshot.sequence = impl_->next_snapshot_sequence.fetch_add(1U);
    snapshot.captured_at_steady_ns = SteadyNowNs();
    snapshot.trigger = SafeText(trigger, 96U);

    {
        std::unique_lock lock(impl_->control_mutex, std::try_to_lock);
        if (lock.owns_lock()) {
            snapshot.lifecycle_phase = impl_->lifecycle_phase;
            snapshot.lifecycle_generation = impl_->lifecycle_generation;
            snapshot.lifecycle_unchanged_ns =
                snapshot.captured_at_steady_ns - impl_->lifecycle_changed_ns;
            snapshot.teardown_active = impl_->teardown_active;
            snapshot.sections.push_back(
                {"lifecycle", DiagnosticSectionStatus::complete,
                 snapshot.captured_at_steady_ns,
                 impl_->lifecycle_generation, {}});
        } else {
            snapshot.sections.push_back(
                {"lifecycle", DiagnosticSectionStatus::unavailable,
                 snapshot.captured_at_steady_ns, 0U, "busy"});
        }
    }
    {
        std::unique_lock lock(impl_->executions_mutex, std::try_to_lock);
        if (lock.owns_lock()) {
            for (const auto& [id, execution] : impl_->executions) {
                static_cast<void>(id);
                snapshot.executions.push_back(execution);
            }
            snapshot.executions.insert(snapshot.executions.end(),
                                       impl_->tombstones.begin(),
                                       impl_->tombstones.end());
            std::ranges::sort(snapshot.executions, {},
                              &DiagnosticExecution::sequence);
            snapshot.sections.push_back(
                {"executions", DiagnosticSectionStatus::complete,
                 snapshot.captured_at_steady_ns,
                 impl_->next_execution_id.load(), {}});
        } else {
            snapshot.sections.push_back(
                {"executions", DiagnosticSectionStatus::unavailable,
                 snapshot.captured_at_steady_ns, 0U, "busy"});
        }
    }
    if (auto values = impl_->syscall_ring.TryCopy(); values) {
        snapshot.syscalls = std::move(values->values);
        snapshot.syscalls_dropped = values->dropped;
        snapshot.sections.push_back(
            {"syscalls", DiagnosticSectionStatus::complete,
             snapshot.captured_at_steady_ns,
             impl_->next_event_sequence.load(), {}});
    } else {
        snapshot.sections.push_back(
            {"syscalls", DiagnosticSectionStatus::unavailable,
             snapshot.captured_at_steady_ns, 0U, "busy"});
    }
    if (auto values = impl_->native_ring.TryCopy(); values) {
        snapshot.native_calls = std::move(values->values);
        snapshot.native_calls_dropped = values->dropped;
        snapshot.sections.push_back(
            {"native_calls", DiagnosticSectionStatus::complete,
             snapshot.captured_at_steady_ns,
             impl_->next_event_sequence.load(), {}});
    } else {
        snapshot.sections.push_back(
            {"native_calls", DiagnosticSectionStatus::unavailable,
             snapshot.captured_at_steady_ns, 0U, "busy"});
    }
    {
        std::function<cpu::FutexTableSnapshot()> provider;
        std::unique_lock lock(impl_->futex_provider_mutex, std::try_to_lock);
        if (lock.owns_lock()) provider = impl_->futex_provider;
        if (!lock.owns_lock()) {
            snapshot.sections.push_back(
                {"futexes", DiagnosticSectionStatus::unavailable,
                 snapshot.captured_at_steady_ns, 0U, "provider_busy"});
        } else if (!provider) {
            snapshot.sections.push_back(
                {"futexes", DiagnosticSectionStatus::unavailable,
                 snapshot.captured_at_steady_ns, 0U, "not_connected"});
        } else {
            try {
                snapshot.futexes = provider();
                snapshot.sections.push_back(
                    {"futexes", snapshot.futexes.complete
                                    ? DiagnosticSectionStatus::complete
                                    : DiagnosticSectionStatus::partial,
                     snapshot.captured_at_steady_ns, 0U,
                     snapshot.futexes.complete ? ""
                                               : "busy_queue_omitted"});
            } catch (...) {
                snapshot.sections.push_back(
                    {"futexes", DiagnosticSectionStatus::unavailable,
                     snapshot.captured_at_steady_ns, 0U, "provider_error"});
            }
        }
    }
    std::function<std::string(std::uint64_t)> native_resolver;
    std::function<std::optional<DiagnosticDexVmSnapshot>()> dexvm_provider;
    std::function<std::optional<std::vector<DiagnosticMonitor>>()> monitor_provider;
    std::function<std::optional<DiagnosticPacer>()> pacer_provider;
    std::function<std::optional<std::vector<DiagnosticGlesEvent>>()> gles_provider;
    bool supplemental_provider_busy{};
    {
        std::unique_lock lock(impl_->supplemental_provider_mutex,
                              std::try_to_lock);
        if (lock.owns_lock()) {
            native_resolver = impl_->native_method_resolver;
            dexvm_provider = impl_->dexvm_provider;
            monitor_provider = impl_->monitor_provider;
            pacer_provider = impl_->pacer_provider;
            gles_provider = impl_->gles_provider;
        } else {
            supplemental_provider_busy = true;
            for (const std::string_view name :
                 {"dexvm", "monitors", "pacer", "gles"}) {
                snapshot.sections.push_back(
                    {std::string(name), DiagnosticSectionStatus::unavailable,
                     snapshot.captured_at_steady_ns, 0U, "provider_busy"});
            }
        }
    }
    if (native_resolver) {
        for (auto& event : snapshot.native_calls) {
            try {
                event.method = SafeText(native_resolver(event.method_id));
            } catch (...) {
                event.method = "<resolution-error>";
            }
        }
    }
    if (dexvm_provider) {
        std::optional<DiagnosticDexVmSnapshot> value;
        bool provider_error{};
        try { value = dexvm_provider(); } catch (...) { provider_error = true; }
        if (value) {
            snapshot.dexvm_events = std::move(value->events);
            snapshot.java_threads = std::move(value->threads);
            for (auto& event : snapshot.dexvm_events) {
                event.kind = SafeText(event.kind, 64U);
                event.method = SafeText(event.method);
            }
            for (auto& thread : snapshot.java_threads) {
                thread.name = SafeText(thread.name, 96U);
                thread.status = SafeText(thread.status, 32U);
                thread.wait_state = SafeText(thread.wait_state, 32U);
                thread.pending_exception = SafeText(thread.pending_exception);
                for (auto& frame : thread.frames) {
                    frame.method = SafeText(frame.method);
                }
            }
            snapshot.sections.push_back(
                {"dexvm", DiagnosticSectionStatus::complete,
                 snapshot.captured_at_steady_ns, 0U, {}});
        } else {
            snapshot.sections.push_back(
                {"dexvm", DiagnosticSectionStatus::unavailable,
                 snapshot.captured_at_steady_ns, 0U,
                 provider_error ? "provider_error" : "busy"});
        }
    } else if (!supplemental_provider_busy) {
        snapshot.sections.push_back(
            {"dexvm", DiagnosticSectionStatus::unavailable,
             snapshot.captured_at_steady_ns, 0U, "not_connected"});
    }
    if (monitor_provider) {
        std::optional<std::vector<DiagnosticMonitor>> value;
        bool provider_error{};
        try { value = monitor_provider(); } catch (...) { provider_error = true; }
        if (value) {
            snapshot.monitors = std::move(*value);
            snapshot.confirmed_cycles = FindMonitorCycles(snapshot.monitors);
            snapshot.sections.push_back(
                {"monitors", DiagnosticSectionStatus::complete,
                 snapshot.captured_at_steady_ns, 0U, {}});
        } else {
            snapshot.sections.push_back(
                {"monitors", DiagnosticSectionStatus::unavailable,
                 snapshot.captured_at_steady_ns, 0U,
                 provider_error ? "provider_error" : "busy"});
        }
    } else if (!supplemental_provider_busy) {
        snapshot.sections.push_back(
            {"monitors", DiagnosticSectionStatus::unavailable,
             snapshot.captured_at_steady_ns, 0U, "not_connected"});
    }
    if (pacer_provider) {
        std::optional<DiagnosticPacer> value;
        bool provider_error{};
        try { value = pacer_provider(); } catch (...) { provider_error = true; }
        if (value) {
            snapshot.pacer = *value;
            snapshot.sections.push_back(
                {"pacer", DiagnosticSectionStatus::complete,
                 snapshot.captured_at_steady_ns, value->generation, {}});
        } else {
            snapshot.sections.push_back(
                {"pacer", DiagnosticSectionStatus::unavailable,
                 snapshot.captured_at_steady_ns, 0U,
                 provider_error ? "provider_error" : "busy"});
        }
    } else if (!supplemental_provider_busy) {
        snapshot.sections.push_back(
            {"pacer", DiagnosticSectionStatus::unavailable,
             snapshot.captured_at_steady_ns, 0U, "not_connected"});
    }
    if (gles_provider) {
        std::optional<std::vector<DiagnosticGlesEvent>> value;
        bool provider_error{};
        try { value = gles_provider(); } catch (...) { provider_error = true; }
        if (value) {
            snapshot.gles_events = std::move(*value);
            for (auto& event : snapshot.gles_events) {
                event.call = SafeText(event.call, 96U);
            }
            snapshot.sections.push_back(
                {"gles", DiagnosticSectionStatus::complete,
                 snapshot.captured_at_steady_ns, 0U, {}});
        } else {
            snapshot.sections.push_back(
                {"gles", DiagnosticSectionStatus::unavailable,
                 snapshot.captured_at_steady_ns, 0U,
                 provider_error ? "provider_error" : "busy"});
        }
    } else if (!supplemental_provider_busy) {
        snapshot.sections.push_back(
            {"gles", DiagnosticSectionStatus::unavailable,
             snapshot.captured_at_steady_ns, 0U, "not_connected"});
    }
    return snapshot;
}

std::uint64_t CurrentHostThreadId() noexcept {
    return hal::CurrentNativeThreadId();
}

std::string RenderGuestStallSnapshotText(
    const GuestStallSnapshot& snapshot) {
    std::size_t complete{};
    std::size_t unavailable{};
    for (const auto& section : snapshot.sections) {
        if (section.status == DiagnosticSectionStatus::complete) ++complete;
        if (section.status == DiagnosticSectionStatus::unavailable) ++unavailable;
    }
    std::ostringstream out;
    out << "stall_snapshot schema=" << GuestStallSnapshot::kSchemaVersion
        << " trigger=" << snapshot.trigger
        << " teardown=" << (snapshot.teardown_active ? "true" : "false")
        << " phase=" << snapshot.lifecycle_phase
        << " phase_unchanged_ns=" << snapshot.lifecycle_unchanged_ns
        << " complete=" << complete
        << " unavailable=" << unavailable << '\n';
    for (const auto& section : snapshot.sections) {
        out << "section " << section.name << " status="
            << SectionStatusName(section.status)
            << " captured_at_steady_ns=" << section.captured_at_steady_ns
            << " generation=" << section.generation;
        if (!section.reason.empty()) out << " reason=" << section.reason;
        out << '\n';
    }
    for (const auto& execution : snapshot.executions) {
        out << "thread guest=" << execution.guest_tid
            << " host=" << execution.host_tid
            << " context=" << execution.context_token
            << " path=" << execution.path
            << " state=" << (execution.active ? "active" : "recently_exited")
            << " pc=0x" << std::hex << execution.arm_pc << std::dec
            << " entered_at_steady_ns=" << execution.entered_at_steady_ns
            << " last_progress_steady_ns="
            << execution.last_progress_steady_ns;
        if (!execution.name.empty()) out << " name=" << execution.name;
        out << '\n';
    }
    for (const auto& event : snapshot.native_calls) {
        out << "native call=" << event.call_id << " guest=" << event.guest_tid
            << " phase=" << NativePhaseName(event.phase)
            << " method_id=" << event.method_id;
        if (!event.method.empty()) out << " method=" << event.method;
        out << '\n';
    }
    for (const auto& event : snapshot.syscalls) {
        out << "syscall guest=" << event.guest_tid << " nr="
            << event.syscall_nr << " result=" << event.result
            << " progress=" << ProgressName(event.progress) << '\n';
    }
    for (const auto& address : snapshot.futexes.addresses) {
        out << "futex address=0x" << std::hex << address.address.Value()
            << std::dec << " wake_count=" << address.wake_count
            << " wake_tokens=" << address.wake_tokens << '\n';
        for (const auto& waiter : address.waiters) {
            out << "  waiter guest=" << waiter.thread_id
                << " expected=" << waiter.expected
                << " timeout=" << (waiter.timed ? "timed" : "none")
                << '\n';
        }
    }
    out << "ring syscalls_dropped=" << snapshot.syscalls_dropped
        << " native_calls_dropped=" << snapshot.native_calls_dropped << '\n';
    for (const auto& thread : snapshot.java_threads) {
        out << "java_thread guest=" << thread.guest_tid
            << " context=" << thread.context_token
            << " status=" << thread.status
            << " wait=" << thread.wait_state
            << " ticks=" << thread.ticks
            << " name=" << thread.name;
        if (!thread.pending_exception.empty()) {
            out << " pending_exception=" << thread.pending_exception;
        }
        out << '\n';
        for (const auto& frame : thread.frames) {
            out << "  frame dex_pc=" << frame.dex_pc
                << " method=" << frame.method << '\n';
        }
    }
    for (const auto& event : snapshot.dexvm_events) {
        out << "dexvm sequence=" << event.sequence
            << " context=" << event.context_token
            << " kind=" << event.kind
            << " dex_pc=" << event.dex_pc;
        if (!event.method.empty()) out << " method=" << event.method;
        out << '\n';
    }
    for (const auto& monitor : snapshot.monitors) {
        out << "monitor object=" << monitor.object
            << " owner_context=" << monitor.owner_context
            << " recursion=" << monitor.recursion;
        for (const auto waiter : monitor.entry_waiters) {
            out << " entry_waiter=" << waiter;
        }
        for (const auto waiter : monitor.notify_wait_set) {
            out << " notify_waiter=" << waiter;
        }
        out << '\n';
    }
    for (const auto& cycle : snapshot.confirmed_cycles) {
        out << "confirmed_cycle contexts=";
        for (std::size_t index = 0; index < cycle.size(); ++index) {
            if (index != 0U) out << "->";
            out << cycle[index];
        }
        if (!cycle.empty()) out << "->" << cycle.front();
        out << '\n';
    }
    if (snapshot.pacer) {
        out << "pacer attached=" << snapshot.pacer->attached
            << " driver_blocked=" << snapshot.pacer->driver_blocked
            << " shutdown=" << snapshot.pacer->shutdown
            << " surface_retired=" << snapshot.pacer->surface_retired
            << " generation=" << snapshot.pacer->generation << '\n';
    }
    for (const auto& event : snapshot.gles_events) {
        out << "gles call=" << event.call << " r0=" << event.r0
            << " r1=" << event.r1 << " r2=" << event.r2
            << " r3=" << event.r3;
        if (event.error) out << " error=" << *event.error;
        out << '\n';
    }
    return out.str();
}

std::string RenderGuestStallSnapshotJson(
    const GuestStallSnapshot& snapshot) {
    core::JsonWriter writer;
    const auto root = writer.Object();
    writer.AddUnsignedInteger(root, "schema_version",
                              GuestStallSnapshot::kSchemaVersion);
    writer.AddUnsignedInteger(root, "sequence", snapshot.sequence);
    writer.AddUnsignedInteger(root, "captured_at_steady_ns",
                              snapshot.captured_at_steady_ns);
    writer.AddString(root, "trigger", snapshot.trigger);
    const auto lifecycle = writer.Object();
    writer.AddString(lifecycle, "phase", snapshot.lifecycle_phase);
    writer.AddUnsignedInteger(lifecycle, "generation",
                              snapshot.lifecycle_generation);
    writer.AddUnsignedInteger(lifecycle, "unchanged_ns",
                              snapshot.lifecycle_unchanged_ns);
    writer.AddBool(lifecycle, "teardown_active", snapshot.teardown_active);
    writer.Add(root, "lifecycle", lifecycle);

    const auto sections = writer.Array();
    for (const auto& section : snapshot.sections) {
        const auto value = writer.Object();
        writer.AddString(value, "name", section.name);
        writer.AddString(value, "status", SectionStatusName(section.status));
        writer.AddUnsignedInteger(value, "captured_at_steady_ns",
                                  section.captured_at_steady_ns);
        writer.AddUnsignedInteger(value, "generation", section.generation);
        if (section.reason.empty()) writer.AddNull(value, "reason");
        else writer.AddString(value, "reason", section.reason);
        writer.Append(sections, value);
    }
    writer.Add(root, "sections", sections);

    const auto executions = writer.Array();
    for (const auto& execution : snapshot.executions) {
        const auto value = writer.Object();
        writer.AddUnsignedInteger(value, "host_tid", execution.host_tid);
        writer.AddUnsignedInteger(value, "guest_tid", execution.guest_tid);
        writer.AddUnsignedInteger(value, "context_token", execution.context_token);
        writer.AddUnsignedInteger(value, "arm_pc", execution.arm_pc);
        writer.AddString(value, "path", execution.path);
        writer.AddString(value, "name", execution.name);
        writer.AddBool(value, "active", execution.active);
        writer.AddUnsignedInteger(value, "entered_at_steady_ns",
                                  execution.entered_at_steady_ns);
        writer.AddUnsignedInteger(value, "last_progress_steady_ns",
                                  execution.last_progress_steady_ns);
        writer.Append(executions, value);
    }
    writer.Add(root, "executions", executions);

    const auto syscalls = writer.Array();
    for (const auto& event : snapshot.syscalls) {
        const auto value = writer.Object();
        writer.AddUnsignedInteger(value, "sequence", event.sequence);
        writer.AddUnsignedInteger(value, "steady_ns", event.steady_ns);
        writer.AddUnsignedInteger(value, "guest_tid", event.guest_tid);
        writer.AddUnsignedInteger(value, "syscall_nr", event.syscall_nr);
        writer.AddInteger(value, "result", event.result);
        writer.AddString(value, "progress", ProgressName(event.progress));
        writer.Append(syscalls, value);
    }
    writer.Add(root, "syscalls", syscalls);
    writer.AddUnsignedInteger(root, "syscalls_dropped",
                              snapshot.syscalls_dropped);

    const auto native_calls = writer.Array();
    for (const auto& event : snapshot.native_calls) {
        const auto value = writer.Object();
        writer.AddUnsignedInteger(value, "sequence", event.sequence);
        writer.AddUnsignedInteger(value, "steady_ns", event.steady_ns);
        writer.AddUnsignedInteger(value, "context_token", event.context_token);
        writer.AddUnsignedInteger(value, "guest_tid", event.guest_tid);
        writer.AddUnsignedInteger(value, "call_id", event.call_id);
        writer.AddUnsignedInteger(value, "method_id", event.method_id);
        writer.AddString(value, "phase", NativePhaseName(event.phase));
        writer.AddString(value, "method", event.method);
        writer.Append(native_calls, value);
    }
    writer.Add(root, "native_calls", native_calls);
    writer.AddUnsignedInteger(root, "native_calls_dropped",
                              snapshot.native_calls_dropped);

    const auto dexvm = writer.Array();
    for (const auto& event : snapshot.dexvm_events) {
        const auto value = writer.Object();
        writer.AddUnsignedInteger(value, "sequence", event.sequence);
        writer.AddUnsignedInteger(value, "context_token", event.context_token);
        writer.AddUnsignedInteger(value, "tick", event.tick);
        writer.AddUnsignedInteger(value, "dex_pc", event.dex_pc);
        writer.AddString(value, "kind", event.kind);
        writer.AddString(value, "method", event.method);
        writer.Append(dexvm, value);
    }
    writer.Add(root, "dexvm", dexvm);

    const auto java_threads = writer.Array();
    for (const auto& thread : snapshot.java_threads) {
        const auto value = writer.Object();
        writer.AddUnsignedInteger(value, "guest_tid", thread.guest_tid);
        writer.AddUnsignedInteger(value, "context_token", thread.context_token);
        writer.AddString(value, "name", thread.name);
        writer.AddString(value, "status", thread.status);
        writer.AddString(value, "wait_state", thread.wait_state);
        writer.AddUnsignedInteger(value, "ticks", thread.ticks);
        if (thread.pending_exception.empty()) {
            writer.AddNull(value, "pending_exception");
        } else {
            writer.AddString(value, "pending_exception",
                             thread.pending_exception);
        }
        const auto frames = writer.Array();
        for (const auto& frame : thread.frames) {
            const auto item = writer.Object();
            writer.AddString(item, "method", frame.method);
            writer.AddUnsignedInteger(item, "dex_pc", frame.dex_pc);
            writer.Append(frames, item);
        }
        writer.Add(value, "frames", frames);
        writer.Append(java_threads, value);
    }
    writer.Add(root, "java_threads", java_threads);

    const auto monitors = writer.Array();
    for (const auto& monitor : snapshot.monitors) {
        const auto value = writer.Object();
        writer.AddUnsignedInteger(value, "object", monitor.object);
        writer.AddUnsignedInteger(value, "owner_context", monitor.owner_context);
        writer.AddUnsignedInteger(value, "recursion", monitor.recursion);
        const auto entry_waiters = writer.Array();
        for (const auto waiter : monitor.entry_waiters) {
            writer.Append(entry_waiters, writer.UnsignedInteger(waiter));
        }
        writer.Add(value, "entry_waiters", entry_waiters);
        const auto notify_wait_set = writer.Array();
        for (const auto waiter : monitor.notify_wait_set) {
            writer.Append(notify_wait_set, writer.UnsignedInteger(waiter));
        }
        writer.Add(value, "notify_wait_set", notify_wait_set);
        writer.Append(monitors, value);
    }
    writer.Add(root, "monitors", monitors);

    const auto confirmed_cycles = writer.Array();
    for (const auto& cycle : snapshot.confirmed_cycles) {
        const auto contexts = writer.Array();
        for (const auto context : cycle) {
            writer.Append(contexts, writer.UnsignedInteger(context));
        }
        writer.Append(confirmed_cycles, contexts);
    }
    writer.Add(root, "confirmed_cycles", confirmed_cycles);

    if (snapshot.pacer) {
        const auto pacer = writer.Object();
        writer.AddBool(pacer, "attached", snapshot.pacer->attached);
        writer.AddBool(pacer, "driver_blocked", snapshot.pacer->driver_blocked);
        writer.AddBool(pacer, "shutdown", snapshot.pacer->shutdown);
        writer.AddBool(pacer, "surface_retired", snapshot.pacer->surface_retired);
        writer.AddUnsignedInteger(pacer, "generation", snapshot.pacer->generation);
        writer.Add(root, "pacer", pacer);
    } else {
        writer.AddNull(root, "pacer");
    }

    const auto gles = writer.Array();
    for (const auto& event : snapshot.gles_events) {
        const auto value = writer.Object();
        writer.AddString(value, "call", event.call);
        writer.AddUnsignedInteger(value, "r0", event.r0);
        writer.AddUnsignedInteger(value, "r1", event.r1);
        writer.AddUnsignedInteger(value, "r2", event.r2);
        writer.AddUnsignedInteger(value, "r3", event.r3);
        if (event.error) writer.AddUnsignedInteger(value, "error", *event.error);
        else writer.AddNull(value, "error");
        writer.Append(gles, value);
    }
    writer.Add(root, "gles", gles);

    const auto futexes = writer.Array();
    for (const auto& address : snapshot.futexes.addresses) {
        const auto value = writer.Object();
        writer.AddUnsignedInteger(value, "address", address.address.Value());
        writer.AddUnsignedInteger(value, "wake_tokens", address.wake_tokens);
        writer.AddUnsignedInteger(value, "wake_count", address.wake_count);
        const auto waiters = writer.Array();
        for (const auto& waiter : address.waiters) {
            const auto item = writer.Object();
            writer.AddUnsignedInteger(item, "guest_tid", waiter.thread_id);
            writer.AddUnsignedInteger(item, "expected", waiter.expected);
            writer.AddBool(item, "timed", waiter.timed);
            writer.AddUnsignedInteger(item, "wait_since_steady_ns",
                                      waiter.wait_since_steady_ns);
            writer.Append(waiters, item);
        }
        writer.Add(value, "waiters", waiters);
        writer.Append(futexes, value);
    }
    writer.Add(root, "futexes", futexes);
    return writer.Serialize(root);
}

class DiagCoordinator::Impl final {
public:
    Impl(std::shared_ptr<DiagnosticState> state_value,
         DiagCoordinatorConfig config_value)
        : state(std::move(state_value)), config(std::move(config_value)),
          pid(hal::HostProcessId()), nonce(RandomNonce()) {
        if (!state || config.output_directory.empty() ||
            config.maximum_snapshots == 0U ||
            config.maximum_file_bytes < 1024U ||
            config.maximum_directory_bytes < 2U * config.maximum_file_bytes) {
            throw std::invalid_argument("diagnostic coordinator config is invalid");
        }
        control_file = config.output_directory /
                       ("diag-control-" + std::to_string(pid) + ".json");
    }

    ~Impl() { Stop(); }

    void Start() {
        std::filesystem::create_directories(config.output_directory);
        os_trigger = hal::CreateDiagnosticTrigger(pid, nonce);
        external_name = os_trigger->Name();
        core::JsonWriter writer;
        const auto root = writer.Object();
        writer.AddUnsignedInteger(root, "schema_version", 1U);
        writer.AddUnsignedInteger(root, "pid", pid);
        writer.AddString(root, "external_trigger", external_name);
        WriteAtomic(control_file, writer.Serialize(root));
        worker = std::thread([this] { WorkerLoop(); });
        trigger = std::thread([this] { TriggerLoop(); });
        if (config.logger) {
            config.logger->Write(
                core::LogLevel::info, "runtime.diagnostics",
                "stall diagnostics ready", {},
                {{"pid", pid}, {"trigger", external_name}});
        }
    }

    void Stop() noexcept {
        if (stopped.exchange(true)) return;
        {
            std::scoped_lock lock(state->impl_->control_mutex);
            state->impl_->stopping = true;
            state->impl_->control_changed.notify_all();
        }
        if (os_trigger) os_trigger->Stop();
        if (trigger.joinable()) trigger.join();
        if (worker.joinable()) worker.join();
        os_trigger.reset();
        std::error_code error;
        std::filesystem::remove(control_file, error);
    }

    void WorkerLoop() noexcept {
        std::uint64_t fired_teardown_epoch{};
        for (;;) {
            std::string reason;
            {
                std::unique_lock lock(state->impl_->control_mutex);
                const auto ready = [&] {
                    return state->impl_->stopping ||
                           !state->impl_->requests.empty();
                };
                if (!ready()) {
                    if (config.teardown_timeout &&
                        state->impl_->teardown_active &&
                        state->impl_->teardown_epoch != fired_teardown_epoch) {
                        const auto epoch = state->impl_->teardown_epoch;
                        const auto deadline = std::chrono::steady_clock::time_point(
                            std::chrono::nanoseconds(state->impl_->teardown_started_ns)) +
                            *config.teardown_timeout;
                        if (!state->impl_->control_changed.wait_until(lock, deadline, ready) &&
                            state->impl_->teardown_active &&
                            state->impl_->teardown_epoch == epoch) {
                            fired_teardown_epoch = epoch;
                            reason = "teardown_timeout";
                        }
                    } else {
                        state->impl_->control_changed.wait(lock);
                    }
                }
                if (state->impl_->stopping) return;
                if (reason.empty() && !state->impl_->requests.empty()) {
                    reason = std::move(state->impl_->requests.front());
                    state->impl_->requests.pop_front();
                }
            }
            if (reason.empty()) continue;
            try {
                Publish(state->Collect(reason));
            } catch (const std::exception& error) {
                if (config.logger) {
                    config.logger->Write(
                        core::LogLevel::error, "runtime.diagnostics",
                        "stall snapshot failed", {},
                        {{"reason", std::string(error.what())}});
                }
            }
        }
    }

    void Publish(const GuestStallSnapshot& snapshot) {
        const auto stem = "diag-" + std::to_string(pid) + "-" +
                          std::to_string(snapshot.sequence);
        DiagnosticSnapshotFiles files{
            config.output_directory / (stem + ".txt"),
            config.output_directory / (stem + ".json")};
        auto text = RenderGuestStallSnapshotText(snapshot);
        auto json = RenderGuestStallSnapshotJson(snapshot);
        if (text.size() > config.maximum_file_bytes) {
            constexpr std::string_view suffix{"\ntruncated=true\n"};
            text.resize(config.maximum_file_bytes - suffix.size());
            text += suffix;
        }
        if (json.size() > config.maximum_file_bytes) {
            core::JsonWriter writer;
            const auto root = writer.Object();
            writer.AddUnsignedInteger(root, "schema_version",
                                      GuestStallSnapshot::kSchemaVersion);
            writer.AddUnsignedInteger(root, "sequence", snapshot.sequence);
            writer.AddString(root, "trigger", snapshot.trigger);
            writer.AddBool(root, "truncated", true);
            json = writer.Serialize(root);
        }
        WriteAtomic(files.text, text);
        WriteAtomic(files.json, json);
        Prune();
        {
            std::scoped_lock lock(completed_mutex);
            last_files = files;
            ++completed_sequence;
            completed_changed.notify_all();
        }
        if (config.logger) {
            config.logger->Write(
                core::LogLevel::warn, "runtime.diagnostics",
                "stall snapshot written", {},
                {{"sequence", snapshot.sequence},
                 {"trigger", snapshot.trigger}});
        }
    }

    void Prune() {
        struct SnapshotFiles final {
            std::filesystem::path json;
            std::filesystem::path text;
            std::filesystem::file_time_type written;
            std::uintmax_t bytes{};
        };
        std::vector<SnapshotFiles> snapshots;
        std::uintmax_t directory_bytes{};
        for (const auto& entry :
             std::filesystem::directory_iterator(config.output_directory)) {
            if (!entry.is_regular_file()) continue;
            const auto name = entry.path().filename().string();
            if (!name.starts_with("diag-") ||
                entry.path().extension() != ".json") continue;
            auto text = entry.path();
            text.replace_extension(".txt");
            std::error_code error;
            auto bytes = entry.file_size(error);
            if (error) bytes = 0U;
            error.clear();
            if (std::filesystem::is_regular_file(text, error)) {
                error.clear();
                const auto text_bytes = std::filesystem::file_size(text, error);
                if (!error) bytes += text_bytes;
            }
            snapshots.push_back(
                {entry.path(), std::move(text), entry.last_write_time(), bytes});
            directory_bytes += bytes;
        }
        std::ranges::sort(snapshots, {}, &SnapshotFiles::written);
        while (!snapshots.empty() &&
               (snapshots.size() > config.maximum_snapshots ||
                directory_bytes > config.maximum_directory_bytes)) {
            auto files = std::move(snapshots.front());
            snapshots.erase(snapshots.begin());
            std::error_code error;
            std::filesystem::remove(files.json, error);
            error.clear();
            std::filesystem::remove(files.text, error);
            directory_bytes = files.bytes > directory_bytes
                ? 0U : directory_bytes - files.bytes;
        }
    }

    void TriggerLoop() noexcept {
        for (;;) {
            const auto result = os_trigger->Wait();
            if (result == hal::DiagnosticWaitResult::stopped ||
                result == hal::DiagnosticWaitResult::failed) return;
            state->RequestSnapshot("os_event");
        }
    }

    std::shared_ptr<DiagnosticState> state;
    DiagCoordinatorConfig config;
    std::uint64_t pid{};
    std::string nonce;
    std::string external_name;
    std::filesystem::path control_file;
    std::thread worker;
    std::thread trigger;
    std::atomic_bool stopped{};
    std::mutex completed_mutex;
    std::condition_variable completed_changed;
    std::uint64_t completed_sequence{};
    std::optional<DiagnosticSnapshotFiles> last_files;
    std::unique_ptr<hal::DiagnosticTrigger> os_trigger;
};

std::unique_ptr<DiagCoordinator> DiagCoordinator::Start(
    std::shared_ptr<DiagnosticState> state, DiagCoordinatorConfig config) {
    auto impl = std::make_unique<Impl>(std::move(state), std::move(config));
    impl->Start();
    return std::unique_ptr<DiagCoordinator>(
        new DiagCoordinator(std::move(impl)));
}

DiagCoordinator::DiagCoordinator(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

DiagCoordinator::~DiagCoordinator() = default;

void DiagCoordinator::RequestSnapshot(const std::string_view trigger) {
    impl_->state->RequestSnapshot(trigger);
}

std::optional<DiagnosticSnapshotFiles> DiagCoordinator::RequestSnapshotAndWait(
    const std::string_view trigger, const std::chrono::milliseconds timeout) {
    std::unique_lock lock(impl_->completed_mutex);
    const auto starting = impl_->completed_sequence;
    impl_->state->RequestSnapshot(trigger);
    if (!impl_->completed_changed.wait_for(lock, timeout, [&] {
            return impl_->completed_sequence != starting;
        })) return std::nullopt;
    return impl_->last_files;
}

std::filesystem::path DiagCoordinator::ControlFile() const {
    return impl_->control_file;
}

std::string DiagCoordinator::ExternalTriggerName() const {
    return impl_->external_name;
}

std::string TriggerExternalDiagnostic(
    const std::uint64_t process_id, const std::filesystem::path& directory) {
    const auto control = directory /
        ("diag-control-" + std::to_string(process_id) + ".json");
    std::ifstream input(control, std::ios::binary);
    if (!input) {
        throw std::runtime_error("diagnostic control file not found: " +
                                 control.string());
    }
    const std::string contents((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
    core::JsonParseError error;
    const auto document = core::JsonDocument::ParseStrict(contents, error);
    if (!document) {
        throw std::runtime_error("diagnostic control file is invalid: " +
                                 error.message);
    }
    const auto trigger = document->Root().Member("external_trigger");
    const auto name = trigger ? trigger->String() : std::nullopt;
    if (!name) throw std::runtime_error("diagnostic control trigger is missing");
    hal::SignalDiagnosticTrigger(process_id, *name);
    return std::string(*name);
}

}  // namespace ogplay::runtime::debug
