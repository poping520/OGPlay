#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include "ogplay/agent/control_service.h"
#include "ogplay/core/json.h"

namespace ogplay::agent {

// Carries key-editor requests (control.*, ADR-0134) from the loopback worker to the guest
// main loop, which owns the ControlMapper, and returns the main loop's reply. The worker
// never touches session state; an unanswered request times out as "session busy".
class ControlEditorChannel final {
public:
    static constexpr std::size_t kMaximumPendingRequests = 16U;
    using Handler = std::function<ControlResponse(std::string_view method, core::JsonValue params)>;

    [[nodiscard]] ControlResponse Call(std::string_view method, core::JsonValue params,
                                       std::chrono::milliseconds timeout = std::chrono::seconds(2));
    // Main loop: answers every pending request in order; handler exceptions become errors.
    void ServePending(const Handler& handler);
    // Rejects pending and future requests (session shutting down).
    void Close();

private:
    struct Pending final {
        std::string method, params;
        bool done{};
        ControlResponse response;
    };
    std::mutex mutex_;
    std::condition_variable answered_;
    std::deque<std::shared_ptr<Pending>> pending_;
    bool closed_{};
};

[[nodiscard]] ControlResponse ControlError(int code, std::string_view message);
// Wraps a serialized JSON value as {"result": value}.
[[nodiscard]] ControlResponse ControlResult(std::string result_json);

}  // namespace ogplay::agent
