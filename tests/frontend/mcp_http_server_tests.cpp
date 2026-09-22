#include "doctest/doctest.h"

#include "ogplay/agent/mcp_protocol.h"
#include "ogplay/agent/mcp_session_control.h"
#include "ogplay/agent/dashboard.h"
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
                    const std::string_view extra_headers = {}, const std::string_view host = "127.0.0.1") {
    boost::asio::io_context io;
    tcp::socket socket(io);
    boost::system::error_code error;
    socket.connect(tcp::endpoint(boost::asio::ip::make_address_v4("127.0.0.1"), port), error);
    if (error) {
        throw std::runtime_error("test HTTP connect failed: " + error.message());
    }

    std::string request = std::string(method) + " " + std::string(target) +
                          " HTTP/1.1\r\nHost: " + std::string(host) + "\r\n"
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
    ogplay::agent::McpInputQueue inputs;
    auto server = ogplay::frontend::McpHttpServer::Start(0, frames, inputs);

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
    ogplay::agent::McpInputQueue inputs;
    static_cast<void>(frames.Publish({2, 1, 73, {255, 0, 0, 255, 0, 255, 0, 255}}));
    auto server = ogplay::frontend::McpHttpServer::Start(0, frames, inputs);

    const auto response = Request(
        server->Port(), "POST", "/mcp",
        R"({"jsonrpc":"2.0","id":"capture","method":"tools/call","params":{"name":"frame_capture","arguments":{}}})");
    CHECK(response.find("HTTP/1.1 200 OK") != std::string::npos);
    CHECK(response.find("\"mimeType\":\"image/jpeg\"") != std::string::npos);
    CHECK(response.find("\"format\":\"jpeg\"") != std::string::npos);
    CHECK(response.find("\"sequence\":73") != std::string::npos);
    CHECK(response.find("\"width\":2") != std::string::npos);
    CHECK(response.find("\"height\":1") != std::string::npos);

    const auto click = Request(
        server->Port(), "POST", "/mcp",
        R"({"jsonrpc":"2.0","id":"click","method":"tools/call","params":{"name":"click","arguments":{"x":1,"y":0}}})");
    CHECK(click.find("HTTP/1.1 200 OK") != std::string::npos);
    CHECK(click.find("\"requestSequence\":1") != std::string::npos);
    CHECK(click.find("\"frameSequence\":73") != std::string::npos);
    const auto down = inputs.TakeNextPointerEvent();
    const auto up = inputs.TakeNextPointerEvent();
    REQUIRE(down.has_value());
    REQUIRE(up.has_value());
    CHECK(down->pressed);
    CHECK_FALSE(up->pressed);
    CHECK(down->x == 1U);
    CHECK(down->y == 0U);

    const auto swipe = Request(
        server->Port(), "POST", "/mcp",
        R"({"jsonrpc":"2.0","id":"swipe","method":"tools/call","params":{"name":"swipe","arguments":{"startX":0,"startY":0,"endX":1,"endY":0,"steps":2}}})");
    CHECK(swipe.find("HTTP/1.1 200 OK") != std::string::npos);
    CHECK(swipe.find("\"requestSequence\":2") != std::string::npos);
    CHECK(swipe.find("\"frameSequence\":73") != std::string::npos);
    const auto swipe_down = inputs.TakeNextPointerEvent();
    const auto first_motion = inputs.TakeNextPointerEvent();
    const auto second_motion = inputs.TakeNextPointerEvent();
    const auto swipe_up = inputs.TakeNextPointerEvent();
    REQUIRE(swipe_down.has_value());
    REQUIRE(first_motion.has_value());
    REQUIRE(second_motion.has_value());
    REQUIRE(swipe_up.has_value());
    CHECK(swipe_down->type == ogplay::agent::McpPointerEvent::Type::button);
    CHECK(first_motion->type == ogplay::agent::McpPointerEvent::Type::motion);
    CHECK(second_motion->type == ogplay::agent::McpPointerEvent::Type::motion);
    CHECK(swipe_up->type == ogplay::agent::McpPointerEvent::Type::button);
    CHECK_FALSE(swipe_up->pressed);
}

TEST_CASE("MCP HTTP rejects non-loopback origins and unsupported routes") {
    ogplay::agent::FrameSnapshotStore frames;
    ogplay::agent::McpInputQueue inputs;
    auto server = ogplay::frontend::McpHttpServer::Start(0, frames, inputs);

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

TEST_CASE("MCP HTTP bridges session commands without entering guest code") {
    ogplay::agent::FrameSnapshotStore frames;
    ogplay::agent::McpInputQueue inputs;
    ogplay::agent::McpSessionControl control;
    control.Publish({
        .lifecycle = ogplay::agent::McpLifecycleState::running,
        .frame = 23U,
        .guest_ticks = 23000U,
    });
    auto server = ogplay::frontend::McpHttpServer::Start(
        0, frames, inputs, control);

    const auto state = Request(
        server->Port(), "POST", "/mcp",
        R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"session_state","arguments":{}}})");
    CHECK(state.find("HTTP/1.1 200 OK") != std::string::npos);
    CHECK(state.find("\"frame\":23") != std::string::npos);
    const auto step = Request(
        server->Port(), "POST", "/mcp",
        R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"step","arguments":{"frames":2}}})");
    CHECK(step.find("\"targetFrame\":25") != std::string::npos);
    const auto command = control.TakeNextCommand();
    REQUIRE(command.has_value());
    CHECK(command->type == ogplay::agent::McpSessionCommand::Type::step);
    CHECK(command->frames == 2U);
}

