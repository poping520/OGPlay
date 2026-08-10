#include "ogplay/agent/mcp_protocol.h"

#include <array>
#include <cctype>
#include <charconv>
#include <limits>
#include <span>
#include <sstream>
#include <stdexcept>
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
        default:
            if (character < 0x20U) {
                constexpr char hex[] = "0123456789abcdef";
                result += "\\u00";
                result += hex[character >> 4U];
                result += hex[character & 0x0fU];
            } else {
                result += static_cast<char>(character);
            }
            break;
        }
    }
    result += '"';
    return result;
}

std::optional<std::size_t> MemberValueStart(const std::string_view json,
                                            const std::string_view key) {
    const auto key_position = json.find('"' + std::string(key) + '"');
    if (key_position == std::string_view::npos) return std::nullopt;
    const auto colon = json.find(':', key_position + key.size() + 2U);
    if (colon == std::string_view::npos) {
        throw std::invalid_argument("JSON member has no value");
    }
    auto position = colon + 1U;
    while (position < json.size() &&
           std::isspace(static_cast<unsigned char>(json[position]))) {
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

std::string IdMember(const std::string_view json) {
    const auto start = MemberValueStart(json, "id");
    if (!start) return "null";
    if (json[*start] == '"') return JsonString(*StringMember(json, "id"));
    auto end = *start;
    while (end < json.size() &&
           (std::isdigit(static_cast<unsigned char>(json[end])) || json[end] == '-')) {
        ++end;
    }
    if (end == *start) {
        if (json.substr(*start, 4U) == "null") return "null";
        throw std::invalid_argument("JSON-RPC id must be string, integer or null");
    }
    return std::string(json.substr(*start, end - *start));
}

std::string RpcResult(const std::string_view id, const std::string_view result) {
    return "{\"jsonrpc\":\"2.0\",\"id\":" + std::string(id) +
           ",\"result\":" + std::string(result) + '}';
}

std::string RpcError(const std::string_view id, const int code,
                     const std::string_view message) {
    std::ostringstream json;
    json << "{\"jsonrpc\":\"2.0\",\"id\":" << id
         << ",\"error\":{\"code\":" << code
         << ",\"message\":" << JsonString(message) << "}}";
    return json.str();
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

std::string ToolsList() {
    return R"({"tools":[{"name":"frame_capture","title":"Capture latest OGPlay frame","description":"Returns the latest presented guest frame as PNG without advancing guest execution or consuming input.","inputSchema":{"type":"object","properties":{},"additionalProperties":false},"annotations":{"readOnlyHint":true,"destructiveHint":false,"idempotentHint":true,"openWorldHint":false}}]})";
}

std::string CaptureResult(FrameSnapshotStore& frames) {
    const auto frame = frames.Latest();
    if (!frame) {
        return R"({"content":[{"type":"text","text":"No presented guest frame is available."}],"isError":true})";
    }
    const auto png = EncodePng(*frame);
    std::ostringstream result;
    result << "{\"content\":[{\"type\":\"image\",\"data\":"
           << JsonString(Base64(png))
           << ",\"mimeType\":\"image/png\"},{\"type\":\"text\",\"text\":"
           << JsonString("Captured frame " + std::to_string(frame->sequence) + " (" +
                         std::to_string(frame->width) + "x" +
                         std::to_string(frame->height) + ").")
           << "}],\"structuredContent\":{\"sequence\":" << frame->sequence
           << ",\"width\":" << frame->width << ",\"height\":" << frame->height
           << "},\"isError\":false}";
    return result.str();
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
    std::string id = "null";
    try {
        const auto first = request.find_first_not_of(" \t\r\n");
        const auto last = request.find_last_not_of(" \t\r\n");
        if (first == std::string_view::npos || request[first] != '{' ||
            request[last] != '}') {
            throw std::invalid_argument("JSON-RPC request must be an object");
        }
        if (StringMember(request, "jsonrpc") != "2.0") {
            return RpcError(id, -32600, "invalid JSON-RPC version");
        }
        const auto method = StringMember(request, "method");
        if (!method || method->empty()) return RpcError(id, -32600, "method is required");
        const bool notification = !MemberValueStart(request, "id").has_value();
        if (!notification) id = IdMember(request);

        if (*method == "notifications/initialized" || *method == "notifications/cancelled") {
            return notification ? std::nullopt : std::optional{RpcResult(id, "{}")};
        }
        if (notification) return std::nullopt;
        if (*method == "initialize") {
            const auto requested = StringMember(request, "protocolVersion");
            const auto negotiated = requested &&
                                            (*requested == "2025-11-25" ||
                                             *requested == "2025-06-18" ||
                                             *requested == "2025-03-26")
                                        ? *requested
                                        : std::string(kProtocolVersion);
            std::ostringstream result;
            result << "{\"protocolVersion\":" << JsonString(negotiated)
                   << ",\"capabilities\":{\"tools\":{\"listChanged\":false}},"
                      "\"serverInfo\":{\"name\":\"ogplay\",\"version\":"
                   << JsonString(server_version_)
                   << "},\"instructions\":"
                   << JsonString("OGPlay exposes read-only runtime inspection. "
                                 "frame_capture returns the latest presented guest frame "
                                 "and never advances execution or consumes input.")
                   << '}';
            return RpcResult(id, result.str());
        }
        if (*method == "ping") return RpcResult(id, "{}");
        if (*method == "tools/list") return RpcResult(id, ToolsList());
        if (*method == "tools/call") {
            const auto name = StringMember(request, "name");
            if (!name || name->empty()) return RpcError(id, -32602, "tool name is required");
            if (*name != "frame_capture") {
                return RpcError(id, -32602, "unknown MCP tool: " + *name);
            }
            return RpcResult(id, CaptureResult(frames_));
        }
        return RpcError(id, -32601, "method not found: " + *method);
    } catch (const std::invalid_argument& error) {
        return RpcError(id, -32602, error.what());
    } catch (const std::exception& error) {
        return RpcError(id, -32603, error.what());
    }
}

}  // namespace ogplay::agent
