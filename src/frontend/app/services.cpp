#include "ogplay/app/agent.h"
#include "json_private.h"
#include "ogplay/core/encoding.h"
#include "ogplay/app/diagnostics.h"
#include "ogplay/app/host.h"
#include "ogplay/agent/control_service.h"
#include "ogplay/agent/json_rpc.h"
#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"
#include "ogplay/hal/clock.h"
#include "ogplay/hal/host_environment.h"
#include "ogplay/frontend/data_directory.h"
#include "ogplay/frontend/user_data_dir.h"
#include "ogplay/runtime/debug/stall_diagnostics.h"
#include "ogplay/session/session.h"
#include <stdexcept>
#include <array>
namespace ogplay::app {
struct JsonRpcDispatcher::Impl {
    agent::JsonRpcAdapter rpc;
    explicit Impl(RequestHandler handler) : rpc([handler = std::move(handler)](std::string_view method, core::JsonValue params) {
        auto result = handler(method, JsonAccess::Wrap(params));
        return agent::ControlResponse{result.ok, std::move(result.json)};
    }) {}
};
JsonRpcDispatcher::JsonRpcDispatcher(RequestHandler handler) : impl_(std::make_unique<Impl>(std::move(handler))) {}
JsonRpcDispatcher::~JsonRpcDispatcher() = default;
std::string JsonRpcDispatcher::Handle(std::string_view request) { return impl_->rpc.Handle(request); }
std::string NewControlToken() {
    std::array<std::byte, 16> entropy{};
    hal::FillSecureRandom(entropy);
    return core::EncodeHex(entropy);
}
struct AgentConsole::Impl {
    core::CapabilityLedger ledger;
    core::Logger logger;
    hal::FixedStepClock clock{1000, 60000};
    session::Session session{clock};
    agent::ControlService service{ledger, logger, session};
    agent::JsonRpcAdapter rpc{service};
    explicit Impl(const std::filesystem::path& path) : ledger(core::CapabilityLedger::Load(path)) {}
};
AgentConsole::AgentConsole(const std::filesystem::path& path) : impl_(std::make_unique<Impl>(path)) {}
AgentConsole::~AgentConsole() = default;
ConsoleResponse AgentConsole::Request(std::string_view method) {
    auto result = impl_->service.Request(method); return {result.ok, std::move(result.json)};
}
std::string AgentConsole::HandleJsonRpc(std::string_view line) { return impl_->rpc.Handle(line); }
std::string RequestDiagnosticSnapshot(std::uint64_t pid, const std::filesystem::path& directory) {
    return runtime::debug::TriggerExternalDiagnostic(pid, directory);
}
DataPaths BundledDataPaths() {
    auto paths = frontend::HostBundledDataPaths();
    return {paths.root, paths.profiles_directory, paths.quirk_registry};
}
std::filesystem::path ExecutablePath() { return hal::HostExecutablePath(); }
std::filesystem::path DefaultLibraryRoot() {
    auto root = frontend::UserDataDirectory();
    if (!root) throw std::runtime_error("host user-data directory is unavailable; use --library-root <dir>");
    return *root;
}
std::string UtcTimestamp() { return hal::Clock::UtcTimestamp(); }
std::uint64_t MonotonicClock::NowNs() { return hal::Clock::SteadyTimestampNs(); }
}
