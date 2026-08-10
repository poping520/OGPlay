#include "doctest/doctest.h"

#include "ogplay/agent/mcp_protocol.h"
#include "ogplay/frontend/mcp_http_server.h"

#include <boost/asio.hpp>

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using boost::asio::ip::tcp;

std::string Request(const std::uint16_t port, const std::string_view method,
                    const std::string_view target, const std::string_view body,
                    const std::string_view extra_headers = {}) {
    boost::asio::io_context io;
    tcp::socket socket(io);
    boost::system::error_code error;
    socket.connect(tcp::endpoint(boost::asio::ip::make_address_v4("127.0.0.1"), port), error);
    if (error) {
        throw std::runtime_error("test HTTP connect failed: " + error.message());
    }

    std::string request = std::string(method) + " " + std::string(target) +
                          " HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                          "Accept: application/json, text/event-stream\r\n"
                          "Content-Type: application/json\r\nContent-Length: " +
                          std::to_string(body.size()) + "\r\n";
    request.append(extra_headers);
    request.append("Connection: close\r\n\r\n");
    request.append(body);
    boost::asio::write(socket, boost::asio::buffer(request), error);
    if (error) {
        throw std::runtime_error("test HTTP send failed: " + error.message());
    }

    std::string response;
    std::array<char, 4096> chunk{};
    for (;;) {
        const std::size_t received = socket.read_some(boost::asio::buffer(chunk), error);
        response.append(chunk.data(), received);
        if (error == boost::asio::error::eof) {
            break;
        }
        if (error) {
            throw std::runtime_error("test HTTP receive failed: " + error.message());
        }
    }
    return response;
}

}  // namespace

TEST_CASE("MCP HTTP binds loopback and serves protocol responses") {
    ogplay::agent::FrameSnapshotStore frames;
    auto server = ogplay::frontend::McpHttpServer::Start(0, frames);

    CHECK(server->Port() != 0);
    CHECK(server->Endpoint() ==
          "http://127.0.0.1:" + std::to_string(server->Port()) + "/mcp");

    const auto response = Request(
        server->Port(), "POST", "/mcp",
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"fixture","version":"1"}}})");
    CHECK(response.find("HTTP/1.1 200 OK") != std::string::npos);
    CHECK(response.find("MCP-Protocol-Version: 2025-11-25") != std::string::npos);
    CHECK(response.find("\"protocolVersion\":\"2025-11-25\"") != std::string::npos);

    const auto notification = Request(
        server->Port(), "POST", "/mcp",
        R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
    CHECK(notification.find("HTTP/1.1 202 Accepted") != std::string::npos);
}

TEST_CASE("MCP HTTP returns the latest frame through a real loopback request") {
    ogplay::agent::FrameSnapshotStore frames;
    static_cast<void>(frames.Publish({2, 1, 73, {255, 0, 0, 255, 0, 255, 0, 255}}));
    auto server = ogplay::frontend::McpHttpServer::Start(0, frames);

    const auto response = Request(
        server->Port(), "POST", "/mcp",
        R"({"jsonrpc":"2.0","id":"capture","method":"tools/call","params":{"name":"frame_capture","arguments":{}}})");
    CHECK(response.find("HTTP/1.1 200 OK") != std::string::npos);
    CHECK(response.find("\"mimeType\":\"image/png\"") != std::string::npos);
    CHECK(response.find("\"sequence\":73") != std::string::npos);
    CHECK(response.find("\"width\":2") != std::string::npos);
    CHECK(response.find("\"height\":1") != std::string::npos);
}

TEST_CASE("MCP HTTP rejects non-loopback origins and unsupported routes") {
    ogplay::agent::FrameSnapshotStore frames;
    auto server = ogplay::frontend::McpHttpServer::Start(0, frames);

    const auto forbidden = Request(
        server->Port(), "POST", "/mcp", R"({"jsonrpc":"2.0","id":1,"method":"ping"})",
        "Origin: https://example.com\r\n");
    CHECK(forbidden.find("HTTP/1.1 403 Forbidden") != std::string::npos);

    const auto wrong_method = Request(server->Port(), "GET", "/mcp", {});
    CHECK(wrong_method.find("HTTP/1.1 405 Method Not Allowed") != std::string::npos);
    CHECK(wrong_method.find("Allow: POST") != std::string::npos);

    const auto wrong_route = Request(server->Port(), "POST", "/other", "{}");
    CHECK(wrong_route.find("HTTP/1.1 404 Not Found") != std::string::npos);
}
