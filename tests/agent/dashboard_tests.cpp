#include <doctest/doctest.h>
#include <atomic>
#include <chrono>
#include <future>
#include "ogplay/agent/dashboard.h"
#include "ogplay/agent/json_rpc.h"
#include "ogplay/hal/clock.h"

namespace {
using namespace ogplay;
using namespace ogplay::runtime;
core::JsonDocument Parse(std::string_view text) {
    core::JsonParseError error;
    auto document = core::JsonDocument::ParseStrict(text, error);
    REQUIRE_MESSAGE(document.has_value(), error.message);
    return std::move(*document);
}
core::JsonValue At(core::JsonValue value, std::string_view key) {
    const auto member = value.Member(key);
    REQUIRE_MESSAGE(member.has_value(), key);
    return *member;
}
core::JsonDocument Call(agent::DashboardService& service, std::string_view method,
                        std::string_view params = "{}") {
    const auto arguments = Parse(params);
    const auto response = service.Request(method, arguments.Root());
    REQUIRE_MESSAGE(response.ok, response.json);
    return Parse(response.json);
}
core::JsonValue Result(const core::JsonDocument& document) { return At(document.Root(), "result"); }
std::uint64_t Uint(core::JsonValue value, std::string_view key) {
    const auto number = At(value, key).UnsignedInteger(); REQUIRE(number); return *number;
}
}

