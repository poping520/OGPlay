#include "ogplay/agent/mcp_protocol.h"

#include <cstdint>
#include <string>

#include <doctest/doctest.h>

#if OGPLAY_TEST_HAS_SDL3
#include <SDL3/SDL.h>
#endif

namespace {

std::uint32_t BigEndian(const std::string& bytes, const std::size_t offset) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset])) << 24U |
           static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 1U])) << 16U |
           static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 2U])) << 8U |
           static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 3U]));
}

std::string DecodeBase64(const std::string_view input) {
    const auto value = [](const char character) -> std::uint32_t {
        if (character >= 'A' && character <= 'Z') return character - 'A';
        if (character >= 'a' && character <= 'z') return character - 'a' + 26U;
        if (character >= '0' && character <= '9') return character - '0' + 52U;
        if (character == '+') return 62U;
        if (character == '/') return 63U;
        return 0U;
    };
    std::string output;
    for (std::size_t offset = 0; offset < input.size(); offset += 4U) {
        const auto word = value(input[offset]) << 18U |
                          value(input[offset + 1U]) << 12U |
                          value(input[offset + 2U]) << 6U |
                          value(input[offset + 3U]);
        output += static_cast<char>(word >> 16U);
        if (input[offset + 2U] != '=') output += static_cast<char>(word >> 8U);
        if (input[offset + 3U] != '=') output += static_cast<char>(word);
    }
    return output;
}

std::string ImageData(const std::string& response) {
    const std::string marker{"\"data\":\""};
    const auto start = response.find(marker);
    REQUIRE(start != std::string::npos);
    const auto value_start = start + marker.size();
    const auto end = response.find('"', value_start);
    REQUIRE(end != std::string::npos);
    return response.substr(value_start, end - value_start);
}

}  // namespace

TEST_CASE("MCP frame store validates and exchanges latest ownership") {
    ogplay::agent::FrameSnapshotStore frames;
    CHECK_FALSE(frames.Latest().has_value());
    CHECK_THROWS_WITH_AS(
        static_cast<void>(frames.Publish({2U, 1U, 1U, {0U}})),
                         "frame RGBA8 layout does not match dimensions",
                         std::invalid_argument);

    CHECK_FALSE(frames.Publish({1U, 1U, 3U, {1U, 2U, 3U, 4U}}).has_value());
    const auto previous = frames.Publish(
        {1U, 1U, 4U, {5U, 6U, 7U, 8U}});
    REQUIRE(previous.has_value());
    CHECK(previous->sequence == 3U);
    const auto latest = frames.Take();
    REQUIRE(latest.has_value());
    CHECK(latest->sequence == 4U);
    CHECK_FALSE(frames.Take().has_value());
}

TEST_CASE("MCP protocol negotiates lifecycle and lists screenshot tool") {
    ogplay::agent::FrameSnapshotStore frames;
    ogplay::agent::McpProtocolAdapter mcp{frames, "test"};
    const auto initialized = mcp.Handle(
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"fixture","version":"1"}}})");
    REQUIRE(initialized.has_value());
    CHECK(initialized->find("\"protocolVersion\":\"2025-11-25\"") !=
          std::string::npos);
    CHECK(initialized->find("\"name\":\"ogplay\"") != std::string::npos);
    CHECK_FALSE(mcp.Handle(
        R"({"jsonrpc":"2.0","method":"notifications/initialized"})")
                    .has_value());

    const auto tools = mcp.Handle(
        R"({"jsonrpc":"2.0","id":"tools","method":"tools/list","params":{}})");
    REQUIRE(tools.has_value());
    CHECK(tools->find("\"name\":\"frame_capture\"") != std::string::npos);
    CHECK(tools->find("\"readOnlyHint\":true") != std::string::npos);
}

TEST_CASE("MCP screenshot tool fails closed without a frame") {
    ogplay::agent::FrameSnapshotStore frames;
    ogplay::agent::McpProtocolAdapter mcp{frames};
    const auto response = mcp.Handle(
        R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"frame_capture","arguments":{}}})");
    REQUIRE(response.has_value());
    CHECK(response->find("\"isError\":true") != std::string::npos);
    CHECK(response->find("No presented guest frame") != std::string::npos);
}

TEST_CASE("MCP screenshot tool returns a PNG image and exact frame metadata") {
    ogplay::agent::FrameSnapshotStore frames;
    static_cast<void>(frames.Publish(
        {2U, 1U, 7U, {255U, 0U, 0U, 255U, 0U, 255U, 0U, 255U}}));
    ogplay::agent::McpProtocolAdapter mcp{frames};
    const auto response = mcp.Handle(
        R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"frame_capture","arguments":{}}})");
    REQUIRE(response.has_value());
    CHECK(response->find("\"mimeType\":\"image/png\"") != std::string::npos);
    CHECK(response->find("\"sequence\":7") != std::string::npos);
    CHECK(response->find("\"width\":2") != std::string::npos);
    CHECK(response->find("\"height\":1") != std::string::npos);
    CHECK(response->find("\"isError\":false") != std::string::npos);

    const auto png = DecodeBase64(ImageData(*response));
    REQUIRE(png.size() > 33U);
    CHECK(png.substr(0U, 8U) == std::string("\x89PNG\r\n\x1a\n", 8U));
    CHECK(BigEndian(png, 16U) == 2U);
    CHECK(BigEndian(png, 20U) == 1U);
    CHECK(static_cast<unsigned char>(png[24U]) == 8U);
    CHECK(static_cast<unsigned char>(png[25U]) == 6U);
#if OGPLAY_TEST_HAS_SDL3
    auto* const io = SDL_IOFromConstMem(png.data(), png.size());
    REQUIRE(io != nullptr);
    auto* const surface = SDL_LoadPNG_IO(io, true);
    REQUIRE(surface != nullptr);
    CHECK(surface->w == 2);
    CHECK(surface->h == 1);
    SDL_DestroySurface(surface);
#endif
}

TEST_CASE("MCP protocol rejects unknown tools and malformed requests") {
    ogplay::agent::FrameSnapshotStore frames;
    ogplay::agent::McpProtocolAdapter mcp{frames};
    const auto unknown = mcp.Handle(
        R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"missing","arguments":{}}})");
    REQUIRE(unknown.has_value());
    CHECK(unknown->find("\"code\":-32602") != std::string::npos);
    CHECK(unknown->find("unknown MCP tool") != std::string::npos);
    const auto malformed = mcp.Handle("not-json");
    REQUIRE(malformed.has_value());
    CHECK(malformed->find("\"code\":-32602") != std::string::npos);
}
