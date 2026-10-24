#include "frontend/gui/dashboard.h"
#include "ogplay/app/json.h"
#include "ogplay/app/host.h"
#include <boost/asio.hpp>
#include <array>
#include <chrono>
#include <stdexcept>
#include <thread>

namespace ogplay::frontend {
namespace {

enum class Exchange { complete, refused, send_failed, oversize, disconnected, timeout };

// One bounded non-blocking HTTP/1.1 exchange with a loopback service; the server closes the
// connection after its single response.
Exchange LoopbackExchange(const std::uint16_t port, const std::string_view request, std::string& response) {
    using boost::asio::ip::tcp;
    boost::asio::io_context io;
    tcp::socket socket(io); boost::system::error_code error;
    socket.open(tcp::v4(), error); if (error) return Exchange::refused;
    socket.non_blocking(true, error); if (error) return Exchange::refused;
    const auto deadline = app::MonotonicClock::NowNs() + 750000000ULL;
    socket.connect({boost::asio::ip::make_address_v4("127.0.0.1"), port}, error);
    const auto pending = [](auto e) { return e == boost::asio::error::would_block || e == boost::asio::error::try_again || e == boost::asio::error::in_progress; };
    if (error && !pending(error)) return Exchange::refused;
    std::size_t sent{}; std::array<char, 2048> buffer{};
    while (app::MonotonicClock::NowNs() < deadline) {
        if (sent < request.size()) {
            sent += socket.write_some(boost::asio::buffer(request.data() + sent, request.size() - sent), error);
            if (error && !pending(error) && error != boost::asio::error::not_connected) return Exchange::send_failed;
        } else {
            const auto n = socket.read_some(boost::asio::buffer(buffer), error); response.append(buffer.data(), n);
            if (response.size() > 8192) return Exchange::oversize;
            if (error == boost::asio::error::eof) return Exchange::complete;
            if (error && !pending(error)) return Exchange::disconnected;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return Exchange::timeout;
}

[[nodiscard]] std::string PostRequest(const std::uint16_t port, const std::string_view path, const std::string_view body,
                                      const std::string_view extra_headers = {}) {
    return "POST " + std::string(path) + " HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) +
        "\r\nContent-Type: application/json\r\nAccept: application/json\r\nConnection: close\r\n" +
        std::string(extra_headers) + "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + std::string(body);
}

}  // namespace

DashboardProbe DecodeDashboardProbe(std::string_view response, std::uint64_t pid) {
    if (!pid || !response.starts_with("HTTP/1.1 200 ")) return {false, "Dashboard 服务尚未就绪。"};
    const auto body = response.find("\r\n\r\n");
    if (body == std::string_view::npos || response.size() > 8192) return {false, "Dashboard 响应无效。"};
    app::JsonParseError error;
    auto doc = app::JsonDocument::ParseStrict(response.substr(body + 4), error, 8192);
    const auto root = doc ? doc->Root() : app::JsonValue{};
    const auto result = root.Member("result");
    const auto id = result ? result->Member("process_id") : std::nullopt;
    const auto schema = result ? result->Member("schema_version") : std::nullopt;
    if (!root.Member("jsonrpc") || root.Member("jsonrpc")->String() != "2.0" || root.Member("error") || !root.Member("id") || root.Member("id")->UnsignedInteger() != 1 || !schema || schema->UnsignedInteger() != 1)
        return {false, "Dashboard 协议不匹配。"};
    if (!id || id->UnsignedInteger() != pid) return {false, "端口属于其他进程，不能打开这个游戏的 Dashboard。"};
    return {true, "Dashboard 已就绪。"};
}
DashboardProbe ProbeDashboard(std::uint16_t port, std::uint64_t pid) {
    if (!port || !pid) return {false, "运行中的游戏信息无效。"};
    try {
        std::string response;
        switch (LoopbackExchange(port, PostRequest(port, "/dash/rpc", R"({"jsonrpc":"2.0","id":1,"method":"dash.snapshot","params":{"sections":[]}})"), response)) {
        case Exchange::complete: return DecodeDashboardProbe(response, pid);
        case Exchange::refused: case Exchange::send_failed: return {false, "等待 Dashboard 服务启动。"};
        case Exchange::oversize: return {false, "Dashboard 响应超出上限。"};
        case Exchange::disconnected: return {false, "Dashboard 连接已断开。"};
        case Exchange::timeout: break;
        }
        return {false, "Dashboard 服务未响应，稍后自动重试。"};
    } catch (const std::exception&) { return {false, "Dashboard 服务不可用。"}; }
}

KeyEditorProbe DecodeKeyEditorProbe(const std::string_view response) {
    if (response.starts_with("HTTP/1.1 401 ")) return {false, 0, "按键设置令牌不匹配，端口可能属于其他进程。"};
    if (!response.starts_with("HTTP/1.1 200 ")) return {false, 0, "按键设置服务尚未就绪。"};
    const auto body = response.find("\r\n\r\n");
    if (body == std::string_view::npos || response.size() > 8192) return {false, 0, "按键设置响应无效。"};
    app::JsonParseError error;
    auto doc = app::JsonDocument::ParseStrict(response.substr(body + 4), error, 8192);
    const auto root = doc ? doc->Root() : app::JsonValue{};
    const auto result = root.Member("result");
    const auto requests = result ? result->Member("edit_requests") : std::nullopt;
    const auto count = requests ? requests->UnsignedInteger() : std::nullopt;
    if (!root.Member("id") || root.Member("id")->UnsignedInteger() != 1 || root.Member("error") || !count)
        return {false, 0, "按键设置协议不匹配。"};
    return {true, *count, "按键设置可用。"};
}
KeyEditorProbe ProbeKeyEditor(const std::uint16_t port, const std::string_view token) {
    if (!port || token.empty()) return {false, 0, "运行中的游戏信息无效。"};
    try {
        std::string response;
        const auto request = PostRequest(port, "/controls/rpc",
            R"({"jsonrpc":"2.0","id":1,"method":"control.status","params":{"client":"launcher"}})",
            "Authorization: Bearer " + std::string(token) + "\r\n");
        switch (LoopbackExchange(port, request, response)) {
        case Exchange::complete: return DecodeKeyEditorProbe(response);
        case Exchange::refused: case Exchange::send_failed: return {false, 0, "等待按键设置服务启动。"};
        case Exchange::oversize: case Exchange::disconnected: return {false, 0, "按键设置连接异常。"};
        case Exchange::timeout: break;
        }
        return {false, 0, "按键设置服务未响应，稍后自动重试。"};
    } catch (const std::exception&) { return {false, 0, "按键设置服务不可用。"}; }
}

std::uint16_t PickLoopbackPort() {
    using boost::asio::ip::tcp;
    boost::asio::io_context io;
    tcp::acceptor acceptor(io, tcp::endpoint(boost::asio::ip::make_address_v4("127.0.0.1"), 0));
    const auto port = acceptor.local_endpoint().port();
    if (port == 0) throw std::runtime_error("no free loopback port for the key editor");
    return port;
}
}
