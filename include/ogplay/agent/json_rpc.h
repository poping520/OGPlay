#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <functional>

#include "ogplay/agent/control_service.h"
#include "ogplay/core/json.h"

namespace ogplay::agent {

class JsonRpcAdapter final {
public:
    // Handler responses are produced in-process and may embed library icons, so they get a
    // larger bound than untrusted requests (JsonDocument::kDefaultMaximumBytes).
    static constexpr std::size_t kMaximumResponseBytes = 64U * 1024U * 1024U;

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
