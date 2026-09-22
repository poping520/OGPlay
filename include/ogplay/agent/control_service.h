#pragma once

#include <cstdint>
#include <optional>
#include <memory>
#include "ogplay/core/json.h"
#include <string>
#include <string_view>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/gpu_state.h"
#include "ogplay/core/logger.h"
#include "ogplay/session/session.h"

namespace ogplay::agent {
class DashboardService;

struct ControlParams {
    std::uint64_t frames{1};
    std::uint64_t target_frame{};
    std::uint64_t max_frames{};
    std::optional<std::uint64_t> address;
    std::optional<std::string> filter;
    std::uint64_t limit{100};
};

struct ControlResponse {
    bool ok{};
    std::string json;
};

class ControlService final {
public:
    ControlService(core::CapabilityLedger& ledger,
                   core::Logger& logger,
                   session::Session& session,
                   const core::GpuStateProvider* gpu = nullptr,
                   std::shared_ptr<DashboardService> dashboard = {});

    [[nodiscard]] ControlResponse Request(std::string_view method,
                                          const ControlParams& params = {});
    [[nodiscard]] ControlResponse RequestDashboard(std::string_view method, core::JsonValue params);

private:
    core::CapabilityLedger& ledger_;
    core::Logger& logger_;
    session::Session& session_;
    const core::GpuStateProvider* gpu_{};
    std::shared_ptr<DashboardService> dashboard_;
};

}  // namespace ogplay::agent
