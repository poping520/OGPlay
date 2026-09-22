#pragma once
#include <deque>
#include <map>
#include "ogplay/runtime/integration/native_library_loader.h"
#include <functional>
#include <memory>
#include <mutex>
#include "ogplay/agent/control_service.h"
#include "ogplay/agent/mcp_session_control.h"
#include "ogplay/runtime/debug/stall_diagnostics.h"
#include "ogplay/runtime/vfs/vfs.h"
#include "ogplay/runtime/integration/dexvm_android.h"

namespace ogplay::agent {
// Providers must use atomic reads or try-lock snapshots, never guest execution or IO.
// Sources are immutable after construction and must outlive this service.
struct DashboardSources final {
    runtime::debug::DiagnosticState* diagnostics{};
    const McpSessionControl* session{};
    const core::Logger* logger{};
    const core::CapabilityLedger* ledger{};
    std::function<std::optional<core::GpuStats>()> gpu;
    std::function<std::optional<runtime::VfsIoStatistics>()> vfs;
    std::function<std::optional<std::vector<runtime::AndroidAudioTrackDiagnosticSnapshot>>()> audio;
    std::function<std::optional<runtime::dexvm::InterpreterSnapshot>()> dexvm;
    std::function<std::optional<runtime::JniReferenceSnapshot>()> jni;
    std::function<std::optional<memory::MemoryStatistics>()> memory;
    std::function<std::optional<std::vector<cpu::DynarmicCacheSnapshot>>()> cpu;
    std::function<std::optional<runtime::NativeLibrarySnapshot>()> libraries;
    std::function<std::optional<runtime::VfsSnapshot>()> filesystem;
    std::function<std::optional<runtime::AndroidUiSnapshot>()> ui;
    std::function<std::optional<std::vector<runtime::AndroidVideoSnapshot>>()> video;
    std::map<std::string, std::string> metadata;
    bool gpu_errors_available{true};

};
class DashboardService final {
public:
    explicit DashboardService(DashboardSources sources = {}, std::size_t capacity = 4096);
    [[nodiscard]] ControlResponse Request(std::string_view method, core::JsonValue params = {});
private:
    struct Event {
        std::uint64_t sequence{}, source_sequence{};
        std::string kind;
        std::optional<std::uint64_t> steady_ns, guest_tid, context_token, frame;
        std::uint64_t code{};
        std::int64_t result{};
        std::string detail;
        std::uint64_t source_detail{}; // Native method_id or DexVM tick.
        std::optional<std::int32_t> fd;
        std::optional<std::uint64_t> node_id;
        std::string capability;
        std::optional<std::uint64_t> player, observed_at;
        std::uint64_t delta{};
    };
    void CollectEvents(const runtime::debug::GuestStallSnapshot& snapshot);
    void CollectCounters();
    void Observe(std::string_view kind, std::string key, std::uint64_t total, std::string capability = {}, std::optional<std::uint64_t> player = {});
    std::map<std::string, std::uint64_t> counters_;
    std::map<std::string, std::string> counter_status_;
    DashboardSources sources_;
    const std::size_t capacity_;
    const std::uint64_t stream_id_;
    std::mutex mutex_;
    std::deque<Event> events_;
    std::uint64_t sequence_{}, dropped_{}, syscall_cursor_{}, native_cursor_{}, dexvm_cursor_{}, lifecycle_cursor_{};
};
}
