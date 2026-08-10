#include "ogplay/agent/mcp_protocol.h"

#include "ogplay/core/json.h"

#include <array>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace ogplay::agent {
namespace {

constexpr std::uint64_t kMaximumFrameBytes = 64ULL * 1024ULL * 1024ULL;
constexpr std::string_view kProtocolVersion = "2025-11-25";

void ValidateFrame(const FrameSnapshot& frame) {
    if (frame.width == 0 || frame.height == 0) {
        throw std::invalid_argument("frame dimensions must be positive");
    }
    const auto pixels = static_cast<std::uint64_t>(frame.width) * frame.height;
    if (pixels > kMaximumFrameBytes / 4U || frame.rgba8.size() != pixels * 4U) {
        throw std::invalid_argument("frame RGBA8 layout does not match dimensions");
    }
}

void AddRpcId(core::JsonWriter& writer, const core::JsonWriter::Value envelope,
              const std::optional<core::JsonValue> id) {
    if (id.has_value()) writer.Add(envelope, "id", writer.Copy(*id));
    else writer.AddNull(envelope, "id");
}

std::string RpcError(const std::optional<core::JsonValue> id, const int code,
                     const std::string_view message) {
    core::JsonWriter writer;
    const auto envelope = writer.Object();
    writer.AddString(envelope, "jsonrpc", "2.0");
    AddRpcId(writer, envelope, id);
    const auto error = writer.Object();
    writer.AddInteger(error, "code", code);
    writer.AddString(error, "message", message);
    writer.Add(envelope, "error", error);
    return writer.Serialize(envelope);
}

template <typename BuildResult>
std::string RpcResult(const core::JsonValue id, BuildResult&& build_result) {
    core::JsonWriter writer;
    const auto envelope = writer.Object();
    writer.AddString(envelope, "jsonrpc", "2.0");
    writer.Add(envelope, "id", writer.Copy(id));
    writer.Add(envelope, "result", build_result(writer));
    return writer.Serialize(envelope);
}

std::uint32_t Crc32(const std::span<const std::uint8_t> bytes) {
    std::uint32_t crc = 0xffffffffU;
    for (const auto byte : bytes) {
        crc ^= byte;
        for (unsigned bit = 0; bit < 8U; ++bit) {
            crc = (crc >> 1U) ^ (0xedb88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

void AppendBigEndian(std::vector<std::uint8_t>& output, const std::uint32_t value) {
    output.push_back(static_cast<std::uint8_t>(value >> 24U));
    output.push_back(static_cast<std::uint8_t>(value >> 16U));
    output.push_back(static_cast<std::uint8_t>(value >> 8U));
    output.push_back(static_cast<std::uint8_t>(value));
}

void AppendChunk(std::vector<std::uint8_t>& output,
                 const std::array<std::uint8_t, 4>& type,
                 const std::span<const std::uint8_t> payload) {
    if (payload.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument("PNG chunk is too large");
    }
    AppendBigEndian(output, static_cast<std::uint32_t>(payload.size()));
    const auto crc_start = output.size();
    output.insert(output.end(), type.begin(), type.end());
    output.insert(output.end(), payload.begin(), payload.end());
    AppendBigEndian(output, Crc32(std::span(output).subspan(crc_start)));
}

std::vector<std::uint8_t> DeflateStored(const std::span<const std::uint8_t> input) {
    std::vector<std::uint8_t> output;
    output.reserve(input.size() + input.size() / 65535U * 5U + 16U);
    output.push_back(0x78U);
    output.push_back(0x01U);
    std::size_t offset = 0;
    do {
        const auto size = static_cast<std::uint16_t>(
            std::min<std::size_t>(65535U, input.size() - offset));
        const bool final = offset + size == input.size();
        output.push_back(final ? 0x01U : 0x00U);
        output.push_back(static_cast<std::uint8_t>(size));
        output.push_back(static_cast<std::uint8_t>(size >> 8U));
        const auto complement = static_cast<std::uint16_t>(~size);
        output.push_back(static_cast<std::uint8_t>(complement));
        output.push_back(static_cast<std::uint8_t>(complement >> 8U));
        output.insert(output.end(), input.begin() + static_cast<std::ptrdiff_t>(offset),
                      input.begin() + static_cast<std::ptrdiff_t>(offset + size));
        offset += size;
    } while (offset < input.size());

    std::uint32_t first = 1U;
    std::uint32_t second = 0U;
    for (const auto byte : input) {
        first = (first + byte) % 65521U;
        second = (second + first) % 65521U;
    }
    AppendBigEndian(output, (second << 16U) | first);
    return output;
}

std::vector<std::uint8_t> EncodePng(const FrameSnapshot& frame) {
    ValidateFrame(frame);
    const auto row_bytes = static_cast<std::size_t>(frame.width) * 4U;
    std::vector<std::uint8_t> filtered;
    filtered.reserve((row_bytes + 1U) * frame.height);
    for (std::uint32_t row = 0; row < frame.height; ++row) {
        filtered.push_back(0U);
        const auto offset = static_cast<std::size_t>(row) * row_bytes;
        filtered.insert(filtered.end(), frame.rgba8.begin() +
                                           static_cast<std::ptrdiff_t>(offset),
                        frame.rgba8.begin() +
                            static_cast<std::ptrdiff_t>(offset + row_bytes));
    }

    std::vector<std::uint8_t> png{0x89U, 0x50U, 0x4eU, 0x47U,
                                  0x0dU, 0x0aU, 0x1aU, 0x0aU};
    std::vector<std::uint8_t> header;
    AppendBigEndian(header, frame.width);
    AppendBigEndian(header, frame.height);
    header.insert(header.end(), {8U, 6U, 0U, 0U, 0U});
    AppendChunk(png, {'I', 'H', 'D', 'R'}, header);
    const auto compressed = DeflateStored(filtered);
    AppendChunk(png, {'I', 'D', 'A', 'T'}, compressed);
    AppendChunk(png, {'I', 'E', 'N', 'D'}, {});
    return png;
}

std::string Base64(const std::span<const std::uint8_t> input) {
    constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve((input.size() + 2U) / 3U * 4U);
    for (std::size_t offset = 0; offset < input.size(); offset += 3U) {
        const auto remaining = input.size() - offset;
        const std::uint32_t value = static_cast<std::uint32_t>(input[offset]) << 16U |
                                    (remaining > 1U
                                         ? static_cast<std::uint32_t>(input[offset + 1U]) << 8U
                                         : 0U) |
                                    (remaining > 2U ? input[offset + 2U] : 0U);
        output += alphabet[(value >> 18U) & 0x3fU];
        output += alphabet[(value >> 12U) & 0x3fU];
        output += remaining > 1U ? alphabet[(value >> 6U) & 0x3fU] : '=';
        output += remaining > 2U ? alphabet[value & 0x3fU] : '=';
    }
    return output;
}

core::JsonWriter::Value ToolsList(core::JsonWriter& writer) {
    const auto result = writer.Object();
    const auto tools = writer.Array();
    const auto tool = writer.Object();
    writer.AddString(tool, "name", "frame_capture");
    writer.AddString(tool, "title", "Capture latest OGPlay frame");
    writer.AddString(tool, "description",
                     "Returns the latest presented guest frame as PNG without advancing "
                     "guest execution or consuming input.");
    const auto input_schema = writer.Object();
    writer.AddString(input_schema, "type", "object");
    writer.Add(input_schema, "properties", writer.Object());
    writer.AddBool(input_schema, "additionalProperties", false);
    writer.Add(tool, "inputSchema", input_schema);

    const auto output_schema = writer.Object();
    writer.AddString(output_schema, "type", "object");
    const auto properties = writer.Object();
    for (const std::string_view name : {"sequence", "width", "height"}) {
        const auto property = writer.Object();
        writer.AddString(property, "type", "integer");
        writer.Add(properties, name, property);
    }
    writer.Add(output_schema, "properties", properties);
    const auto required = writer.Array();
    writer.Append(required, writer.String("sequence"));
    writer.Append(required, writer.String("width"));
    writer.Append(required, writer.String("height"));
    writer.Add(output_schema, "required", required);
    writer.Add(tool, "outputSchema", output_schema);

    const auto annotations = writer.Object();
    writer.AddBool(annotations, "readOnlyHint", true);
    writer.AddBool(annotations, "destructiveHint", false);
    writer.AddBool(annotations, "idempotentHint", true);
    writer.AddBool(annotations, "openWorldHint", false);
    writer.Add(tool, "annotations", annotations);
    writer.Append(tools, tool);
    writer.Add(result, "tools", tools);
    return result;
}

std::string FrameMetadataText(const FrameSnapshot& frame) {
    core::JsonWriter writer;
    const auto metadata = writer.Object();
    writer.AddUnsignedInteger(metadata, "sequence", frame.sequence);
    writer.AddUnsignedInteger(metadata, "width", frame.width);
    writer.AddUnsignedInteger(metadata, "height", frame.height);
    return writer.Serialize(metadata);
}

core::JsonWriter::Value ToolError(core::JsonWriter& writer, const std::string_view message) {
    const auto result = writer.Object();
    const auto content = writer.Array();
    const auto text = writer.Object();
    writer.AddString(text, "type", "text");
    writer.AddString(text, "text", message);
    writer.Append(content, text);
    writer.Add(result, "content", content);
    writer.AddBool(result, "isError", true);
    return result;
}

core::JsonWriter::Value CaptureResult(core::JsonWriter& writer, FrameSnapshotStore& frames) {
    const auto frame = frames.Latest();
    if (!frame) return ToolError(writer, "No presented guest frame is available.");

    const auto png = EncodePng(*frame);
    const auto result = writer.Object();
    const auto content = writer.Array();
    const auto image = writer.Object();
    writer.AddString(image, "type", "image");
    writer.AddString(image, "data", Base64(png));
    writer.AddString(image, "mimeType", "image/png");
    writer.Append(content, image);
    const auto text = writer.Object();
    writer.AddString(text, "type", "text");
    writer.AddString(text, "text", FrameMetadataText(*frame));
    writer.Append(content, text);
    writer.Add(result, "content", content);
    const auto structured = writer.Object();
    writer.AddUnsignedInteger(structured, "sequence", frame->sequence);
    writer.AddUnsignedInteger(structured, "width", frame->width);
    writer.AddUnsignedInteger(structured, "height", frame->height);
    writer.Add(result, "structuredContent", structured);
    writer.AddBool(result, "isError", false);
    return result;
}

}  // namespace

std::optional<FrameSnapshot> FrameSnapshotStore::Publish(FrameSnapshot frame) {
    ValidateFrame(frame);
    std::scoped_lock lock(mutex_);
    auto previous = std::move(latest_);
    latest_ = std::move(frame);
    return previous;
}

std::optional<FrameSnapshot> FrameSnapshotStore::Latest() const {
    std::scoped_lock lock(mutex_);
    return latest_;
}

std::optional<FrameSnapshot> FrameSnapshotStore::Take() {
    std::scoped_lock lock(mutex_);
    auto result = std::move(latest_);
    latest_.reset();
    return result;
}

McpProtocolAdapter::McpProtocolAdapter(FrameSnapshotStore& frames,
                                       std::string server_version)
    : frames_(frames), server_version_(std::move(server_version)) {
    if (server_version_.empty()) throw std::invalid_argument("MCP server version is empty");
}

std::optional<std::string> McpProtocolAdapter::Handle(
    const std::string_view request) const {
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
    const bool notification = !id.has_value();
    if (id.has_value() && !id->IsString() && !id->IsInteger()) {
        return RpcError(std::nullopt, -32600,
                        "JSON-RPC id must be a string or integer");
    }
    const auto version = root.Member("jsonrpc");
    if (!version.has_value() || version->String() != std::optional<std::string_view>{"2.0"}) {
        return RpcError(id, -32600, "invalid JSON-RPC version");
    }
    const auto method_value = root.Member("method");
    const auto method = method_value ? method_value->String() : std::nullopt;
    if (!method.has_value() || method->empty()) {
        return RpcError(id, -32600, "method is required");
    }

    try {
        if (*method == "notifications/initialized" || *method == "notifications/cancelled") {
            if (notification) return std::nullopt;
            return RpcError(id, -32600, "notification method must not include an id");
        }
        if (notification) return std::nullopt;

        const auto params = root.Member("params");
        if (params.has_value() && !params->IsObject()) {
            return RpcError(id, -32602, "params must be an object");
        }
        if (*method == "initialize") {
            if (!params.has_value()) {
                return RpcError(id, -32602, "initialize params are required");
            }
            const auto requested_value = params->Member("protocolVersion");
            const auto requested = requested_value ? requested_value->String() : std::nullopt;
            const auto capabilities = params->Member("capabilities");
            const auto client_info = params->Member("clientInfo");
            const auto client_name = client_info ? client_info->Member("name") : std::nullopt;
            const auto client_version = client_info ? client_info->Member("version") : std::nullopt;
            if (!requested.has_value() || !capabilities.has_value() ||
                !capabilities->IsObject() || !client_info.has_value() ||
                !client_info->IsObject() || !client_name.has_value() ||
                !client_name->IsString() || !client_version.has_value() ||
                !client_version->IsString()) {
                return RpcError(id, -32602,
                                "initialize requires protocolVersion, capabilities and clientInfo");
            }
            const auto negotiated = *requested == "2025-11-25" ||
                                            *requested == "2025-06-18" ||
                                            *requested == "2025-03-26"
                                        ? *requested
                                        : kProtocolVersion;
            return RpcResult(*id, [&](core::JsonWriter& writer) {
                const auto result = writer.Object();
                writer.AddString(result, "protocolVersion", negotiated);
                const auto server_capabilities = writer.Object();
                const auto tools = writer.Object();
                writer.AddBool(tools, "listChanged", false);
                writer.Add(server_capabilities, "tools", tools);
                writer.Add(result, "capabilities", server_capabilities);
                const auto server_info = writer.Object();
                writer.AddString(server_info, "name", "ogplay");
                writer.AddString(server_info, "version", server_version_);
                writer.Add(result, "serverInfo", server_info);
                writer.AddString(
                    result, "instructions",
                    "OGPlay exposes read-only runtime inspection. frame_capture returns the "
                    "latest presented guest frame and never advances execution or consumes input.");
                return result;
            });
        }
        if (*method == "ping") {
            return RpcResult(*id, [](core::JsonWriter& writer) { return writer.Object(); });
        }
        if (*method == "tools/list") {
            if (params.has_value()) {
                const auto cursor = params->Member("cursor");
                if (cursor.has_value()) {
                    if (!cursor->IsString()) {
                        return RpcError(id, -32602, "tools/list cursor must be a string");
                    }
                    return RpcError(id, -32602, "tools/list cursor is not recognized");
                }
            }
            return RpcResult(*id, [](core::JsonWriter& writer) { return ToolsList(writer); });
        }
        if (*method == "tools/call") {
            if (!params.has_value()) {
                return RpcError(id, -32602, "tools/call params are required");
            }
            const auto name_value = params->Member("name");
            const auto name = name_value ? name_value->String() : std::nullopt;
            if (!name.has_value() || name->empty()) {
                return RpcError(id, -32602, "tool name is required");
            }
            if (*name != "frame_capture") {
                return RpcError(id, -32602, "unknown MCP tool: " + std::string(*name));
            }
            const auto arguments = params->Member("arguments");
            if (arguments.has_value() && !arguments->IsObject()) {
                return RpcError(id, -32602, "tool arguments must be an object");
            }
            if (arguments.has_value() && arguments->Size() != 0U) {
                return RpcResult(*id, [](core::JsonWriter& writer) {
                    return ToolError(writer, "frame_capture does not accept arguments");
                });
            }
            return RpcResult(*id, [&](core::JsonWriter& writer) {
                return CaptureResult(writer, frames_);
            });
        }
        return RpcError(id, -32601, "method not found: " + std::string(*method));
    } catch (const std::exception& error) {
        return RpcError(id, -32603, error.what());
    }
}

}  // namespace ogplay::agent
