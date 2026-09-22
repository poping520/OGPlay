#pragma once

#include <string>
#include <string_view>
#include <functional>

#include "ogplay/agent/control_service.h"
#include "ogplay/core/json.h"

namespace ogplay::agent {

class JsonRpcAdapter final {
public:
    explicit JsonRpcAdapter(ControlService& service);
    using RequestHandler = std::function<ControlResponse(std::string_view, core::JsonValue)>;
    // The callback is synchronous; JsonValue views expire when Handle returns.
    explicit JsonRpcAdapter(RequestHandler handler);

    [[nodiscard]] std::string Handle(std::string_view request);

private:
    ControlService* service_{};
    RequestHandler handler_;
};

}  // namespace ogplay::agent
