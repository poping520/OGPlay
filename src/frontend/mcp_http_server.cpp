#include "ogplay/frontend/mcp_http_server.h"

#include "ogplay/agent/mcp_protocol.h"

#include <boost/asio.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace ogplay::frontend {
namespace {

using boost::asio::ip::tcp;

constexpr std::size_t kMaximumHeaderBytes = 64U * 1024U;
constexpr std::size_t kMaximumRequestBytes = 1024U * 1024U;
constexpr std::size_t kMaximumBufferedBytes =
    kMaximumHeaderBytes + kMaximumRequestBytes + 4U;

struct HttpRequest {
    std::string method;
    std::string target;
    std::string content_type;
    std::string accept;
    std::string origin;
    std::size_t content_length = 0;
    std::string body;
};

std::string LowerAscii(std::string_view value) {
    std::string lowered(value);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lowered;
}

std::string_view TrimAscii(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }
    return value;
}

std::optional<HttpRequest> ParseHeaders(const std::string_view bytes, std::string& error) {
    const auto header_end = bytes.find("\r\n\r\n");
    if (header_end == std::string_view::npos || header_end > kMaximumHeaderBytes) {
        error = "request headers exceed limit or are incomplete";
        return std::nullopt;
    }
    const std::string_view headers = bytes.substr(0, header_end);
    const auto first_line_end = headers.find("\r\n");
    const std::string_view request_line = headers.substr(0, first_line_end);
    const auto first_space = request_line.find(' ');
    const auto second_space = first_space == std::string_view::npos
                                  ? std::string_view::npos
                                  : request_line.find(' ', first_space + 1);
    if (first_space == std::string_view::npos || second_space == std::string_view::npos ||
        request_line.substr(second_space + 1) != "HTTP/1.1") {
        error = "invalid HTTP/1.1 request line";
        return std::nullopt;
    }

    HttpRequest request;
    request.method = std::string(request_line.substr(0, first_space));
    request.target = std::string(request_line.substr(first_space + 1,
                                                     second_space - first_space - 1));
    std::optional<std::size_t> content_length;
    std::size_t cursor = first_line_end == std::string_view::npos
                             ? headers.size()
                             : first_line_end + 2;
    while (cursor < headers.size()) {
        const auto line_end = headers.find("\r\n", cursor);
        const std::string_view line = headers.substr(
            cursor, (line_end == std::string_view::npos ? headers.size() : line_end) - cursor);
        cursor = line_end == std::string_view::npos ? headers.size() : line_end + 2;
        const auto colon = line.find(':');
        if (colon == std::string_view::npos) {
            error = "invalid HTTP header";
            return std::nullopt;
        }
        const std::string name = LowerAscii(TrimAscii(line.substr(0, colon)));
        const std::string_view value = TrimAscii(line.substr(colon + 1));
        if (name == "content-length") {
            std::size_t parsed = 0;
            const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
                (content_length.has_value() && *content_length != parsed)) {
                error = "invalid Content-Length";
                return std::nullopt;
            }
            content_length = parsed;
        } else if (name == "content-type") {
            request.content_type = LowerAscii(value);
        } else if (name == "accept") {
            request.accept = LowerAscii(value);
        } else if (name == "origin") {
            request.origin = LowerAscii(value);
        } else if (name == "transfer-encoding") {
            error = "Transfer-Encoding is unsupported";
            return std::nullopt;
        }
    }
    request.content_length = content_length.value_or(0);
    if (request.content_length > kMaximumRequestBytes) {
        error = "request body exceeds limit";
        return std::nullopt;
    }
    return request;
}

bool IsLoopbackOrigin(const std::string_view origin) {
    if (origin.empty()) {
        return true;
    }
    constexpr std::array<std::string_view, 2> bases = {
        "http://127.0.0.1", "http://localhost"};
    for (const auto base : bases) {
        if (origin == base) {
            return true;
        }
        if (origin.starts_with(base) && origin.size() > base.size() + 1 &&
            origin[base.size()] == ':') {
            const auto port = origin.substr(base.size() + 1);
            if (std::all_of(port.begin(), port.end(), [](const unsigned char ch) {
                    return std::isdigit(ch) != 0;
                })) {
                return true;
            }
        }
    }
    return false;
}

std::string MakeResponse(const int status, const std::string_view reason,
                         const std::string_view content_type, const std::string_view body,
                         const std::string_view extra_headers = {}) {
    std::string response = "HTTP/1.1 " + std::to_string(status) + " " + std::string(reason) +
                           "\r\nContent-Type: " + std::string(content_type) +
                           "\r\nContent-Length: " + std::to_string(body.size()) +
                           "\r\nConnection: close\r\nCache-Control: no-store\r\n";
    response.append(extra_headers);
    response.append("\r\n");
    response.append(body);
    return response;
}

class HttpSession final : public std::enable_shared_from_this<HttpSession> {
public:
    HttpSession(tcp::socket socket, agent::McpProtocolAdapter& protocol)
        : socket_(std::move(socket)), protocol_(protocol) {}

