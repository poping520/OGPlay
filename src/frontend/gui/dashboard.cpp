#include "ogplay/frontend/gui_dashboard.h"
#include "ogplay/core/json.h"
#include "ogplay/hal/clock.h"
#include <boost/asio.hpp>
#include <array>
#include <chrono>
#include <thread>

namespace ogplay::frontend {
DashboardProbe DecodeDashboardProbe(std::string_view response, std::uint64_t pid) {
    if (!pid || !response.starts_with("HTTP/1.1 200 ")) return {false, "Dashboard 服务尚未就绪。"};
    const auto body = response.find("\r\n\r\n");
    if (body == std::string_view::npos || response.size() > 8192) return {false, "Dashboard 响应无效。"};
    core::JsonParseError error;
    auto doc = core::JsonDocument::ParseStrict(response.substr(body + 4), error, 8192);
    const auto root = doc ? doc->Root() : core::JsonValue{};
    const auto result = root.Member("result");
    const auto id = result ? result->Member("process_id") : std::nullopt;
    const auto schema = result ? result->Member("schema_version") : std::nullopt;
    if (!root.Member("jsonrpc") || root.Member("jsonrpc")->String() != "2.0" || root.Member("error") || !root.Member("id") || root.Member("id")->UnsignedInteger() != 1 || !schema || schema->UnsignedInteger() != 1)
        return {false, "Dashboard 协议不匹配。"};
    if (!id || id->UnsignedInteger() != pid) return {false, "端口属于其他进程，不能打开此实例的 Dashboard。"};
    return {true, "Dashboard 已就绪。"};
}
DashboardProbe ProbeDashboard(std::uint16_t port, std::uint64_t pid) {
    if (!port || !pid) return {false, "无效的运行实例。"};
    try {
        using boost::asio::ip::tcp;
        boost::asio::io_context io;
        tcp::socket socket(io); boost::system::error_code error;
        socket.open(tcp::v4(), error); if (error) return {false, "无法创建本机连接。"};
        socket.non_blocking(true, error); if (error) return {false, "无法设置本机连接。"};
        const auto deadline = hal::Clock::SteadyTimestampNs() + 750000000ULL;
        socket.connect({boost::asio::ip::make_address_v4("127.0.0.1"), port}, error);
        const auto pending = [](auto e) { return e == boost::asio::error::would_block || e == boost::asio::error::try_again || e == boost::asio::error::in_progress; };
        if (error && !pending(error)) return {false, "等待 Dashboard 服务启动。"};
        const std::string body = R"({"jsonrpc":"2.0","id":1,"method":"dash.snapshot","params":{"sections":[]}})";
        const auto request = "POST /dash/rpc HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) +
            "\r\nContent-Type: application/json\r\nAccept: application/json\r\nConnection: close\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
        std::size_t sent{}; std::string response; std::array<char, 2048> buffer{};
        while (hal::Clock::SteadyTimestampNs() < deadline) {
            if (sent < request.size()) {
                sent += socket.write_some(boost::asio::buffer(request.data() + sent, request.size() - sent), error);
                if (error && !pending(error) && error != boost::asio::error::not_connected) return {false, "等待 Dashboard 服务启动。"};
            } else {
                const auto n = socket.read_some(boost::asio::buffer(buffer), error); response.append(buffer.data(), n);
                if (response.size() > 8192) return {false, "Dashboard 响应超出上限。"};
                if (error == boost::asio::error::eof) return DecodeDashboardProbe(response, pid);
                if (error && !pending(error)) return {false, "Dashboard 连接已断开。"};
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return {false, "Dashboard 服务未响应，稍后自动重试。"};
    } catch (const std::exception&) { return {false, "Dashboard 服务不可用。"}; }
}
}
