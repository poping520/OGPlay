#include "ogplay/agent/control_editor.h"

#include <stdexcept>

namespace ogplay::agent {

ControlResponse ControlError(const int code, const std::string_view message) {
    core::JsonWriter writer;
    const auto root = writer.Object();
    const auto error = writer.Object();
    writer.AddInteger(error, "code", code);
    writer.AddString(error, "message", message);
    writer.Add(root, "error", error);
    return {false, writer.Serialize(root)};
}

ControlResponse ControlResult(std::string result_json) {
    return {true, "{\"result\":" + std::move(result_json) + "}"};
}

ControlResponse ControlEditorChannel::Call(const std::string_view method, const core::JsonValue params,
                                           const std::chrono::milliseconds timeout) {
    auto request = std::make_shared<Pending>();
    request->method = std::string(method);
    if (params.IsValid()) {
        core::JsonWriter writer;
        request->params = writer.Serialize(writer.Copy(params));
    }
    std::unique_lock lock(mutex_);
    if (closed_) return ControlError(-32004, "game session is closing");
    if (pending_.size() >= kMaximumPendingRequests) return ControlError(-32002, "key editor queue is full; retry");
    pending_.push_back(request);
    if (!answered_.wait_for(lock, timeout, [&] { return request->done || closed_; })) {
        // Withdraw it so a late main loop never applies a request the client saw fail.
        std::erase(pending_, request);
        if (!request->done) return ControlError(-32002, "game session busy; retry");
    }
    if (!request->done) return ControlError(-32004, "game session is closing");
    return request->response;
}

void ControlEditorChannel::ServePending(const Handler& handler) {
    std::deque<std::shared_ptr<Pending>> batch;
    {
        std::scoped_lock lock(mutex_);
        batch.swap(pending_);
    }
    if (batch.empty()) return;
    for (auto& request : batch) {
        ControlResponse response;
        try {
            core::JsonDocument document;
            core::JsonValue params;
            if (!request->params.empty()) {
                core::JsonParseError error;
                auto parsed = core::JsonDocument::ParseStrict(request->params, error);
                if (!parsed) throw std::invalid_argument("params are not valid JSON");
                document = std::move(*parsed);
                params = document.Root();
            }
            response = handler(request->method, params);
        } catch (const std::invalid_argument& error) {
            response = ControlError(-32602, error.what());
        } catch (const std::exception& error) {
            response = ControlError(-32603, error.what());
        }
        std::scoped_lock lock(mutex_);
        request->response = std::move(response);
        request->done = true;
    }
    answered_.notify_all();
}

void ControlEditorChannel::Close() {
    {
        std::scoped_lock lock(mutex_);
        closed_ = true;
        pending_.clear();
    }
    answered_.notify_all();
}

}  // namespace ogplay::agent
