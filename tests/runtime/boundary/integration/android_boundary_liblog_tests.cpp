#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include "ogplay/hal/clock.h"
#include <future>
#include "ogplay/runtime/vfs/vfs.h"
#include <unordered_map>
#include <utility>
#include <vector>

#include "ogplay/cpu/interpreter.h"
#include "ogplay/cpu/dynarmic.h"
#include "ogplay/core/logger.h"
#include "ogplay/gles/egl_lifecycle.h"
#include "ogplay/gles/gles_dispatch.h"
#include "ogplay/gles/gles_transfer_state.h"
#include "ogplay/memory/bus.h"
#include "ogplay/loader/link_namespace.h"
#include "ogplay/runtime/boundary/android_boundary_hle.h"
#include "runtime/boundary/core/a32_call_frame.h"
#include "runtime/boundary/modules/gles1/gles1_dispatch.h"
#include "runtime/boundary/modules/gles1/gles1_draw.h"
#include "runtime/boundary/modules/gles1/gles1_query.h"
#include "runtime/boundary/modules/gles1/gles1_support.h"
#include "runtime/boundary/core/boundary_symbols.h"
#include "runtime/boundary/services/graphics_dispatch.h"
#include "runtime/boundary/modules/opensles/opensles_abi.h"

#include "android_boundary_fixture.h"

using namespace ogplay::test::android_boundary;

TEST_CASE("Android 4.4 liblog publishes its complete target export surface") {
    BoundaryFixture fixture;
    static constexpr std::array<std::string_view, 23> exports{
        "__android_log_dev_available", "__android_log_write",
        "__android_log_buf_write", "__android_log_vprint", "__android_log_print",
        "__android_log_buf_print", "__android_log_assert", "__android_log_bwrite",
        "__android_log_btwrite", "android_log_format_new",
        "android_log_format_free", "android_log_setPrintFormat",
        "android_log_formatFromString", "android_log_addFilterRule",
        "android_log_addFilterString", "android_log_shouldPrintLine",
        "android_log_processLogBuffer", "android_log_processBinaryLogBuffer",
        "android_log_formatLogLine", "android_log_printLogLine",
        "android_openEventTagMap", "android_closeEventTagMap",
        "android_lookupEventTag"};
    for (const auto symbol : exports) {
        CAPTURE(symbol);
        CHECK(fixture.boundary.Symbols().Lookup("liblog.so", symbol).has_value());
    }
}

TEST_CASE("Android liblog text and A32 variadic calls enter structured guest logs") {
    BoundaryFixture fixture;
    const auto tag = fixture.output;
    const auto message = fixture.output.Add(64U);
    const auto format = fixture.output.Add(128U);
    const auto text = fixture.output.Add(192U);
    WriteGuestString(fixture, tag, "PVZ");
    WriteGuestString(fixture, message, "loaded");
    WriteGuestString(fixture, format, "score=%d name=%s");
    WriteGuestString(fixture, text, "pea");

    CHECK(FastBoundaryCall(fixture, "liblog.so", "__android_log_write",
              {4U, tag.Value(), message.Value(), 0U}) > 0U);
    CHECK(FastBoundaryCall(fixture, "liblog.so", "__android_log_print",
              {3U, tag.Value(), format.Value(), 42U}) > 0U);
    auto records = fixture.logger.Snapshot(std::nullopt, "guest.liblog");
    REQUIRE(records.size() == 2U);
    CHECK(records[0].level == ogplay::core::LogLevel::info);
    CHECK(records[0].message == "[guest] PVZ: loaded");
    CHECK(records[1].level == ogplay::core::LogLevel::debug);
    CHECK(records[1].message == "[guest] PVZ: score=42 name=");

    const auto arguments = fixture.output.Add(256U);
    fixture.bus.Write32(arguments, 7U, 1U);
    fixture.bus.Write32(arguments.Add(4U), text.Value(), 1U);
    CHECK(FastBoundaryCall(fixture, "liblog.so", "__android_log_vprint",
              {5U, tag.Value(), format.Value(), arguments.Value()}) > 0U);
    records = fixture.logger.Snapshot(std::nullopt, "guest.liblog");
    REQUIRE(records.size() == 3U);
    CHECK(records.back().message == "[guest] PVZ: score=7 name=pea");
}

TEST_CASE("Android liblog format handles apply AOSP-style priority filters") {
    BoundaryFixture fixture;
    const auto rule = fixture.output;
    const auto tag = fixture.output.Add(64U);
    WriteGuestString(fixture, rule, "*:w");
    WriteGuestString(fixture, tag, "Guest");
    const auto format = fixture.Call("liblog.so", "android_log_format_new");
    REQUIRE(format != 0U);
    CHECK(fixture.Call("liblog.so", "android_log_addFilterRule",
              {format, rule.Value(), 0U, 0U}) == 0U);
    CHECK(fixture.Call("liblog.so", "android_log_shouldPrintLine",
              {format, tag.Value(), 4U, 0U}) == 0U);
    CHECK(fixture.Call("liblog.so", "android_log_shouldPrintLine",
              {format, tag.Value(), 5U, 0U}) == 1U);
    CHECK(fixture.Call("liblog.so", "android_log_format_free",
              {format, 0U, 0U, 0U}) == 0U);
}

