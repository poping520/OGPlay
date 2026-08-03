#pragma once

#include <string>
#include <string_view>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"

namespace ogplay::agent {

struct ControlResponse {
    bool ok{};
    std::string json;
};

class ControlService final {
public:
    ControlService(core::CapabilityLedger& ledger, core::Logger& logger);

    [[nodiscard]] ControlResponse Request(std::string_view method) const;

private:
    core::CapabilityLedger& ledger_;
    core::Logger& logger_;
};

}  // namespace ogplay::agent

