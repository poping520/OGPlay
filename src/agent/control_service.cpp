#include "ogplay/agent/control_service.h"

#include <sstream>

namespace ogplay::agent {
namespace {

std::string JsonString(const std::string_view input) {
    std::string result{"\""};
    for (const char ch : input) {
        switch (ch) {
        case '\\': result += "\\\\"; break;
        case '"': result += "\\\""; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result += ch; break;
        }
    }
    result += '"';
    return result;
}

ControlResponse Error(const std::string_view code, const std::string_view message) {
    return {false, "{\"error\":{\"code\":" + JsonString(code) +
                       ",\"message\":" + JsonString(message) + "}}"};
}

}  // namespace

ControlService::ControlService(core::CapabilityLedger& ledger, core::Logger& logger)
    : ledger_(ledger), logger_(logger) {}

ControlResponse ControlService::Request(const std::string_view method) const {
    if (method == "system.ping") {
        return {true, "{\"result\":{\"service\":\"ogplay-agent\",\"status\":\"ok\"}}"};
    }
    if (method == "session.state") {
        return {true, "{\"result\":{\"phase\":\"idle\",\"frame\":0,\"guest_threads\":0}}"};
    }
    if (method == "run.step") {
        return Error("no_active_session", "run.step requires an open session");
    }
    if (method == "sym.resolve") {
        return Error("symbol_provider_unavailable", "no guest image is loaded");
    }
    if (method == "hle.capabilities") {
        std::ostringstream json;
        json << "{\"result\":[";
        bool first = true;
        for (const auto& capability : ledger_.All()) {
            if (!first) json << ',';
            first = false;
            json << "{\"id\":" << JsonString(capability.id)
                 << ",\"status\":" << JsonString(core::ToString(capability.status))
                 << ",\"test\":" << JsonString(capability.test) << '}';
        }
        json << "]}";
        return {true, json.str()};
    }
    if (method == "hle.unimplemented") {
        std::ostringstream json;
        json << "{\"result\":[";
        bool first = true;
        for (const auto& hit : ledger_.Unimplemented()) {
            if (!first) json << ',';
            first = false;
            json << "{\"symbol\":" << JsonString(hit.id)
                 << ",\"count\":" << hit.count
                 << ",\"first_lr\":" << hit.first_lr
                 << ",\"last_lr\":" << hit.last_lr << '}';
        }
        json << "]}";
        return {true, json.str()};
    }
    if (method == "log.tail") {
        std::ostringstream json;
        json << "{\"result\":[";
        bool first = true;
        for (const auto& record : logger_.Snapshot(std::nullopt, {}, 100)) {
            if (!first) json << ',';
            first = false;
            json << "{\"frame\":" << record.frame
                 << ",\"level\":" << JsonString(core::ToString(record.level))
                 << ",\"category\":" << JsonString(record.category)
                 << ",\"message\":" << JsonString(record.message) << '}';
        }
        json << "]}";
        return {true, json.str()};
    }
    return Error("method_not_found", method);
}

}  // namespace ogplay::agent