TEST_CASE("Android liblog event tag maps use the injected guest filesystem") {
    BoundaryFixture fixture;
    const std::string map_text{"42 guest_answer (value|1)\n77 guest_state\n"};
    std::vector<std::byte> map_bytes(map_text.size());
    for (std::size_t index = 0; index < map_text.size(); ++index) {
        map_bytes[index] = static_cast<std::byte>(
            static_cast<unsigned char>(map_text[index]));
    }
    fixture.guest_files.emplace("/system/etc/event-log-tags", std::move(map_bytes));
    WriteGuestString(fixture, fixture.output, "/system/etc/event-log-tags");
    const auto map = fixture.Call("liblog.so", "android_openEventTagMap",
                                  {fixture.output.Value(), 0U, 0U, 0U});
    REQUIRE(map != 0U);
    const auto tag = fixture.Call("liblog.so", "android_lookupEventTag",
                                  {map, 42U, 0U, 0U});
    REQUIRE(tag != 0U);
    const auto tag_address = ogplay::memory::GuestAddress{tag};
    const auto length = fixture.memory.CStringLength(tag_address, 64U, 1U);
    std::vector<std::byte> encoded(length);
    fixture.memory.Read(tag_address, encoded, 1U);
    std::string decoded(length, '\0');
    for (std::size_t index = 0; index < length; ++index) {
        decoded[index] = static_cast<char>(std::to_integer<unsigned char>(encoded[index]));
    }
    CHECK(decoded == "guest_answer");
    fixture.Call("liblog.so", "android_closeEventTagMap", {map, 0U, 0U, 0U});
    CHECK(fixture.Call("liblog.so", "android_lookupEventTag",
                      {map, 42U, 0U, 0U}) == 0U);
}

TEST_CASE("Android liblog decodes KitKat binary event wire buffers") {
    BoundaryFixture fixture;
    const auto source = fixture.output.Add(512U);
    const auto entry = fixture.output.Add(640U);
    const auto message = fixture.output.Add(768U);
    fixture.bus.Write16(source, 9U, 1U);
    fixture.bus.Write32(source.Add(4U), 101U, 1U);
    fixture.bus.Write32(source.Add(8U), 202U, 1U);
    fixture.bus.Write32(source.Add(12U), 303U, 1U);
    fixture.bus.Write32(source.Add(16U), 404U, 1U);
    fixture.bus.Write32(source.Add(20U), 42U, 1U);
    fixture.memory.Write8(source.Add(24U), 0U, 1U);
    fixture.bus.Write32(source.Add(25U), 123U, 1U);
    fixture.bus.Write32(fixture.stack, 128U, 1U);
    CHECK(fixture.Call("liblog.so", "android_log_processBinaryLogBuffer",
              {source.Value(), entry.Value(), 0U, message.Value()}) == 0U);
    CHECK(fixture.bus.Read32(entry.Add(8U), 1U) == 4U);
    const auto tag = ogplay::memory::GuestAddress{
        fixture.bus.Read32(entry.Add(20U), 1U)};
    const auto text = ogplay::memory::GuestAddress{
        fixture.bus.Read32(entry.Add(28U), 1U)};
    CHECK(fixture.memory.CStringLength(tag, 16U, 1U) == 4U);
    CHECK(fixture.memory.CStringLength(text, 16U, 1U) == 3U);
}

TEST_CASE("Android liblog processes and formats KitKat text wire buffers") {
    BoundaryFixture fixture;
    const auto source = fixture.output.Add(512U);
    const auto entry = fixture.output.Add(640U);
    const auto line = fixture.output.Add(768U);
    const auto out_length = fixture.output.Add(1000U);
    const std::string payload{"\x04Guest\0hello\0", 13U};
    fixture.bus.Write16(source, static_cast<std::uint16_t>(payload.size()), 1U);
    fixture.bus.Write32(source.Add(4U), 11U, 1U);
    fixture.bus.Write32(source.Add(8U), 12U, 1U);
    fixture.bus.Write32(source.Add(12U), 13U, 1U);
    fixture.bus.Write32(source.Add(16U), 14U, 1U);
    std::vector<std::byte> encoded(payload.size());
    for (std::size_t index = 0; index < payload.size(); ++index) {
        encoded[index] = static_cast<std::byte>(
            static_cast<unsigned char>(payload[index]));
    }
    fixture.memory.Write(source.Add(20U), encoded, 1U);
    CHECK(fixture.Call("liblog.so", "android_log_processLogBuffer",
              {source.Value(), entry.Value(), 0U, 0U}) == 0U);
    CHECK(fixture.bus.Read32(entry.Add(8U), 1U) == 4U);
    CHECK(fixture.bus.Read32(entry.Add(24U), 1U) == 5U);

    const auto format = fixture.Call("liblog.so", "android_log_format_new");
    fixture.bus.Write32(fixture.stack, out_length.Value(), 1U);
    CHECK(fixture.Call("liblog.so", "android_log_formatLogLine",
              {format, line.Value(), 128U, entry.Value()}) == line.Value());
    CHECK(fixture.bus.Read32(out_length, 1U) > 5U);
    const auto line_length = fixture.memory.CStringLength(line, 128U, 1U);
    std::vector<std::byte> line_bytes(line_length);
    fixture.memory.Read(line, line_bytes, 1U);
    std::string rendered(line_length, '\0');
    for (std::size_t index = 0; index < line_length; ++index) {
        rendered[index] = static_cast<char>(
            std::to_integer<unsigned char>(line_bytes[index]));
    }
    CHECK(rendered.find("Guest") != std::string::npos);
    CHECK(rendered.find("hello") != std::string::npos);
}