TEST_CASE("Dashboard HTTP serves bounded assets and enforces origin host and path boundaries") {
    using namespace ogplay;
    agent::FrameSnapshotStore frames; agent::McpInputQueue inputs;
    frontend::DashboardHttpConfig config{std::make_shared<agent::DashboardService>(),
        std::filesystem::path(OGPLAY_SOURCE_DIR) / "data/webui/dashboard"};
    auto server = frontend::McpHttpServer::Start(0, frames, inputs, config);
    const auto port = server->Port();
    for (const auto target : {"/dash", "/dash/", "/dash/index.html", "/dash/dashboard.js", "/dash/dashboard.css", "/dash/manifest.json", "/dash/THIRD-PARTY-LICENSES.txt"}) {
        CAPTURE(target);
        const auto response = Request(port, "GET", target, {}, "Origin: http://127.0.0.1\r\n");
        CHECK(response.starts_with("HTTP/1.1 200 OK"));
        CHECK(response.find("X-Content-Type-Options: nosniff") != std::string::npos);
        CHECK(response.find("frame-ancestors 'none'") != std::string::npos);
        CHECK(response.find("Access-Control-Allow-Origin") == std::string::npos);
    }
    for (const auto path : {"/dash/../package.json", "/dash/%2e%2e/package.json", "/dash/..%5cpackage.json", "/dash/C:/secret", "/dash/dashboard.js:secret", "/dash//index.html", "/dash/index.html?x=1", "/dash/private.json"})
        CHECK(Request(port, "GET", path, {}).starts_with("HTTP/1.1 404"));
    for (const auto origin : {"https://example.com", "null", "http://127.0.0.1.evil", "http://localhost@evil", "http://localhost:123/"}) {
        for (const auto path : {"/dash/", "/dash/dashboard.js", "/dash/rpc"})
            CHECK(Request(port, "GET", path, {}, std::string("Origin: ") + origin + "\r\n").starts_with("HTTP/1.1 403"));
    }
    CHECK(Request(port, "GET", "/dash/", {}, "Origin: http://localhost\r\nOrigin: http://127.0.0.1\r\n").starts_with("HTTP/1.1 400"));
    CHECK(Request(port, "GET", "/dash/", {}, "Host: evil.example\r\n").starts_with("HTTP/1.1 400"));
    CHECK(Request(port, "GET", "/dash/", {}, {}, "evil.example").starts_with("HTTP/1.1 403"));
    CHECK(Request(port, "POST", "/dash/", "{}").starts_with("HTTP/1.1 405"));
    CHECK(Request(port, "GET", "/dash/rpc", {}).starts_with("HTTP/1.1 405"));
    config.assets /= "missing";
    CHECK_THROWS(frontend::McpHttpServer::Start(0, frames, inputs, config));
}

TEST_CASE("Dashboard HTTP exposes actual agent snapshots without advancing session or accepting controls") {
    using namespace ogplay;
    agent::FrameSnapshotStore frames; agent::McpInputQueue inputs; agent::McpSessionControl session;
    session.Publish({.frame = 17, .guest_ticks = 17000});
    runtime::debug::DiagnosticState diagnostics;
    diagnostics.RecordSyscall(7, 240, -4, runtime::SupervisorCallProgress::handled_idle);
    agent::DashboardSources sources; sources.session = &session; sources.diagnostics = &diagnostics;
    auto dashboard = std::make_shared<agent::DashboardService>(sources);
    auto server = frontend::McpHttpServer::Start(0, frames, inputs, session,
        {dashboard, std::filesystem::path(OGPLAY_SOURCE_DIR) / "data/webui/dashboard"});
    const auto response = Request(server->Port(), "POST", "/dash/rpc",
        R"({"jsonrpc":"2.0","id":1,"method":"dash.snapshot"})");
    CHECK(response.starts_with("HTTP/1.1 200"));
    CHECK(response.find("\"frame\":17") != std::string::npos);
    CHECK(response.find("\"syscall_nr\":240") != std::string::npos);
    CHECK(response.find("\"status\":\"unavailable\"") != std::string::npos);
    CHECK(Request(server->Port(), "POST", "/dash/rpc", R"({"jsonrpc":"2.0","id":2,"method":"dash.events","params":{"since_sequence":0}})").find("\"kind\":\"syscall\"") != std::string::npos);
    CHECK(Request(server->Port(), "POST", "/dash/rpc", R"({"jsonrpc":"2.0","id":3,"method":"run.step"})").find("-32601") != std::string::npos);
    CHECK(Request(server->Port(), "POST", "/dash/rpc", R"({"jsonrpc":"2.0","id":4,"method":"dash.snapshot","params":{"unknown":true}})").find("-32602") != std::string::npos);
    CHECK(session.Snapshot().frame == 17); CHECK(session.PendingCommands() == 0); CHECK(inputs.PendingGestures() == 0);
}
