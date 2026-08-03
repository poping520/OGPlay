#include "ogplay/agent/control_service.h"

#include <cctype>
#include <charconv>
#include <optional>
#include <sstream>
#include <stdexcept>

#include "ogplay/agent/json_rpc.h"

namespace ogplay::agent {
namespace {

std::string JsonString(const std::string_view input) {
    std::string result{"\""};
    for (const char raw_character : input) {
        const auto character = static_cast<unsigned char>(raw_character);
        switch (character) {
        case '\\': result += "\\\\"; break;
        case '"': result += "\\\""; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result += static_cast<char>(character); break;
        }
    }
    result += '"';
    return result;
}

ControlResponse Error(const int code,
                      const std::string_view reason,
                      const std::string_view message) {
    std::ostringstream json;
    json << "{\"error\":{\"code\":" << code
         << ",\"message\":" << JsonString(message)
         << ",\"data\":{\"reason\":" << JsonString(reason) << "}}}";
    return {false, json.str()};
}

std::string StateJson(const session::SessionState& state) {
    std::ostringstream json;
    json << "{\"phase\":" << JsonString(session::ToString(state.phase))
         << ",\"session_id\":" << JsonString(state.session_id)
         << ",\"frame\":" << state.frame
         << ",\"guest_threads\":" << state.guest_threads
         << ",\"wall_ms\":" << state.wall_ms
         << ",\"guest_ticks\":" << state.guest_ticks << '}';
    return json.str();
}

std::optional<std::size_t> MemberValueStart(const std::string_view json,
                                            const std::string_view key) {
    const auto key_position = json.find('"' + std::string(key) + '"');
    if (key_position == std::string_view::npos) return std::nullopt;
    const auto colon = json.find(':', key_position + key.size() + 2U);
    if (colon == std::string_view::npos) throw std::invalid_argument("JSON member has no value");
    auto position = colon + 1U;
    while (position < json.size() && std::isspace(static_cast<unsigned char>(json[position]))) {
        ++position;
    }
    if (position == json.size()) throw std::invalid_argument("JSON member has an empty value");
    return position;
}

std::optional<std::string> StringMember(const std::string_view json,
                                        const std::string_view key) {
    const auto start = MemberValueStart(json, key);
    if (!start) return std::nullopt;
    if (json[*start] != '"') throw std::invalid_argument("JSON member must be a string");
    std::string value;
    bool escaped = false;
    for (auto position = *start + 1U; position < json.size(); ++position) {
        const auto character = json[position];
        if (escaped) {
            switch (character) {
            case '"': value += '"'; break;
            case '\\': value += '\\'; break;
            case 'n': value += '\n'; break;
            case 'r': value += '\r'; break;
            case 't': value += '\t'; break;
            default: throw std::invalid_argument("unsupported JSON escape");
            }
            escaped = false;
        } else if (character == '\\') {
            escaped = true;
        } else if (character == '"') {
            return value;
        } else {
            value += character;
        }
    }
    throw std::invalid_argument("unterminated JSON string");
}

std::optional<std::uint64_t> UnsignedMember(const std::string_view json,
                                            const std::string_view key) {
    const auto start = MemberValueStart(json, key);
    if (!start) return std::nullopt;
    std::uint64_t value = 0;
    const auto* first = json.data() + *start;
    const auto* last = json.data() + json.size();
    const auto result = std::from_chars(first, last, value);
    if (result.ec != std::errc{}) throw std::invalid_argument("JSON member must be unsigned integer");
    return value;
}

std::string IdMember(const std::string_view json) {
    const auto start = MemberValueStart(json, "id");
    if (!start) return "null";
    if (json[*start] == '"') {
        const auto value = StringMember(json, "id");
        return JsonString(*value);
    }
    auto end = *start;
    while (end < json.size() && (std::isdigit(static_cast<unsigned char>(json[end])) ||
                                 json[end] == '-')) {
        ++end;
    }
    if (end == *start) {
        if (json.substr(*start, 4) == "null") return "null";
        throw std::invalid_argument("JSON-RPC id must be string, integer or null");
    }
    return std::string(json.substr(*start, end - *start));
}

std::string RpcError(const std::string_view id,
                     const int code,
                     const std::string_view message) {
    std::ostringstream json;
    json << "{\"jsonrpc\":\"2.0\",\"id\":" << id
         << ",\"error\":{\"code\":" << code
         << ",\"message\":" << JsonString(message) << "}}";
    return json.str();
}

}  // namespace

ControlService::ControlService(core::CapabilityLedger& ledger,
                               core::Logger& logger,
                               session::Session& session)
    : ledger_(ledger), logger_(logger), session_(session) {}

ControlResponse ControlService::Request(const std::string_view method,
                                        const ControlParams& params) {
    try {
        if (method == "system.ping") {
            return {true, "{\"result\":{\"service\":\"ogplay-agent\",\"status\":\"ok\"}}"};
        }
        if (method == "session.open") {
            return {true, "{\"result\":" + StateJson(session_.OpenEmpty()) + "}"};
        }
        if (method == "session.close") {
            return {true, "{\"result\":" + StateJson(session_.Close()) + "}"};
        }
        if (method == "session.state") {
            return {true, "{\"result\":" + StateJson(session_.State()) + "}"};
        }
        if (method == "run.step") {
            return {true, "{\"result\":" + StateJson(session_.Step(params.frames)) + "}"};
        }
        if (method == "run.until") {
            const auto result = session_.UntilFrame(params.target_frame, params.max_frames);
            return {true, "{\"result\":{\"reached\":" + std::string(result.reached ? "true" : "false") +
                              ",\"state\":" + StateJson(result.state) + "}}"};
        }
        if (method == "run.pause") {
            return {true, "{\"result\":" + StateJson(session_.Pause()) + "}"};
        }
        if (method == "run.resume") {
            return {true, "{\"result\":" + StateJson(session_.Resume()) + "}"};
        }
        if (method == "sym.resolve") {
            if (!params.address) {
                return Error(-32602, "invalid_params", "sym.resolve requires address");
            }
            const auto symbol = logger_.ResolveGuestAddress(*params.address);
            if (!symbol) {
                return Error(-32002, "symbol_not_found", "guest address is not mapped to a symbol");
            }
            std::ostringstream json;
            json << "{\"result\":{\"address\":" << *params.address
                 << ",\"module\":" << JsonString(symbol->module)
                 << ",\"symbol\":" << JsonString(symbol->symbol)
                 << ",\"offset\":" << symbol->offset
                 << ",\"source_hint\":" << JsonString(symbol->source_hint) << "}}";
            return {true, json.str()};
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
        if (method == "hle.null_calls") {
            std::ostringstream json;
            json << "{\"result\":[";
            bool first = true;
            for (const auto& hit : ledger_.NullCalls()) {
                if (!first) json << ',';
                first = false;
                json << "{\"lr\":" << hit.link_register
                     << ",\"symbol\":" << JsonString(hit.symbol)
                     << ",\"count\":" << hit.count << '}';
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
                json << logger_.RenderJson(record);
            }
            json << "]}";
            return {true, json.str()};
        }
        return Error(-32601, "method_not_found", method);
    } catch (const std::invalid_argument& error) {
        return Error(-32602, "invalid_params", error.what());
    } catch (const std::logic_error& error) {
        return Error(-32001, "invalid_state", error.what());
    }
}

JsonRpcAdapter::JsonRpcAdapter(ControlService& service) : service_(service) {}

std::string JsonRpcAdapter::Handle(const std::string_view request) {
    std::string id = "null";
    try {
        const auto first = request.find_first_not_of(" \t\r\n");
        const auto last = request.find_last_not_of(" \t\r\n");
        if (first == std::string_view::npos || request[first] != '{' || request[last] != '}') {
            throw std::invalid_argument("JSON-RPC request must be an object");
        }
        id = IdMember(request);
        if (StringMember(request, "jsonrpc") != "2.0") {
            return RpcError(id, -32600, "invalid JSON-RPC version");
        }
        const auto method = StringMember(request, "method");
        if (!method || method->empty()) return RpcError(id, -32600, "method is required");

        ControlParams params;
        if (const auto frames = UnsignedMember(request, "frames")) params.frames = *frames;
        if (const auto target = UnsignedMember(request, "target_frame")) params.target_frame = *target;
        if (const auto maximum = UnsignedMember(request, "max_frames")) params.max_frames = *maximum;
        params.address = UnsignedMember(request, "address");
        if (*method == "run.until" && params.max_frames == 0) {
            return RpcError(id, -32602, "run.until requires positive max_frames");
        }

        const auto response = service_.Request(*method, params);
        return "{\"jsonrpc\":\"2.0\",\"id\":" + id + ',' + response.json.substr(1);
    } catch (const std::exception& error) {
        return RpcError(id, -32700, error.what());
    }
}

}  // namespace ogplay::agent
