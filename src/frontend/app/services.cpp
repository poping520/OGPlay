#include "ogplay/app/agent.h"
#include "json_private.h"
#include "ogplay/core/encoding.h"
#include "ogplay/app/diagnostics.h"
#include "ogplay/app/host.h"
#include "ogplay/agent/json_rpc.h"
#include "ogplay/hal/clock.h"
#include "ogplay/hal/host_environment.h"
#include "ogplay/frontend/data_directory.h"
#include "ogplay/frontend/user_data_dir.h"
#include "ogplay/runtime/debug/stall_diagnostics.h"
#include <stdexcept>
#include <array>
#include <system_error>
#include <utility>

namespace ogplay::app {
static_assert(JsonRpcDispatcher::kMaximumResponseBytes == agent::JsonRpcAdapter::kMaximumResponseBytes);

struct JsonRpcDispatcher::Impl {
    agent::JsonRpcAdapter rpc;

    explicit Impl(RequestHandler handler)
        : rpc([handler = std::move(handler)](std::string_view method, core::JsonValue params) {
              auto result = handler(method, JsonAccess::Wrap(params));
              return agent::ControlResponse{result.ok, std::move(result.json)};
          }) {}
};

JsonRpcDispatcher::JsonRpcDispatcher(RequestHandler handler) : impl_(std::make_unique<Impl>(std::move(handler))) {}

JsonRpcDispatcher::~JsonRpcDispatcher() = default;

std::string JsonRpcDispatcher::Handle(std::string_view request) {
    return impl_->rpc.Handle(request);
}

std::string NewControlToken() {
    std::array<std::byte, 16> entropy{};
    hal::FillSecureRandom(entropy);
    return core::EncodeHex(entropy);
}

std::string RequestDiagnosticSnapshot(std::uint64_t pid, const std::filesystem::path& directory) {
    return runtime::debug::TriggerExternalDiagnostic(pid, directory);
}

DataPaths BundledDataPaths() {
    auto paths = frontend::HostBundledDataPaths();
    return {paths.root, paths.profiles_directory};
}

std::filesystem::path ExecutablePath() {
    return hal::HostExecutablePath();
}

std::string HostPlatformName() {
    return std::string(hal::HostPlatformName());
}

namespace {

frontend::UserDataLocation RequireDefaultLibraryLocation() {
    auto location = frontend::HostUserDataLocation();
    if (!location) throw std::runtime_error("host user-data directory is unavailable; use --library-root <dir>");
    return std::move(*location);
}

}  // namespace

std::filesystem::path DefaultLibraryRoot() {
    return RequireDefaultLibraryLocation().root;
}

LibraryRootLocation DefaultLibraryLocation() {
    auto location = RequireDefaultLibraryLocation();
    std::optional<std::filesystem::path> legacy_root;
    if (location.portable) {
        auto standard = hal::HostUserDataDirectory();
        std::error_code error;
        if (standard && std::filesystem::exists(*standard, error)) legacy_root = std::move(standard);
    }
    return {std::move(location.root), location.portable, std::move(legacy_root)};
}

std::string UtcTimestamp() {
    return hal::Clock::UtcTimestamp();
}

std::uint64_t MonotonicClock::NowNs() {
    return hal::Clock::SteadyTimestampNs();
}
}  // namespace ogplay::app