TEST_CASE("Dashboard closed RPC schemas reject invalid requests before providers") {
    unsigned calls{};
    agent::DashboardSources sources;
    sources.gpu = [&]() -> std::optional<core::GpuStats> { ++calls; return core::GpuStats{}; };
    auto dashboard = std::make_shared<agent::DashboardService>(sources);
    core::CapabilityLedger ledger; core::Logger logger;
    hal::FixedStepClock clock{1000, 60000}; session::Session session{clock};
    agent::ControlService service{ledger, logger, session, nullptr, dashboard};
    agent::JsonRpcAdapter rpc{service};
    for (const auto request : {
        R"({"jsonrpc":"2.0","id":1,"method":"dash.overview","extra":1})",
        R"({"jsonrpc":"2.0","id":1,"method":"dash.snapshot","params":{"sections":["bogus"]}})",
        R"({"jsonrpc":"2.0","id":1,"method":"dash.snapshot","params":{"sections":["gpu","gpu"]}})",
        R"({"jsonrpc":"2.0","id":1,"method":"dash.snapshot","params":{"extra":0}})",
        R"({"jsonrpc":"2.0","id":1,"method":"dash.snapshot","params":{"sections":1}})",
        R"({"jsonrpc":"2.0","id":1,"method":"dash.snapshot","params":[]})",
        R"({"jsonrpc":"2.0","id":1,"method":"dash.events","params":{}})",
        R"({"jsonrpc":"2.0","id":1,"method":"dash.events","params":{"since_sequence":0,"limit":1001}})",
        R"({"jsonrpc":"2.0","id":1,"method":"dash.events","params":{"since_sequence":0,"limit":0}})",
        R"({"jsonrpc":"2.0","id":1,"method":"dash.events","params":{"since_sequence":-1}})",
        R"({"jsonrpc":"2.0","id":1,"method":"dash.events","params":{"since_sequence":0.5}})",
        R"({"jsonrpc":"2.0","id":1,"method":"dash.events","params":{"since_sequence":0,"kinds":["bogus"]}})",
        R"({"jsonrpc":"2.0","id":1,"method":"dash.thread","params":{"guest_tid":0}})",
        R"({"jsonrpc":"2.0","id":1,"method":"dash.thread","params":{"guest_tid":"1"}})",
        R"({"jsonrpc":"2.0","id":1,"method":"dash.thread","params":{"guest_tid":1,"guest_tid":2}})"}) {
        CAPTURE(request);
        const auto response = Parse(rpc.Handle(request));
        CHECK(response.Root().Member("error"));
    }
    CHECK(calls == 0);
    for (const auto method : {"dash.overview", "dash.snapshot", "dash.events", "dash.thread"}) {
        const auto params = std::string_view(method) == "dash.events" ? R"({"since_sequence":0})" :
            std::string_view(method) == "dash.thread" ? R"({"guest_tid":1})" : "{}";
        const auto response = Parse(rpc.Handle(std::string(R"({"jsonrpc":"2.0","id":"dash","method":")") + method + R"(","params":)" + params + "}"));
        CHECK(Uint(Result(response), "schema_version") == 1);
        CHECK(At(response.Root(), "id").String() == "dash");
    }
    const auto unknown = Parse(rpc.Handle(R"({"jsonrpc":"2.0","id":1,"method":"dash.mutate"})"));
    CHECK(At(At(unknown.Root(), "error"), "code").Integer() == -32601);
}

TEST_CASE("Dashboard unavailable partial and structured sources preserve read-only state") {
    agent::McpSessionControl control;
    agent::McpSessionSnapshot state; state.frame = 91; state.guest_ticks = 9007199254740993ULL;
    control.Publish(state);
    REQUIRE(control.TryEnqueue(agent::McpSessionCommand::Type::step, 2));
    core::Logger logger; core::CapabilityLedger ledger;
    ledger.RecordUnimplemented("test.missing", 0x1234);
    logger.Write(core::LogLevel::info, "dash", "record", {91, 300, 4, 7}, {{"capability", std::string("test.missing")}});
    agent::DashboardSources sources; sources.session = &control; sources.logger = &logger; sources.ledger = &ledger;
    sources.gpu = []() -> std::optional<core::GpuStats> { return std::nullopt; };
    sources.vfs = []() -> std::optional<VfsIoStatistics> { throw std::runtime_error("private provider error"); };
    agent::DashboardService service{sources};
    const auto document = Call(service, "dash.snapshot"); const auto result = Result(document);
    CHECK(At(At(result, "gpu"), "reason").String() == "busy");
    CHECK(At(At(result, "vfs"), "reason").String() == "provider_error");
    CHECK(At(At(result, "diagnostics"), "reason").String() == "not_connected");
    for (const auto name : {"gpu", "vfs", "diagnostics", "audio"}) {
        CHECK(At(At(result, name), "status").String() == "unavailable");
        CHECK(At(At(result, name), "data").IsNull());
    }
    CHECK(Uint(At(At(result, "session"), "data"), "guest_ticks") == state.guest_ticks);
    CHECK(control.Snapshot().frame == 91);
    CHECK(control.PendingCommands() == 1);
    const auto logs = At(At(result, "log"), "data"); REQUIRE(logs.Size() == 1);
    CHECK(At(*At(*logs.Element(0), "fields").Element(0), "value").String() == "test.missing");
    CHECK(At(*At(At(result, "capabilities"), "data").Element(0), "id").String() == "test.missing");
    const auto overview = Call(service, "dash.overview");
    CHECK(Uint(Result(overview), "stream_id") == Uint(result, "stream_id"));
    CHECK(Uint(result, "stream_id") > 0);
    CHECK(At(Result(overview), "session").Member("data"));
    CHECK_FALSE(At(Result(overview), "log").Member("data"));
    CHECK(At(Result(overview), "session").Member("captured_at_steady_ns"));
}

TEST_CASE("Dashboard copies existing GPU VFS audio counters and honors section selection") {
    agent::DashboardSources sources; unsigned gpu_calls{};
    sources.gpu = [&]() -> std::optional<core::GpuStats> { ++gpu_calls; core::GpuStats s; s.draws = 42; return s; };
    sources.vfs = []() -> std::optional<VfsIoStatistics> { VfsIoStatistics s; s.backing_read_bytes = 123; return s; };
    sources.audio = []() -> std::optional<std::vector<AndroidAudioTrackDiagnosticSnapshot>> {
        AndroidAudioTrackDiagnosticSnapshot s; s.player = 9; s.underrun_count = 3; return std::vector{s};
    };
    agent::DashboardService service{sources};
    const auto selected = Call(service, "dash.snapshot", R"({"sections":["vfs","audio"]})");
    CHECK(gpu_calls == 0); CHECK_FALSE(Result(selected).Member("gpu"));
    CHECK(Uint(At(At(Result(selected), "vfs"), "data"), "backing_read_bytes") == 123);
    CHECK(Uint(*At(At(Result(selected), "audio"), "data").Element(0), "underrun_count") == 3);
    const auto gpu = Call(service, "dash.snapshot", R"({"sections":["gpu"]})");
    CHECK(Uint(At(At(Result(gpu), "gpu"), "data"), "draws") == 42);
    CHECK(gpu_calls == 1);
}

TEST_CASE("Dashboard event ring supports independent cursors filtering loss and deduplication") {
    debug::DiagnosticState state{2, 2};
    agent::DashboardSources sources; sources.diagnostics = &state;
    agent::DashboardService service{sources, 3};
    const auto initial = Call(service, "dash.events", R"({"since_sequence":0})");
    auto cursor = Uint(Result(initial), "next_sequence");
    for (unsigned i = 0; i < 4; ++i) {
        state.RecordSyscall(7, i, -1, SupervisorCallProgress::handled_idle);
        const auto poll = Call(service, "dash.events", "{\"since_sequence\":" + std::to_string(cursor) + "}");
        cursor = Uint(Result(poll), "next_sequence");
    }
    const auto replay = Call(service, "dash.events", R"({"since_sequence":0,"limit":1,"kinds":["syscall"]})");
    CHECK(At(Result(replay), "gap").Bool() == true);
    CHECK(Uint(Result(replay), "dropped") > 0);
    CHECK(Uint(Result(replay), "syscalls_source_dropped") == 2);
    REQUIRE(At(Result(replay), "events").Size() == 1);
    const auto event = *At(Result(replay), "events").Element(0);
    CHECK(At(event, "frame").IsNull()); CHECK(Uint(event, "guest_tid") == 7);
    const auto next = Uint(Result(replay), "next_sequence");
    CHECK(next < cursor);
    const auto rest = Call(service, "dash.events", "{\"since_sequence\":" + std::to_string(next) + "}");
    CHECK(At(Result(rest), "events").Size() == 2);
    const auto empty = Call(service, "dash.events", "{\"since_sequence\":" + std::to_string(cursor) + "}");
    CHECK(At(Result(empty), "events").Size() == 0);
    CHECK(Uint(Result(empty), "latest_sequence") == cursor);
    const auto filtered = Call(service, "dash.events", R"({"since_sequence":0,"kinds":["gc"]})");
    CHECK(At(Result(filtered), "events").Size() == 0);
    CHECK(Uint(Result(filtered), "next_sequence") == cursor);
    const auto future = Parse(R"({"since_sequence":99999})");
    CHECK_FALSE(service.Request("dash.events", future.Root()).ok);
}

TEST_CASE("Dashboard thread joins context keys and preserves unavailable source evidence") {
    debug::DiagnosticState state;
    const auto execution = state.EnterExecution(7, 11, "guest_call", "selected", 0x1234);
    const auto other = state.EnterExecution(8, 12, "guest_call", "unrelated", 0x5678);
    state.RecordSyscall(7, 240, 0, SupervisorCallProgress::handled_idle);
    state.RecordSyscall(8, 1, 0, SupervisorCallProgress::handled_idle);
    const auto native = state.BeginNativeCall(11, 7, 1);
    state.SetDexVmProvider([]() -> std::optional<debug::DiagnosticDexVmSnapshot> {
        debug::DiagnosticDexVmSnapshot s;
        s.threads.push_back({7, 11, "selected"}); s.threads.push_back({8, 12, "unrelated"});
        // Deliberately descending: source watermark must not discard the second event.
        s.events.push_back({20, 11, 5, 1, "invoke", "selected"});
        s.events.push_back({19, 12, 4, 2, "invoke", "unrelated"});
        return s;
    });
    state.SetMonitorProvider([]() -> std::optional<std::vector<debug::DiagnosticMonitor>> {
        return std::vector<debug::DiagnosticMonitor>{{1, 12, 1, {11}, {}}, {2, 13, 1, {14}, {}}};
    });
    state.SetPacerProvider([]() -> std::optional<debug::DiagnosticPacer> { return std::nullopt; });
    state.SetFutexProvider([] {
        cpu::FutexTableSnapshot s;
        s.addresses.push_back({memory::GuestAddress{0x1000}, 0, 0, {{7, 0, false, 1}}});
        s.addresses.push_back({memory::GuestAddress{0x2000}, 0, 0, {{8, 0, false, 1}}});
        return s;
    });
    agent::DashboardSources sources; sources.diagnostics = &state; agent::DashboardService service{sources};
    const auto document = Call(service, "dash.thread", R"({"guest_tid":7})");
    const auto data = At(At(Result(document), "diagnostics"), "data");
    CHECK(At(data, "executions").Size() == 1); CHECK(At(data, "syscalls").Size() == 1);
    CHECK(At(data, "java_threads").Size() == 1); CHECK(At(data, "monitors").Size() == 1);
    CHECK(At(data, "futexes").Size() == 1); CHECK(At(data, "native_calls").Size() == 1);
    const auto events = Call(service, "dash.events", R"({"since_sequence":0,"kinds":["dexvm"]})");
    REQUIRE(At(Result(events), "events").Size() == 2);
    for (std::size_t i = 0; i < 2; ++i) CHECK(At(*At(Result(events), "events").Element(i), "steady_ns").IsNull());
    CHECK(At(At(At(Result(events), "sources"), "pacer"), "reason").String() == "busy");
    const auto no_thread = Call(service, "dash.thread", R"({"guest_tid":999})");
    CHECK(At(At(At(Result(no_thread), "diagnostics"), "data"), "executions").Size() == 0);
    state.EndNativeCall(native, false); state.LeaveExecution(execution); state.LeaveExecution(other);
}

TEST_CASE("Dashboard contended request returns without waiting for a provider") {
    std::promise<void> entered, release;
    auto released = release.get_future().share();
    agent::DashboardSources sources;
    sources.gpu = [&]() -> std::optional<core::GpuStats> { entered.set_value(); released.wait(); return core::GpuStats{}; };
    agent::DashboardService service{sources};
    auto first = std::async(std::launch::async, [&] { return service.Request("dash.snapshot"); });
    entered.get_future().wait();
    const auto start = hal::Clock::SteadyTimestampNs();
    const auto response = service.Request("dash.overview");
    const auto elapsed = hal::Clock::SteadyTimestampNs() - start;
    release.set_value();
    CHECK(first.get().ok);
    CHECK_FALSE(response.ok); CHECK(elapsed < 100000000ULL);
    const auto error = Parse(response.json); CHECK(At(At(error.Root(), "error"), "code").Integer() == -32002);
}

TEST_CASE("Dashboard bounded log copy retains typed fields and UTF8 without rendering") {
    core::Logger logger{256};
    for (unsigned i = 0; i < 140; ++i)
        logger.Write(core::LogLevel::info, "bounded", std::to_string(i), {}, {}, {core::RateLimitMode::none});
    const auto tail = logger.TrySnapshot(1000); REQUIRE(tail); REQUIRE(tail->size() == 128);
    CHECK(tail->front().message == "12"); CHECK(tail->back().message == "139");
    std::vector<core::LogField> fields;
    for (unsigned i = 0; i < 40; ++i) fields.push_back({"number_" + std::to_string(i), std::uint64_t{9007199254740993ULL}});
    logger.Write(core::LogLevel::info, "bounded", std::string(511, 'a') + "中文", {}, fields, {core::RateLimitMode::none});
    const auto bounded = logger.TrySnapshot(1); REQUIRE(bounded); REQUIRE(bounded->size() == 1);
    CHECK(bounded->front().message.size() == 511); CHECK(bounded->front().fields.size() == 32);
    CHECK(std::get<std::uint64_t>(bounded->front().fields[0].value) == 9007199254740993ULL);
}

TEST_CASE("Dashboard bounds diagnostic stacks and propagates busy and error sections") {
    debug::DiagnosticState state;
    state.SetDexVmProvider([]() -> std::optional<debug::DiagnosticDexVmSnapshot> {
        debug::DiagnosticDexVmSnapshot s;
        debug::DiagnosticJavaThread thread; thread.guest_tid = 7; thread.context_token = 11;
        thread.frames.resize(40, {"frame", 1}); s.threads.push_back(std::move(thread)); return s;
    });
    state.SetMonitorProvider([]() -> std::optional<std::vector<debug::DiagnosticMonitor>> { return std::nullopt; });
    state.SetGlesProvider([]() -> std::optional<std::vector<debug::DiagnosticGlesEvent>> { throw std::runtime_error("failure"); });
    agent::DashboardSources sources; sources.diagnostics = &state; agent::DashboardService service{sources};
    const auto snapshot = Call(service, "dash.snapshot", R"({"sections":["diagnostics"]})");
    const auto section = At(Result(snapshot), "diagnostics"); CHECK(At(section, "status").String() == "partial");
    const auto data = At(section, "data");
    CHECK(At(*At(data, "java_threads").Element(0), "frames").Size() == 32);
    const auto events = Call(service, "dash.events", R"({"since_sequence":0})");
    const auto source = At(Result(events), "sources");
    CHECK(At(At(source, "monitors"), "status").String() == "unavailable");
    CHECK(At(At(source, "monitors"), "reason").String() == "busy");
    CHECK(At(At(source, "gles"), "reason").String() == "provider_error");
}

TEST_CASE("Dashboard counter events retain deltas and never invent frame or occurrence time") {
    agent::DashboardSources sources; std::uint64_t collections=3;
    sources.dexvm = [&]() -> std::optional<runtime::dexvm::InterpreterSnapshot> { runtime::dexvm::InterpreterSnapshot s; s.stats.gc_collections=collections; s.heap_used=123; return s; };
    sources.audio = []() -> std::optional<std::vector<AndroidAudioTrackDiagnosticSnapshot>> { return std::vector<AndroidAudioTrackDiagnosticSnapshot>{{.player=9,.underrun_count=2}}; };
    core::CapabilityLedger ledger; ledger.RecordUnimplemented("test.missing",123); sources.ledger=&ledger;
    agent::DashboardService service(sources);
    auto first=Call(service,"dash.events",R"({"since_sequence":0})"); const auto values=At(Result(first),"events"); REQUIRE(values.Size()==3);
    const auto gc=*values.Element(0); CHECK(At(gc,"kind").String()=="gc"); CHECK(Uint(gc,"delta")==3);
    CHECK(At(gc,"frame").IsNull()); CHECK(At(gc,"steady_ns").IsNull()); CHECK(Uint(gc,"observed_at_steady_ns")>0);
    const auto cursor=Uint(Result(first),"next_sequence"); collections=5;
    auto next=Call(service,"dash.events","{\"since_sequence\":"+std::to_string(cursor)+"}");
    REQUIRE(At(Result(next),"events").Size()==1); CHECK(Uint(*At(Result(next),"events").Element(0),"delta")==2);
    auto snap=Call(service,"dash.snapshot",R"({"sections":["dexvm","jni","memory","cpu","libraries","ui","video"]})");
    CHECK(Uint(At(At(Result(snap),"dexvm"),"data"),"heap_used")==123);
    for (const auto name : {"jni","memory","cpu","libraries","ui","video"}) CHECK(At(At(Result(snap),name),"data").IsNull());
}

TEST_CASE("Dashboard unpublished CPU samples are unavailable rather than zero") {
    agent::DashboardSources sources;
    sources.cpu = []() -> std::optional<std::vector<cpu::DynarmicCacheSnapshot>> {
        return std::vector<cpu::DynarmicCacheSnapshot>{{0,0,0,0,0},{1,64,12,2,99}};
    };
    agent::DashboardService service(sources);
    const auto snapshot = Call(service, "dash.snapshot", R"({"sections":["cpu"]})");
    const auto rows = At(At(Result(snapshot), "cpu"), "data");
    CHECK(At(*rows.Element(0), "capacity_bytes").IsNull());
    CHECK(At(*rows.Element(0), "status").String() == "unavailable");
    CHECK(Uint(*rows.Element(1), "used_bytes") == 12);
}

TEST_CASE("Dashboard does not report zero for uninstrumented GL errors") {
    agent::DashboardSources sources; sources.gpu_errors_available = false;
    sources.gpu = []() -> std::optional<core::GpuStats> { core::GpuStats stats; stats.gl_errors = 9; return stats; };
    agent::DashboardService service(sources);
    const auto snapshot = Call(service, "dash.snapshot", R"({"sections":["gpu"]})");
    CHECK(At(At(At(Result(snapshot), "gpu"), "data"), "gl_errors").IsNull());
    const auto events = Call(service, "dash.events", R"({"since_sequence":0,"kinds":["gles_error"]})");
    CHECK(At(Result(events), "events").Size() == 0);
}

TEST_CASE("Dashboard busy FD access flags remain unknown") {
    agent::DashboardSources sources;
    sources.filesystem = []() -> std::optional<runtime::VfsSnapshot> {
        runtime::VfsSnapshot snapshot; runtime::VfsDescriptorSnapshot row; row.fd = 3; row.busy = true;
        snapshot.descriptors.push_back(row); snapshot.partial = true; return snapshot;
    };
    agent::DashboardService service(sources);
    const auto snapshot = Call(service, "dash.snapshot", R"({"sections":["vfs"]})");
    const auto rows = At(At(At(Result(snapshot), "vfs"), "data"), "descriptors");
    CHECK(At(*rows.Element(0), "readable").IsNull());
    CHECK(At(*rows.Element(0), "writable").IsNull());
}