    void Start() {
        auto self = shared_from_this();
        boost::asio::async_read_until(
            socket_, boost::asio::dynamic_buffer(request_bytes_, kMaximumBufferedBytes),
            "\r\n\r\n",
            [self](const boost::system::error_code& error, const std::size_t) {
                if (error) {
                    self->Reply(MakeResponse(400, "Bad Request", "text/plain; charset=utf-8",
                                             "request headers are incomplete"));
                    return;
                }
                self->ReadBody();
            });
    }

private:
    void ReadBody() {
        std::string parse_error;
        request_ = ParseHeaders(request_bytes_, parse_error);
        if (!request_.has_value()) {
            Reply(MakeResponse(400, "Bad Request", "text/plain; charset=utf-8", parse_error));
            return;
        }
        const std::size_t body_offset = request_bytes_.find("\r\n\r\n") + 4;
        const std::size_t available = request_bytes_.size() - body_offset;
        if (available >= request_->content_length) {
            FinishRequest(body_offset);
            return;
        }

        const std::size_t missing = request_->content_length - available;
        auto self = shared_from_this();
        boost::asio::async_read(
            socket_, boost::asio::dynamic_buffer(request_bytes_, kMaximumBufferedBytes),
            boost::asio::transfer_exactly(missing),
            [self, body_offset](const boost::system::error_code& error, const std::size_t) {
                if (error) {
                    self->Reply(MakeResponse(400, "Bad Request", "text/plain; charset=utf-8",
                                             "request ended before body"));
                    return;
                }
                self->FinishRequest(body_offset);
            });
    }

    void FinishRequest(const std::size_t body_offset) {
        request_->body.assign(request_bytes_.data() + body_offset, request_->content_length);
        if (request_->target != "/mcp") {
            Reply(MakeResponse(404, "Not Found", "text/plain; charset=utf-8",
                               "MCP endpoint is /mcp"));
            return;
        }
        if (!IsLoopbackOrigin(request_->origin)) {
            Reply(MakeResponse(403, "Forbidden", "text/plain; charset=utf-8",
                               "Origin is not loopback"));
            return;
        }
        if (request_->method != "POST") {
            Reply(MakeResponse(405, "Method Not Allowed", "text/plain; charset=utf-8",
                               "Only POST is supported", "Allow: POST\r\n"));
            return;
        }
        if (!request_->content_type.starts_with("application/json")) {
            Reply(MakeResponse(415, "Unsupported Media Type", "text/plain; charset=utf-8",
                               "Content-Type must be application/json"));
            return;
        }
        if (!request_->accept.empty() &&
            request_->accept.find("application/json") == std::string::npos &&
            request_->accept.find("*/*") == std::string::npos) {
            Reply(MakeResponse(406, "Not Acceptable", "text/plain; charset=utf-8",
                               "Accept must include application/json"));
            return;
        }

        const auto response = protocol_.Handle(request_->body);
        if (!response.has_value()) {
            Reply(MakeResponse(202, "Accepted", "application/json", {}));
            return;
        }
        Reply(MakeResponse(200, "OK", "application/json", *response,
                           "MCP-Protocol-Version: 2025-11-25\r\n"));
    }

    void Reply(std::string response) {
        response_ = std::move(response);
        auto self = shared_from_this();
        boost::asio::async_write(
            socket_, boost::asio::buffer(response_),
            [self](const boost::system::error_code&, const std::size_t) {
                boost::system::error_code ignored;
                self->socket_.shutdown(tcp::socket::shutdown_both, ignored);
                self->socket_.close(ignored);
            });
    }

    tcp::socket socket_;
    agent::McpProtocolAdapter& protocol_;
    std::string request_bytes_;
    std::optional<HttpRequest> request_;
    std::string response_;
};

}  // namespace

struct McpHttpServer::Impl final {
    explicit Impl(const std::uint16_t requested_port, agent::FrameSnapshotStore& frames)
        : acceptor(io), protocol(frames) {
        boost::system::error_code error;
        const tcp::endpoint endpoint(boost::asio::ip::make_address_v4("127.0.0.1"), requested_port);
        acceptor.open(endpoint.protocol(), error);
        if (!error) {
            acceptor.set_option(tcp::acceptor::reuse_address(true), error);
        }
        if (!error) {
            acceptor.bind(endpoint, error);
        }
        if (!error) {
            acceptor.listen(8, error);
        }
        if (error) {
            throw std::runtime_error("MCP HTTP failed to bind 127.0.0.1:" +
                                     std::to_string(requested_port) + ": " + error.message());
        }
        port = acceptor.local_endpoint().port();
        AcceptNext();
        worker = std::thread([this] { io.run(); });
    }

    ~Impl() {
        io.stop();
        if (worker.joinable()) {
            worker.join();
        }
    }

    void AcceptNext() {
        acceptor.async_accept([this](const boost::system::error_code& error, tcp::socket socket) {
            if (!error) {
                std::make_shared<HttpSession>(std::move(socket), protocol)->Start();
            }
            if (acceptor.is_open()) {
                AcceptNext();
            }
        });
    }

    boost::asio::io_context io{1};
    tcp::acceptor acceptor;
    agent::McpProtocolAdapter protocol;
    std::uint16_t port = 0;
    std::thread worker;
};

std::unique_ptr<McpHttpServer> McpHttpServer::Start(
    const std::uint16_t port, agent::FrameSnapshotStore& frames) {
    return std::unique_ptr<McpHttpServer>(
        new McpHttpServer(std::make_unique<Impl>(port, frames)));
}

McpHttpServer::McpHttpServer(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
McpHttpServer::~McpHttpServer() = default;

std::uint16_t McpHttpServer::Port() const noexcept { return impl_->port; }

std::string McpHttpServer::Endpoint() const {
    return "http://127.0.0.1:" + std::to_string(Port()) + "/mcp";
}

}  // namespace ogplay::frontend
