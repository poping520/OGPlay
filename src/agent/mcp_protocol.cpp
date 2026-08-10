#include "ogplay/agent/mcp_protocol.h"

#include "ogplay/agent/coordinate_overlay.h"
#include "ogplay/core/json.h"

#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <stb_image_write.h>

namespace ogplay::agent {
namespace {

constexpr std::uint64_t kMaximumFrameBytes = 64ULL * 1024ULL * 1024ULL;
constexpr std::string_view kProtocolVersion = "2025-11-25";
constexpr int kJpegQuality = 85;

enum class CaptureFormat { kJpeg, kPng };
enum class CaptureOverlay { kNone, kCoordinates };

struct EncodedFrame final {
    std::vector<std::uint8_t> bytes;
    std::string_view format;
    std::string_view mime_type;
};

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

struct ImageWriteContext final {
    std::vector<std::uint8_t> bytes;
    bool failed{};
};

void AppendEncodedBytes(void* context, void* data, const int size) noexcept {
    auto& output = *static_cast<ImageWriteContext*>(context);
    if (output.failed || data == nullptr || size <= 0) return;
    try {
        const auto* begin = static_cast<const std::uint8_t*>(data);
        output.bytes.insert(output.bytes.end(), begin, begin + size);
    } catch (...) {
        output.failed = true;
    }
}

EncodedFrame EncodeFrame(const FrameSnapshot& frame, const CaptureFormat format) {
    ValidateFrame(frame);
    ImageWriteContext output;
    const auto width = static_cast<int>(frame.width);
    const auto height = static_cast<int>(frame.height);
    int encoded{};
    if (format == CaptureFormat::kPng) {
        const auto stride = static_cast<int>(frame.width * 4U);
        encoded = stbi_write_png_to_func(AppendEncodedBytes, &output, width, height, 4,
                                         frame.rgba8.data(), stride);
    } else {
        encoded = stbi_write_jpg_to_func(AppendEncodedBytes, &output, width, height, 4,
                                         frame.rgba8.data(), kJpegQuality);
    }
    if (encoded == 0 || output.failed || output.bytes.empty()) {
        throw std::runtime_error("frame image encoding failed");
    }
    if (format == CaptureFormat::kPng) {
        return {std::move(output.bytes), "png", "image/png"};
    }
    return {std::move(output.bytes), "jpeg", "image/jpeg"};
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
    const auto capture = writer.Object();
    writer.AddString(capture, "name", "frame_capture");
    writer.AddString(capture, "title", "Capture latest OGPlay frame");
    writer.AddString(capture, "description",
                     "Returns the latest presented guest frame as compressed JPEG or PNG "
                     "without advancing guest execution or consuming input. Defaults to JPEG "
                     "quality 85 when format is omitted. Set overlay to coordinates for guest-"
                     "pixel guides on the returned copy.");
    const auto capture_input = writer.Object();
    writer.AddString(capture_input, "type", "object");
    const auto capture_input_properties = writer.Object();
    const auto capture_format = writer.Object();
    writer.AddString(capture_format, "type", "string");
    const auto capture_format_enum = writer.Array();
    writer.Append(capture_format_enum, writer.String("jpeg"));
    writer.Append(capture_format_enum, writer.String("png"));
    writer.Add(capture_format, "enum", capture_format_enum);
    writer.AddString(capture_format, "default", "jpeg");
    writer.Add(capture_input_properties, "format", capture_format);
    const auto capture_overlay = writer.Object();
    writer.AddString(capture_overlay, "type", "string");
    const auto capture_overlay_enum = writer.Array();
    writer.Append(capture_overlay_enum, writer.String("coordinates"));
    writer.Add(capture_overlay, "enum", capture_overlay_enum);
    writer.Add(capture_input_properties, "overlay", capture_overlay);
    writer.Add(capture_input, "properties", capture_input_properties);
    writer.AddBool(capture_input, "additionalProperties", false);
    writer.Add(capture, "inputSchema", capture_input);

    const auto capture_output = writer.Object();
    writer.AddString(capture_output, "type", "object");
    const auto capture_properties = writer.Object();
    for (const std::string_view name : {"sequence", "width", "height"}) {
        const auto property = writer.Object();
        writer.AddString(property, "type", "integer");
        writer.Add(capture_properties, name, property);
    }
    const auto output_format = writer.Object();
    writer.AddString(output_format, "type", "string");
    const auto output_format_enum = writer.Array();
    writer.Append(output_format_enum, writer.String("jpeg"));
    writer.Append(output_format_enum, writer.String("png"));
    writer.Add(output_format, "enum", output_format_enum);
    writer.Add(capture_properties, "format", output_format);
    const auto output_overlay = writer.Object();
    writer.AddString(output_overlay, "type", "string");
    const auto output_overlay_enum = writer.Array();
    writer.Append(output_overlay_enum, writer.String("none"));
    writer.Append(output_overlay_enum, writer.String("coordinates"));
    writer.Add(output_overlay, "enum", output_overlay_enum);
    writer.Add(capture_properties, "overlay", output_overlay);
    writer.Add(capture_output, "properties", capture_properties);
    const auto capture_required = writer.Array();
    writer.Append(capture_required, writer.String("sequence"));
    writer.Append(capture_required, writer.String("width"));
    writer.Append(capture_required, writer.String("height"));
    writer.Append(capture_required, writer.String("format"));
    writer.Append(capture_required, writer.String("overlay"));
    writer.Add(capture_output, "required", capture_required);
    writer.AddBool(capture_output, "additionalProperties", false);
    writer.Add(capture, "outputSchema", capture_output);

    const auto capture_annotations = writer.Object();
    writer.AddBool(capture_annotations, "readOnlyHint", true);
    writer.AddBool(capture_annotations, "destructiveHint", false);
    writer.AddBool(capture_annotations, "idempotentHint", true);
    writer.AddBool(capture_annotations, "openWorldHint", false);
    writer.Add(capture, "annotations", capture_annotations);
    writer.Append(tools, capture);

    const auto click = writer.Object();
    writer.AddString(click, "name", "click");
    writer.AddString(click, "title", "Click OGPlay guest frame");
    writer.AddString(
        click, "description",
        "Queues one primary-pointer click at integer pixel coordinates in the latest "
        "presented guest frame. Down and up are dispatched on consecutive guest steps.");
    const auto click_input = writer.Object();
    writer.AddString(click_input, "type", "object");
    const auto click_input_properties = writer.Object();
    for (const std::string_view name : {"x", "y"}) {
        const auto property = writer.Object();
        writer.AddString(property, "type", "integer");
        writer.AddInteger(property, "minimum", 0);
        writer.Add(click_input_properties, name, property);
    }
    writer.Add(click_input, "properties", click_input_properties);
    const auto click_input_required = writer.Array();
    writer.Append(click_input_required, writer.String("x"));
    writer.Append(click_input_required, writer.String("y"));
    writer.Add(click_input, "required", click_input_required);
    writer.AddBool(click_input, "additionalProperties", false);
    writer.Add(click, "inputSchema", click_input);

    const auto click_output = writer.Object();
    writer.AddString(click_output, "type", "object");
    const auto click_output_properties = writer.Object();
    for (const std::string_view name :
         {"requestSequence", "frameSequence", "x", "y"}) {
        const auto property = writer.Object();
        writer.AddString(property, "type", "integer");
        writer.Add(click_output_properties, name, property);
    }
    writer.Add(click_output, "properties", click_output_properties);
    const auto click_output_required = writer.Array();
    for (const std::string_view name :
         {"requestSequence", "frameSequence", "x", "y"}) {
        writer.Append(click_output_required, writer.String(name));
    }
    writer.Add(click_output, "required", click_output_required);
    writer.AddBool(click_output, "additionalProperties", false);
    writer.Add(click, "outputSchema", click_output);

    const auto click_annotations = writer.Object();
    writer.AddBool(click_annotations, "readOnlyHint", false);
    writer.AddBool(click_annotations, "destructiveHint", false);
    writer.AddBool(click_annotations, "idempotentHint", false);
    writer.AddBool(click_annotations, "openWorldHint", false);
    writer.Add(click, "annotations", click_annotations);
    writer.Append(tools, click);
    writer.Add(result, "tools", tools);
    return result;
}

std::string_view OverlayName(const CaptureOverlay overlay) {
    return overlay == CaptureOverlay::kCoordinates ? "coordinates" : "none";
}

std::string FrameMetadataText(const FrameSnapshot& frame, const std::string_view format,
                              const CaptureOverlay overlay) {
    core::JsonWriter writer;
    const auto metadata = writer.Object();
    writer.AddUnsignedInteger(metadata, "sequence", frame.sequence);
    writer.AddUnsignedInteger(metadata, "width", frame.width);
    writer.AddUnsignedInteger(metadata, "height", frame.height);
    writer.AddString(metadata, "format", format);
    writer.AddString(metadata, "overlay", OverlayName(overlay));
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

core::JsonWriter::Value CaptureResult(core::JsonWriter& writer, FrameSnapshotStore& frames,
                                      const CaptureFormat format,
                                      const CaptureOverlay overlay) {
    auto frame = frames.Latest();
    if (!frame) return ToolError(writer, "No presented guest frame is available.");

    if (overlay == CaptureOverlay::kCoordinates) {
        DrawCoordinateOverlay(frame->width, frame->height, frame->rgba8);
    }
    const auto encoded = EncodeFrame(*frame, format);
    const auto result = writer.Object();
    const auto content = writer.Array();
    const auto image = writer.Object();
    writer.AddString(image, "type", "image");
    writer.AddString(image, "data", Base64(encoded.bytes));
    writer.AddString(image, "mimeType", encoded.mime_type);
    writer.Append(content, image);
    const auto text = writer.Object();
    writer.AddString(text, "type", "text");
    writer.AddString(text, "text", FrameMetadataText(*frame, encoded.format, overlay));
    writer.Append(content, text);
    writer.Add(result, "content", content);
    const auto structured = writer.Object();
    writer.AddUnsignedInteger(structured, "sequence", frame->sequence);
    writer.AddUnsignedInteger(structured, "width", frame->width);
    writer.AddUnsignedInteger(structured, "height", frame->height);
    writer.AddString(structured, "format", encoded.format);
    writer.AddString(structured, "overlay", OverlayName(overlay));
    writer.Add(result, "structuredContent", structured);
    writer.AddBool(result, "isError", false);
    return result;
}

core::JsonWriter::Value ClickResult(
    core::JsonWriter& writer, FrameSnapshotStore& frames,
    McpInputQueue* inputs, const core::JsonValue arguments) {
    if (inputs == nullptr) {
        return ToolError(writer, "Click input is unavailable for this MCP session.");
    }
    if (!arguments.IsObject() || arguments.Size() != 2U) {
        return ToolError(writer, "click requires exactly integer x and y arguments");
    }
    const auto x_value = arguments.Member("x");
    const auto y_value = arguments.Member("y");
    const auto x = x_value ? x_value->UnsignedInteger() : std::nullopt;
    const auto y = y_value ? y_value->UnsignedInteger() : std::nullopt;
    if (!x.has_value() || !y.has_value() ||
        *x > (std::numeric_limits<std::uint32_t>::max)() ||
        *y > (std::numeric_limits<std::uint32_t>::max)()) {
        return ToolError(writer, "click x and y must be non-negative 32-bit integers");
    }
    const auto frame = frames.LatestMetadata();
    if (!frame.has_value()) {
        return ToolError(writer, "No presented guest frame is available for click.");
    }
    if (*x >= frame->width || *y >= frame->height) {
        return ToolError(writer, "click coordinates are outside the latest guest frame");
    }
    const auto request_sequence = inputs->TryEnqueueClick(
        frame->sequence, static_cast<std::uint32_t>(*x),
        static_cast<std::uint32_t>(*y));
    if (!request_sequence.has_value()) {
        return ToolError(writer, "click queue is full");
    }

    const auto add_metadata = [&](const core::JsonWriter::Value object) {
        writer.AddUnsignedInteger(object, "requestSequence", *request_sequence);
        writer.AddUnsignedInteger(object, "frameSequence", frame->sequence);
        writer.AddUnsignedInteger(object, "x", *x);
        writer.AddUnsignedInteger(object, "y", *y);
    };
    core::JsonWriter text_writer;
    const auto text_metadata = text_writer.Object();
    text_writer.AddUnsignedInteger(text_metadata, "requestSequence", *request_sequence);
    text_writer.AddUnsignedInteger(text_metadata, "frameSequence", frame->sequence);
    text_writer.AddUnsignedInteger(text_metadata, "x", *x);
    text_writer.AddUnsignedInteger(text_metadata, "y", *y);

    const auto result = writer.Object();
    const auto content = writer.Array();
    const auto text = writer.Object();
    writer.AddString(text, "type", "text");
    writer.AddString(text, "text", text_writer.Serialize(text_metadata));
    writer.Append(content, text);
    writer.Add(result, "content", content);
    const auto structured = writer.Object();
    add_metadata(structured);
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

std::optional<FrameSnapshotMetadata> FrameSnapshotStore::LatestMetadata() const {
    std::scoped_lock lock(mutex_);
    if (!latest_.has_value()) return std::nullopt;
    return FrameSnapshotMetadata{
        latest_->width, latest_->height, latest_->sequence};
}

std::optional<FrameSnapshot> FrameSnapshotStore::Take() {
    std::scoped_lock lock(mutex_);
    auto result = std::move(latest_);
    latest_.reset();
    return result;
}

std::optional<std::uint64_t> McpInputQueue::TryEnqueueClick(
    const std::uint64_t frame_sequence, const std::uint32_t x,
    const std::uint32_t y) {
    std::scoped_lock lock(mutex_);
    if (clicks_.size() >= kMaximumPendingClicks) return std::nullopt;
    if (next_request_sequence_ ==
        (std::numeric_limits<std::uint64_t>::max)()) {
        throw std::overflow_error("MCP click request sequence overflow");
    }
    const auto sequence = next_request_sequence_++;
    clicks_.push_back({sequence, frame_sequence, x, y, false});
    return sequence;
}

std::optional<McpPointerEvent> McpInputQueue::TakeNextPointerEvent() {
    std::scoped_lock lock(mutex_);
    if (clicks_.empty()) return std::nullopt;
    auto& click = clicks_.front();
    const McpPointerEvent result{
        click.request_sequence, click.frame_sequence, click.x, click.y,
        !click.down_dispatched};
    if (click.down_dispatched) clicks_.pop_front();
    else click.down_dispatched = true;
    return result;
}

std::size_t McpInputQueue::PendingClicks() const {
    std::scoped_lock lock(mutex_);
    return clicks_.size();
}

McpProtocolAdapter::McpProtocolAdapter(FrameSnapshotStore& frames,
                                       std::string server_version)
    : frames_(frames), server_version_(std::move(server_version)) {
    if (server_version_.empty()) throw std::invalid_argument("MCP server version is empty");
}

McpProtocolAdapter::McpProtocolAdapter(
    FrameSnapshotStore& frames, McpInputQueue& inputs,
    std::string server_version)
    : McpProtocolAdapter(frames, std::move(server_version)) {
    inputs_ = &inputs;
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
                    "frame_capture returns the latest presented guest frame as JPEG by default "
                    "or PNG when requested, without advancing execution. Its optional coordinate "
                    "overlay marks guest pixels on the returned copy. click queues a bounded "
                    "primary-pointer tap in guest pixels.");
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
            const auto arguments = params->Member("arguments");
            if (arguments.has_value() && !arguments->IsObject()) {
                return RpcError(id, -32602, "tool arguments must be an object");
            }
            if (*name == "frame_capture") {
                CaptureFormat format = CaptureFormat::kJpeg;
                CaptureOverlay overlay = CaptureOverlay::kNone;
                if (arguments.has_value() && arguments->Size() != 0U) {
                    const auto format_value = arguments->Member("format");
                    const auto overlay_value = arguments->Member("overlay");
                    const auto known_arguments = static_cast<std::size_t>(
                        format_value.has_value()) + static_cast<std::size_t>(
                        overlay_value.has_value());
                    if (arguments->Size() != known_arguments) {
                        return RpcResult(*id, [](core::JsonWriter& writer) {
                            return ToolError(
                                writer,
                                "frame_capture accepts only optional format and overlay arguments");
                        });
                    }
                    if (format_value.has_value()) {
                        const auto requested_format = format_value->String();
                        if (!requested_format.has_value() ||
                            (*requested_format != "jpeg" && *requested_format != "png")) {
                            return RpcResult(*id, [](core::JsonWriter& writer) {
                                return ToolError(
                                    writer,
                                    "frame_capture format must be exactly 'jpeg' or 'png'");
                            });
                        }
                        if (*requested_format == "png") format = CaptureFormat::kPng;
                    }
                    if (overlay_value.has_value()) {
                        const auto requested_overlay = overlay_value->String();
                        if (!requested_overlay.has_value() ||
                            *requested_overlay != "coordinates") {
                            return RpcResult(*id, [](core::JsonWriter& writer) {
                                return ToolError(
                                    writer,
                                    "frame_capture overlay must be exactly 'coordinates'");
                            });
                        }
                        overlay = CaptureOverlay::kCoordinates;
                    }
                }
                return RpcResult(*id, [&](core::JsonWriter& writer) {
                    return CaptureResult(writer, frames_, format, overlay);
                });
            }
            if (*name == "click") {
                if (!arguments.has_value()) {
                    return RpcResult(*id, [](core::JsonWriter& writer) {
                        return ToolError(writer, "click requires x and y arguments");
                    });
                }
                return RpcResult(*id, [&](core::JsonWriter& writer) {
                    return ClickResult(writer, frames_, inputs_, *arguments);
                });
            }
            return RpcError(id, -32602, "unknown MCP tool: " + std::string(*name));
        }
        return RpcError(id, -32601, "method not found: " + std::string(*method));
    } catch (const std::exception& error) {
        return RpcError(id, -32603, error.what());
    }
}

}  // namespace ogplay::agent
