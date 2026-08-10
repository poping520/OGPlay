#include "ogplay/agent/control_service.h"

#include "ogplay/agent/json_rpc.h"
#include "ogplay/core/json.h"

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace ogplay::agent {
namespace {

using JsonValue = core::JsonWriter::Value;

template <typename BuildResult>
ControlResponse Success(BuildResult&& build_result) {
    core::JsonWriter writer;
    const auto root = writer.Object();
    writer.Add(root, "result", build_result(writer));
    return {true, writer.Serialize(root)};
}

ControlResponse Error(const int code, const std::string_view reason,
                      const std::string_view message) {
    core::JsonWriter writer;
    const auto root = writer.Object();
    const auto error = writer.Object();
    writer.AddInteger(error, "code", code);
    writer.AddString(error, "message", message);
    const auto data = writer.Object();
    writer.AddString(data, "reason", reason);
    writer.Add(error, "data", data);
    writer.Add(root, "error", error);
    return {false, writer.Serialize(root)};
}

JsonValue StateValue(core::JsonWriter& writer, const session::SessionState& state) {
    const auto value = writer.Object();
    writer.AddString(value, "phase", session::ToString(state.phase));
    writer.AddString(value, "session_id", state.session_id);
    writer.AddUnsignedInteger(value, "frame", state.frame);
    writer.AddUnsignedInteger(value, "guest_threads", state.guest_threads);
    writer.AddUnsignedInteger(value, "wall_ms", state.wall_ms);
    writer.AddUnsignedInteger(value, "guest_ticks", state.guest_ticks);
    return value;
}

JsonValue GpuStatsValue(core::JsonWriter& writer, const core::GpuStats& stats) {
    const auto value = writer.Object();
    writer.AddUnsignedInteger(value, "draws", stats.draws);
    writer.AddUnsignedInteger(value, "clears", stats.clears);
    writer.AddUnsignedInteger(value, "shader_compiles", stats.shader_compiles);
    writer.AddUnsignedInteger(value, "program_links", stats.program_links);
    writer.AddUnsignedInteger(value, "gl_errors", stats.gl_errors);
    const auto targets = writer.Array();
    for (const auto& target : stats.draw_targets) {
        const auto item = writer.Object();
        writer.AddUnsignedInteger(item, "fbo", target.fbo);
        writer.AddUnsignedInteger(item, "draws", target.draws);
        writer.AddString(item, "attachment", target.attachment);
        writer.Append(targets, item);
    }
    writer.Add(value, "draw_targets", targets);
    return value;
}

JsonValue GpuRenderTargetsValue(core::JsonWriter& writer,
                                const std::vector<core::GpuRenderTarget>& targets) {
    const auto result = writer.Array();
    for (const auto& target : targets) {
        const auto item = writer.Object();
        writer.AddUnsignedInteger(item, "fbo", target.fbo);
        const auto size = writer.Object();
        writer.AddUnsignedInteger(size, "width", target.width);
        writer.AddUnsignedInteger(size, "height", target.height);
        writer.Add(item, "size", size);
        writer.AddString(item, "format", target.format);
        const auto attachments = writer.Array();
        for (const auto& attachment : target.attachments) {
            writer.Append(attachments, writer.String(attachment));
        }
        writer.Add(item, "attachments", attachments);
        writer.AddBool(item, "created_by_guest", target.created_by_guest);
        writer.Append(result, item);
    }
    return result;
}

JsonValue GpuCapabilitiesValue(core::JsonWriter& writer,
                               const core::GpuCapabilities& capabilities) {
    const auto result = writer.Object();
    const auto extensions = writer.Array();
    for (const auto& extension : capabilities.reported_extensions) {
        writer.Append(extensions, writer.String(extension));
    }
    writer.Add(result, "reported_extensions", extensions);
    const auto limits = writer.Object();
    for (const auto& [name, value] : capabilities.reported_limits) {
        writer.AddInteger(limits, name, value);
    }
    writer.Add(result, "reported_limits", limits);
    writer.AddString(result, "host_backend", capabilities.host_backend);
    return result;
}

JsonValue GpuTraceValue(core::JsonWriter& writer,
                        const std::vector<core::GpuTraceEntry>& entries) {
    const auto result = writer.Array();
    for (const auto& entry : entries) {
        const auto item = writer.Object();
        writer.AddString(item, "call", entry.call);
        const auto arguments = writer.Object();
        for (const auto& [name, value] : entry.arguments) {
            writer.AddString(arguments, name, value);
        }
        writer.Add(item, "args", arguments);
        if (entry.error.has_value()) writer.AddUnsignedInteger(item, "error", *entry.error);
        else writer.AddNull(item, "error");
        writer.Append(result, item);
    }
    return result;
}

std::optional<std::uint64_t> UnsignedParam(const core::JsonValue params,
                                           const std::string_view name) {
    const auto member = params.Member(name);
    if (!member.has_value()) return std::nullopt;
    const auto value = member->UnsignedInteger();
    if (!value.has_value()) {
        throw std::invalid_argument(std::string(name) + " must be an unsigned integer");
    }
    return value;
}

std::optional<std::string> StringParam(const core::JsonValue params,
                                       const std::string_view name) {
    const auto member = params.Member(name);
    if (!member.has_value()) return std::nullopt;
    const auto value = member->String();
    if (!value.has_value()) {
        throw std::invalid_argument(std::string(name) + " must be a string");
    }
    return std::string(*value);
}

void AddRpcId(core::JsonWriter& writer, const JsonValue envelope,
              const std::optional<core::JsonValue> id) {
    if (id.has_value()) writer.Add(envelope, "id", writer.Copy(*id));
    else writer.AddNull(envelope, "id");
}

std::string RpcError(const std::optional<core::JsonValue> id, const int code,
                     const std::string_view message) {
    core::JsonWriter writer;
    const auto root = writer.Object();
    writer.AddString(root, "jsonrpc", "2.0");
    AddRpcId(writer, root, id);
    const auto error = writer.Object();
    writer.AddInteger(error, "code", code);
    writer.AddString(error, "message", message);
    writer.Add(root, "error", error);
    return writer.Serialize(root);
}

}  // namespace

ControlService::ControlService(core::CapabilityLedger& ledger, core::Logger& logger,
                               session::Session& session,
                               const core::GpuStateProvider* const gpu)
    : ledger_(ledger), logger_(logger), session_(session), gpu_(gpu) {}

ControlResponse ControlService::Request(const std::string_view method,
                                        const ControlParams& params) {
    try {
        if (method == "system.ping") {
            return Success([](core::JsonWriter& writer) {
                const auto result = writer.Object();
                writer.AddString(result, "service", "ogplay-agent");
                writer.AddString(result, "status", "ok");
                return result;
            });
        }
        if (method == "session.open") {
            return Success([&](core::JsonWriter& writer) {
                return StateValue(writer, session_.OpenEmpty());
            });
        }
        if (method == "session.close") {
            return Success([&](core::JsonWriter& writer) {
                return StateValue(writer, session_.Close());
            });
        }
        if (method == "session.state") {
            return Success([&](core::JsonWriter& writer) {
                return StateValue(writer, session_.State());
            });
        }
        if (method == "run.step") {
            return Success([&](core::JsonWriter& writer) {
                return StateValue(writer, session_.Step(params.frames));
            });
        }
        if (method == "run.until") {
            const auto until = session_.UntilFrame(params.target_frame, params.max_frames);
            return Success([&](core::JsonWriter& writer) {
                const auto result = writer.Object();
                writer.AddBool(result, "reached", until.reached);
                writer.Add(result, "state", StateValue(writer, until.state));
                return result;
            });
        }
        if (method == "run.pause") {
            return Success([&](core::JsonWriter& writer) {
                return StateValue(writer, session_.Pause());
            });
        }
        if (method == "run.resume") {
            return Success([&](core::JsonWriter& writer) {
                return StateValue(writer, session_.Resume());
            });
        }
        if (method == "sym.resolve") {
            if (!params.address) {
                return Error(-32602, "invalid_params", "sym.resolve requires address");
            }
            const auto symbol = logger_.ResolveGuestAddress(*params.address);
            if (!symbol) {
                return Error(-32002, "symbol_not_found",
                             "guest address is not mapped to a symbol");
            }
            return Success([&](core::JsonWriter& writer) {
                const auto result = writer.Object();
                writer.AddUnsignedInteger(result, "address", *params.address);
                writer.AddString(result, "module", symbol->module);
                writer.AddString(result, "symbol", symbol->symbol);
                writer.AddUnsignedInteger(result, "offset", symbol->offset);
                writer.AddString(result, "source_hint", symbol->source_hint);
                return result;
            });
        }
        if (method == "gpu.stats") {
            if (gpu_ == nullptr) {
                return Error(-32004, "gpu_unavailable", "GPU state provider is not attached");
            }
            return Success([&](core::JsonWriter& writer) {
                return GpuStatsValue(writer, gpu_->Stats());
            });
        }
        if (method == "gpu.render_targets") {
            if (gpu_ == nullptr) {
                return Error(-32004, "gpu_unavailable", "GPU state provider is not attached");
            }
            return Success([&](core::JsonWriter& writer) {
                return GpuRenderTargetsValue(writer, gpu_->RenderTargets());
            });
        }
        if (method == "gpu.capabilities") {
            if (gpu_ == nullptr) {
                return Error(-32004, "gpu_unavailable", "GPU state provider is not attached");
            }
            return Success([&](core::JsonWriter& writer) {
                return GpuCapabilitiesValue(writer, gpu_->Capabilities());
            });
        }
        if (method == "gpu.trace") {
            if (gpu_ == nullptr) {
                return Error(-32004, "gpu_unavailable", "GPU state provider is not attached");
            }
            if (params.limit == 0 || params.limit > 1000) {
                return Error(-32602, "invalid_params", "gpu.trace limit must be in 1..1000");
            }
            const auto entries = gpu_->Trace(params.filter.value_or(""),
                                             static_cast<std::size_t>(params.limit));
            if (entries.size() > params.limit) {
                throw std::logic_error("GPU state provider exceeded the trace limit");
            }
            return Success([&](core::JsonWriter& writer) {
                return GpuTraceValue(writer, entries);
            });
        }
        if (method == "hle.capabilities") {
            return Success([&](core::JsonWriter& writer) {
                const auto result = writer.Array();
                for (const auto& capability : ledger_.All()) {
                    const auto item = writer.Object();
                    writer.AddString(item, "id", capability.id);
                    writer.AddString(item, "status", core::ToString(capability.status));
                    writer.AddString(item, "test", capability.test);
                    writer.Append(result, item);
                }
                return result;
            });
        }
        if (method == "hle.unimplemented") {
            return Success([&](core::JsonWriter& writer) {
                const auto result = writer.Array();
                for (const auto& hit : ledger_.Unimplemented()) {
                    const auto item = writer.Object();
                    writer.AddString(item, "symbol", hit.id);
                    writer.AddUnsignedInteger(item, "count", hit.count);
                    writer.AddUnsignedInteger(item, "first_lr", hit.first_lr);
                    writer.AddUnsignedInteger(item, "last_lr", hit.last_lr);
                    writer.Append(result, item);
                }
                return result;
            });
        }
        if (method == "hle.null_calls") {
            return Success([&](core::JsonWriter& writer) {
                const auto result = writer.Array();
                for (const auto& hit : ledger_.NullCalls()) {
                    const auto item = writer.Object();
                    writer.AddUnsignedInteger(item, "lr", hit.link_register);
                    writer.AddString(item, "symbol", hit.symbol);
                    writer.AddUnsignedInteger(item, "count", hit.count);
                    writer.Append(result, item);
                }
                return result;
            });
        }
        if (method == "log.tail") {
            return Success([&](core::JsonWriter& writer) {
                const auto result = writer.Array();
                for (const auto& record : logger_.Snapshot(std::nullopt, {}, 100)) {
                    core::JsonParseError error;
                    auto document = core::JsonDocument::ParseStrict(
                        logger_.RenderJson(record), error);
                    if (!document.has_value()) {
                        throw std::logic_error("logger produced invalid JSON: " + error.message);
                    }
                    writer.Append(result, writer.Copy(document->Root()));
                }
                return result;
            });
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
    core::JsonParseError parse_error;
    auto document = core::JsonDocument::ParseStrict(request, parse_error);
    if (!document.has_value()) {
        const int code = parse_error.kind == core::JsonParseErrorKind::syntax ? -32700 : -32600;
        return RpcError(std::nullopt, code, parse_error.message);
    }
    const auto root = document->Root();
    if (!root.IsObject()) {
        return RpcError(std::nullopt, -32600, "JSON-RPC request must be an object");
    }
    auto id = root.Member("id");
    if (id.has_value() && !id->IsNull() && !id->IsString() && !id->IsInteger()) {
        return RpcError(std::nullopt, -32600,
                        "JSON-RPC id must be string, integer or null");
    }
    const auto version = root.Member("jsonrpc");
    if (!version.has_value() || version->String() != std::optional<std::string_view>{"2.0"}) {
        return RpcError(id, -32600, "invalid JSON-RPC version");
    }
    const auto method_member = root.Member("method");
    const auto method = method_member ? method_member->String() : std::nullopt;
    if (!method.has_value() || method->empty()) {
        return RpcError(id, -32600, "method is required");
    }
    const auto params_value = root.Member("params");
    if (params_value.has_value() && !params_value->IsObject()) {
        return RpcError(id, -32602, "params must be an object");
    }
    const core::JsonValue params = params_value.value_or(core::JsonValue{});

    try {
        ControlParams control;
        if (params.IsValid()) {
            if (const auto frames = UnsignedParam(params, "frames")) control.frames = *frames;
            if (const auto target = UnsignedParam(params, "target_frame")) {
                control.target_frame = *target;
            }
            if (const auto maximum = UnsignedParam(params, "max_frames")) {
                control.max_frames = *maximum;
            }
            control.address = UnsignedParam(params, "address");
            control.filter = StringParam(params, "filter");
            if (const auto limit = UnsignedParam(params, "limit")) control.limit = *limit;
        }
        if (*method == "run.until" && control.max_frames == 0) {
            return RpcError(id, -32602, "run.until requires positive max_frames");
        }

        const auto response = service_.Request(*method, control);
        core::JsonParseError response_error;
        auto response_document = core::JsonDocument::ParseStrict(response.json, response_error);
        if (!response_document.has_value() || !response_document->Root().IsObject()) {
            return RpcError(id, -32603, "control service produced invalid JSON");
        }
        const auto response_root = response_document->Root();
        const auto payload = response.ok ? response_root.Member("result")
                                         : response_root.Member("error");
        if (!payload.has_value()) {
            return RpcError(id, -32603, "control service response has no payload");
        }

        core::JsonWriter writer;
        const auto envelope = writer.Object();
        writer.AddString(envelope, "jsonrpc", "2.0");
        AddRpcId(writer, envelope, id);
        writer.Add(envelope, response.ok ? "result" : "error", writer.Copy(*payload));
        return writer.Serialize(envelope);
    } catch (const std::invalid_argument& error) {
        return RpcError(id, -32602, error.what());
    } catch (const std::exception& error) {
        return RpcError(id, -32603, error.what());
    }
}

}  // namespace ogplay::agent
