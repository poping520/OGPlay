#include "ogplay/agent/dashboard.h"
#include "ogplay/core/text.h"
#include "ogplay/hal/clock.h"
#include <algorithm>
#include <array>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <cmath>

namespace ogplay::agent {
namespace {
using Writer = core::JsonWriter;
using Value = Writer::Value;
using Snapshot = runtime::debug::GuestStallSnapshot;
using Status = runtime::debug::DiagnosticSectionStatus;
constexpr std::array<std::string_view, 7> sections{"session", "diagnostics", "gpu", "vfs", "audio", "capabilities", "log"};
constexpr std::array<std::string_view, 9> kinds{"gc", "gles_error", "syscall", "native", "dexvm", "capability_miss", "audio_underrun", "lifecycle", "vfs_flush"};
std::string Text(std::string_view text) {
    if (!core::IsValidUtf8(text)) return "<invalid_utf8>";
    auto size = std::min<std::size_t>(text.size(), 512);
    while (size && !core::IsValidUtf8(text.substr(0, size))) --size;
    return std::string(text.substr(0, size));
}
ControlResponse Error(int code, std::string_view message) {
    Writer writer; auto root = writer.Object(), error = writer.Object();
    writer.AddInteger(error, "code", code); writer.AddString(error, "message", message); writer.Add(root, "error", error);
    return {false, writer.Serialize(root)};
}
void Fields(core::JsonValue params, std::initializer_list<std::string_view> keys) {
    if (params.IsValid() && !params.IsObject()) throw std::invalid_argument("params must be an object");
    std::size_t count{}; for (auto key : keys) if (params.Member(key)) ++count;
    if (count != params.Size()) throw std::invalid_argument("unknown Dashboard parameter");
}
std::uint64_t Number(core::JsonValue params, std::string_view key, std::uint64_t fallback, bool required = false) {
    const auto value = params.Member(key);
    if (!value) { if (required) throw std::invalid_argument(std::string(key) + " required"); return fallback; }
    if (!value->UnsignedInteger()) throw std::invalid_argument(std::string(key) + " must be unsigned integer");
    return *value->UnsignedInteger();
}
template<std::size_t N>
std::set<std::string> Names(core::JsonValue params, std::string_view key, const std::array<std::string_view, N>& allowed) {
    std::set<std::string> result;
    auto value = params.Member(key);
    if (!value) { for (auto name : allowed) result.insert(std::string(name)); return result; }
    if (!value->IsArray() || value->Size() > N) throw std::invalid_argument("invalid Dashboard selector array");
    for (std::size_t i = 0; i < value->Size(); ++i) {
        const auto name = value->Element(i)->String();
        if (!name || std::find(allowed.begin(), allowed.end(), *name) == allowed.end() || !result.insert(std::string(*name)).second)
            throw std::invalid_argument("unknown or duplicate Dashboard selector");
    }
    return result;
}
template<class T> void Optional(Writer& writer, Value value, std::string_view key, const std::optional<T>& number) {
    if (number) writer.AddUnsignedInteger(value, key, *number); else writer.AddNull(value, key);
}
template<class T> bool Cap(std::vector<T>& values, std::size_t limit = 128) {
    if (values.size() <= limit) return false;
    values.resize(limit); return true;
}
void Bound(Snapshot& snapshot) {
    bool truncated = Cap(snapshot.executions) | Cap(snapshot.syscalls, 256) | Cap(snapshot.native_calls, 256) |
        Cap(snapshot.dexvm_events, 256) | Cap(snapshot.java_threads) | Cap(snapshot.monitors) | Cap(snapshot.confirmed_cycles) |
        Cap(snapshot.gles_events) | Cap(snapshot.futexes.addresses);
    for (auto& cycle : snapshot.confirmed_cycles) truncated |= Cap(cycle);
    for (auto& thread : snapshot.java_threads) truncated |= Cap(thread.frames, 32);
    for (auto& monitor : snapshot.monitors) { truncated |= Cap(monitor.entry_waiters); truncated |= Cap(monitor.notify_wait_set); }
    for (auto& address : snapshot.futexes.addresses) truncated |= Cap(address.waiters);
    if (truncated) for (auto& section : snapshot.sections) if (section.status == Status::complete) {
        section.status = Status::partial; section.reason = "dashboard_limit";
    }
}
Snapshot Thread(Snapshot snapshot, std::uint64_t tid) {
    std::set<std::uint64_t> contexts;
    for (const auto& value : snapshot.executions) if (value.guest_tid == tid && value.context_token) contexts.insert(value.context_token);
    for (const auto& value : snapshot.java_threads) if (value.guest_tid == tid && value.context_token) contexts.insert(value.context_token);
    std::erase_if(snapshot.executions, [&](const auto& v) { return v.guest_tid != tid; });
    std::erase_if(snapshot.java_threads, [&](const auto& v) { return v.guest_tid != tid; });
    std::erase_if(snapshot.syscalls, [&](const auto& v) { return v.guest_tid != tid; });
    std::erase_if(snapshot.native_calls, [&](const auto& v) { return v.guest_tid != tid && !contexts.contains(v.context_token); });
    std::erase_if(snapshot.dexvm_events, [&](const auto& v) { return !contexts.contains(v.context_token); });
    const auto linked = [&](const auto& list) { return std::any_of(list.begin(), list.end(), [&](auto v) { return contexts.contains(v); }); };
    std::erase_if(snapshot.monitors, [&](const auto& v) { return !contexts.contains(v.owner_context) && !linked(v.entry_waiters) && !linked(v.notify_wait_set); });
    std::erase_if(snapshot.confirmed_cycles, [&](const auto& v) { return !linked(v); });
    std::erase_if(snapshot.futexes.addresses, [&](const auto& v) { return std::none_of(v.waiters.begin(), v.waiters.end(), [&](const auto& waiter) { return waiter.thread_id == tid; }); });
    snapshot.gles_events.clear(); // Existing GLES trace has no thread key.
    return snapshot;
}
}
DashboardService::DashboardService(DashboardSources sources, std::size_t capacity)
    : sources_(std::move(sources)), capacity_(capacity) {
    if (!capacity || capacity > 4096) throw std::invalid_argument("Dashboard capacity must be 1..4096");
}
void DashboardService::CollectEvents(const Snapshot& snapshot) {
    std::vector<Event> fresh;
    const auto syscall_cursor = syscall_cursor_, native_cursor = native_cursor_, dexvm_cursor = dexvm_cursor_;
    for (const auto& event : snapshot.syscalls) if (event.sequence > syscall_cursor) {
        fresh.push_back({0, event.sequence, "syscall", event.steady_ns, event.guest_tid, {}, {}, event.syscall_nr, event.result, {}});
        syscall_cursor_ = std::max(syscall_cursor_, event.sequence);
    }
    for (const auto& event : snapshot.native_calls) if (event.sequence > native_cursor) {
        fresh.push_back({0, event.sequence, "native", event.steady_ns, event.guest_tid, event.context_token, {},
            event.call_id, static_cast<std::int64_t>(event.phase), Text(event.method), event.method_id});
        native_cursor_ = std::max(native_cursor_, event.sequence);
    }
    for (const auto& event : snapshot.dexvm_events) if (event.sequence > dexvm_cursor) {
        fresh.push_back({0, event.sequence, "dexvm", {}, {}, event.context_token, {}, event.dex_pc, 0, Text(event.kind + ": " + event.method), event.tick});
        dexvm_cursor_ = std::max(dexvm_cursor_, event.sequence);
    }
    if (snapshot.lifecycle_generation > lifecycle_cursor_) {
        fresh.push_back({0, snapshot.lifecycle_generation, "lifecycle", snapshot.captured_at_steady_ns - std::min(snapshot.captured_at_steady_ns, snapshot.lifecycle_unchanged_ns),
            {}, {}, {}, 0, 0, Text(snapshot.lifecycle_phase)});
        lifecycle_cursor_ = snapshot.lifecycle_generation;
    }
    // Sequence is observation order, not an invented cross-source clock order.
    for (auto& event : fresh) {
        event.sequence = ++sequence_;
        if (events_.size() == capacity_) { events_.pop_front(); ++dropped_; }
        events_.push_back(std::move(event));
    }
}
ControlResponse DashboardService::Request(std::string_view method, core::JsonValue params) {
    try {
        const bool events = method == "dash.events", thread = method == "dash.thread", overview = method == "dash.overview";
        if (!events && !thread && !overview && method != "dash.snapshot") return Error(-32601, "unknown Dashboard method");
        if (events) Fields(params, {"since_sequence", "limit", "kinds"});
        else if (thread) Fields(params, {"guest_tid"});
        else if (overview) Fields(params, {});
        else Fields(params, {"sections"});
        auto selected = Names(params, "sections", sections);
        auto selected_kinds = Names(params, "kinds", kinds);
        const auto since = events ? Number(params, "since_sequence", 0, true) : 0;
        const auto limit = events ? Number(params, "limit", 100) : 100;
        const auto tid = thread ? Number(params, "guest_tid", 0, true) : 0;
        if (limit == 0 || limit > 1000 || (thread && tid == 0)) throw std::invalid_argument("Dashboard parameter outside range");
        std::unique_lock lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock()) return Error(-32002, "Dashboard busy; retry");
        if (events && since > sequence_) return Error(-32602, "cursor ahead of this Dashboard stream; reset since_sequence to 0");
        const auto now = hal::Clock::SteadyTimestampNs();
        std::optional<Snapshot> diagnostic;
        if ((events || thread || selected.contains("diagnostics")) && sources_.diagnostics) {
            diagnostic = sources_.diagnostics->Collect("dashboard");
            CollectEvents(*diagnostic);
            if (!thread) Bound(*diagnostic);
        }
        Writer writer; const auto root = writer.Object(), result = writer.Object();
        writer.AddUnsignedInteger(result, "schema_version", 1);
        writer.AddUnsignedInteger(result, "captured_at_steady_ns", now);
        const auto section = [&](std::string_view name, std::string_view status, std::uint64_t generation, std::string_view reason, Value data) {
            const auto value = writer.Object(); writer.AddString(value, "status", status);
            writer.AddUnsignedInteger(value, "captured_at_steady_ns", now); writer.AddUnsignedInteger(value, "generation", generation);
            writer.AddString(value, "reason", reason); if (!overview || name == "session") writer.Add(value, "data", data);
            writer.Add(result, name, value);
        };
        if (events) {
            const auto values = writer.Array(); std::uint64_t next = since; std::size_t count{};
            for (const auto& event : events_) {
                if (event.sequence <= since) continue;
                next = event.sequence;
                if (!selected_kinds.contains(event.kind)) continue;
                const auto value = writer.Object();
                writer.AddUnsignedInteger(value, "sequence", event.sequence); writer.AddUnsignedInteger(value, "source_sequence", event.source_sequence);
                writer.AddString(value, "kind", event.kind); Optional(writer, value, "steady_ns", event.steady_ns);
                Optional(writer, value, "frame", event.frame); Optional(writer, value, "guest_tid", event.guest_tid); Optional(writer, value, "context_token", event.context_token);
                if (event.kind == "syscall") {
                    writer.AddUnsignedInteger(value, "syscall_nr", event.code); writer.AddInteger(value, "result", event.result);
                } else if (event.kind == "native") {
                    writer.AddUnsignedInteger(value, "call_id", event.code); writer.AddUnsignedInteger(value, "method_id", event.source_detail);
                    writer.AddString(value, "phase", event.result == 0 ? "enter" : event.result == 1 ? "returned" : "threw");
                    writer.AddString(value, "method", event.detail);
                } else if (event.kind == "dexvm") {
                    writer.AddUnsignedInteger(value, "dex_pc", event.code); writer.AddUnsignedInteger(value, "tick", event.source_detail);
                    writer.AddString(value, "detail", event.detail);
                } else writer.AddString(value, "lifecycle_phase", event.detail);
                writer.Append(values, value); if (++count == limit) break;
            }
            writer.Add(result, "events", values); writer.AddUnsignedInteger(result, "next_sequence", next);
            writer.AddUnsignedInteger(result, "latest_sequence", sequence_); writer.AddUnsignedInteger(result, "dropped", dropped_);
            writer.AddUnsignedInteger(result, "oldest_sequence", events_.empty() ? sequence_ + 1 : events_.front().sequence);
            writer.AddBool(result, "gap", !events_.empty() && since < events_.front().sequence - 1);
            writer.AddString(result, "status", diagnostic ? "partial" : "unavailable");
            const auto supported = writer.Array(); for (const auto name : {"syscall", "native", "dexvm", "lifecycle"}) writer.Append(supported, writer.String(name));
            writer.Add(result, "supported_kinds", supported);
            writer.AddUnsignedInteger(result, "generation", sequence_);
            const auto source_sections = writer.Object();
            if (diagnostic) for (const auto& source : diagnostic->sections) {
                const auto info = writer.Object();
                writer.AddString(info, "status", source.status == Status::complete ? "complete" : source.status == Status::partial ? "partial" : "unavailable");
                writer.AddString(info, "reason", source.reason);
                writer.AddUnsignedInteger(info, "captured_at_steady_ns", source.captured_at_steady_ns);
                writer.AddUnsignedInteger(info, "generation", source.generation);
                writer.Add(source_sections, source.name, info);
                if (source.name == "syscalls" || source.name == "native_calls") {
                    const auto key = source.name == "syscalls" ? "syscalls_source_dropped" : "native_source_dropped";
                    if (source.status == Status::unavailable) writer.AddNull(result, key);
                    else writer.AddUnsignedInteger(result, key, source.name == "syscalls" ? diagnostic->syscalls_dropped : diagnostic->native_calls_dropped);
                }
            }
            writer.Add(result, "sources", source_sections);
        } else {
            if (thread) {
                selected = {"diagnostics"}; writer.AddUnsignedInteger(result, "guest_tid", tid);
                if (diagnostic) { *diagnostic = Thread(std::move(*diagnostic), tid); Bound(*diagnostic); }
            }
            for (const auto& name : selected) {
                bool connected = false;
                try {
                    if (name == "diagnostics") {
                        connected = sources_.diagnostics != nullptr;
                        if (diagnostic) {
                            const auto complete = std::all_of(diagnostic->sections.begin(), diagnostic->sections.end(), [](const auto& s) { return s.status == Status::complete; });
                            const auto available = std::any_of(diagnostic->sections.begin(), diagnostic->sections.end(), [](const auto& s) { return s.status != Status::unavailable; });
                            section(name, complete ? "complete" : available ? "partial" : "unavailable", diagnostic->sequence,
                                complete ? "" : "see source sections", available ? runtime::debug::AppendGuestStallSnapshotJson(writer, *diagnostic) : writer.Null());
                            continue;
                        }
                    } else if (name == "session") {
                        connected = sources_.session != nullptr;
                        if (connected) if (const auto s = sources_.session->TrySnapshot()) {
                            const auto value = writer.Object(); writer.AddString(value, "lifecycle", McpLifecycleStateName(s->lifecycle));
                            writer.AddUnsignedInteger(value, "frame", s->frame); writer.AddUnsignedInteger(value, "guest_ticks", s->guest_ticks);
                            Optional(writer, value, "presented_frame", s->presented_frame); writer.AddBool(value, "shutdown_requested", s->shutdown_requested);
                            writer.AddBool(value, "process_exit", s->process_exit);
                            if (s->guest_fault) writer.AddString(value, "guest_fault", Text(*s->guest_fault)); else writer.AddNull(value, "guest_fault");
                            section(name, "complete", 0, "", value); continue;
                        }
                    } else if (name == "gpu") {
                        connected = static_cast<bool>(sources_.gpu);
                        if (connected) if (const auto s = sources_.gpu()) {
                            const auto value = writer.Object(); writer.AddUnsignedInteger(value, "draws", s->draws); writer.AddUnsignedInteger(value, "clears", s->clears);
                            writer.AddUnsignedInteger(value, "shader_compiles", s->shader_compiles); writer.AddUnsignedInteger(value, "program_links", s->program_links);
                            writer.AddUnsignedInteger(value, "gl_errors", s->gl_errors); section(name, "partial", 0, "stats only", value); continue;
                        }
                    } else if (name == "vfs") {
                        connected = static_cast<bool>(sources_.vfs);
                        if (connected) if (const auto s = sources_.vfs()) {
                            const auto value = writer.Object();
                            writer.AddUnsignedInteger(value, "backing_read_bytes", s->backing_read_bytes);
                            writer.AddUnsignedInteger(value, "full_materialized_bytes", s->full_materialized_bytes);
                            writer.AddUnsignedInteger(value, "resource_memory_bytes", s->resource_memory_bytes);
                            writer.AddUnsignedInteger(value, "resource_memory_high_water", s->resource_memory_high_water);
                            writer.AddUnsignedInteger(value, "lease_snapshot_bytes", s->lease_snapshot_bytes);
                            writer.AddUnsignedInteger(value, "lease_snapshot_high_water", s->lease_snapshot_high_water);
                            section(name, "partial", 0, "IO statistics only; mount/FD snapshots pending", value); continue;
                        }
                    } else if (name == "capabilities") {
                        connected = sources_.ledger != nullptr;
                        if (connected) if (auto hits = sources_.ledger->TryUnimplemented(129)) {
                            const bool truncated = Cap(*hits); const auto value = writer.Array();
                            for (const auto& hit : *hits) { const auto item = writer.Object(); writer.AddString(item, "id", Text(hit.id));
                                writer.AddUnsignedInteger(item, "count", hit.count); writer.AddUnsignedInteger(item, "first_lr", hit.first_lr);
                                writer.AddUnsignedInteger(item, "last_lr", hit.last_lr); writer.Append(value, item); }
                            section(name, "partial", 0, truncated ? "limit; unimplemented hits only" : "unimplemented hits only", value); continue;
                        }
                    } else if (name == "log") {
                        connected = sources_.logger != nullptr;
                        if (connected) if (auto logs = sources_.logger->TrySnapshot(128)) {
                            const auto value = writer.Array();
                            for (const auto& log : *logs) { const auto item = writer.Object();
                                writer.AddUnsignedInteger(item, "frame", log.frame); writer.AddUnsignedInteger(item, "guest_ticks", log.guest_ticks);
                                writer.AddUnsignedInteger(item, "host_tid", log.host_thread); writer.AddUnsignedInteger(item, "guest_tid", log.guest_thread);
                                writer.AddString(item, "level", core::ToString(log.level)); writer.AddString(item, "category", Text(log.category));
                                writer.AddString(item, "message", Text(log.message));
                                const auto fields = writer.Array();
                                for (std::size_t i = 0; i < std::min<std::size_t>(log.fields.size(), 32); ++i) {
                                    const auto field = writer.Object(); writer.AddString(field, "key", Text(log.fields[i].key));
                                    std::visit([&](const auto& v) {
                                        using T = std::decay_t<decltype(v)>;
                                        if constexpr (std::is_same_v<T, std::string>) writer.AddString(field, "value", Text(v));
                                        else if constexpr (std::is_same_v<T, core::GuestAddress>) writer.AddUnsignedInteger(field, "value", v.value);
                                        else if constexpr (std::is_same_v<T, bool>) writer.AddBool(field, "value", v);
                                        else if constexpr (std::is_same_v<T, double>) { if (std::isfinite(v)) writer.AddReal(field, "value", v); else writer.AddNull(field, "value"); }
                                        else if constexpr (std::is_signed_v<T>) writer.AddInteger(field, "value", v);
                                        else writer.AddUnsignedInteger(field, "value", v);
                                    }, log.fields[i].value);
                                    writer.Append(fields, field);
                                }
                                writer.Add(item, "fields", fields); writer.Append(value, item); }
                            section(name, "partial", 0, "bounded structured tail: 128 records, 32 fields, 512-byte strings", value); continue;
                        }
                    } else if (name == "audio") {
                        connected = static_cast<bool>(sources_.audio);
                        if (connected) if (auto tracks = sources_.audio()) {
                            const bool truncated = Cap(*tracks); const auto value = writer.Array();
                            for (const auto& track : *tracks) { const auto item = writer.Object(); writer.AddUnsignedInteger(item, "player", track.player);
                                writer.AddUnsignedInteger(item, "written_frames", track.written_frames); writer.AddUnsignedInteger(item, "consumed_frames", track.consumed_frames);
                                writer.AddUnsignedInteger(item, "queued_bytes", track.queued_bytes); writer.AddUnsignedInteger(item, "underrun_count", track.underrun_count);
                                writer.AddUnsignedInteger(item, "underrun_output_frames", track.underrun_output_frames); writer.Append(value, item); }
                            section(name, "partial", 0, truncated ? "limit; AudioTrack only" : "AudioTrack only", value); continue;
                        }
                    }
                    section(name, "unavailable", 0, connected ? "busy" : "not_connected", writer.Null());
                } catch (const std::exception&) { section(name, "unavailable", 0, "provider_error", writer.Null()); }
            }
        }
        writer.Add(root, "result", result); auto json = writer.Serialize(root);
        if (json.size() > 1024 * 1024) return Error(-32003, "Dashboard response exceeds 1 MiB; select fewer sections");
        return {true, std::move(json)};
    } catch (const std::invalid_argument& error) { return Error(-32602, error.what()); }
}
}
