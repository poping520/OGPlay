#pragma once

#include <string>
#include <string_view>

#include "ogplay/agent/control_service.h"

namespace ogplay::agent {

class JsonRpcAdapter final {
public:
    explicit JsonRpcAdapter(ControlService& service);

    [[nodiscard]] std::string Handle(std::string_view request);

private:
    ControlService& service_;
};

}  // namespace ogplay::agent
