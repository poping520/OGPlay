#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"
#include "ogplay/session/session.h"

namespace ogplay::agent {

struct ControlParams {
    std::uint64_t frames{1};
    std::uint64_t target_frame{};
    std::uint64_t max_frames{};
    std::optional<std::uint64_t> address;
};

struct ControlResponse {
    bool ok{};
    std::string json;
};

class ControlService final {
public:
    ControlService(core::CapabilityLedger& ledger,
                   core::Logger& logger,
                   session::Session& session);

    [[nodiscard]] ControlResponse Request(std::string_view method,
                                          const ControlParams& params = {});

private:
    core::CapabilityLedger& ledger_;
    core::Logger& logger_;
    session::Session& session_;
};

}  // namespace ogplay::agent
