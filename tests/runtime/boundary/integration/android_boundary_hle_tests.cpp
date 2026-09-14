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
#include "ogplay/runtime/boundary/android_boundary_hle.h"
#include "runtime/boundary/core/a32_call_frame.h"
#include "runtime/boundary/modules/gles1/gles1_dispatch.h"
#include "runtime/boundary/modules/gles1/gles1_draw.h"
#include "runtime/boundary/modules/gles1/gles1_query.h"
#include "runtime/boundary/modules/gles1/gles1_support.h"
#include "runtime/boundary/core/boundary_symbols.h"
#include "runtime/boundary/modules/opensles/opensles_abi.h"

namespace {

#if defined(_WIN32)
constexpr ogplay::gles::AngleRenderer kNativeRenderer =
    ogplay::gles::AngleRenderer::d3d11;
#elif defined(__APPLE__)
constexpr ogplay::gles::AngleRenderer kNativeRenderer =
    ogplay::gles::AngleRenderer::metal;
#else
constexpr ogplay::gles::AngleRenderer kNativeRenderer =
    ogplay::gles::AngleRenderer::vulkan;
#endif

class BoundaryFixture final {
public:
    explicit BoundaryFixture(
        const std::uint32_t supersample_factor = 1,
        std::vector<ogplay::runtime::OpenSlesGuestCallback>* callbacks = nullptr,
        const ogplay::runtime::BionicDynamicLinkHooks dynamic_link = {},
        const bool allow_single_stage_texcoord_fallback = true)
        : bus(memory), cpu(bus), boundary(memory,
              {kNativeRenderer,
               ogplay::gles::AngleDevice::hardware}, 4, 3,
              supersample_factor,
              {.allow_gles1_single_stage_texcoord_fallback =
                   allow_single_stage_texcoord_fallback,
               .logger = &logger,
               .guest_file_owner = &guest_files,
               .read_guest_file = +[](void* owner, const std::string_view path,
                                      std::vector<std::byte>& output) {
                   const auto& files = *static_cast<const std::unordered_map<
                       std::string, std::vector<std::byte>>*>(owner);
                   const auto found = files.find(std::string(path));
                   if (found == files.end()) return false;
                   output = found->second;
                   return true;
               },
               .dynamic_link = dynamic_link,
               .open_sles_callbacks = {
                   callbacks,
                   +[](void* owner,
                       const ogplay::runtime::OpenSlesGuestCallback& callback) {
                       if (owner != nullptr) {
                           static_cast<std::vector<
                               ogplay::runtime::OpenSlesGuestCallback>*>(owner)
                               ->push_back(callback);
                       }
                   }}}) {
        memory.Map({stack, memory.PageSize()},
                   ogplay::memory::PageProtection::read |
                       ogplay::memory::PageProtection::write);
        memory.Map({output, memory.PageSize()},
                   ogplay::memory::PageProtection::read |
                       ogplay::memory::PageProtection::write);
        boundary.MapThunks();
        ogplay::cpu::A32State state;
        state.SetThreadId(1);
        state.SetRegister(ogplay::cpu::CoreRegister::sp, stack.Value());
        cpu.SetState(state);
    }

    ogplay::runtime::SupervisorCallProgress CallProgress(
        const std::string_view library, const std::string_view symbol,
        const std::array<std::uint32_t, 4> arguments = {}) {
        const auto address = boundary.Symbols().Lookup(library, symbol);
        REQUIRE(address.has_value());
        auto state = cpu.GetState();
        for (std::size_t index = 0; index < arguments.size(); ++index) {
            state.SetRegister(static_cast<ogplay::cpu::CoreRegister>(index), arguments[index]);
        }
        cpu.SetState(state);
        const ogplay::cpu::RunResult stopped{
            1, ogplay::cpu::RunStopReason::supervisor_call,
            ogplay::memory::GuestAddress{address->Value() & ~1U}, 0xdf02U, 2, std::nullopt};
        const auto progress = boundary.HandleWithProgress(cpu, stopped);
        if (progress == ogplay::runtime::SupervisorCallProgress::not_handled) {
            throw std::runtime_error("Android boundary fixture call was not handled");
        }
        return progress;
    }

    std::uint32_t Call(const std::string_view library,
                       const std::string_view symbol,
                       const std::array<std::uint32_t, 4> arguments = {}) {
        static_cast<void>(CallProgress(library, symbol, arguments));
        return cpu.GetState().Register(ogplay::cpu::CoreRegister::r0);
    }

    ogplay::memory::AddressSpace memory;
    ogplay::memory::CheckedMemoryBus bus;
    ogplay::cpu::InterpreterCpu cpu;
    ogplay::core::Logger logger;
    std::unordered_map<std::string, std::vector<std::byte>> guest_files;
    ogplay::runtime::AndroidBoundaryHle boundary;
    const ogplay::memory::GuestAddress stack{0x6e100000U};
    const ogplay::memory::GuestAddress output{0x6e101000U};
};

void WriteGuestString(BoundaryFixture& fixture,
                      const ogplay::memory::GuestAddress address,
                      const std::string_view value,
                      const bool terminated = true) {
    std::vector<std::byte> bytes;
    bytes.reserve(value.size() + (terminated ? 1U : 0U));
    for (const auto character : value) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    if (terminated) bytes.push_back(std::byte{});
    fixture.memory.Write(address, bytes, 1);
}

std::uint32_t FastBoundaryCall(
    BoundaryFixture& fixture, const std::string_view library,
    const std::string_view symbol,
    const std::array<std::uint32_t, 4>& arguments,
    const std::uint64_t thread_id = 1U) {
    const auto address = fixture.boundary.Symbols().Lookup(library, symbol);
    REQUIRE(address.has_value());
    std::array<std::uint32_t, 16> registers{};
    std::copy(arguments.begin(), arguments.end(), registers.begin());
    registers[13] = fixture.stack.Value();
    ogplay::cpu::A32HostCallContext call{
        registers, thread_id,
        ogplay::memory::GuestAddress{address->Value() & ~UINT32_C(1)}};
    const auto hook = fixture.boundary.FastHostCallHook();
    REQUIRE(hook.invoke != nullptr);
    REQUIRE(hook.invoke(hook.userdata, 2U, call) ==
            ogplay::cpu::HostCallResult::handled);
    return registers[0];
}

std::uint32_t BoundaryCallAddress(
    BoundaryFixture& fixture, const std::uint32_t thumb_address,
    const std::span<const std::uint32_t> arguments) {
    REQUIRE(arguments.size() <= ogplay::runtime::kMaximumA32CallArguments);
    auto state = fixture.cpu.GetState();
    for (std::size_t index = 0;
         index < std::min(arguments.size(), std::size_t{4}); ++index) {
        state.SetRegister(static_cast<ogplay::cpu::CoreRegister>(index),
                          arguments[index]);
    }
    if (arguments.size() > 4U) {
        std::array<std::byte,
                   (ogplay::runtime::kMaximumA32CallArguments - 4U) * 4U> bytes{};
        for (std::size_t word = 4U; word < arguments.size(); ++word) {
            for (std::size_t byte = 0; byte < 4U; ++byte) {
                bytes[(word - 4U) * 4U + byte] =
                    static_cast<std::byte>(arguments[word] >> (byte * 8U));
            }
        }
        fixture.memory.Write(
            fixture.stack,
            std::span(bytes).first((arguments.size() - 4U) * 4U), 1U);
    }
    fixture.cpu.SetState(state);
    const ogplay::cpu::RunResult stopped{
        1U, ogplay::cpu::RunStopReason::supervisor_call,
        ogplay::memory::GuestAddress{thumb_address & ~UINT32_C(1)}, 0xdf02U,
        2U, std::nullopt};
    if (!fixture.boundary.Handle(fixture.cpu, stopped)) {
        throw std::runtime_error("boundary address call was not handled");
    }
    return fixture.cpu.GetState().Register(ogplay::cpu::CoreRegister::r0);
}

void PrepareNativeEgl(BoundaryFixture& fixture) {
    REQUIRE(fixture.Call("libEGL.so", "eglInitialize", {1U}) == 1U);
    REQUIRE(fixture.Call("libEGL.so", "eglCreateWindowSurface",
                         {1U, 2U, 1U, 0U}) == 3U);
    REQUIRE(fixture.Call("libEGL.so", "eglCreateContext",
                         {1U, 2U, 0U, 0U}) == 4U);
}

}  // namespace

TEST_CASE("Android boundary progress table is conservative by family") {
    using ogplay::runtime::SupervisorCallProgress;
    CHECK(ogplay::runtime::detail::ClassifyAndroidBoundaryProgress(
              "libEGL.so", "eglSwapBuffers", 1U) ==
          SupervisorCallProgress::handled_advanced);
    CHECK(ogplay::runtime::detail::ClassifyAndroidBoundaryProgress(
              "libEGL.so", "eglSwapBuffers", 0U) ==
          SupervisorCallProgress::handled_idle);
    CHECK(ogplay::runtime::detail::ClassifyAndroidBoundaryProgress(
              "libOpenSLES.so", "$BufferQueue.Enqueue", 0U) ==
          SupervisorCallProgress::handled_advanced);
    CHECK(ogplay::runtime::detail::ClassifyAndroidBoundaryProgress(
              "libOpenSLES.so", "$BufferQueue.GetState", 0U) ==
          SupervisorCallProgress::handled_idle);
    CHECK(ogplay::runtime::detail::ClassifyAndroidBoundaryProgress(
              "libGLESv2.so", "glDrawArrays", 0U) ==
          SupervisorCallProgress::handled_idle);
}

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

TEST_CASE("Android boundary maps explicit Thumb HLE thunks") {
    BoundaryFixture fixture;
    const auto address = fixture.boundary.Symbols().Lookup("libEGL.so", "eglGetDisplay");
    REQUIRE(address.has_value());
    CHECK(fixture.bus.Fetch16(ogplay::memory::GuestAddress{address->Value() & ~1U}) == 0xdf02U);
    auto state = fixture.cpu.GetState();
    state.SetState(ogplay::cpu::ExecutionState::thumb);
    state.SetRegister(ogplay::cpu::CoreRegister::pc, address->Value() & ~1U);
    fixture.cpu.SetState(state);
    const auto executed = fixture.cpu.Run(4);
    INFO("executed HLE pc=" << executed.pc.Value());
    REQUIRE(executed.reason == ogplay::cpu::RunStopReason::supervisor_call);
    REQUIRE(fixture.boundary.Handle(fixture.cpu, executed));
    CHECK(fixture.CallProgress("libEGL.so", "eglGetDisplay") ==
          ogplay::runtime::SupervisorCallProgress::handled_idle);
    CHECK(fixture.cpu.GetState().Register(ogplay::cpu::CoreRegister::r0) == 1);
    const ogplay::cpu::RunResult unknown{
        1, ogplay::cpu::RunStopReason::supervisor_call,
        ogplay::memory::GuestAddress{0x70000f00U}, 0xdf02U, 2, std::nullopt};
    CHECK_FALSE(fixture.boundary.Handle(fixture.cpu, unknown));
}

TEST_CASE("Android boundary fast host call continues inside Dynarmic run") {
    BoundaryFixture fixture;
    const ogplay::memory::GuestAddress return_trap{0x6e102000U};
    fixture.memory.Map(
        {return_trap, fixture.memory.PageSize()},
        ogplay::memory::PageProtection::read |
            ogplay::memory::PageProtection::write);
    fixture.bus.Write16(return_trap, 0xdf01U);
    fixture.memory.Protect(
        {return_trap, fixture.memory.PageSize()},
        ogplay::memory::PageProtection::read |
            ogplay::memory::PageProtection::execute);
    const auto address =
        fixture.boundary.Symbols().Lookup("libEGL.so", "eglGetDisplay");
    REQUIRE(address.has_value());

    ogplay::cpu::DynarmicCpu cpu(fixture.bus);
    cpu.SetHostCallHook(fixture.boundary.FastHostCallHook());
    ogplay::cpu::A32State state;
    state.SetState(ogplay::cpu::ExecutionState::thumb);
    state.SetThreadId(1U);
    state.SetRegister(ogplay::cpu::CoreRegister::pc,
                      address->Value() & ~UINT32_C(1));
    state.SetRegister(ogplay::cpu::CoreRegister::lr,
                      return_trap.Value() | UINT32_C(1));
    state.SetRegister(ogplay::cpu::CoreRegister::sp, fixture.stack.Value());
    cpu.SetState(state);

    const auto stopped = cpu.Run(16U);
    CHECK(stopped.reason == ogplay::cpu::RunStopReason::supervisor_call);
    CHECK(stopped.immediate == 1U);
    CHECK(cpu.GetState().Register(ogplay::cpu::CoreRegister::r0) == 1U);
}

TEST_CASE("Android boundary fast and slow failures preserve memory fault identity") {
    BoundaryFixture fixture;
    const auto address =
        fixture.boundary.Symbols().Lookup("libEGL.so", "eglInitialize");
    REQUIRE(address.has_value());

    std::string slow_message;
    try {
        static_cast<void>(fixture.Call(
            "libEGL.so", "eglInitialize", {1U, 0x12345000U, 0U, 0U}));
        FAIL("slow boundary call did not fault");
    } catch (const ogplay::memory::MemoryFault& error) {
        slow_message = error.what();
    }

    ogplay::cpu::DynarmicCpu cpu(fixture.bus);
    cpu.SetHostCallHook(fixture.boundary.FastHostCallHook());
    ogplay::cpu::A32State state;
    state.SetState(ogplay::cpu::ExecutionState::thumb);
    state.SetThreadId(1U);
    state.SetRegister(ogplay::cpu::CoreRegister::pc,
                      address->Value() & ~UINT32_C(1));
    state.SetRegister(ogplay::cpu::CoreRegister::r0, 1U);
    state.SetRegister(ogplay::cpu::CoreRegister::r1, 0x12345000U);
    state.SetRegister(ogplay::cpu::CoreRegister::sp, fixture.stack.Value());
    cpu.SetState(state);
    const auto stopped = cpu.Run(4U);
    REQUIRE(stopped.reason == ogplay::cpu::RunStopReason::host_call_fault);
    try {
        static_cast<void>(fixture.boundary.Handle(cpu, stopped));
        FAIL("fast boundary fault was not restored");
    } catch (const ogplay::memory::MemoryFault& error) {
        CHECK(std::string(error.what()) == slow_message);
        CHECK(error.Address() == ogplay::memory::GuestAddress{0x12345000U});
        CHECK(error.ThreadId() == 1U);
    }
}

TEST_CASE("Android EGL publishes and implements API 19 base query surface") {
    BoundaryFixture fixture;
    static constexpr std::array<std::string_view, 13> added{
        "eglGetError", "eglQueryString", "eglGetProcAddress", "eglGetConfigs",
        "eglGetCurrentContext", "eglGetCurrentSurface", "eglGetCurrentDisplay",
        "eglQueryContext", "eglBindAPI", "eglQueryAPI", "eglReleaseThread",
        "eglSwapInterval", "eglCreatePbufferSurface"};
    for (const auto symbol : added) {
        CAPTURE(symbol);
        CHECK(fixture.boundary.Symbols().Lookup("libEGL.so", symbol).has_value());
    }

    REQUIRE(fixture.Call("libEGL.so", "eglInitialize", {1U}) == 1U);
    REQUIRE(fixture.Call("libEGL.so", "eglCreateContext",
                         {1U, 2U, 0U, 0U}) == 4U);

    const auto count = fixture.output;
    const auto configs = fixture.output.Add(4U);
    CHECK(fixture.Call("libEGL.so", "eglGetConfigs",
                       {1U, configs.Value(), 1U, count.Value()}) == 1U);
    CHECK(fixture.bus.Read32(configs, 1U) == 2U);
    CHECK(fixture.bus.Read32(count, 1U) == 1U);

    const auto query = fixture.output.Add(32U);
    CHECK(fixture.Call("libEGL.so", "eglQueryContext",
                       {1U, 4U, 0x3098U, query.Value()}) == 1U);
    CHECK(fixture.bus.Read32(query, 1U) == 1U);
    CHECK(fixture.Call("libEGL.so", "eglQueryAPI") == 0x30A0U);
    CHECK(fixture.Call("libEGL.so", "eglGetCurrentContext") == 0U);
    CHECK(fixture.Call("libEGL.so", "eglGetCurrentDisplay") == 0U);
    CHECK(fixture.Call("libEGL.so", "eglQueryContext",
                       {99U, 4U, 0x3098U, query.Value()}) == 0U);
    CHECK(fixture.Call("libEGL.so", "eglGetError") == 0x3008U);
    CHECK(fixture.Call("libEGL.so", "eglGetConfigs",
                       {1U, 0U, 0U, 0U}) == 0U);
    CHECK(fixture.Call("libEGL.so", "eglGetError") == 0x300CU);
}

TEST_CASE("Android EGL publishes the complete EGL 1.4 core surface") {
    BoundaryFixture fixture;
    static constexpr std::array<std::string_view, 9> core{
        "eglCreatePixmapSurface", "eglCopyBuffers", "eglSurfaceAttrib",
        "eglBindTexImage", "eglReleaseTexImage", "eglWaitGL",
        "eglWaitNative", "eglWaitClient", "eglCreatePbufferFromClientBuffer"};
    for (const auto symbol : core) {
        CAPTURE(symbol);
        CHECK(fixture.boundary.Symbols().Lookup("libEGL.so", symbol).has_value());
    }
    REQUIRE(fixture.Call("libEGL.so", "eglInitialize", {1U}) == 1U);
    CHECK(fixture.Call("libEGL.so", "eglCreatePixmapSurface", {1U, 2U, 1U, 0U}) == 0U);
    CHECK(fixture.Call("libEGL.so", "eglGetError") == 0x300AU);
    const auto client_buffer_entry = fixture.boundary.Symbols().Lookup(
        "libEGL.so", "eglCreatePbufferFromClientBuffer");
    REQUIRE(client_buffer_entry.has_value());
    const std::array client_buffer_args{1U, 0x3096U, 1U, 2U, 0U};
    CHECK(BoundaryCallAddress(fixture, client_buffer_entry->Value(), client_buffer_args) == 0U);
    CHECK(fixture.Call("libEGL.so", "eglGetError") == 0x3009U);
    CHECK(fixture.Call("libEGL.so", "eglWaitNative", {0U}) == 0U);
    CHECK(fixture.Call("libEGL.so", "eglGetError") == 0x300CU);
    CHECK(fixture.Call("libEGL.so", "eglWaitNative", {0x305BU}) == 1U);

    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    const auto attributes = fixture.output;
    fixture.bus.Write32(attributes, 0x3057U, 1U);
    fixture.bus.Write32(attributes.Add(4U), 8U, 1U);
    fixture.bus.Write32(attributes.Add(8U), 0x3056U, 1U);
    fixture.bus.Write32(attributes.Add(12U), 8U, 1U);
    fixture.bus.Write32(attributes.Add(16U), 0x3038U, 1U);
    const auto surface = fixture.Call("libEGL.so", "eglCreatePbufferSurface",
                                      {1U, 2U, attributes.Value()});
    const auto context = fixture.Call("libEGL.so", "eglCreateContext", {1U, 2U, 0U, 0U});
    REQUIRE(surface != 0U);
    REQUIRE(context != 0U);
    REQUIRE(fixture.Call("libEGL.so", "eglMakeCurrent",
                         {1U, surface, surface, context}) == 1U);
    CHECK(fixture.Call("libEGL.so", "eglWaitGL") == 1U);
    CHECK(fixture.Call("libEGL.so", "eglWaitClient") == 1U);
    CHECK(fixture.Call("libEGL.so", "eglSurfaceAttrib",
                       {1U, surface, 0x3093U, 0x3095U}) == 1U);
    CHECK(fixture.Call("libEGL.so", "eglSurfaceAttrib",
                       {1U, surface, 0x3093U, 0x3094U}) == 0U);
    CHECK(fixture.Call("libEGL.so", "eglGetError") == 0x3009U);
    CHECK(fixture.Call("libEGL.so", "eglBindTexImage", {1U, surface, 0x3084U}) == 0U);
    CHECK(fixture.Call("libEGL.so", "eglGetError") == 0x3009U);
    CHECK(fixture.Call("libEGL.so", "eglReleaseTexImage", {1U, surface, 0U}) == 0U);
    CHECK(fixture.Call("libEGL.so", "eglGetError") == 0x300CU);
    CHECK(fixture.Call("libEGL.so", "eglCopyBuffers", {1U, surface, 1U}) == 0U);
    CHECK(fixture.Call("libEGL.so", "eglGetError") == 0x300AU);
}

TEST_CASE("Android EGL proc address resolves sealed public thunks") {
    BoundaryFixture fixture;
    const auto name = fixture.output;
    WriteGuestString(fixture, name, "glBindAttribLocation");
    const auto resolved = fixture.Call("libEGL.so", "eglGetProcAddress",
                                       {name.Value(), 0U, 0U, 0U});
    const auto direct = fixture.boundary.Symbols().Lookup(
        "libGLESv2.so", "glBindAttribLocation");
    REQUIRE(direct.has_value());
    CHECK(resolved != 0U);
    CHECK(resolved != direct->Value());

    WriteGuestString(fixture, name, "glDefinitelyUnavailableEXT");
    CHECK(fixture.Call("libEGL.so", "eglGetProcAddress",
                       {name.Value(), 0U, 0U, 0U}) == 0U);
}

TEST_CASE("Android EGL routes proc addresses by current context version") {
    BoundaryFixture fixture;
    REQUIRE(fixture.Call("libEGL.so", "eglInitialize", {1U}) == 1U);

    const auto pbuffer_attributes = fixture.output;
    fixture.bus.Write32(pbuffer_attributes, 0x3057U, 1U);
    fixture.bus.Write32(pbuffer_attributes.Add(4U), 16U, 1U);
    fixture.bus.Write32(pbuffer_attributes.Add(8U), 0x3056U, 1U);
    fixture.bus.Write32(pbuffer_attributes.Add(12U), 16U, 1U);
    fixture.bus.Write32(pbuffer_attributes.Add(16U), 0x3038U, 1U);
    const auto surface = fixture.Call(
        "libEGL.so", "eglCreatePbufferSurface",
        {1U, 2U, pbuffer_attributes.Value(), 0U});
    REQUIRE(surface == 3U);

    const auto es1 = fixture.Call("libEGL.so", "eglCreateContext",
                                  {1U, 2U, 0U, 0U});
    const auto context_attributes = fixture.output.Add(32U);
    fixture.bus.Write32(context_attributes, 0x3098U, 1U);
    fixture.bus.Write32(context_attributes.Add(4U), 2U, 1U);
    fixture.bus.Write32(context_attributes.Add(8U), 0x3038U, 1U);
    const auto es2 = fixture.Call(
        "libEGL.so", "eglCreateContext",
        {1U, 2U, 0U, context_attributes.Value()});
    REQUIRE(es1 == 4U);
    REQUIRE(es2 == 5U);

    const auto name = fixture.output.Add(64U);
    WriteGuestString(fixture, name, "glGetString");
    const auto cached = fixture.Call("libEGL.so", "eglGetProcAddress",
                                     {name.Value(), 0U, 0U, 0U});
    REQUIRE(cached != 0U);
    const std::array version_args{0x1F02U};
    CHECK(FastBoundaryCall(fixture, "libEGL.so", "eglGetProcAddress",
                           {name.Value(), 0U, 0U, 0U}, 2U) == cached);
    REQUIRE(fixture.Call("libEGL.so", "eglMakeCurrent",
                         {1U, surface, surface, es1}) == 1U);
    CHECK(fixture.Call("libEGL.so", "eglGetProcAddress",
                       {name.Value(), 0U, 0U, 0U}) == cached);
    CHECK(BoundaryCallAddress(fixture, cached, version_args) ==
          fixture.Call("libGLESv1_CM.so", "glGetString", {0x1F02U}));

    REQUIRE(fixture.Call("libEGL.so", "eglMakeCurrent",
                         {1U, surface, surface, es2}) == 1U);
    CHECK(fixture.Call("libEGL.so", "eglGetProcAddress",
                       {name.Value(), 0U, 0U, 0U}) == cached);
    CHECK(BoundaryCallAddress(fixture, cached, version_args) ==
          fixture.Call("libGLESv2.so", "glGetString", {0x1F02U}));

    REQUIRE(fixture.Call("libEGL.so", "eglMakeCurrent", {1U, 0U, 0U, 0U}) ==
            1U);
    CHECK(fixture.Call("libEGL.so", "eglGetProcAddress",
                       {name.Value(), 0U, 0U, 0U}) == cached);
    CHECK(BoundaryCallAddress(fixture, cached, version_args) == 0U);
}

TEST_CASE("Android EGL errors and API binding are isolated by guest thread") {
    BoundaryFixture fixture;
    CHECK(FastBoundaryCall(fixture, "libEGL.so", "eglBindAPI",
                           {0xDEADU, 0U, 0U, 0U}, 11U) == 0U);
    CHECK(FastBoundaryCall(fixture, "libEGL.so", "eglGetError", {}, 12U) ==
          0x3000U);
    CHECK(FastBoundaryCall(fixture, "libEGL.so", "eglGetError", {}, 11U) ==
          0x300CU);
    CHECK(FastBoundaryCall(fixture, "libEGL.so", "eglGetError", {}, 11U) ==
          0x3000U);
    CHECK(FastBoundaryCall(fixture, "libEGL.so", "eglBindAPI",
                           {0x30A0U, 0U, 0U, 0U}, 11U) == 1U);
    CHECK(FastBoundaryCall(fixture, "libEGL.so", "eglQueryAPI", {}, 11U) ==
          0x30A0U);
}

TEST_CASE("Android EGL query strings and pbuffer attributes use guest memory") {
    BoundaryFixture fixture;
    REQUIRE(fixture.Call("libEGL.so", "eglInitialize", {1U}) == 1U);
    const auto vendor = fixture.Call("libEGL.so", "eglQueryString",
                                     {1U, 0x3053U, 0U, 0U});
    REQUIRE(vendor != 0U);
    CHECK(fixture.memory.CStringLength(
              ogplay::memory::GuestAddress{vendor}, 64U, 1U) == 6U);

    const auto attributes = fixture.output;
    fixture.bus.Write32(attributes, 0x3057U, 1U);
    fixture.bus.Write32(attributes.Add(4U), 64U, 1U);
    fixture.bus.Write32(attributes.Add(8U), 0x3056U, 1U);
    fixture.bus.Write32(attributes.Add(12U), 32U, 1U);
    fixture.bus.Write32(attributes.Add(16U), 0x3038U, 1U);
    CHECK(fixture.Call("libEGL.so", "eglCreatePbufferSurface",
                       {1U, 2U, attributes.Value(), 0U}) == 3U);
    const auto size = fixture.output.Add(32U);
    CHECK(fixture.Call("libEGL.so", "eglQuerySurface",
                       {1U, 3U, 0x3057U, size.Value()}) == 1U);
    CHECK(fixture.bus.Read32(size, 1U) == 64U);
    CHECK(fixture.Call("libEGL.so", "eglQuerySurface",
                       {1U, 3U, 0x3056U, size.Value()}) == 1U);
    CHECK(fixture.bus.Read32(size, 1U) == 32U);
    const std::array surface_queries{
        std::pair{0x3028U, 1U}, std::pair{0x3080U, 0x305CU},
        std::pair{0x3081U, 0x305CU}, std::pair{0x3082U, 0U},
        std::pair{0x3083U, 0U}, std::pair{0x3086U, 0x3084U},
        std::pair{0x3093U, 0x3095U}, std::pair{0x3099U, 0x309AU}};
    for (const auto& [attribute, expected] : surface_queries) {
        REQUIRE(fixture.Call("libEGL.so", "eglQuerySurface",
                             {1U, 3U, attribute, size.Value()}) == 1U);
        CHECK(fixture.bus.Read32(size, 1U) == expected);
    }
    for (const auto attribute : {0x3027U, 0x302FU, 0x3034U}) {
        REQUIRE(fixture.Call("libEGL.so", "eglGetConfigAttrib",
                             {1U, 2U, attribute, size.Value()}) == 1U);
        CHECK(fixture.bus.Read32(size, 1U) == 0x3038U);
    }
    CHECK(fixture.Call("libEGL.so", "eglSwapInterval",
                       {1U, 0U, 0U, 0U}) == 0U);
    CHECK(fixture.Call("libEGL.so", "eglGetError") == 0x300DU);
}

TEST_CASE("Android EGL owns distinct objects and defers current destruction") {
    BoundaryFixture fixture;
    CHECK(fixture.Call("libEGL.so", "eglCreateContext", {1U, 2U}) == 0U);
    CHECK(fixture.Call("libEGL.so", "eglGetError") == 0x3001U);
    REQUIRE(fixture.Call("libEGL.so", "eglInitialize", {1U}) == 1U);

    const auto context_attributes = fixture.output;
    fixture.bus.Write32(context_attributes, 0x3098U, 1U);
    fixture.bus.Write32(context_attributes.Add(4U), 2U, 1U);
    fixture.bus.Write32(context_attributes.Add(8U), 0x3038U, 1U);
    const auto first_context = fixture.Call(
        "libEGL.so", "eglCreateContext",
        {1U, 2U, 0U, context_attributes.Value()});
    const auto second_context = fixture.Call(
        "libEGL.so", "eglCreateContext",
        {1U, 2U, first_context, context_attributes.Value()});
    CHECK(first_context == 4U);
    CHECK(second_context == 5U);

    const auto surface_attributes = fixture.output.Add(32U);
    fixture.bus.Write32(surface_attributes, 0x3057U, 1U);
    fixture.bus.Write32(surface_attributes.Add(4U), 7U, 1U);
    fixture.bus.Write32(surface_attributes.Add(8U), 0x3056U, 1U);
    fixture.bus.Write32(surface_attributes.Add(12U), 5U, 1U);
    fixture.bus.Write32(surface_attributes.Add(16U), 0x3038U, 1U);
    const auto first_surface = fixture.Call(
        "libEGL.so", "eglCreatePbufferSurface",
        {1U, 2U, surface_attributes.Value()});
    const auto second_surface = fixture.Call(
        "libEGL.so", "eglCreateWindowSurface", {1U, 2U, 1U, 0U});
    CHECK(first_surface == 3U);
    CHECK(second_surface == 4U);

    REQUIRE(fixture.Call("libEGL.so", "eglMakeCurrent",
                         {1U, first_surface, first_surface, first_context}) == 1U);
    CHECK(fixture.Call("libEGL.so", "eglDestroyContext",
                       {1U, first_context}) == 1U);
    CHECK(fixture.Call("libEGL.so", "eglDestroySurface",
                       {1U, first_surface}) == 1U);
    CHECK(fixture.Call("libEGL.so", "eglQueryContext",
                       {1U, first_context, 0x3098U,
                        fixture.output.Add(128U).Value()}) == 1U);
    REQUIRE(fixture.Call("libEGL.so", "eglMakeCurrent", {1U, 0U, 0U, 0U}) == 1U);
    CHECK(fixture.Call("libEGL.so", "eglQueryContext",
                       {1U, first_context, 0x3098U,
                        fixture.output.Add(128U).Value()}) == 0U);
    CHECK(fixture.Call("libEGL.so", "eglGetError") == 0x3006U);
}

TEST_CASE("Android EGL permits distinct host-thread contexts and safe handoff") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
    REQUIRE(fixture.Call("libEGL.so", "eglInitialize", {1U}) == 1U);
    const auto attributes = fixture.output;
    fixture.bus.Write32(attributes, 0x3057U, 1U);
    fixture.bus.Write32(attributes.Add(4U), 4U, 1U);
    fixture.bus.Write32(attributes.Add(8U), 0x3056U, 1U);
    fixture.bus.Write32(attributes.Add(12U), 4U, 1U);
    fixture.bus.Write32(attributes.Add(16U), 0x3038U, 1U);
    const auto surface_a = fixture.Call("libEGL.so", "eglCreatePbufferSurface",
                                        {1U, 2U, attributes.Value()});
    const auto surface_b = fixture.Call("libEGL.so", "eglCreatePbufferSurface",
                                        {1U, 2U, attributes.Value()});
    const auto context_a = fixture.Call("libEGL.so", "eglCreateContext",
                                        {1U, 2U, 0U, 0U});
    const auto context_attributes = fixture.output.Add(32U);
    fixture.bus.Write32(context_attributes, 0x3098U, 1U);
    fixture.bus.Write32(context_attributes.Add(4U), 2U, 1U);
    fixture.bus.Write32(context_attributes.Add(8U), 0x3038U, 1U);
    const auto context_b = fixture.Call("libEGL.so", "eglCreateContext",
                                        {1U, 2U, 0U,
                                         context_attributes.Value()});
    REQUIRE(surface_a != 0U);
    REQUIRE(surface_b != 0U);
    REQUIRE(context_a != 0U);
    REQUIRE(context_b != 0U);

    const auto address = [&](const std::string_view symbol) {
        const auto value = fixture.boundary.Symbols().Lookup("libEGL.so", symbol);
        REQUIRE(value.has_value());
        return value->Value();
    };
    const auto make_current = address("eglMakeCurrent");
    const auto get_current = address("eglGetCurrentContext");
    const auto get_error = address("eglGetError");
    const auto release_thread = address("eglReleaseThread");
    const auto proc_name = fixture.output.Add(64U);
    WriteGuestString(fixture, proc_name, "glGetString");
    const auto cached_proc = fixture.Call(
        "libEGL.so", "eglGetProcAddress", {proc_name.Value()});
    REQUIRE(cached_proc != 0U);
    const auto hook = fixture.boundary.FastHostCallHook();
    const auto invoke = [&](const std::uint32_t target,
                            const std::array<std::uint32_t, 4>& arguments,
                            const std::uint64_t tid) {
        std::array<std::uint32_t, 16> registers{};
        std::copy(arguments.begin(), arguments.end(), registers.begin());
        ogplay::cpu::A32HostCallContext call{
            registers, tid,
            ogplay::memory::GuestAddress{target & ~UINT32_C(1)}};
        const auto result = hook.invoke(hook.userdata, 2U, call);
        return std::pair{result, registers[0]};
    };

    std::atomic<bool> first_current{};
    std::atomic<bool> attempted_takeover{};
    std::atomic<bool> first_released{};
    std::array<std::uint32_t, 9> results{};
    std::thread first([&] {
        results[0] = invoke(make_current,
                            {1U, surface_a, surface_a, context_a}, 11U).second;
        results[7] = invoke(cached_proc, {0x1F02U}, 11U).second;
        first_current.store(true, std::memory_order_release);
        while (!attempted_takeover.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        results[1] = invoke(release_thread, {}, 11U).second;
        first_released.store(true, std::memory_order_release);
    });
    std::thread second([&] {
        while (!first_current.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        results[2] = invoke(make_current,
                            {1U, surface_b, surface_b, context_b}, 22U).second;
        results[8] = invoke(cached_proc, {0x1F02U}, 22U).second;
        results[3] = invoke(make_current,
                            {1U, surface_a, surface_a, context_a}, 22U).second;
        results[4] = invoke(get_error, {}, 22U).second;
        results[5] = invoke(get_current, {}, 22U).second;
        attempted_takeover.store(true, std::memory_order_release);
        while (!first_released.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        results[6] = invoke(make_current,
                            {1U, surface_a, surface_a, context_a}, 22U).second;
        static_cast<void>(invoke(release_thread, {}, 22U));
    });
    first.join();
    second.join();

    CHECK(results[0] == 1U);
    CHECK(results[1] == 1U);
    CHECK(results[2] == 1U);
    CHECK(results[3] == 0U);
    CHECK(results[4] == 0x3002U);
    CHECK(results[5] == context_b);
    CHECK(results[6] == 1U);
    CHECK(results[7] != 0U);
    CHECK(results[8] != 0U);
    CHECK(results[7] != results[8]);
}

TEST_CASE("Android EGL creates ES3 and routes vertex array core calls") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
    REQUIRE(fixture.Call("libEGL.so", "eglInitialize", {1U}) == 1U);

    const auto surface_attributes = fixture.output;
    fixture.bus.Write32(surface_attributes, 0x3057U, 1U);
    fixture.bus.Write32(surface_attributes.Add(4U), 4U, 1U);
    fixture.bus.Write32(surface_attributes.Add(8U), 0x3056U, 1U);
    fixture.bus.Write32(surface_attributes.Add(12U), 4U, 1U);
    fixture.bus.Write32(surface_attributes.Add(16U), 0x3038U, 1U);
    const auto surface = fixture.Call("libEGL.so", "eglCreatePbufferSurface",
                                      {1U, 2U, surface_attributes.Value()});

    const auto context_attributes = fixture.output.Add(32U);
    fixture.bus.Write32(context_attributes, 0x3098U, 1U);
    fixture.bus.Write32(context_attributes.Add(4U), 3U, 1U);
    fixture.bus.Write32(context_attributes.Add(8U), 0x3038U, 1U);
    const auto context = fixture.Call("libEGL.so", "eglCreateContext",
                                      {1U, 2U, 0U,
                                       context_attributes.Value()});
    REQUIRE(surface != 0U);
    REQUIRE(context != 0U);
    REQUIRE(fixture.Call("libEGL.so", "eglMakeCurrent",
                         {1U, surface, surface, context}) == 1U);

    const auto name = fixture.output.Add(64U);
    CHECK(fixture.Call("libGLESv2.so", "glGenVertexArrays",
                       {1U, name.Value()}) == 0U);
    const auto vao = fixture.bus.Read32(name, 1U);
    CHECK(vao != 0U);
    CHECK(fixture.Call("libGLESv2.so", "glIsVertexArray", {vao}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glBindVertexArray", {vao}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glIsVertexArray", {vao}) == 1U);
    CHECK(fixture.Call("libGLESv2.so", "glDeleteVertexArrays",
                       {1U, name.Value()}) == 0U);
    const auto integer64 = fixture.output.Add(72U);
    CHECK(fixture.Call("libGLESv2.so", "glGetInteger64v",
                       {0x8D6BU, integer64.Value()}) == 0U);
    const bool integer64_nonzero = fixture.bus.Read32(integer64, 1U) != 0U ||
                                   fixture.bus.Read32(integer64.Add(4U), 1U) != 0U;
    CHECK(integer64_nonzero);
    const auto extension = fixture.Call("libGLESv2.so", "glGetStringi",
                                        {0x1F03U, 0U});
    CHECK(extension != 0U);
    CHECK(fixture.bus.Read8(ogplay::memory::GuestAddress{extension}, 1U) != 0U);
    const auto sync = fixture.Call("libGLESv2.so", "glFenceSync",
                                   {0x9117U, 0U});
    REQUIRE(sync != 0U);
    CHECK(fixture.Call("libGLESv2.so", "glIsSync", {sync}) == 1U);
    const auto wait = fixture.Call("libGLESv2.so", "glClientWaitSync",
                                   {sync, 0U, 0U, 0U});
    const bool valid_wait = wait == 0x911AU || wait == 0x911BU ||
                            wait == 0x911CU;
    CHECK(valid_wait);
    CHECK(fixture.Call("libGLESv2.so", "glDeleteSync", {sync}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glIsSync", {sync}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetError") == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetError") == 0U);

    const auto buffer_name = fixture.output.Add(96U);
    REQUIRE(fixture.Call("libGLESv2.so", "glGenBuffers",
                         {1U, buffer_name.Value()}) == 0U);
    const auto buffer = fixture.bus.Read32(buffer_name, 1U);
    REQUIRE(buffer != 0U);
    CHECK(fixture.Call("libGLESv2.so", "glBindBuffer",
                       {0x8892U, buffer}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glBufferData",
                       {0x8892U, 16U, 0U, 0x88E8U}) == 0U);
    fixture.bus.Write32(fixture.stack, 0U, 1U);
    fixture.bus.Write32(fixture.stack.Add(4U), 0U, 1U);
    for (const auto type : {0x140BU, 0x1404U, 0x1405U, 0x8D9FU, 0x8368U}) {
        CAPTURE(type);
        CHECK(fixture.Call("libGLESv2.so", "glVertexAttribPointer",
                           {0U, 4U, type, 0U}) == 0U);
        CHECK(fixture.Call("libGLESv2.so", "glGetError") == 0U);
    }
    const auto mapped = fixture.Call("libGLESv2.so", "glMapBufferRange",
                                     {0x8892U, 0U, 16U, 0x0002U});
    REQUIRE(mapped >= 0x78000000U);
    fixture.bus.Write32(ogplay::memory::GuestAddress{mapped}, 0x12345678U, 1U);
    const auto pointer_out = fixture.output.Add(112U);
    CHECK(fixture.Call("libGLESv2.so", "glGetBufferPointerv",
                       {0x8892U, 0x88BDU, pointer_out.Value()}) == 0U);
    CHECK(fixture.bus.Read32(pointer_out, 1U) == mapped);
    CHECK(fixture.Call("libGLESv2.so", "glUnmapBuffer", {0x8892U}) == 1U);
    CHECK(fixture.Call("libGLESv2.so", "glGetError") == 0U);
    const auto remapped = fixture.Call("libGLESv2.so", "glMapBufferRange",
                                       {0x8892U, 0U, 16U, 0x0001U});
    REQUIRE(remapped >= 0x78000000U);
    CHECK(fixture.bus.Read32(ogplay::memory::GuestAddress{remapped}, 1U) ==
          0x12345678U);
    CHECK(fixture.Call("libGLESv2.so", "glUnmapBuffer", {0x8892U}) == 1U);
    CHECK(fixture.Call("libGLESv2.so", "glGetError") == 0U);

    const auto texture_name = fixture.output.Add(128U);
    REQUIRE(fixture.Call("libGLESv2.so", "glGenTextures",
                         {1U, texture_name.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetError") == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glBindTexture",
                       {0x806FU, fixture.bus.Read32(texture_name, 1U)}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetError") == 0U);
    const auto pixels = fixture.output.Add(144U);
    for (std::uint32_t index = 0; index < 8U; ++index)
        fixture.bus.Write8(pixels.Add(index), static_cast<std::uint8_t>(index), 1U);
    fixture.bus.Write32(fixture.stack, 1U, 1U);
    fixture.bus.Write32(fixture.stack.Add(4U), 2U, 1U);
    fixture.bus.Write32(fixture.stack.Add(8U), 0U, 1U);
    fixture.bus.Write32(fixture.stack.Add(12U), 0x1908U, 1U);
    fixture.bus.Write32(fixture.stack.Add(16U), 0x1401U, 1U);
    fixture.bus.Write32(fixture.stack.Add(20U), pixels.Value(), 1U);
    CHECK(fixture.Call("libGLESv2.so", "glTexImage3D",
                       {0x806FU, 0U, 0x8058U, 1U}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetError") == 0U);
}

TEST_CASE("Android EGL terminate retires current objects before reinitialize") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
    REQUIRE(fixture.Call("libEGL.so", "eglInitialize", {1U}) == 1U);
    const auto attributes = fixture.output;
    fixture.bus.Write32(attributes, 0x3057U, 1U);
    fixture.bus.Write32(attributes.Add(4U), 3U, 1U);
    fixture.bus.Write32(attributes.Add(8U), 0x3056U, 1U);
    fixture.bus.Write32(attributes.Add(12U), 3U, 1U);
    fixture.bus.Write32(attributes.Add(16U), 0x3038U, 1U);
    const auto old_surface = fixture.Call(
        "libEGL.so", "eglCreatePbufferSurface",
        {1U, 2U, attributes.Value()});
    const auto old_context = fixture.Call("libEGL.so", "eglCreateContext",
                                          {1U, 2U, 0U, 0U});
    REQUIRE(old_surface != 0U);
    REQUIRE(old_context != 0U);

    const auto make_address = fixture.boundary.Symbols().Lookup(
        "libEGL.so", "eglMakeCurrent");
    const auto release_address = fixture.boundary.Symbols().Lookup(
        "libEGL.so", "eglReleaseThread");
    REQUIRE(make_address.has_value());
    REQUIRE(release_address.has_value());
    const auto hook = fixture.boundary.FastHostCallHook();
    std::atomic<bool> current{};
    std::atomic<bool> terminated{};
    std::uint32_t bind_result{};
    std::uint32_t release_result{};
    std::thread worker([&] {
        const auto invoke = [&](const std::uint32_t target,
                                const std::array<std::uint32_t, 4>& arguments) {
            std::array<std::uint32_t, 16> registers{};
            std::copy(arguments.begin(), arguments.end(), registers.begin());
            ogplay::cpu::A32HostCallContext call{
                registers, 33U,
                ogplay::memory::GuestAddress{target & ~UINT32_C(1)}};
            const auto status = hook.invoke(hook.userdata, 2U, call);
            return status == ogplay::cpu::HostCallResult::handled
                       ? registers[0]
                       : UINT32_MAX;
        };
        bind_result = invoke(make_address->Value(),
                             {1U, old_surface, old_surface, old_context});
        current.store(true, std::memory_order_release);
        while (!terminated.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        release_result = invoke(release_address->Value(), {});
    });
    while (!current.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    CHECK(fixture.Call("libEGL.so", "eglTerminate", {1U}) == 1U);
    terminated.store(true, std::memory_order_release);
    worker.join();
    CHECK(bind_result == 1U);
    CHECK(release_result == 1U);

    REQUIRE(fixture.Call("libEGL.so", "eglInitialize", {1U}) == 1U);
    CHECK(fixture.Call("libEGL.so", "eglQueryContext",
                       {1U, old_context, 0x3098U,
                        fixture.output.Add(64U).Value()}) == 0U);
    CHECK(fixture.Call("libEGL.so", "eglGetError") == 0x3006U);
    const auto new_surface = fixture.Call(
        "libEGL.so", "eglCreatePbufferSurface",
        {1U, 2U, attributes.Value()});
    const auto new_context = fixture.Call("libEGL.so", "eglCreateContext",
                                          {1U, 2U, 0U, 0U});
    CHECK(new_surface != old_surface);
    CHECK(new_context != old_context);
    CHECK(fixture.Call("libEGL.so", "eglMakeCurrent",
                       {1U, new_surface, new_surface, new_context}) == 1U);
    CHECK(fixture.Call("libEGL.so", "eglReleaseThread") == 1U);
}

TEST_CASE("libc overrides use equivalent export-specific fast and slow bindings") {
    BoundaryFixture slow;
    BoundaryFixture fast;
    const std::array initial{std::byte{'a'}, std::byte{'b'}, std::byte{'c'},
                             std::byte{}};
    slow.memory.Write(slow.output, initial, 1U);
    fast.memory.Write(fast.output, initial, 1U);

    const auto execute = [](BoundaryFixture& fixture, const bool use_fast) {
        const auto call = [&](const std::string_view symbol,
                              const std::array<std::uint32_t, 4>& arguments) {
            return use_fast
                       ? FastBoundaryCall(fixture, "libc.so", symbol, arguments)
                       : fixture.Call("libc.so", symbol, arguments);
        };
        std::array<std::uint32_t, 5> results{};
        results[0] = call("memcpy", {fixture.output.Add(64).Value(),
                                     fixture.output.Value(), 4U, 0U});
        results[1] = call("memmove", {fixture.output.Add(1).Value(),
                                      fixture.output.Value(), 3U, 0U});
        results[2] = call("memset", {fixture.output.Add(72).Value(), 'x', 2U, 0U});
        results[3] = call("memcmp", {fixture.output.Value(),
                                     fixture.output.Add(64).Value(), 4U, 0U});
        results[4] = call("strlen", {fixture.output.Add(64).Value(), 0U, 0U, 0U});
        std::array<std::byte, 80> bytes{};
        fixture.memory.Read(fixture.output, bytes, 1U);
        return std::pair{results, bytes};
    };

    const auto slow_result = execute(slow, false);
    const auto fast_result = execute(fast, true);
    CHECK(fast_result == slow_result);
    CHECK(fast_result.first[4] == 3U);
}

TEST_CASE("libdl overrides bridge process lookup and consume per-thread errors") {
    struct Probe final {
        std::string library;
        std::string symbol;
        std::uint32_t flags{};
        std::uint64_t thread{};
        bool fail_open{};
    } probe;
    const ogplay::runtime::BionicDynamicLinkHooks hooks{
        &probe,
        +[](void* owner, const std::string_view library,
            const std::uint32_t flags, const std::uint64_t thread) {
            auto& state = *static_cast<Probe*>(owner);
            if (state.fail_open) throw std::runtime_error("library unavailable");
            state.library = library;
            state.flags = flags;
            state.thread = thread;
            return 0x80000001U;
        },
        +[](void* owner, const std::uint32_t handle,
            const std::string_view symbol, const std::uint64_t thread) {
            auto& state = *static_cast<Probe*>(owner);
            if (handle != 0x80000001U) {
                throw std::runtime_error("invalid test handle");
            }
            state.symbol = symbol;
            state.thread = thread;
            return 0x70000101U;
        },
        +[](void*, const std::uint32_t handle, const std::uint64_t) {
            if (handle != 0x80000001U) {
                throw std::runtime_error("invalid test handle");
            }
            return 0;
        }};
    BoundaryFixture fixture(1U, nullptr, hooks);
    WriteGuestString(fixture, fixture.output, "/system/lib/libGLESv1_CM.so");
    WriteGuestString(fixture, fixture.output.Add(64U), "glVertexPointer");

    const auto handle = fixture.Call(
        "libdl.so", "dlopen", {fixture.output.Value(), 2U, 0U, 0U});
    CHECK(handle == 0x80000001U);
    CHECK(probe.library == "/system/lib/libGLESv1_CM.so");
    CHECK(probe.flags == 2U);
    CHECK(fixture.Call("libdl.so", "dlsym",
                       {handle, fixture.output.Add(64U).Value(), 0U, 0U}) ==
          0x70000101U);
    CHECK(probe.symbol == "glVertexPointer");
    CHECK(fixture.Call("libdl.so", "dlclose", {handle, 0U, 0U, 0U}) == 0U);

    probe.fail_open = true;
    CHECK(fixture.Call("libdl.so", "dlopen",
                       {fixture.output.Value(), 2U, 0U, 0U}) == 0U);
    const auto error = fixture.Call("libdl.so", "dlerror");
    REQUIRE(error != 0U);
    const auto length = fixture.memory.CStringLength(
        ogplay::memory::GuestAddress{error}, 64U, 1U);
    std::string text(length, '\0');
    fixture.memory.Read(ogplay::memory::GuestAddress{error},
                        std::as_writable_bytes(std::span(text)), 1U);
    CHECK(text == "library unavailable");
    CHECK(fixture.Call("libdl.so", "dlerror") == 0U);
}

TEST_CASE("libc override direct bindings are safe across guest threads") {
    BoundaryFixture fixture;
    const std::array source{std::byte{'t'}, std::byte{'h'}, std::byte{'r'},
                            std::byte{'e'}, std::byte{'a'}, std::byte{'d'},
                            std::byte{}};
    fixture.memory.Write(fixture.output, source, 1U);
    fixture.memory.Write(fixture.output.Add(768), source, 1U);
    const auto hook = fixture.boundary.FastHostCallHook();
    const auto address_for = [&](const std::string_view symbol) {
        const auto address = fixture.boundary.Symbols().Lookup("libc.so", symbol);
        REQUIRE(address.has_value());
        return ogplay::memory::GuestAddress{address->Value() & ~UINT32_C(1)};
    };
    const auto memcpy_pc = address_for("memcpy");
    const auto memset_pc = address_for("memset");
    const auto strlen_pc = address_for("strlen");
    std::atomic<bool> start{};
    std::atomic<bool> correct{true};
    constexpr std::size_t iterations = 2'000U;
    const auto run = [&](const std::uint64_t thread_id,
                         const ogplay::memory::GuestAddress pc,
                         const std::array<std::uint32_t, 4> arguments,
                         const std::uint32_t expected) {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
            std::array<std::uint32_t, 16> registers{};
            std::copy(arguments.begin(), arguments.end(), registers.begin());
            registers[13] = fixture.stack.Value();
            ogplay::cpu::A32HostCallContext call{registers, thread_id, pc};
            if (hook.invoke(hook.userdata, 2U, call) !=
                    ogplay::cpu::HostCallResult::handled ||
                registers[0] != expected) {
                correct.store(false, std::memory_order_relaxed);
                return;
            }
        }
    };
    std::thread copy(run, 11U, memcpy_pc,
                     std::array<std::uint32_t, 4>{fixture.output.Add(256).Value(),
                                                  fixture.output.Value(), 7U, 0U},
                     fixture.output.Add(256).Value());
    std::thread fill(run, 12U, memset_pc,
                     std::array<std::uint32_t, 4>{fixture.output.Add(512).Value(),
                                                  'z', 7U, 0U},
                     fixture.output.Add(512).Value());
    std::thread length(run, 13U, strlen_pc,
                       std::array<std::uint32_t, 4>{fixture.output.Add(768).Value(),
                                                    0U, 0U, 0U},
                       6U);
    start.store(true, std::memory_order_release);
    copy.join();
    fill.join();
    length.join();
    CHECK(correct.load(std::memory_order_relaxed));

    std::array<std::byte, 7> copied{};
    std::array<std::byte, 7> filled{};
    fixture.memory.Read(fixture.output.Add(256), copied, 11U);
    fixture.memory.Read(fixture.output.Add(512), filled, 12U);
    CHECK(copied == source);
    CHECK(filled == std::array{std::byte{'z'}, std::byte{'z'}, std::byte{'z'},
                               std::byte{'z'}, std::byte{'z'}, std::byte{'z'},
                               std::byte{'z'}});
}

TEST_CASE("Android boundary decodes dense HLE thunks in constant time") {
    const auto symbols =
        ogplay::runtime::detail::BuildAndroidBoundarySymbols();
    const auto descriptors =
        ogplay::runtime::detail::BuildAndroidBoundaryDescriptors(symbols);
    REQUIRE(descriptors.size() != symbols.size());
    const auto address = symbols.front().address.Value();
    CHECK(ogplay::runtime::detail::DecodeAndroidBoundaryThunk(
              address, descriptors) == &descriptors.front());
    CHECK(ogplay::runtime::detail::DecodeAndroidBoundaryThunk(
              address & ~1U, descriptors) == &descriptors.front());
    CHECK(ogplay::runtime::detail::DecodeAndroidBoundaryThunk(
              (address & ~1U) + 2U, descriptors) == nullptr);
    CHECK(ogplay::runtime::detail::DecodeAndroidBoundaryThunk(
              0x6FFFFFFEU, descriptors) == nullptr);

    ogplay::runtime::BionicHleSymbolProvider provider(symbols);
    constexpr std::size_t iterations = 1'000'000U;
    std::size_t decoded{};
    const auto decode_begin = std::chrono::steady_clock::now();
    for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
        decoded += ogplay::runtime::detail::DecodeAndroidBoundaryThunk(
                       address, descriptors) != nullptr
                       ? 1U
                       : 0U;
    }
    const auto decode_elapsed = std::chrono::steady_clock::now() - decode_begin;
    std::size_t resolved{};
    const auto resolve_begin = std::chrono::steady_clock::now();
    for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
        resolved += provider.Resolve(address).has_value() ? 1U : 0U;
    }
    const auto resolve_elapsed = std::chrono::steady_clock::now() - resolve_begin;
    INFO("decode_us=" << std::chrono::duration_cast<std::chrono::microseconds>(
                              decode_elapsed).count()
                       << " resolve_us="
                       << std::chrono::duration_cast<std::chrono::microseconds>(
                              resolve_elapsed).count());
    CHECK(decoded == iterations);
    CHECK(resolved == iterations);
}

TEST_CASE("Virtual SO end-to-end host call benchmark records ABI shapes") {
    BoundaryFixture fixture;
    const ogplay::memory::GuestAddress return_trap{0x6e102000U};
    fixture.memory.Map(
        {return_trap, fixture.memory.PageSize()},
        ogplay::memory::PageProtection::read |
            ogplay::memory::PageProtection::write);
    fixture.bus.Write16(return_trap, 0xdf01U);
    fixture.memory.Protect(
        {return_trap, fixture.memory.PageSize()},
        ogplay::memory::PageProtection::read |
            ogplay::memory::PageProtection::execute);

    const auto run_fast = [&](const std::string_view library,
                              const std::string_view symbol,
                              const std::array<std::uint32_t, 4> registers,
                              const std::span<const std::uint32_t> stack_words,
                              const std::size_t iterations) {
        const auto address = fixture.boundary.Symbols().Lookup(library, symbol);
        REQUIRE(address.has_value());
        std::array<std::byte, 20> stack_bytes{};
        for (std::size_t word = 0; word < stack_words.size(); ++word) {
            for (std::size_t byte = 0; byte < 4U; ++byte) {
                stack_bytes[word * 4U + byte] = static_cast<std::byte>(
                    stack_words[word] >> (byte * 8U));
            }
        }
        if (!stack_words.empty()) {
            fixture.memory.Write(
                fixture.stack,
                std::span(stack_bytes).first(stack_words.size() * 4U), 1U);
        }
        ogplay::cpu::DynarmicCpu cpu(fixture.bus);
        cpu.SetHostCallHook(fixture.boundary.FastHostCallHook());
        ogplay::cpu::A32State state;
        state.SetState(ogplay::cpu::ExecutionState::thumb);
        state.SetThreadId(1U);
        state.SetRegister(ogplay::cpu::CoreRegister::pc,
                          address->Value() & ~UINT32_C(1));
        state.SetRegister(ogplay::cpu::CoreRegister::lr,
                          return_trap.Value() | UINT32_C(1));
        state.SetRegister(ogplay::cpu::CoreRegister::sp,
                          fixture.stack.Value());
        for (std::size_t index = 0; index < registers.size(); ++index) {
            state.SetRegister(static_cast<ogplay::cpu::CoreRegister>(index),
                              registers[index]);
        }
        const auto begin = std::chrono::steady_clock::now();
        for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
            cpu.SetState(state);
            const auto stopped = cpu.Run(16U);
            REQUIRE(stopped.reason ==
                    ogplay::cpu::RunStopReason::supervisor_call);
            REQUIRE(stopped.immediate == 1U);
        }
        return std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::steady_clock::now() - begin).count();
    };

    constexpr std::size_t host_iterations = 2'000U;
    const auto zero_us = run_fast("libandroid.so", "AConfiguration_new",
                                  {}, {}, host_iterations);
    const auto four_us = run_fast(
        "libandroid.so", "ANativeWindow_setBuffersGeometry",
        {1U, 2U, 3U, 4U}, {}, host_iterations);
    const std::array stack_words{5U, 6U};
    const auto stack_us = run_fast(
        "libandroid.so", "ALooper_addFd", {1U, 2U, 3U, 4U},
        stack_words, host_iterations);

    fixture.boundary.OpenManagedSurface();
    const auto gles_us = run_fast(
        "libGLESv2.so", "glClearColor",
        {std::bit_cast<std::uint32_t>(0.1F),
         std::bit_cast<std::uint32_t>(0.2F),
         std::bit_cast<std::uint32_t>(0.3F),
         std::bit_cast<std::uint32_t>(1.0F)}, {}, 500U);
    fixture.boundary.CloseManagedSurface();

    const auto slow_begin = std::chrono::steady_clock::now();
    for (std::size_t iteration = 0; iteration < host_iterations; ++iteration) {
        CHECK(fixture.Call("libandroid.so", "AConfiguration_new") ==
              0x6e003000U);
    }
    const auto slow_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - slow_begin).count();
    INFO("host_call_benchmark_us iterations=" << host_iterations
         << " zero=" << zero_us << " four_word=" << four_us
         << " stack_6word=" << stack_us << " gles_500=" << gles_us
         << " forced_slow=" << slow_us);
    CHECK(zero_us >= 0);
    CHECK(four_us >= 0);
    CHECK(stack_us >= 0);
    CHECK(gles_us >= 0);
    CHECK(slow_us >= 0);
}

TEST_CASE("Android boundary descriptors carry module-local ids") {
    const auto symbols =
        ogplay::runtime::detail::BuildAndroidBoundarySymbols();
    const auto descriptors =
        ogplay::runtime::detail::BuildAndroidBoundaryDescriptors(symbols);
    const auto descriptor_for = [&](const std::string_view library,
                                    const std::string_view name) {
        for (const auto& descriptor : descriptors) {
            if (descriptor.library == library && descriptor.name == name) {
                return &descriptor;
            }
        }
        return static_cast<const ogplay::runtime::detail::HleThunkDescriptor*>(
            nullptr);
    };

    const auto* android = descriptor_for("libandroid.so", "ALooper_prepare");
    REQUIRE(android != nullptr);
    CHECK(android->local_id == 5U);

    const auto* gles2 = descriptor_for("libGLESv2.so", "glDrawElements");
    REQUIRE(gles2 != nullptr);
    CHECK(gles2->local_id == 41U);

    const auto* gles1 = descriptor_for("libGLESv1_CM.so", "glDrawElements");
    REQUIRE(gles1 != nullptr);
    CHECK(gles1->local_id == 36U);
    CHECK(gles1->parameter_count == 4U);

    const auto* bounds = descriptor_for(
        "libGLESv1_CM.so", "glColorPointerBounds");
    REQUIRE(bounds != nullptr);
    CHECK(bounds->local_id == 148U);
    const auto* get_mapped = descriptor_for(
        "libGLESv1_CM.so", "glGetBufferPointervOES");
    REQUIRE(get_mapped != nullptr);
    CHECK(get_mapped->local_id == 155U);
    const auto* map = descriptor_for("libGLESv1_CM.so", "glMapBufferOES");
    REQUIRE(map != nullptr);
    CHECK(map->local_id == 156U);
    const auto* unmap = descriptor_for(
        "libGLESv1_CM.so", "glUnmapBufferOES");
    REQUIRE(unmap != nullptr);
    CHECK(unmap->local_id == 157U);

    const auto* tex_image = descriptor_for("libGLESv2.so", "glTexImage2D");
    REQUIRE(tex_image != nullptr);
    CHECK(tex_image->parameter_count == 9U);

    const auto* add_fd = descriptor_for("libandroid.so", "ALooper_addFd");
    REQUIRE(add_fd != nullptr);
    CHECK(add_fd->parameter_count == 6U);
}

TEST_CASE("OpenSL ES AOSP IID globals map exact read-only guest ABI") {
    BoundaryFixture fixture;
    const auto variable =
        fixture.boundary.Symbols().Lookup("libOpenSLES.so", "SL_IID_OBJECT");
    REQUIRE(variable.has_value());
    const auto object = std::find_if(
        ogplay::runtime::OpenSlesIids().begin(),
        ogplay::runtime::OpenSlesIids().end(), [](const auto& iid) {
            return iid.name == "SL_IID_OBJECT";
        });
    REQUIRE(object != ogplay::runtime::OpenSlesIids().end());
    CHECK(*variable == object->variable_address);
    CHECK(fixture.bus.Read32(*variable, 1U) == object->value_address.Value());
    std::array<std::byte, 16> bytes{};
    fixture.memory.Read(object->value_address, bytes, 1U);
    CHECK(bytes == std::array{
                       std::byte{0x60}, std::byte{0x63}, std::byte{0x21},
                       std::byte{0x79}, std::byte{0xd7}, std::byte{0xdd},
                       std::byte{0xdb}, std::byte{0x11}, std::byte{0x16},
                       std::byte{0xac}, std::byte{0x00}, std::byte{0x02},
                       std::byte{0xa5}, std::byte{0xd5}, std::byte{0xc5},
                       std::byte{0x1b}});
    CHECK_THROWS(fixture.bus.Write32(*variable, 0U, 1U));
}

TEST_CASE("OpenSL ES guest vtables create and play a PCM buffer queue") {
    std::vector<ogplay::runtime::OpenSlesGuestCallback> callbacks;
    BoundaryFixture fixture(1U, &callbacks);
    const auto call = [&](const std::uint32_t address,
                          const std::initializer_list<std::uint32_t> arguments) {
        const std::vector words(arguments);
        return BoundaryCallAddress(fixture, address, words);
    };
    const auto iid = [](const std::string_view name) {
        const auto found = std::find_if(
            ogplay::runtime::OpenSlesIids().begin(),
            ogplay::runtime::OpenSlesIids().end(),
            [&](const auto& candidate) { return candidate.name == name; });
        REQUIRE(found != ogplay::runtime::OpenSlesIids().end());
        return found->value_address.Value();
    };
    const auto create = fixture.boundary.Symbols().Lookup(
        "libOpenSLES.so", "slCreateEngine");
    REQUIRE(create.has_value());
    CHECK(call(create->Value(), {fixture.output.Value(), 0U, 0U, 0U, 0U, 0U}) == 0U);
    const auto engine_object = fixture.bus.Read32(fixture.output, 1U);
    const auto object_vtable = fixture.bus.Read32(
        ogplay::memory::GuestAddress{engine_object}, 1U);
    const auto realize = fixture.bus.Read32(
        ogplay::memory::GuestAddress{object_vtable}, 1U);
    CHECK(call(realize, {engine_object, 0U}) == 0U);
    const auto get_interface = fixture.bus.Read32(
        ogplay::memory::GuestAddress{object_vtable + 12U}, 1U);
    CHECK(call(get_interface,
               {engine_object, iid("SL_IID_ENGINE"), fixture.output.Add(4U).Value()}) == 0U);
    const auto engine = fixture.bus.Read32(fixture.output.Add(4U), 1U);
    const auto engine_vtable = fixture.bus.Read32(
        ogplay::memory::GuestAddress{engine}, 1U);
    const auto create_output_mix = fixture.bus.Read32(
        ogplay::memory::GuestAddress{engine_vtable + 7U * 4U}, 1U);
    CHECK(call(create_output_mix,
               {engine, fixture.output.Add(8U).Value(), 0U, 0U, 0U}) == 0U);
    const auto mix_object = fixture.bus.Read32(fixture.output.Add(8U), 1U);
    const auto mix_object_vtable = fixture.bus.Read32(
        ogplay::memory::GuestAddress{mix_object}, 1U);
    CHECK(call(fixture.bus.Read32(ogplay::memory::GuestAddress{mix_object_vtable}, 1U),
               {mix_object, 0U}) == 0U);
    CHECK(call(fixture.bus.Read32(
                   ogplay::memory::GuestAddress{mix_object_vtable + 12U}, 1U),
               {mix_object, iid("SL_IID_OUTPUTMIX"),
                fixture.output.Add(28U).Value()}) == 0U);
    const auto mix_interface = fixture.bus.Read32(fixture.output.Add(28U), 1U);
    const auto output_mix_vtable = fixture.bus.Read32(
        ogplay::memory::GuestAddress{mix_interface}, 1U);
    const auto device_count = fixture.output.Add(32U);
    CHECK(call(fixture.bus.Read32(
                   ogplay::memory::GuestAddress{output_mix_vtable}, 1U),
               {mix_interface, device_count.Value(), 0U}) == 0U);
    CHECK(fixture.bus.Read32(device_count, 1U) == 1U);

    const auto source = fixture.output.Add(0x100U);
    const auto source_locator = fixture.output.Add(0x120U);
    const auto source_format = fixture.output.Add(0x140U);
    const auto sink = fixture.output.Add(0x180U);
    const auto sink_locator = fixture.output.Add(0x1a0U);
    fixture.bus.Write32(source, source_locator.Value(), 1U);
    fixture.bus.Write32(source.Add(4U), source_format.Value(), 1U);
    fixture.bus.Write32(source_locator, 0x800007bdU, 1U);
    fixture.bus.Write32(source_locator.Add(4U), 1U, 1U);
    fixture.bus.Write32(source_format, 2U, 1U);
    fixture.bus.Write32(source_format.Add(4U), 1U, 1U);
    fixture.bus.Write32(source_format.Add(8U), 48000000U, 1U);
    fixture.bus.Write32(source_format.Add(12U), 16U, 1U);
    fixture.bus.Write32(source_format.Add(16U), 16U, 1U);
    fixture.bus.Write32(source_format.Add(20U), 4U, 1U);
    fixture.bus.Write32(source_format.Add(24U), 2U, 1U);
    fixture.bus.Write32(sink, sink_locator.Value(), 1U);
    fixture.bus.Write32(sink.Add(4U), 0U, 1U);
    fixture.bus.Write32(sink_locator, 4U, 1U);
    fixture.bus.Write32(sink_locator.Add(4U), mix_object, 1U);
    const auto interface_ids = fixture.output.Add(0x1c0U);
    const auto interface_required = fixture.output.Add(0x1d0U);
    fixture.bus.Write32(interface_ids,
                        iid("SL_IID_ANDROIDSIMPLEBUFFERQUEUE"), 1U);
    fixture.bus.Write32(interface_ids.Add(4U), iid("SL_IID_VOLUME"), 1U);
    fixture.bus.Write32(interface_required, 1U, 1U);
    fixture.bus.Write32(interface_required.Add(4U), 1U, 1U);
    const auto create_player = fixture.bus.Read32(
        ogplay::memory::GuestAddress{engine_vtable + 2U * 4U}, 1U);
    CHECK(call(create_player,
               {engine, fixture.output.Add(12U).Value(), source.Value(),
                sink.Value(), 2U, interface_ids.Value(),
                interface_required.Value()}) == 0U);
    const auto player_object = fixture.bus.Read32(fixture.output.Add(12U), 1U);
    const auto player_object_vtable = fixture.bus.Read32(
        ogplay::memory::GuestAddress{player_object}, 1U);
    CHECK(call(fixture.bus.Read32(
                   ogplay::memory::GuestAddress{player_object_vtable + 4U * 4U}, 1U),
               {player_object, 0x60000001U, 0x1234U}) == 0U);
    CHECK(call(fixture.bus.Read32(
                   ogplay::memory::GuestAddress{player_object_vtable}, 1U),
               {player_object, 1U}) == 0U);
    REQUIRE(callbacks.size() == 1U);
    CHECK(callbacks[0].function == 0x60000001U);
    CHECK(callbacks[0].argument_count == 6U);
    CHECK(callbacks[0].arguments[0] == player_object);
    CHECK(callbacks[0].arguments[1] == 0x1234U);
    CHECK(callbacks[0].arguments[2] == 2U);
    const auto query_itf = [&](const std::string_view name,
                               const ogplay::memory::GuestAddress destination) {
        CHECK(call(fixture.bus.Read32(
                       ogplay::memory::GuestAddress{player_object_vtable + 12U}, 1U),
                   {player_object, iid(name), destination.Value()}) == 0U);
        return fixture.bus.Read32(destination, 1U);
    };
    const auto play = query_itf("SL_IID_PLAY", fixture.output.Add(16U));
    const auto queue = query_itf("SL_IID_ANDROIDSIMPLEBUFFERQUEUE",
                                 fixture.output.Add(20U));
    const auto volume = query_itf("SL_IID_VOLUME", fixture.output.Add(24U));
    const auto queue_vtable = fixture.bus.Read32(
        ogplay::memory::GuestAddress{queue}, 1U);
    CHECK(call(fixture.bus.Read32(
                   ogplay::memory::GuestAddress{queue_vtable + 3U * 4U}, 1U),
               {queue, 0x60000003U, 0x5678U}) == 0U);
    const auto pcm = fixture.output.Add(0x200U);
    const std::array pcm_bytes{
        std::byte{0xe8}, std::byte{0x03}, std::byte{0xd0}, std::byte{0x07}};
    fixture.memory.Write(pcm, pcm_bytes, 1U);
    CHECK(call(fixture.bus.Read32(
                   ogplay::memory::GuestAddress{queue_vtable}, 1U),
               {queue, pcm.Value(), static_cast<std::uint32_t>(pcm_bytes.size())}) == 0U);
    CHECK(call(fixture.bus.Read32(
                   ogplay::memory::GuestAddress{queue_vtable}, 1U),
               {queue, 0x12345000U, 4U}) == 7U);
    const auto play_vtable = fixture.bus.Read32(
        ogplay::memory::GuestAddress{play}, 1U);
    CHECK(call(fixture.bus.Read32(
                   ogplay::memory::GuestAddress{play_vtable + 4U * 4U}, 1U),
               {play, 0x60000005U, 0x9abcU}) == 0U);
    CHECK(call(fixture.bus.Read32(
                   ogplay::memory::GuestAddress{play_vtable + 5U * 4U}, 1U),
               {play, 1U}) == 0U);
    CHECK(call(fixture.bus.Read32(
                   ogplay::memory::GuestAddress{play_vtable}, 1U),
               {play, 3U}) == 0U);
    const auto volume_vtable = fixture.bus.Read32(
        ogplay::memory::GuestAddress{volume}, 1U);
    CHECK(call(fixture.bus.Read32(
                   ogplay::memory::GuestAddress{volume_vtable + 5U * 4U}, 1U),
               {volume, 1U}) == 0U);
    CHECK(call(fixture.bus.Read32(
                   ogplay::memory::GuestAddress{volume_vtable + 7U * 4U}, 1U),
               {volume, 1000U}) == 0U);
    std::array<std::int16_t, 4> mixed{};
    const auto consumed = fixture.boundary.MixOpenSlesPcm16(mixed, 48000U);
    REQUIRE(consumed.size() == 1U);
    CHECK(mixed == std::array<std::int16_t, 4>{0, 1000, 0, 2000});
    REQUIRE(callbacks.size() == 3U);
    CHECK(callbacks[1].function == 0x60000003U);
    CHECK(callbacks[1].argument_count == 2U);
    CHECK(callbacks[1].arguments[0] == queue);
    CHECK(callbacks[1].arguments[1] == 0x5678U);
    CHECK(callbacks[2].function == 0x60000005U);
    CHECK(callbacks[2].argument_count == 3U);
    CHECK(callbacks[2].arguments[0] == play);
    CHECK(callbacks[2].arguments[1] == 0x9abcU);
    CHECK(callbacks[2].arguments[2] == 1U);
    const auto queue_state = fixture.output.Add(0x220U);
    CHECK(call(fixture.bus.Read32(
                   ogplay::memory::GuestAddress{queue_vtable + 2U * 4U}, 1U),
               {queue, queue_state.Value()}) == 0U);
    CHECK(fixture.bus.Read32(queue_state, 1U) == 0U);
    CHECK(fixture.bus.Read32(queue_state.Add(4U), 1U) == 1U);

    const auto enqueue = fixture.bus.Read32(
        ogplay::memory::GuestAddress{queue_vtable}, 1U);
    std::string slow_fault;
    try {
        static_cast<void>(call(enqueue, {queue, 0x12345000U, 4U}));
        FAIL("slow OpenSL queue memory call did not fault");
    } catch (const ogplay::memory::MemoryFault& error) {
        slow_fault = error.what();
    }
    ogplay::cpu::DynarmicCpu fast_cpu(fixture.bus);
    fast_cpu.SetHostCallHook(fixture.boundary.FastHostCallHook());
    ogplay::cpu::A32State fast_state;
    fast_state.SetState(ogplay::cpu::ExecutionState::thumb);
    fast_state.SetThreadId(1U);
    fast_state.SetRegister(ogplay::cpu::CoreRegister::pc,
                           enqueue & ~UINT32_C(1));
    fast_state.SetRegister(ogplay::cpu::CoreRegister::r0, queue);
    fast_state.SetRegister(ogplay::cpu::CoreRegister::r1, 0x12345000U);
    fast_state.SetRegister(ogplay::cpu::CoreRegister::r2, 4U);
    fast_state.SetRegister(ogplay::cpu::CoreRegister::sp,
                           fixture.stack.Value());
    fast_cpu.SetState(fast_state);
    const auto stopped = fast_cpu.Run(4U);
    REQUIRE(stopped.reason == ogplay::cpu::RunStopReason::host_call_fault);
    try {
        static_cast<void>(fixture.boundary.Handle(fast_cpu, stopped));
        FAIL("fast OpenSL queue memory fault was not restored");
    } catch (const ogplay::memory::MemoryFault& error) {
        CHECK(std::string(error.what()) == slow_fault);
        CHECK(error.Address() == ogplay::memory::GuestAddress{0x12345000U});
        CHECK(error.ThreadId() == 1U);
    }
}

TEST_CASE("A32 call frame bulk decodes register and stack arguments") {
    ogplay::memory::AddressSpace memory;
    const ogplay::memory::GuestAddress stack{0x6e200000U};
    memory.Map({stack, memory.PageSize()},
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write);
    const std::array<std::uint32_t, 5> stack_words{
        0x44444444U, 0x55555555U, 0x66666666U, 0x77777777U,
        0x88888888U};
    std::array<std::byte, stack_words.size() * sizeof(std::uint32_t)> bytes{};
    for (std::size_t word = 0; word < stack_words.size(); ++word) {
        for (std::size_t byte = 0; byte < sizeof(std::uint32_t); ++byte) {
            bytes[word * sizeof(std::uint32_t) + byte] =
                static_cast<std::byte>(stack_words[word] >> (byte * 8U));
        }
    }
    memory.Write(stack, bytes, 71);

    ogplay::cpu::A32State state;
    state.SetThreadId(71);
    state.SetRegister(ogplay::cpu::CoreRegister::r0, 0x00000000U);
    state.SetRegister(ogplay::cpu::CoreRegister::r1, 0x11111111U);
    state.SetRegister(ogplay::cpu::CoreRegister::r2, 0x22222222U);
    state.SetRegister(ogplay::cpu::CoreRegister::r3, 0x33333333U);
    state.SetRegister(ogplay::cpu::CoreRegister::sp, stack.Value());
    state.SetRegister(ogplay::cpu::CoreRegister::lr, 0xabcdef01U);
    const ogplay::runtime::A32CallFrame call(memory, state, 9U);
    REQUIRE(call.Arguments().size() == 9U);
    for (std::size_t index = 0; index < call.Arguments().size(); ++index) {
        CHECK(call.Argument(index) == static_cast<std::uint32_t>(index) *
                                          0x11111111U);
    }
    CHECK(call.ThreadId() == 71U);
    CHECK(call.LinkRegister() == 0xabcdef01U);
    CHECK(call.Scalar<std::int32_t>(1) ==
          std::bit_cast<std::int32_t>(0x11111111U));
    CHECK(call.Pointer<std::uint32_t>(2).Address() ==
          ogplay::memory::GuestAddress{0x22222222U});
    CHECK(call.CString(3).Address() ==
          ogplay::memory::GuestAddress{0x33333333U});
    CHECK_THROWS_AS(static_cast<void>(call.Argument(9U)), std::out_of_range);
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::runtime::A32CallFrame(
            memory, state,
            ogplay::runtime::kMaximumA32CallArguments + 1U)),
        std::length_error);
}

TEST_CASE("Android boundary publishes the complete generated GLES2 namespace") {
    BoundaryFixture fixture;
    CHECK(ogplay::gles::GlesDispatchTable::FunctionCount() == 142);
    const auto legacy_viewport =
        fixture.boundary.Symbols().Lookup("libGLESv2.so", "glViewport");
    REQUIRE(legacy_viewport.has_value());
    CHECK((legacy_viewport->Value() & 1U) == 1U);
    CHECK(legacy_viewport->Value() >= ogplay::runtime::kBionicHleThunkBegin);
    for (std::size_t index = 0;
         index < ogplay::gles::GlesDispatchTable::FunctionCount(); ++index) {
        const auto function = ogplay::gles::GlesDispatchTable::Describe(
            static_cast<ogplay::gles::GlesThunkId>(index));
        CAPTURE(function.name);
        CHECK(fixture.boundary.Symbols().Lookup("libGLESv2.so", function.name).has_value());
    }

    CHECK_THROWS_WITH_AS(
        fixture.Call("libGLESv2.so", "glValidateProgram"),
        "glValidateProgram has no current ANGLE frame",
        std::runtime_error);
}

TEST_CASE("GLES2 low-transfer state and object predicates use ANGLE") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
    fixture.boundary.OpenManagedSurface();
    CHECK(fixture.Call("libGLESv2.so", "glBlendEquationSeparate",
                       {0x8006U, 0x8006U}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glBlendFuncSeparate",
                       {1U, 0U, 1U, 0U}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glClearDepthf",
                       {std::bit_cast<std::uint32_t>(0.5F)}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glClearStencil", {3U}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glColorMask", {1U, 0U, 1U, 1U}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glCullFace", {0x0405U}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glDepthFunc", {0x0203U}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glDepthMask", {1U}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glDepthRangef",
                       {0U, std::bit_cast<std::uint32_t>(1.0F)}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glFrontFace", {0x0901U}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glHint", {0x8192U, 0x1100U}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glLineWidth",
                       {std::bit_cast<std::uint32_t>(1.0F)}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glPolygonOffset", {0U, 0U}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glStencilFunc",
                       {0x0207U, 1U, 0xFFU}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glStencilMask", {0xFFU}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glStencilOp",
                       {0x1E00U, 0x1E00U, 0x1E00U}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glStencilFuncSeparate",
                       {0x0405U, 0x0205U, 7U, 0xAAU}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glStencilMaskSeparate",
                       {0x0405U, 0x55U}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glStencilOpSeparate",
                       {0x0405U, 0x1E00U, 0x1E01U, 0x1E02U}) == 0U);
    const auto query = fixture.output.Add(0x300U);
    CHECK(fixture.Call("libGLESv2.so", "glGetIntegerv",
                       {0x8800U, query.Value()}) == 0U);
    CHECK(fixture.bus.Read32(query, 1U) == 0x0205U);
    CHECK(fixture.Call("libGLESv2.so", "glGetIntegerv",
                       {0x8CA5U, query.Value()}) == 0U);
    CHECK(fixture.bus.Read32(query, 1U) == 0x55U);

    CHECK(fixture.Call("libGLESv2.so", "glGenBuffers", {1U, query.Value()}) == 0U);
    const auto buffer = fixture.bus.Read32(query, 1U);
    CHECK(fixture.Call("libGLESv2.so", "glBindBuffer", {0x8892U, buffer}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glIsBuffer", {buffer}) == 1U);
    CHECK(fixture.Call("libGLESv2.so", "glGenTextures", {1U, query.Value()}) == 0U);
    const auto texture = fixture.bus.Read32(query, 1U);
    CHECK(fixture.Call("libGLESv2.so", "glBindTexture", {0x0DE1U, texture}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glIsTexture", {texture}) == 1U);
    const auto shader = fixture.Call("libGLESv2.so", "glCreateShader", {0x8B31U});
    REQUIRE(shader != 0U);
    CHECK(fixture.Call("libGLESv2.so", "glIsShader", {shader}) == 1U);
    const auto program = fixture.Call("libGLESv2.so", "glCreateProgram");
    CHECK(fixture.Call("libGLESv2.so", "glIsProgram", {program}) == 1U);
    CHECK(fixture.Call("libGLESv2.so", "glGenFramebuffers", {1U, query.Value()}) == 0U);
    const auto framebuffer = fixture.bus.Read32(query, 1U);
    CHECK(fixture.Call("libGLESv2.so", "glBindFramebuffer",
                       {0x8D40U, framebuffer}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glIsFramebuffer", {framebuffer}) == 1U);
    CHECK(fixture.Call("libGLESv2.so", "glGenRenderbuffers", {1U, query.Value()}) == 0U);
    const auto renderbuffer = fixture.bus.Read32(query, 1U);
    CHECK(fixture.Call("libGLESv2.so", "glBindRenderbuffer",
                       {0x8D41U, renderbuffer}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glIsRenderbuffer", {renderbuffer}) == 1U);
    CHECK(fixture.Call("libGLESv2.so", "glFinish") == 0U);

    fixture.bus.Write32(query, buffer, 1U);
    CHECK(fixture.Call("libGLESv2.so", "glDeleteBuffers", {1U, query.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glIsBuffer", {buffer}) == 0U);
    fixture.bus.Write32(query, texture, 1U);
    CHECK(fixture.Call("libGLESv2.so", "glDeleteTextures", {1U, query.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glIsTexture", {texture}) == 0U);
    fixture.bus.Write32(query, framebuffer, 1U);
    CHECK(fixture.Call("libGLESv2.so", "glDeleteFramebuffers",
                       {1U, query.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glIsFramebuffer", {framebuffer}) == 0U);
    fixture.bus.Write32(query, renderbuffer, 1U);
    CHECK(fixture.Call("libGLESv2.so", "glDeleteRenderbuffers",
                       {1U, query.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glIsRenderbuffer", {renderbuffer}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glDeleteShader", {shader}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glIsShader", {shader}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glDeleteProgram", {program}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glIsProgram", {program}) == 0U);
    fixture.boundary.CloseManagedSurface();
}

TEST_CASE("GLES2 transfer texture and query exports use preflighted ANGLE calls") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
    fixture.boundary.OpenManagedSurface();
    const auto query = fixture.output.Add(0x300U);
    const auto input = fixture.output.Add(0x380U);
    const auto address = [&](const std::string_view symbol) {
        const auto found = fixture.boundary.Symbols().Lookup(
            "libGLESv2.so", symbol);
        REQUIRE(found.has_value());
        return found->Value();
    };
    const auto call = [&](const std::string_view symbol,
                          const std::span<const std::uint32_t> arguments) {
        return BoundaryCallAddress(fixture, address(symbol), arguments);
    };

    CHECK(fixture.Call("libGLESv2.so", "glColorMask",
                       {1U, 0U, 1U, 0U}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetBooleanv",
                       {0x0C23U, query.Value()}) == 0U);
    std::array<std::byte, 4> boolean_mask{};
    fixture.memory.Read(query, boolean_mask, 1U);
    CHECK(boolean_mask == std::array<std::byte, 4>{
                              std::byte{1}, std::byte{0},
                              std::byte{1}, std::byte{0}});
    CHECK(fixture.Call("libGLESv2.so", "glDepthRangef",
                       {std::bit_cast<std::uint32_t>(0.25F),
                        std::bit_cast<std::uint32_t>(0.75F)}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetFloatv",
                       {0x0B70U, query.Value()}) == 0U);
    CHECK(std::bit_cast<float>(fixture.bus.Read32(query, 1U)) ==
          doctest::Approx(0.25F));
    CHECK(std::bit_cast<float>(fixture.bus.Read32(query.Add(4U), 1U)) ==
          doctest::Approx(0.75F));

    CHECK(fixture.Call("libGLESv2.so", "glGenBuffers",
                       {1U, query.Value()}) == 0U);
    const auto buffer = fixture.bus.Read32(query, 1U);
    CHECK(fixture.Call("libGLESv2.so", "glBindBuffer",
                       {0x8892U, buffer}) == 0U);
    std::array<std::byte, 16> buffer_data{};
    fixture.memory.Write(input, buffer_data, 1U);
    CHECK(fixture.Call("libGLESv2.so", "glBufferData",
                       {0x8892U, 16U, input.Value(), 0x88E4U}) == 0U);
    std::array<std::byte, 4> replacement{
        std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    fixture.memory.Write(input, replacement, 1U);
    CHECK(fixture.Call("libGLESv2.so", "glBufferSubData",
                       {0x8892U, 4U, 4U, input.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetBufferParameteriv",
                       {0x8892U, 0x8764U, query.Value()}) == 0U);
    CHECK(fixture.bus.Read32(query, 1U) == 16U);

    CHECK(fixture.Call("libGLESv2.so", "glGenTextures",
                       {1U, query.Value()}) == 0U);
    const auto texture = fixture.bus.Read32(query, 1U);
    CHECK(fixture.Call("libGLESv2.so", "glBindTexture",
                       {0x0DE1U, texture}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glTexParameterf",
                       {0x0DE1U, 0x2801U,
                        std::bit_cast<std::uint32_t>(9729.0F)}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetTexParameterfv",
                       {0x0DE1U, 0x2801U, query.Value()}) == 0U);
    CHECK(std::bit_cast<float>(fixture.bus.Read32(query, 1U)) ==
          doctest::Approx(9729.0F));
    fixture.bus.Write32(input, 0x812FU, 1U);
    CHECK(fixture.Call("libGLESv2.so", "glTexParameteriv",
                       {0x0DE1U, 0x2802U, input.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetTexParameteriv",
                       {0x0DE1U, 0x2802U, query.Value()}) == 0U);
    CHECK(fixture.bus.Read32(query, 1U) == 0x812FU);
    fixture.bus.Write32(input, std::bit_cast<std::uint32_t>(9728.0F), 1U);
    CHECK(fixture.Call("libGLESv2.so", "glTexParameterfv",
                       {0x0DE1U, 0x2800U, input.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetTexParameteriv",
                       {0x0DE1U, 0x2800U, query.Value()}) == 0U);
    CHECK(fixture.bus.Read32(query, 1U) == 0x2600U);

    fixture.bus.Write32(input, 0x2901U, 1U);
    CHECK_THROWS_AS(
        fixture.Call("libGLESv2.so", "glTexParameteriv",
                     {0x0DE1U, 0x2802U, 0x12345000U}),
        ogplay::memory::MemoryFault);
    CHECK(fixture.Call("libGLESv2.so", "glGetTexParameteriv",
                       {0x0DE1U, 0x2802U, query.Value()}) == 0U);
    CHECK(fixture.bus.Read32(query, 1U) == 0x812FU);

    CHECK(fixture.Call("libGLESv2.so", "glPixelStorei",
                       {0x0CF5U, 8U}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glPixelStorei",
                       {0x0D05U, 2U}) == 0U);
    const std::array<std::byte, 8> etc1{};
    fixture.memory.Write(input, etc1, 1U);
    const std::array<std::uint32_t, 8> compressed{
        0x0DE1U, 0U, 0x8D64U, 4U, 4U, 0U, 8U, input.Value()};
    CHECK(call("glCompressedTexImage2D", compressed) == 0U);
    const std::array<std::uint32_t, 9> compressed_sub{
        0x0DE1U, 0U, 0U, 0U, 4U, 4U, 0x8D64U, 8U, input.Value()};
    CHECK(call("glCompressedTexSubImage2D", compressed_sub) == 0U);

    const std::array<std::uint32_t, 8> copy_image{
        0x0DE1U, 0U, 0x1908U, 0U, 0U, 4U, 3U, 0U};
    CHECK(call("glCopyTexImage2D", copy_image) == 0U);
    const std::array<std::uint32_t, 8> copy_sub{
        0x0DE1U, 0U, 0U, 0U, 0U, 0U, 1U, 1U};
    CHECK(call("glCopyTexSubImage2D", copy_sub) == 0U);

    CHECK(fixture.Call("libGLESv2.so", "glGenFramebuffers",
                       {1U, query.Value()}) == 0U);
    const auto framebuffer = fixture.bus.Read32(query, 1U);
    CHECK(fixture.Call("libGLESv2.so", "glBindFramebuffer",
                       {0x8D40U, framebuffer}) == 0U);
    const std::array<std::uint32_t, 5> attach{
        0x8D40U, 0x8CE0U, 0x0DE1U, texture, 0U};
    CHECK(call("glFramebufferTexture2D", attach) == 0U);
    CHECK(fixture.Call("libGLESv2.so",
                       "glGetFramebufferAttachmentParameteriv",
                       {0x8D40U, 0x8CE0U, 0x8CD1U, query.Value()}) == 0U);
    CHECK(fixture.bus.Read32(query, 1U) == texture);

    CHECK(fixture.Call("libGLESv2.so", "glGenRenderbuffers",
                       {1U, query.Value()}) == 0U);
    const auto renderbuffer = fixture.bus.Read32(query, 1U);
    CHECK(fixture.Call("libGLESv2.so", "glBindRenderbuffer",
                       {0x8D41U, renderbuffer}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glRenderbufferStorage",
                       {0x8D41U, 0x8056U, 2U, 3U}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetRenderbufferParameteriv",
                       {0x8D41U, 0x8D42U, query.Value()}) == 0U);
    CHECK(fixture.bus.Read32(query, 1U) == 2U);
    CHECK(fixture.Call("libGLESv2.so", "glGetIntegerv",
                       {0x0CF5U, query.Value()}) == 0U);
    CHECK(fixture.bus.Read32(query, 1U) == 8U);
    CHECK(fixture.Call("libGLESv2.so", "glGetIntegerv",
                       {0x0D05U, query.Value()}) == 0U);
    CHECK(fixture.bus.Read32(query, 1U) == 2U);
    fixture.boundary.CloseManagedSurface();
}

TEST_CASE("Android GLES1 publishes and directly binds KitKat Bounds wrappers") {
    BoundaryFixture fixture;
    static constexpr std::array<std::string_view, 7> bounds{
        "glColorPointerBounds", "glNormalPointerBounds",
        "glTexCoordPointerBounds", "glVertexPointerBounds",
        "glPointSizePointerOESBounds", "glMatrixIndexPointerOESBounds",
        "glWeightPointerOESBounds"};
    for (const auto symbol : bounds) {
        CAPTURE(symbol);
        const auto address = fixture.boundary.Symbols().Lookup(
            "libGLESv1_CM.so", symbol);
        REQUIRE(address.has_value());
        CHECK((address->Value() & 1U) == 1U);
    }
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;

    PrepareNativeEgl(fixture);
    CHECK(fixture.Call("libEGL.so", "eglMakeCurrent", {1U, 3U, 3U, 4U}) == 1U);
    const auto address = fixture.boundary.Symbols().Lookup(
        "libGLESv1_CM.so", "glVertexPointerBounds");
    REQUIRE(address.has_value());
    const std::array arguments{3U, 0x1406U, 0U, fixture.output.Value(), 2U};
    CHECK(BoundaryCallAddress(fixture, address->Value(), arguments) == 0U);
    const auto result = fixture.output.Add(64U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glGetPointerv",
                       {0x808EU, result.Value(), 0U, 0U}) == 0U);
    CHECK(fixture.bus.Read32(result, 1U) == fixture.output.Value());

    const std::array negative{3U, 0x1406U, 0U, fixture.output.Value(),
                              UINT32_MAX};
    CHECK(BoundaryCallAddress(fixture, address->Value(), negative) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glGetError") == 0x0501U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glGetError") == 0U);
    const std::array crossing{
        3U, 0x1406U, 0U,
        fixture.output.Add(fixture.memory.PageSize() - 4U).Value(), 2U};
    std::string slow_fault;
    try {
        static_cast<void>(BoundaryCallAddress(fixture, address->Value(), crossing));
        FAIL("slow Bounds call did not fault");
    } catch (const ogplay::memory::MemoryFault& error) {
        slow_fault = error.what();
    }
    fixture.bus.Write32(fixture.stack, 2U, 1U);
    std::array<std::uint32_t, 16> registers{};
    std::copy_n(crossing.begin(), 4U, registers.begin());
    registers[13] = fixture.stack.Value();
    ogplay::cpu::A32HostCallContext fast{
        registers, 1U,
        ogplay::memory::GuestAddress{address->Value() & ~UINT32_C(1)}};
    const auto hook = fixture.boundary.FastHostCallHook();
    REQUIRE(hook.invoke(hook.userdata, 2U, fast) ==
            ogplay::cpu::HostCallResult::fault);
    const ogplay::cpu::RunResult stopped{
        1U, ogplay::cpu::RunStopReason::host_call_fault,
        fast.pc, 0xdf02U, 2U, std::nullopt};
    try {
        static_cast<void>(fixture.boundary.Handle(fixture.cpu, stopped));
        FAIL("fast Bounds fault was not restored");
    } catch (const ogplay::memory::MemoryFault& error) {
        CHECK(std::string(error.what()) == slow_fault);
    }
    CHECK(fixture.Call("libEGL.so", "eglTerminate", {1U}) == 1U);
}

TEST_CASE("Android boundary GPU trace ring retains the newest raw calls") {
    BoundaryFixture fixture;
    constexpr std::size_t calls = 2050U;
    for (std::size_t index = 0; index < calls; ++index) {
        CHECK(fixture.Call("libEGL.so", "eglGetDisplay") == 1U);
    }
    const auto trace = fixture.boundary.Trace("eglGetDisplay", calls);
    REQUIRE(trace.size() == 2048U);
    for (const auto& entry : trace) {
        CHECK(entry.call == "eglGetDisplay");
        CHECK(entry.arguments.at("r0") == "0");
    }
}

TEST_CASE("GLES1 shade model state validates and resets") {
    ogplay::runtime::detail::AndroidBoundaryGles1State state;
    CHECK(state.ShadeModel() ==
          ogplay::runtime::detail::kGles1SmoothShadeModel);
    state.SetShadeModel(ogplay::runtime::detail::kGles1FlatShadeModel);
    CHECK(state.ShadeModel() ==
          ogplay::runtime::detail::kGles1FlatShadeModel);
    state.SetShadeModel(ogplay::runtime::detail::kGles1SmoothShadeModel);
    CHECK_THROWS_WITH_AS(
        state.SetShadeModel(0U),
        "glShadeModel mode must be GL_FLAT or GL_SMOOTH",
        std::invalid_argument);
    auto transfer = state.TransferState();
    transfer.BindBuffer(0x8892U, 7U);
    state.SetTransferState(std::move(transfer));
    CHECK(state.TransferState().Snapshot().array_buffer == 7U);
    state.SetHint(0x0C50U, 0x1102U);
    CHECK(state.Hint(0x0C50U) == 0x1102U);
    CHECK_THROWS_WITH_AS(
        state.SetHint(0U, 0x1100U),
        "glHint target is invalid for GLES1", std::invalid_argument);
    state.SetCapability(0x0DE1U, true);
    CHECK(state.Capability(0x0DE1U));
    state.BindTexture(0x0DE1U, 7U);
    CHECK_FALSE(state.TextureBaseFormat(0x0DE1U).has_value());
    state.SetTextureBaseFormat(0x0DE1U, 0x1908U);
    REQUIRE(state.TextureBaseFormat(0x0DE1U).has_value());
    CHECK(*state.TextureBaseFormat(0x0DE1U) == 0x1908U);
    CHECK(state.EnabledTextureUnits() == std::vector<std::uint32_t>{0x84C0U});
    CHECK_FALSE(state.GenerateMipmapEnabled(0x0DE1U));
    state.SetGenerateMipmap(0x0DE1U, true);
    CHECK(state.GenerateMipmapEnabled(0x0DE1U));
    state.SetActiveTexture(0x84C1U);
    CHECK_FALSE(state.Capability(0x0DE1U));
    CHECK(state.Capability(0x84C0U, 0x0DE1U));
    REQUIRE(state.TextureBaseFormat(0x84C0U, 0x0DE1U).has_value());
    CHECK(*state.TextureBaseFormat(0x84C0U, 0x0DE1U) == 0x1908U);
    CHECK(state.EnabledTextureUnits() == std::vector<std::uint32_t>{0x84C0U});
    state.BindTexture(0x0DE1U, 8U);
    CHECK_FALSE(state.TextureBaseFormat(0x0DE1U).has_value());
    CHECK_FALSE(state.GenerateMipmapEnabled(0x0DE1U));
    state.SetCapability(0x0DE1U, true);
    CHECK(state.Capability(0x0DE1U));
    CHECK(state.EnabledTextureUnits() ==
          std::vector<std::uint32_t>{0x84C0U, 0x84C1U});
    const std::array deleted_textures{7U};
    state.DeleteTextures(deleted_textures);
    state.SetActiveTexture(0x84C0U);
    CHECK_FALSE(state.TextureBaseFormat(0x0DE1U).has_value());
    CHECK_FALSE(state.GenerateMipmapEnabled(0x0DE1U));
    CHECK_FALSE(state.TextureBaseFormat(0x0DE1U).has_value());
    CHECK_THROWS_WITH_AS(
        state.SetGenerateMipmap(0U, true),
        "GLES1 texture target must be GL_TEXTURE_2D, got 0x0",
        std::invalid_argument);
    CHECK_THROWS_WITH_AS(
        state.SetCapability(0U, true), "GLES1 capability is invalid",
        std::invalid_argument);
    // OES_texture_cube_map: bindings address the cube target while face
    // targets only appear on uploads and normalize onto the same object.
    state.BindTexture(0x8513U, 9U);
    CHECK(state.BoundTexture(0x8513U) == 9U);
    CHECK_THROWS_WITH_AS(
        state.BindTexture(0x8515U, 9U),
        "GLES1 texture target must be GL_TEXTURE_2D, got 0x8515",
        std::invalid_argument);
    state.SetTextureBaseFormat(0x8515U, 0x1908U);
    REQUIRE(state.TextureBaseFormat(0x84C0U, 0x8513U).has_value());
    CHECK(*state.TextureBaseFormat(0x84C0U, 0x8513U) == 0x1908U);
    state.SetGenerateMipmap(0x8513U, true);
    CHECK(state.GenerateMipmapEnabled(0x8513U));
    CHECK(state.GenerateMipmapEnabled(0x8515U));
    state.SetCapability(0x8513U, true);
    CHECK(state.Capability(0x84C0U, 0x8513U));
    CHECK_FALSE(state.Capability(0x84C1U, 0x8513U));
    state.Reset();
    CHECK(state.ShadeModel() ==
          ogplay::runtime::detail::kGles1SmoothShadeModel);
    CHECK(state.TransferState().Snapshot().array_buffer == 0U);
    CHECK(state.Hint(0x0C50U) ==
          ogplay::runtime::detail::kGles1DontCare);
    CHECK(state.ActiveTexture() == 0x84C0U);
    CHECK_FALSE(state.Capability(0x0DE1U));
    CHECK(state.EnabledTextureUnits().empty());
    CHECK_FALSE(state.GenerateMipmapEnabled(0x0DE1U));
}

TEST_CASE("Android boundary shares GLES object namespace and texture state") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
    PrepareNativeEgl(fixture);
    CHECK(fixture.Call("libEGL.so", "eglMakeCurrent", {1, 3, 3, 4}) == 1);

    const auto names = fixture.output;
    static_cast<void>(fixture.Call("libGLESv1_CM.so", "glGenTextures",
                                   {1U, names.Value()}));
    const auto from_gles1 = fixture.bus.Read32(names, 1);
    REQUIRE(from_gles1 != 0U);
    static_cast<void>(fixture.Call("libGLESv2.so", "glActiveTexture",
                                   {0x84C1U}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glBindTexture",
                                   {0x0DE1U, from_gles1}));
    static_cast<void>(fixture.Call("libGLESv1_CM.so", "glGetIntegerv",
                                   {0x8069U, names.Add(8U).Value()}));
    CHECK(fixture.bus.Read32(names.Add(8U), 1) == from_gles1);

    static_cast<void>(fixture.Call("libGLESv2.so", "glGenTextures",
                                   {1U, names.Add(4U).Value()}));
    const auto from_gles2 = fixture.bus.Read32(names.Add(4U), 1);
    REQUIRE(from_gles2 != 0U);
    static_cast<void>(fixture.Call("libGLESv1_CM.so", "glBindTexture",
                                   {0x0DE1U, from_gles2}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glGetIntegerv",
                                   {0x8069U, names.Add(8U).Value()}));
    CHECK(fixture.bus.Read32(names.Add(8U), 1) == from_gles2);

    fixture.bus.Write32(names.Add(12U), from_gles2, 1);
    static_cast<void>(fixture.Call("libGLESv2.so", "glDeleteTextures",
                                   {1U, names.Add(12U).Value()}));
    static_cast<void>(fixture.Call("libGLESv1_CM.so", "glGetIntegerv",
                                   {0x8069U, names.Add(8U).Value()}));
    CHECK(fixture.bus.Read32(names.Add(8U), 1) == 0U);

    fixture.bus.Write32(names.Add(12U), from_gles1, 1);
    static_cast<void>(fixture.Call("libGLESv1_CM.so", "glDeleteTextures",
                                   {1U, names.Add(12U).Value()}));
    CHECK(fixture.Call("libEGL.so", "eglTerminate", {1U}) == 1U);
}

TEST_CASE("DVM-83 managed Java GLES calls share native texture state") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
    PrepareNativeEgl(fixture);
    CHECK(fixture.Call("libEGL.so", "eglMakeCurrent", {1, 3, 3, 4}) == 1U);
    const auto names = fixture.output;
    static_cast<void>(fixture.Call("libGLESv2.so", "glGenTextures",
                                   {1U, names.Value()}));
    const auto texture = fixture.bus.Read32(names, 1U);
    REQUIRE(texture != 0U);
    const std::array bind{0x0DE1U, texture};
    CHECK(fixture.boundary.InvokeManagedGles(
              ogplay::gles::GlesApi::gles2, "glBindTexture", bind, 1U) == 0U);
    static_cast<void>(fixture.Call("libGLESv1_CM.so", "glGetIntegerv",
                                   {0x8069U, names.Add(4U).Value()}));
    CHECK(fixture.bus.Read32(names.Add(4U), 1U) == texture);
    CHECK_THROWS_AS(([&] {
        static_cast<void>(fixture.boundary.InvokeManagedGles(
            ogplay::gles::GlesApi::gles2, "glNoSuchEntry", {}, 1U));
    }()), std::invalid_argument);
    CHECK(fixture.Call("libEGL.so", "eglTerminate", {1U}) == 1U);
}

TEST_CASE("GLES2 active texture selects the GLES1 texture matrix unit") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
    PrepareNativeEgl(fixture);
    CHECK(fixture.Call("libEGL.so", "eglMakeCurrent", {1, 3, 3, 4}) == 1U);
    const auto output = fixture.output;

    CHECK(fixture.Call("libGLESv2.so", "glActiveTexture", {0x84C1U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glMatrixMode",
                       {ogplay::runtime::detail::kGles1Texture}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glLoadIdentity") == 0U);
    CHECK(fixture.Call(
              "libGLESv1_CM.so", "glTranslatef",
              {std::bit_cast<std::uint32_t>(1.0F),
               std::bit_cast<std::uint32_t>(2.0F),
               std::bit_cast<std::uint32_t>(3.0F)}) == 0U);

    CHECK(fixture.Call("libGLESv1_CM.so", "glActiveTexture", {0x84C0U}) ==
          0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glGetFloatv",
                       {0x0BA8U, output.Value()}) == 0U);
    CHECK(std::bit_cast<float>(fixture.bus.Read32(output.Add(48U), 1U)) ==
          doctest::Approx(0.0F));

    CHECK(fixture.Call("libGLESv1_CM.so", "glActiveTexture", {0x84C1U}) ==
          0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glGetFloatv",
                       {0x0BA8U, output.Value()}) == 0U);
    CHECK(std::bit_cast<float>(fixture.bus.Read32(output.Add(48U), 1U)) ==
          doctest::Approx(1.0F));
    CHECK(std::bit_cast<float>(fixture.bus.Read32(output.Add(52U), 1U)) ==
          doctest::Approx(2.0F));
    CHECK(std::bit_cast<float>(fixture.bus.Read32(output.Add(56U), 1U)) ==
          doctest::Approx(3.0F));
    CHECK(fixture.Call("libEGL.so", "eglTerminate", {1U}) == 1U);
}

TEST_CASE("shared texture bindings preserve independent GLES2 targets") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
    PrepareNativeEgl(fixture);
    CHECK(fixture.Call("libEGL.so", "eglMakeCurrent", {1, 3, 3, 4}) == 1U);
    const auto output = fixture.output;
    CHECK(fixture.Call("libGLESv2.so", "glGenTextures",
                       {3U, output.Value()}) == 0U);
    const auto texture_2d = fixture.bus.Read32(output, 1U);
    const auto texture_cube = fixture.bus.Read32(output.Add(4U), 1U);
    const auto texture_gles1 = fixture.bus.Read32(output.Add(8U), 1U);
    REQUIRE(texture_2d != 0U);
    REQUIRE(texture_cube != 0U);
    REQUIRE(texture_gles1 != 0U);

    CHECK(fixture.Call("libGLESv2.so", "glActiveTexture", {0x84C0U}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glBindTexture",
                       {0x0DE1U, texture_2d}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glBindTexture",
                       {0x8513U, texture_cube}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetIntegerv",
                       {0x8069U, output.Add(32U).Value()}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetIntegerv",
                       {0x8514U, output.Add(36U).Value()}) == 0U);
    CHECK(fixture.bus.Read32(output.Add(32U), 1U) == texture_2d);
    CHECK(fixture.bus.Read32(output.Add(36U), 1U) == texture_cube);

    CHECK(fixture.Call("libGLESv1_CM.so", "glBindTexture",
                       {0x0DE1U, texture_gles1}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetIntegerv",
                       {0x8069U, output.Add(32U).Value()}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetIntegerv",
                       {0x8514U, output.Add(36U).Value()}) == 0U);
    CHECK(fixture.bus.Read32(output.Add(32U), 1U) == texture_gles1);
    CHECK(fixture.bus.Read32(output.Add(36U), 1U) == texture_cube);

    fixture.bus.Write32(output.Add(16U), texture_cube, 1U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glDeleteTextures",
                       {1U, output.Add(16U).Value()}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetIntegerv",
                       {0x8069U, output.Add(32U).Value()}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetIntegerv",
                       {0x8514U, output.Add(36U).Value()}) == 0U);
    CHECK(fixture.bus.Read32(output.Add(32U), 1U) == texture_gles1);
    CHECK(fixture.bus.Read32(output.Add(36U), 1U) == 0U);

    ogplay::runtime::SharedGlState metadata;
    metadata.BindTexture(0x0DE1U, 41U);
    metadata.SetTextureBaseFormat(0x0DE1U, 0x1908U);
    metadata.SetGenerateMipmap(0x0DE1U, true);
    metadata.BindTexture(0x8513U, 42U);
    metadata.SetTextureBaseFormat(0x8513U, 0x1907U);
    metadata.SetGenerateMipmap(0x8513U, true);
    metadata.SetActiveTexture(0x84C1U);
    metadata.BindTexture(0x0DE1U, 42U);
    metadata.BindTexture(0x8513U, 42U);
    metadata.SetActiveTexture(0x84C0U);
    CHECK(metadata.TextureBaseFormat(0x0DE1U) == 0x1908U);
    CHECK(metadata.TextureBaseFormat(0x8513U) == 0x1907U);
    CHECK(metadata.GenerateMipmapEnabled(0x0DE1U));
    CHECK(metadata.GenerateMipmapEnabled(0x8513U));
    const std::array deleted_cube{42U};
    metadata.DeleteTextures(deleted_cube);
    CHECK(metadata.BoundTexture(0x0DE1U) == 41U);
    CHECK(metadata.BoundTexture(0x8513U) == 0U);
    CHECK(metadata.BoundTexture(0x84C1U, 0x0DE1U) == 0U);
    CHECK(metadata.BoundTexture(0x84C1U, 0x8513U) == 0U);
    CHECK(metadata.TextureBaseFormat(0x0DE1U) == 0x1908U);
    CHECK_FALSE(metadata.TextureBaseFormat(0x8513U).has_value());
    CHECK(metadata.GenerateMipmapEnabled(0x0DE1U));
    CHECK_FALSE(metadata.GenerateMipmapEnabled(0x8513U));
    CHECK(fixture.Call("libEGL.so", "eglTerminate", {1U}) == 1U);
}

TEST_CASE("BND-29 GLES2 invalid capabilities latch errors without changing state") {
    REQUIRE(ogplay::gles::IsNativeAngleEglAvailable());
    BoundaryFixture fixture;
    PrepareNativeEgl(fixture);
    REQUIRE(fixture.Call("libEGL.so", "eglMakeCurrent", {1, 3, 3, 4}) == 1);
    CHECK(fixture.Call("libGLESv2.so", "glEnable", {0x0BE2U}) == 0);
    for (const auto name : {"glEnable", "glDisable", "glIsEnabled"}) {
        for (const auto cap : {0x0DE1U, 0xffffffffU}) {
            CHECK(fixture.Call("libGLESv2.so", name, {cap}) == 0);
            CHECK(fixture.Call("libGLESv2.so", "glIsEnabled", {0x0BE2U}) == 1);
            CHECK(fixture.Call("libGLESv2.so", "glGetError") == 0x0500U);
            CHECK(fixture.Call("libGLESv2.so", "glGetError") == 0);
        }
    }
    CHECK(fixture.Call("libGLESv2.so", "glEnable", {0x0DE1U}) == 0);
    CHECK(fixture.Call("libGLESv2.so", "glDisable", {0xffffffffU}) == 0);
    CHECK(fixture.Call("libGLESv1_CM.so", "glGetError") == 0x0500U);
    CHECK(fixture.Call("libGLESv2.so", "glGetError") == 0);
    CHECK(fixture.Call("libGLESv2.so", "glDisable", {0x0BE2U}) == 0);
    CHECK(fixture.Call("libGLESv2.so", "glIsEnabled", {0x0BE2U}) == 0);
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnable", {0x0DE1U}) == 0);
    CHECK(fixture.Call("libGLESv1_CM.so", "glIsEnabled", {0x0DE1U}) == 1);
    CHECK(fixture.Call("libGLESv1_CM.so", "glGetError") == 0);
    CHECK(fixture.Call("libEGL.so", "eglTerminate", {1U}) == 1);
    CHECK_THROWS_AS(fixture.Call("libGLESv2.so", "glEnable", {0x0DE1U}), std::runtime_error);
}

TEST_CASE("GLES draw negative ranges latch INVALID_VALUE across API families") {
    REQUIRE(ogplay::gles::IsNativeAngleEglAvailable());
    BoundaryFixture fixture;
    PrepareNativeEgl(fixture);
    REQUIRE(fixture.Call("libEGL.so", "eglMakeCurrent", {1, 3, 3, 4}) == 1);

    for (const auto library : {"libGLESv1_CM.so", "libGLESv2.so"}) {
        CAPTURE(library);
        CHECK(fixture.Call(library, "glDrawArrays",
                           {0x0004U, 0U, 0xFFFFFFFFU}) == 0U);
        CHECK(fixture.Call(library, "glGetError") == 0x0501U);
        CHECK(fixture.Call(library, "glGetError") == 0U);
        CHECK(fixture.Call(library, "glDrawElements",
                           {0x0004U, 0xFFFFFFFFU, 0x1403U, 0U}) == 0U);
        CHECK(fixture.Call(library, "glGetError") == 0x0501U);
        CHECK(fixture.Call(library, "glGetError") == 0U);
    }
}

TEST_CASE("Android boundary shares framebuffer raster and capability state") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
    PrepareNativeEgl(fixture);
    CHECK(fixture.Call("libEGL.so", "eglMakeCurrent", {1, 3, 3, 4}) == 1);
    const auto output = fixture.output;

    static_cast<void>(fixture.Call("libGLESv2.so", "glGenFramebuffers",
                                   {1U, output.Value()}));
    const auto framebuffer = fixture.bus.Read32(output, 1);
    REQUIRE(framebuffer != 0U);
    static_cast<void>(fixture.Call("libGLESv2.so", "glBindFramebuffer",
                                   {0x8D40U, framebuffer}));
    static_cast<void>(fixture.Call("libGLESv1_CM.so", "glGetIntegerv",
                                   {0x8CA6U, output.Add(16U).Value()}));
    CHECK(fixture.bus.Read32(output.Add(16U), 1) == framebuffer);

    static_cast<void>(fixture.Call("libGLESv1_CM.so", "glViewport",
                                   {1U, 2U, 3U, 1U}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glGetIntegerv",
                                   {0x0BA2U, output.Add(32U).Value()}));
    CHECK(fixture.bus.Read32(output.Add(32U), 1) == 1U);
    CHECK(fixture.bus.Read32(output.Add(36U), 1) == 2U);
    CHECK(fixture.bus.Read32(output.Add(40U), 1) == 3U);
    CHECK(fixture.bus.Read32(output.Add(44U), 1) == 1U);

    static_cast<void>(fixture.Call("libGLESv2.so", "glScissor",
                                   {0U, 1U, 2U, 2U}));
    static_cast<void>(fixture.Call("libGLESv1_CM.so", "glGetIntegerv",
                                   {0x0C10U, output.Add(48U).Value()}));
    CHECK(fixture.bus.Read32(output.Add(52U), 1) == 1U);
    CHECK(fixture.bus.Read32(output.Add(56U), 1) == 2U);
    CHECK(fixture.bus.Read32(output.Add(60U), 1) == 2U);

    static_cast<void>(fixture.Call("libGLESv2.so", "glEnable", {0x0BE2U}));
    CHECK(fixture.Call("libGLESv1_CM.so", "glIsEnabled", {0x0BE2U}) == 1U);
    static_cast<void>(fixture.Call("libGLESv1_CM.so", "glDisable", {0x0BE2U}));
    CHECK(fixture.Call("libGLESv2.so", "glIsEnabled", {0x0BE2U}) == 0U);
}

TEST_CASE("supersample cross API queries preserve logical raster state") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture(2U);
    PrepareNativeEgl(fixture);
    CHECK(fixture.Call("libEGL.so", "eglMakeCurrent", {1, 3, 3, 4}) == 1U);
    const auto output = fixture.output;

    CHECK(fixture.Call("libGLESv1_CM.so", "glViewport",
                       {1U, 2U, 100U, 200U}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetIntegerv",
                       {0x0BA2U, output.Value()}) == 0U);
    CHECK(fixture.bus.Read32(output, 1U) == 1U);
    CHECK(fixture.bus.Read32(output.Add(4U), 1U) == 2U);
    CHECK(fixture.bus.Read32(output.Add(8U), 1U) == 100U);
    CHECK(fixture.bus.Read32(output.Add(12U), 1U) == 200U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glGetIntegerv",
                       {0x0BA2U, output.Add(32U).Value()}) == 0U);
    CHECK(fixture.bus.Read32(output.Add(32U), 1U) == 1U);
    CHECK(fixture.bus.Read32(output.Add(36U), 1U) == 2U);
    CHECK(fixture.bus.Read32(output.Add(40U), 1U) == 100U);
    CHECK(fixture.bus.Read32(output.Add(44U), 1U) == 200U);

    CHECK(fixture.Call("libGLESv2.so", "glScissor",
                       {3U, 4U, 50U, 60U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glGetIntegerv",
                       {0x0C10U, output.Add(16U).Value()}) == 0U);
    CHECK(fixture.bus.Read32(output.Add(16U), 1U) == 3U);
    CHECK(fixture.bus.Read32(output.Add(20U), 1U) == 4U);
    CHECK(fixture.bus.Read32(output.Add(24U), 1U) == 50U);
    CHECK(fixture.bus.Read32(output.Add(28U), 1U) == 60U);
    CHECK(fixture.Call("libGLESv2.so", "glGetIntegerv",
                       {0x0C10U, output.Add(48U).Value()}) == 0U);
    CHECK(fixture.bus.Read32(output.Add(48U), 1U) == 3U);
    CHECK(fixture.bus.Read32(output.Add(52U), 1U) == 4U);
    CHECK(fixture.bus.Read32(output.Add(56U), 1U) == 50U);
    CHECK(fixture.bus.Read32(output.Add(60U), 1U) == 60U);
    CHECK(fixture.Call("libEGL.so", "eglTerminate", {1U}) == 1U);
}

TEST_CASE("new GL context exposes logical default raster state") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture(2U);
    PrepareNativeEgl(fixture);
    CHECK(fixture.Call("libEGL.so", "eglMakeCurrent", {1, 3, 3, 4}) == 1U);
    const auto output = fixture.output;

    const auto check_box = [&fixture](const ogplay::memory::GuestAddress address) {
        CHECK(fixture.bus.Read32(address, 1U) == 0U);
        CHECK(fixture.bus.Read32(address.Add(4U), 1U) == 0U);
        CHECK(fixture.bus.Read32(address.Add(8U), 1U) == 4U);
        CHECK(fixture.bus.Read32(address.Add(12U), 1U) == 3U);
    };

    CHECK(fixture.Call("libGLESv2.so", "glGetIntegerv",
                       {0x0BA2U, output.Value()}) == 0U);
    check_box(output);
    CHECK(fixture.Call("libGLESv1_CM.so", "glGetIntegerv",
                       {0x0BA2U, output.Add(16U).Value()}) == 0U);
    check_box(output.Add(16U));

    CHECK(fixture.Call("libGLESv2.so", "glGetIntegerv",
                       {0x0C10U, output.Add(32U).Value()}) == 0U);
    check_box(output.Add(32U));
    CHECK(fixture.Call("libGLESv1_CM.so", "glGetIntegerv",
                       {0x0C10U, output.Add(48U).Value()}) == 0U);
    check_box(output.Add(48U));

    constexpr auto kDither = UINT32_C(0x0BD0);
    CHECK(fixture.Call("libGLESv2.so", "glIsEnabled", {kDither}) == 1U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glIsEnabled", {kDither}) == 1U);
    CHECK(fixture.Call("libEGL.so", "eglTerminate", {1U}) == 1U);
}

TEST_CASE("GLES1 matrix state composes and bounds stacks") {
    ogplay::runtime::SharedGlState shared;
    ogplay::runtime::detail::AndroidBoundaryGles1MatrixState matrices(shared);
    CHECK(matrices.Mode() == ogplay::runtime::detail::kGles1Modelview);
    CHECK(matrices.StackDepth(ogplay::runtime::detail::kGles1Modelview) == 1U);
    CHECK(matrices.Current()[0] == 1.0F);
    CHECK(matrices.Current()[15] == 1.0F);

    matrices.SetMode(ogplay::runtime::detail::kGles1Projection);
    matrices.Translate(1.0F, 2.0F, 3.0F);
    CHECK(matrices.Current()[12] == 1.0F);
    CHECK(matrices.Current()[13] == 2.0F);
    CHECK(matrices.Current()[14] == 3.0F);
    matrices.Push();
    matrices.Rotate(90.0F, 0.0F, 0.0F, 1.0F);
    CHECK(matrices.Current()[0] == doctest::Approx(0.0F).epsilon(0.0001));
    CHECK(matrices.Current()[1] == doctest::Approx(1.0F).epsilon(0.0001));
    CHECK(matrices.Current()[4] == doctest::Approx(-1.0F).epsilon(0.0001));
    matrices.Pop();
    CHECK(matrices.Current()[12] == 1.0F);
    CHECK_THROWS_WITH_AS(matrices.Pop(), "GLES1 matrix stack underflow",
                         std::underflow_error);
    CHECK_THROWS_WITH_AS(
        matrices.SetMode(0U), "glMatrixMode mode is invalid for GLES1",
        std::invalid_argument);
    CHECK_THROWS_WITH_AS(
        matrices.Rotate(1.0F, 0.0F, 0.0F, 0.0F),
        "GLES1 rotation axis is invalid", std::invalid_argument);
    const std::array plane{1.0F, 0.0F, 0.0F, 0.0F};
    auto translated = ogplay::runtime::detail::Gles1IdentityMatrix();
    translated[12] = 2.0F;
    const auto eye_plane =
        ogplay::runtime::detail::Gles1TransformClipPlane(translated, plane);
    CHECK(eye_plane[0] == doctest::Approx(1.0F));
    CHECK(eye_plane[3] == doctest::Approx(-2.0F));
    const ogplay::runtime::detail::Gles1Matrix singular{};
    CHECK_THROWS_WITH_AS(
        ogplay::runtime::detail::Gles1TransformClipPlane(singular, plane),
        "GLES1 clip-plane modelview matrix is singular", std::invalid_argument);
    matrices.SetMode(ogplay::runtime::detail::kGles1Texture);
    shared.SetActiveTexture(0x84C0U);
    matrices.Translate(0.25F, 0.0F, 0.0F);
    shared.SetActiveTexture(0x84C1U);
    CHECK(matrices.Current()[12] == 0.0F);
    matrices.Translate(0.5F, 0.0F, 0.0F);
    CHECK(matrices.Current(ogplay::runtime::detail::kGles1Texture,
                           0x84C0U)[12] == 0.25F);
    CHECK(matrices.Current(ogplay::runtime::detail::kGles1Texture,
                           0x84C1U)[12] == 0.5F);
    CHECK_THROWS_WITH_AS(
        shared.SetActiveTexture(0U),
        "shared GL texture unit is outside GL_TEXTURE0..31",
        std::invalid_argument);
    const ogplay::runtime::detail::Gles1Matrix invalid{
        std::numeric_limits<float>::quiet_NaN()};
    CHECK_THROWS_WITH_AS(
        matrices.Load(invalid), "GLES1 matrix value must be finite",
        std::invalid_argument);
    matrices.Reset();
    for (std::size_t depth = 1; depth < 32; ++depth) matrices.Push();
    CHECK_THROWS_WITH_AS(matrices.Push(), "GLES1 matrix stack overflow",
                         std::overflow_error);
}

TEST_CASE("GLES1 legacy fixed state validates isolates and resets") {
    ogplay::runtime::detail::AndroidBoundaryGles1LegacyState state;
    CHECK(state.AlphaFunction() == 0x0207U);
    CHECK(state.AlphaReference() == 0.0F);
    CHECK(state.ClientActiveTexture() == 0x84C0U);
    CHECK(state.Color()[0] == 1.0F);
    CHECK(state.Normal()[2] == 1.0F);
    CHECK(state.ClipPlane(0x3000U)[0] == 0.0F);
    state.SetAlphaFunction(0x0201U, 2.0F);
    CHECK(state.AlphaFunction() == 0x0201U);
    CHECK(state.AlphaReference() == 1.0F);
    state.SetClientActiveTexture(0x84C1U);
    CHECK(state.ClientActiveTexture() == 0x84C1U);
    const std::array color{-1.0F, 0.25F, 0.5F, 2.0F};
    state.SetColor(color);
    CHECK(state.Color()[0] == 0.0F);
    CHECK(state.Color()[3] == 1.0F);
    const std::array normal{0.25F, 0.5F, 0.75F};
    state.SetNormal(normal);
    CHECK(state.Normal()[1] == 0.5F);
    const std::array clip_plane{1.0F, 0.0F, 0.0F, -0.25F};
    state.SetClipPlane(0x3005U, clip_plane);
    CHECK(state.ClipPlane(0x3005U)[3] == -0.25F);
    CHECK_THROWS_AS(state.SetClipPlane(0x3006U, clip_plane),
                    std::invalid_argument);
    const std::array environment_color{0.1F, 0.2F, 0.3F, 0.4F};
    state.SetTextureEnvironment(
        0x84C1U, ogplay::runtime::detail::kGles1TextureEnvironment,
        ogplay::runtime::detail::kGles1TextureEnvironmentColor,
        environment_color);
    CHECK(state.TextureEnvironment(
              0x84C1U,
              ogplay::runtime::detail::kGles1TextureEnvironmentColor)[2] ==
          doctest::Approx(0.3F));
    CHECK_THROWS_AS(state.SetClientActiveTexture(0U), std::invalid_argument);
    CHECK_THROWS_AS(state.SetAlphaFunction(0U, 0.0F), std::invalid_argument);
    state.Reset();
    CHECK(state.ClientActiveTexture() == 0x84C0U);
    CHECK(state.Normal()[2] == 1.0F);
    CHECK(state.ClipPlane(0x3005U)[3] == 0.0F);
    CHECK(state.TextureEnvironment(
              0x84C0U,
              ogplay::runtime::detail::kGles1TextureEnvironmentMode)[0] ==
          8448.0F);
}

TEST_CASE("GLES1 client array state validates texture units and resets") {
    ogplay::runtime::detail::AndroidBoundaryGles1DrawState state;
    const auto check_defaults = [&state] {
        const auto& vertex = state.Array(
            ogplay::runtime::detail::kGles1VertexArray, 0x84C0U);
        CHECK(vertex.size == 4);
        CHECK(vertex.type == 0x1406U);
        CHECK(vertex.stride == 0);
        CHECK(vertex.pointer == 0U);
        CHECK(vertex.buffer == 0U);
        CHECK_FALSE(vertex.enabled);
        const auto& normal = state.Array(
            ogplay::runtime::detail::kGles1NormalArray, 0x84C0U);
        CHECK(normal.size == 3);
        CHECK(normal.type == 0x1406U);
        const auto& matrix_index = state.Array(
            ogplay::runtime::detail::kGles1MatrixIndexArray, 0x84C0U);
        CHECK(matrix_index.size == 4);
        CHECK(matrix_index.type == 0x1401U);
        const auto& texture = state.Array(
            ogplay::runtime::detail::kGles1TextureCoordArray, 0x84DFU);
        CHECK(texture.size == 4);
        CHECK(texture.type == 0x1406U);
    };
    check_defaults();
    state.SetPointer(ogplay::runtime::detail::kGles1VertexArray, 0x84C0U,
                     3, 0x1406U, 0, 0x1000U, 0U);
    state.SetEnabled(ogplay::runtime::detail::kGles1VertexArray, 0x84C0U,
                     true);
    CHECK(state.Array(ogplay::runtime::detail::kGles1VertexArray,
                      0x84C0U).enabled);
    CHECK(state.Array(ogplay::runtime::detail::kGles1VertexArray,
                      0x84C0U).pointer == 0x1000U);
    state.SetPointer(ogplay::runtime::detail::kGles1TextureCoordArray,
                     0x84C1U, 2, 0x1406U, 8, 0x2000U, 7U);
    CHECK(state.Array(ogplay::runtime::detail::kGles1TextureCoordArray,
                      0x84C1U).buffer == 7U);
    const auto prepared = state.PreparePointer(
        ogplay::runtime::detail::kGles1VertexArray, 0x84C0U,
        2, 0x1406U, 8, 0x3000U, 9U);
    CHECK(state.Array(ogplay::runtime::detail::kGles1VertexArray,
                      0x84C0U).pointer == 0x1000U);
    CHECK(prepared.enabled);
    state.CommitPointer(ogplay::runtime::detail::kGles1VertexArray,
                        0x84C0U, prepared);
    CHECK(state.Array(ogplay::runtime::detail::kGles1VertexArray,
                      0x84C0U).pointer == 0x3000U);
    CHECK(state.Array(ogplay::runtime::detail::kGles1VertexArray,
                      0x84C0U).buffer == 9U);
    CHECK_THROWS_AS(
        state.SetPointer(ogplay::runtime::detail::kGles1ColorArray,
                         0x84C0U, 3, 0x1406U, 0, 0U, 0U),
        std::invalid_argument);
    CHECK_THROWS_AS(
        state.SetPointer(ogplay::runtime::detail::kGles1VertexArray,
                         0x84C0U, 3, 0U, 0, 0U, 0U),
        std::invalid_argument);
    state.SetCurrentPaletteMatrix(7U);
    CHECK(state.CurrentPaletteMatrix() == 7U);
    state.SetPointer(ogplay::runtime::detail::kGles1MatrixIndexArray,
                     0x84C0U, 4, 0x1401U, 0, 0x3000U, 0U);
    state.SetPointer(ogplay::runtime::detail::kGles1WeightArray,
                     0x84C0U, 4, 0x1406U, 0, 0x4000U, 0U);
    CHECK_THROWS_AS(state.SetCurrentPaletteMatrix(32U),
                    std::invalid_argument);
    state.Reset();
    CHECK(state.CurrentPaletteMatrix() == 0U);
    check_defaults();
}

TEST_CASE("GLES1 single-stage texture coordinate fallback is unique and optional") {
    using namespace ogplay::runtime::detail;
    AndroidBoundaryGles1DrawState compatible;
    compatible.SetEnabled(kGles1TextureCoordArray, 0x84C0U, true);
    CHECK(compatible.ResolveTextureCoordinateUnits(
              std::array<std::uint32_t, 1>{0x84DFU}) ==
          std::array<std::uint32_t, 2>{0x84C0U, 0U});

    compatible.SetEnabled(kGles1TextureCoordArray, 0x84DFU, true);
    CHECK(compatible.ResolveTextureCoordinateUnits(
              std::array<std::uint32_t, 1>{0x84DFU}) ==
          std::array<std::uint32_t, 2>{0x84DFU, 0U});
    compatible.SetEnabled(kGles1TextureCoordArray, 0x84DFU, false);
    CHECK_THROWS_WITH_AS(
        static_cast<void>(compatible.ResolveTextureCoordinateUnits(
            std::array<std::uint32_t, 2>{0x84C1U, 0x84DFU})),
        "GLES1 multi-stage draw has no texture coordinate array for one "
        "sampled unit; coordinate arrays cannot be shared",
        std::runtime_error);

    AndroidBoundaryGles1DrawState strict(false);
    strict.SetEnabled(kGles1TextureCoordArray, 0x84C0U, true);
    CHECK_THROWS_WITH_AS(
        static_cast<void>(strict.ResolveTextureCoordinateUnits(
            std::array<std::uint32_t, 1>{0x84DFU})),
        "GLES1 single-stage texture coordinate array fallback is disabled",
        std::runtime_error);
}

TEST_CASE("Android boundary publishes GLES1 core without silent handlers") {
    BoundaryFixture fixture;
    CHECK(ogplay::gles::GlesFunctionCount(ogplay::gles::GlesApi::gles1) == 145);
    for (std::size_t index = 0;
         index < ogplay::gles::GlesFunctionCount(ogplay::gles::GlesApi::gles1);
         ++index) {
        const auto function = ogplay::gles::DescribeGlesFunction(
            ogplay::gles::GlesApi::gles1,
            static_cast<ogplay::gles::GlesThunkId>(index));
        CAPTURE(function.name);
        CHECK(fixture.boundary.Symbols()
                  .Lookup("libGLESv1_CM.so", function.name)
                  .has_value());
    }
    CHECK_THROWS_WITH_AS(
        fixture.Call("libGLESv1_CM.so", "glViewport", {0, 0, 4, 3}),
        "glViewport has no current ANGLE frame", std::runtime_error);
    CHECK_THROWS_WITH_AS(
        fixture.Call("libGLESv1_CM.so", "glScissor", {0, 0, 4, 3}),
        "glScissor has no current ANGLE frame", std::runtime_error);
    CHECK_THROWS_WITH_AS(
        fixture.Call("libGLESv1_CM.so", "glShadeModel",
                     {ogplay::runtime::detail::kGles1SmoothShadeModel}),
        "glShadeModel has no current ANGLE frame", std::runtime_error);
    CHECK_THROWS_WITH_AS(
        fixture.Call("libGLESv1_CM.so", "glClear", {0x00004000U}),
        "glClear has no current ANGLE frame", std::runtime_error);
    CHECK_THROWS_WITH_AS(
        fixture.Call("libGLESv1_CM.so", "glClearColor"),
        "glClearColor has no current ANGLE frame", std::runtime_error);
    CHECK_THROWS_WITH_AS(
        fixture.Call("libGLESv1_CM.so", "glClearDepthf",
                     {std::bit_cast<std::uint32_t>(1.0F)}),
        "glClearDepthf has no current ANGLE frame",
        std::runtime_error);
    CHECK_THROWS_WITH_AS(
        fixture.Call("libGLESv1_CM.so", "glGetIntegerv",
                     {0x84E2U, fixture.output.Value()}),
        "glGetIntegerv has no current ANGLE frame", std::runtime_error);
    CHECK_THROWS_WITH_AS(
        fixture.Call("libGLESv1_CM.so", "glGetBooleanv",
                     {0x0DE1U, fixture.output.Value()}),
        "glGetBooleanv has no current ANGLE frame", std::runtime_error);
    CHECK_THROWS_WITH_AS(
        fixture.Call("libGLESv1_CM.so", "glIsEnabled", {0x0DE1U}),
        "glIsEnabled has no current ANGLE frame", std::runtime_error);
    CHECK_THROWS_WITH_AS(
        fixture.Call("libGLESv1_CM.so", "glGenBuffers",
                     {1U, fixture.output.Value()}),
        "glGenBuffers has no current ANGLE frame", std::runtime_error);
    CHECK_THROWS_WITH_AS(
        fixture.Call("libGLESv1_CM.so", "glBufferData",
                     {0x8892U, 16U, 0U, 0x88E4U}),
        "glBufferData has no current ANGLE frame", std::runtime_error);
    CHECK_THROWS_WITH_AS(
        fixture.Call("libGLESv1_CM.so", "glBufferSubData",
                     {0x8892U, 0U, 4U, fixture.output.Value()}),
        "glBufferSubData has no current ANGLE frame", std::runtime_error);
    fixture.bus.Write32(fixture.stack, 0x1908U, 1U);
    fixture.bus.Write32(fixture.stack.Add(4U), 0x1401U, 1U);
    fixture.bus.Write32(fixture.stack.Add(8U), fixture.output.Value(), 1U);
    CHECK_THROWS_WITH_AS(
        fixture.Call("libGLESv1_CM.so", "glReadPixels",
                     {0U, 0U, 1U, 1U}),
        "glReadPixels has no current ANGLE frame", std::runtime_error);
    CHECK_THROWS_WITH_AS(
        fixture.Call("libGLESv1_CM.so", "glMultMatrixf",
                     {fixture.output.Value()}),
        "glMultMatrixf has no current ANGLE frame", std::runtime_error);
    CHECK_THROWS_WITH_AS(
        fixture.Call("libGLESv1_CM.so", "glNormal3f", {0U, 0U, 0U}),
        "glNormal3f has no current ANGLE frame", std::runtime_error);
    CHECK_THROWS_WITH_AS(
        fixture.Call("libGLESv1_CM.so", "glClipPlanef",
                     {0x3000U, fixture.output.Value()}),
        "glClipPlanef has no current ANGLE frame", std::runtime_error);
    const std::array scalar_calls{
        std::pair<std::string_view, std::array<std::uint32_t, 4>>{
            "glActiveTexture", {0x84C0U}},
        std::pair<std::string_view, std::array<std::uint32_t, 4>>{
            "glBindBuffer", {0x8892U, 0U}},
        std::pair<std::string_view, std::array<std::uint32_t, 4>>{
            "glBindTexture", {0x0DE1U, 0U}},
        std::pair<std::string_view, std::array<std::uint32_t, 4>>{
            "glBlendFunc", {1U, 0U}},
        std::pair<std::string_view, std::array<std::uint32_t, 4>>{
            "glColorMask", {1U, 1U, 1U, 1U}},
        std::pair<std::string_view, std::array<std::uint32_t, 4>>{
            "glClearStencil", {3U}},
        std::pair<std::string_view, std::array<std::uint32_t, 4>>{
            "glCullFace", {0x0405U}},
        std::pair<std::string_view, std::array<std::uint32_t, 4>>{
            "glDepthFunc", {0x0201U}},
        std::pair<std::string_view, std::array<std::uint32_t, 4>>{
            "glDepthMask", {1U}},
        std::pair<std::string_view, std::array<std::uint32_t, 4>>{
            "glDepthRangef", {0U, std::bit_cast<std::uint32_t>(1.0F)}},
        std::pair<std::string_view, std::array<std::uint32_t, 4>>{
            "glDisable", {0x0C11U}},
        std::pair<std::string_view, std::array<std::uint32_t, 4>>{
            "glEnable", {0x0C11U}},
        std::pair<std::string_view, std::array<std::uint32_t, 4>>{
            "glFinish", {}},
        std::pair<std::string_view, std::array<std::uint32_t, 4>>{
            "glFrontFace", {0x0901U}},
        std::pair<std::string_view, std::array<std::uint32_t, 4>>{
            "glGetError", {}},
        std::pair<std::string_view, std::array<std::uint32_t, 4>>{
            "glHint", {0x8192U, 0x1100U}},
        std::pair<std::string_view, std::array<std::uint32_t, 4>>{
            "glLineWidth", {std::bit_cast<std::uint32_t>(1.0F)}},
        std::pair<std::string_view, std::array<std::uint32_t, 4>>{
            "glPixelStorei", {0x0CF5U, 4U}},
        std::pair<std::string_view, std::array<std::uint32_t, 4>>{
            "glPointParameterf",
            {0x8126U, std::bit_cast<std::uint32_t>(1.0F)}},
        std::pair<std::string_view, std::array<std::uint32_t, 4>>{
            "glPointSize", {std::bit_cast<std::uint32_t>(2.0F)}},
        std::pair<std::string_view, std::array<std::uint32_t, 4>>{
            "glPolygonOffset", {0U, 0U}},
        std::pair<std::string_view, std::array<std::uint32_t, 4>>{
            "glStencilFunc", {0x0207U, 0U, 0xFFU}},
        std::pair<std::string_view, std::array<std::uint32_t, 4>>{
            "glStencilMask", {0xFFU}},
        std::pair<std::string_view, std::array<std::uint32_t, 4>>{
            "glStencilOp", {0x1E00U, 0x1E00U, 0x1E00U}},
        std::pair<std::string_view, std::array<std::uint32_t, 4>>{
            "glTexParameterf",
            {0x0DE1U, 0x2801U, std::bit_cast<std::uint32_t>(9729.0F)}},
        std::pair<std::string_view, std::array<std::uint32_t, 4>>{
            "glTexParameteri", {0x0DE1U, 0x2800U, 0x2601U}},
    };
    for (const auto& [symbol, arguments] : scalar_calls) {
        CAPTURE(symbol);
        const auto expected =
            std::string(symbol) + " has no current ANGLE frame";
        CHECK_THROWS_WITH_AS(
            fixture.Call("libGLESv1_CM.so", symbol, arguments),
            expected.c_str(), std::runtime_error);
    }
    const std::array matrix_calls{
        std::pair<std::string_view, std::array<std::uint32_t, 4>>{
            "glMatrixMode", {ogplay::runtime::detail::kGles1Modelview}},
        std::pair<std::string_view, std::array<std::uint32_t, 4>>{
            "glLoadIdentity", {}},
        std::pair<std::string_view, std::array<std::uint32_t, 4>>{
            "glLoadMatrixf", {fixture.output.Value()}},
        std::pair<std::string_view, std::array<std::uint32_t, 4>>{
            "glPushMatrix", {}},
        std::pair<std::string_view, std::array<std::uint32_t, 4>>{
            "glPopMatrix", {}},
        std::pair<std::string_view, std::array<std::uint32_t, 4>>{
            "glRotatef", {std::bit_cast<std::uint32_t>(45.0F), 0U, 0U,
                           std::bit_cast<std::uint32_t>(1.0F)}},
        std::pair<std::string_view, std::array<std::uint32_t, 4>>{
            "glTranslatef", {std::bit_cast<std::uint32_t>(1.0F), 0U, 0U}},
    };
    for (const auto& [symbol, arguments] : matrix_calls) {
        CAPTURE(symbol);
        const auto expected = std::string(symbol) +
                              " has no current ANGLE frame";
        CHECK_THROWS_WITH_AS(
            fixture.Call("libGLESv1_CM.so", symbol, arguments),
            expected.c_str(), std::runtime_error);
    }
    if (ogplay::gles::IsNativeAngleEglAvailable()) {
        fixture.boundary.OpenManagedSurface();
        const auto query_output = fixture.output.Add(0x200U);
        for (const auto [pname, expected] :
             std::array<std::pair<std::uint32_t, std::uint32_t>, 6>{
                 {{0x807AU, 4U}, {0x807BU, 0x1406U}, {0x807CU, 0U},
                  {0x8088U, 4U}, {0x8089U, 0x1406U}, {0x808AU, 0U}}}) {
            CHECK(fixture.Call("libGLESv1_CM.so", "glGetIntegerv",
                               {pname, query_output.Value()}) == 0U);
            CHECK(fixture.bus.Read32(query_output, 1U) == expected);
        }
        const auto vendor = fixture.Call(
            "libGLESv1_CM.so", "glGetString", {0x1F00U});
        REQUIRE(vendor != 0U);
        CHECK(fixture.bus.Read8(ogplay::memory::GuestAddress{vendor}, 1U) != 0U);
        CHECK_THROWS_AS(
            fixture.memory.Write(ogplay::memory::GuestAddress{vendor},
                                 std::array{std::byte{'X'}}, 1U),
            ogplay::memory::MemoryFault);
        const auto shading_language = fixture.Call(
            "libGLESv1_CM.so", "glGetString", {0x8B8CU});
        REQUIRE(shading_language != 0U);
        CHECK(fixture.bus.Read8(
                  ogplay::memory::GuestAddress{shading_language}, 1U) != 0U);
        for (const auto pname : std::array{
                 0x8869U, 0x8872U, 0x8B4CU, 0x8B4DU, 0x8DF9U, 0x8DFBU,
                 0x8DFCU, 0x8DFDU, 0x8B8DU, 0x8CA6U, 0x8CA7U}) {
            fixture.bus.Write32(query_output, 0x7fffffffU, 1U);
            CHECK(fixture.Call("libGLESv1_CM.so", "glGetIntegerv",
                               {pname, query_output.Value()}) == 0U);
            CHECK(fixture.bus.Read32(query_output, 1U) != 0x7fffffffU);
        }
        CHECK(fixture.Call(
                  "libGLESv1_CM.so", "glMatrixMode",
                  {ogplay::runtime::detail::kGles1Projection}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glLoadIdentity") == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glPushMatrix") == 0U);
        CHECK(fixture.Call(
                  "libGLESv1_CM.so", "glTranslatef",
                  {std::bit_cast<std::uint32_t>(1.0F),
                   std::bit_cast<std::uint32_t>(2.0F),
                   std::bit_cast<std::uint32_t>(3.0F)}) == 0U);
        CHECK(fixture.Call(
                  "libGLESv1_CM.so", "glRotatef",
                  {std::bit_cast<std::uint32_t>(45.0F), 0U, 0U,
                   std::bit_cast<std::uint32_t>(1.0F)}) == 0U);
        for (std::size_t element = 0; element < 16; ++element) {
            const auto value = element % 5 == 0 ? 1.0F : 0.0F;
            fixture.bus.Write32(
                fixture.output.Add(element * sizeof(std::uint32_t)),
                std::bit_cast<std::uint32_t>(value), 1U);
        }
        CHECK(fixture.Call("libGLESv1_CM.so", "glLoadMatrixf",
                           {fixture.output.Value()}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glPopMatrix") == 0U);
        CHECK(fixture.Call(
                  "libGLESv1_CM.so", "glMatrixMode",
                  {ogplay::runtime::detail::kGles1Modelview}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glLoadIdentity") == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glTranslatex",
                           {65536U, 131072U, 196608U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glScalex",
                           {131072U, 196608U, 262144U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glGetFloatv",
                           {0x0BA6U, query_output.Value()}) == 0U);
        const auto matrix_value = [&fixture, query_output](const std::size_t index) {
            return std::bit_cast<float>(fixture.bus.Read32(
                query_output.Add(index * sizeof(std::uint32_t)), 1U));
        };
        CHECK(matrix_value(0U) == 2.0F);
        CHECK(matrix_value(5U) == 3.0F);
        CHECK(matrix_value(10U) == 4.0F);
        CHECK(matrix_value(12U) == 1.0F);
        CHECK(matrix_value(13U) == 2.0F);
        CHECK(matrix_value(14U) == 3.0F);
        const auto vector = fixture.output.Add(0x300U);
        fixture.bus.Write32(vector, 65536U, 1U);
        fixture.bus.Write32(vector.Add(4U), 32768U, 1U);
        fixture.bus.Write32(vector.Add(8U), 16384U, 1U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glPointParameterxv",
                           {0x8129U, vector.Value()}) == 0U);
        fixture.bus.Write32(fixture.stack, 65536U, 1U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glMultiTexCoord4x",
                           {0x84C0U, 16384U, 32768U, 0U}) == 0U);
        fixture.bus.Write32(vector.Add(4U), 0U, 1U);
        fixture.bus.Write32(vector.Add(8U), 0U, 1U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glPointParameterxv",
                           {0x8129U, vector.Value()}) == 0U);
        fixture.bus.Write32(fixture.stack, 65536U, 1U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glMultiTexCoord4x",
                           {0x84C0U, 0U, 0U, 0U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glLoadIdentity") == 0U);
        CHECK(fixture.Call(
                  "libGLESv1_CM.so", "glMatrixMode",
                  {ogplay::runtime::detail::kGles1Projection}) == 0U);
        try {
            static_cast<void>(fixture.Call(
                "libGLESv1_CM.so", "glGenTextures", {1U, 0U}));
            FAIL_CHECK("required GLES transfer unexpectedly succeeded");
        } catch (const ogplay::gles::GuestTransferError& error) {
            const std::string message{error.what()};
            CHECK(message.find(
                      "libGLESv1_CM.so!glGenTextures: required guest pointer "
                      "is null") != std::string::npos);
            CHECK(message.find("r0=1 r1=0 r2=0 r3=0") !=
                  std::string::npos);
            CHECK(message.find("sp=") != std::string::npos);
            CHECK(message.find("lr=") != std::string::npos);
            CHECK(message.find("thread=1") != std::string::npos);
        }
        // Invalid GL values latch the per-context error instead of throwing.
        CHECK(fixture.Call("libGLESv1_CM.so", "glGenTextures",
                           {std::bit_cast<std::uint32_t>(-1),
                            fixture.output.Value()}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glGetError") == 0x0501U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glGetError") == 0U);
        CHECK_THROWS_AS(
            fixture.Call(
                "libGLESv1_CM.so", "glGenTextures",
                {2U, fixture.output.Add(fixture.memory.PageSize() - 4U).Value()}),
            ogplay::memory::MemoryFault);
        CHECK(fixture.Call("libGLESv1_CM.so", "glGenTextures",
                           {2U, fixture.output.Value()}) == 0U);
        const auto texture = fixture.bus.Read32(fixture.output, 1U);
        REQUIRE(texture != 0U);
        const auto second_texture =
            fixture.bus.Read32(fixture.output.Add(4U), 1U);
        REQUIRE(second_texture != 0U);
        CHECK(second_texture != texture);
        const auto buffer_names = fixture.output.Add(0x280U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glGenBuffers",
                           {1U, buffer_names.Value()}) == 0U);
        const auto buffer = fixture.bus.Read32(buffer_names, 1U);
        REQUIRE(buffer != 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glBindBuffer",
                           {0x8892U, buffer}) == 0U);
        const auto buffer_data = fixture.output.Add(0x300U);
        fixture.bus.Write32(buffer_data, 0x11223344U, 1U);
        fixture.bus.Write32(buffer_data.Add(4U), 0x55667788U, 1U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glBufferData",
                           {0x8892U, 8U, buffer_data.Value(), 0x88E4U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glBufferSubData",
                           {0x8892U, 4U, 4U, buffer_data.Value()}) == 0U);
        const auto object_query = fixture.output.Add(0x2c0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glGetBufferParameteriv",
                           {0x8892U, 0x8764U, object_query.Value()}) == 0U);
        CHECK(fixture.bus.Read32(object_query, 1U) == 8U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glGetBufferParameteriv",
                           {0x8892U, 0x8765U, object_query.Value()}) == 0U);
        CHECK(fixture.bus.Read32(object_query, 1U) == 0x88E4U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glIsBuffer", {buffer}) == 1U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glBufferData",
                           {0x8892U, 0xFFFFFFFFU, 0U, 0x88E4U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glGetError") == 0x0501U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glGetError") == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glDeleteBuffers",
                           {1U, buffer_names.Value()}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glIsBuffer", {buffer}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glActiveTexture",
                           {0x84C0U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glEnable",
                           {0x0DE1U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glIsEnabled",
                           {0x0DE1U}) == 1U);
        const auto boolean_query = fixture.output.Add(0x210U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glGetBooleanv",
                           {0x0DE1U, boolean_query.Value()}) == 0U);
        CHECK(fixture.bus.Read8(boolean_query, 1U) == 1U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glDisable",
                           {0x0DE1U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glIsEnabled",
                           {0x0DE1U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glBindBuffer",
                           {0x8892U, 0U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glBindTexture",
                           {0x0DE1U, texture}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glIsTexture", {texture}) == 1U);
        fixture.bus.Write32(object_query, 0x2601U, 1U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glTexParameteriv",
                           {0x0DE1U, 0x2801U, object_query.Value()}) == 0U);
        fixture.bus.Write32(object_query, 0U, 1U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glGetTexParameteriv",
                           {0x0DE1U, 0x2801U, object_query.Value()}) == 0U);
        CHECK(fixture.bus.Read32(object_query, 1U) == 0x2601U);
        const auto readback = fixture.output.Add(0x380U);
        CHECK(fixture.Call(
                  "libGLESv1_CM.so", "glClearColor",
                  {std::bit_cast<std::uint32_t>(1.0F), 0U, 0U,
                   std::bit_cast<std::uint32_t>(1.0F)}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glClear",
                           {0x00004000U}) == 0U);
        fixture.bus.Write32(fixture.stack, 0x1908U, 1U);
        fixture.bus.Write32(fixture.stack.Add(4U), 0x1401U, 1U);
        fixture.bus.Write32(fixture.stack.Add(8U), readback.Value(), 1U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glReadPixels",
                           {0U, 0U, 1U, 1U}) == 0U);
        CHECK(fixture.bus.Read8(readback, 1U) == 255U);
        CHECK(fixture.bus.Read8(readback.Add(1U), 1U) == 0U);
        CHECK(fixture.bus.Read8(readback.Add(2U), 1U) == 0U);
        CHECK(fixture.bus.Read8(readback.Add(3U), 1U) == 255U);
        CHECK(fixture.Call(
                  "libGLESv1_CM.so", "glAlphaFunc",
                  {0x0201U, std::bit_cast<std::uint32_t>(0.5F)}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glClientActiveTexture",
                           {0x84C1U}) == 0U);
        CHECK(fixture.Call(
                  "libGLESv1_CM.so", "glColor4f",
                  {std::bit_cast<std::uint32_t>(0.25F),
                   std::bit_cast<std::uint32_t>(0.5F),
                   std::bit_cast<std::uint32_t>(0.75F),
                   std::bit_cast<std::uint32_t>(1.0F)}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glColor4ub",
                           {16U, 32U, 64U, 255U}) == 0U);
        const auto environment = fixture.output.Add(0x100U);
        for (std::size_t index = 0; index < 4U; ++index) {
            fixture.bus.Write32(
                environment.Add(index * sizeof(std::uint32_t)),
                std::bit_cast<std::uint32_t>(0.25F), 1U);
        }
        CHECK(fixture.Call(
                  "libGLESv1_CM.so", "glTexEnvfv",
                  {ogplay::runtime::detail::kGles1TextureEnvironment,
                   ogplay::runtime::detail::kGles1TextureEnvironmentColor,
                   environment.Value()}) == 0U);
        CHECK_THROWS_AS(
            fixture.Call(
                "libGLESv1_CM.so", "glTexEnvfv",
                {ogplay::runtime::detail::kGles1TextureEnvironment,
                 ogplay::runtime::detail::kGles1TextureEnvironmentColor, 0U}),
            ogplay::memory::MemoryFault);
        CHECK(fixture.Call(
                  "libGLESv1_CM.so", "glTexEnvf",
                  {ogplay::runtime::detail::kGles1TextureEnvironment,
                   ogplay::runtime::detail::kGles1TextureEnvironmentMode,
                   std::bit_cast<std::uint32_t>(8448.0F)}) == 0U);
        CHECK(fixture.Call(
                  "libGLESv1_CM.so", "glTexEnvi",
                  {ogplay::runtime::detail::kGles1TextureEnvironment,
                   ogplay::runtime::detail::kGles1TextureEnvironmentMode,
                   0x2100U}) == 0U);
        fixture.bus.Write32(environment, 0x2100U, 1U);
        CHECK(fixture.Call(
                  "libGLESv1_CM.so", "glTexEnvxv",
                  {ogplay::runtime::detail::kGles1TextureEnvironment,
                   ogplay::runtime::detail::kGles1TextureEnvironmentMode,
                   environment.Value()}) == 0U);
        fixture.bus.Write32(environment, 0U, 1U);
        CHECK(fixture.Call(
                  "libGLESv1_CM.so", "glGetTexEnvxv",
                  {ogplay::runtime::detail::kGles1TextureEnvironment,
                   ogplay::runtime::detail::kGles1TextureEnvironmentMode,
                   environment.Value()}) == 0U);
        CHECK(fixture.bus.Read32(environment, 1U) == 0x2100U);
        const auto float_query = fixture.output.Add(0x200U);
        CHECK(fixture.Call(
                  "libGLESv1_CM.so", "glGetFloatv",
                  {ogplay::runtime::detail::kGles1MaxTextureAnisotropy,
                   float_query.Value()}) == 0U);
        CHECK(std::bit_cast<float>(fixture.bus.Read32(float_query, 1U)) >= 1.0F);
        CHECK_THROWS_AS(
            fixture.Call(
                "libGLESv1_CM.so", "glGetFloatv",
                {ogplay::runtime::detail::kGles1MaxTextureAnisotropy, 0U}),
            ogplay::gles::GuestTransferError);
        const auto integer_query = fixture.output.Add(0x220U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glGetIntegerv",
                           {0x84E2U, integer_query.Value()}) == 0U);
        CHECK(fixture.bus.Read32(integer_query, 1U) == 2U);
        const std::array owned_integer_queries{
            std::pair{0x0BA0U, ogplay::runtime::detail::kGles1Projection},
            std::pair{0x0BA4U, 1U}, std::pair{0x0B54U, 0x1D01U},
            std::pair{0x0D32U, 6U},
            std::pair{0x0CF5U, 4U}, std::pair{0x8069U, texture},
            std::pair{0x84E0U, 0x84C0U}, std::pair{0x84E1U, 0x84C1U},
            std::pair{0x8894U, 0U}};
        for (const auto& [pname, expected] : owned_integer_queries) {
            CAPTURE(pname);
            CHECK(fixture.Call("libGLESv1_CM.so", "glGetIntegerv",
                               {pname, integer_query.Value()}) == 0U);
            CHECK(fixture.bus.Read32(integer_query, 1U) == expected);
        }
        CHECK(fixture.Call("libGLESv1_CM.so", "glGetIntegerv",
                           {0x0D56U, integer_query.Value()}) == 0U);
        CHECK(fixture.bus.Read32(integer_query, 1U) > 0U);
        CHECK_THROWS_AS(
            fixture.Call("libGLESv1_CM.so", "glGetIntegerv",
                         {0x84E2U, 0U}),
            ogplay::gles::GuestTransferError);
        CHECK(fixture.Call("libGLESv1_CM.so", "glGetIntegerv",
                           {0xDEADBEEFU, integer_query.Value()}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glGetError") == 0x0500U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glGetError") == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glBlendFunc",
                           {1U, 0U}) == 0U);
        for (const auto [pname, expected] :
             {std::pair{0x0BE0U, 1U}, std::pair{0x0BE1U, 0U}}) {
            CHECK(fixture.Call("libGLESv1_CM.so", "glGetIntegerv",
                               {pname, integer_query.Value()}) == 0U);
            CHECK(fixture.bus.Read32(integer_query, 1U) == expected);
        }
        CHECK(fixture.Call("libGLESv1_CM.so", "glColorMask",
                           {1U, 1U, 1U, 1U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glCullFace",
                           {0x0405U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glDepthFunc",
                           {0x0201U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glDepthMask", {1U}) == 0U);
        CHECK(fixture.Call(
                  "libGLESv1_CM.so", "glDepthRangef",
                  {std::bit_cast<std::uint32_t>(0.25F),
                   std::bit_cast<std::uint32_t>(0.75F)}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glFrontFace",
                           {0x0901U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glHint",
                           {0x8192U, 0x1100U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glHint",
                           {0x0C50U, 0x1102U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glPixelStorei",
                           {0x0CF5U, 4U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glClearStencil", {3U}) == 0U);
        CHECK(fixture.Call(
                  "libGLESv1_CM.so", "glLineWidth",
                  {std::bit_cast<std::uint32_t>(1.0F)}) == 0U);
        CHECK(fixture.Call(
                  "libGLESv1_CM.so", "glPointParameterf",
                  {0x8126U, std::bit_cast<std::uint32_t>(1.0F)}) == 0U);
        CHECK(fixture.Call(
                  "libGLESv1_CM.so", "glPointSize",
                  {std::bit_cast<std::uint32_t>(2.0F)}) == 0U);
        CHECK(fixture.Call(
                  "libGLESv1_CM.so", "glPolygonOffset",
                  {std::bit_cast<std::uint32_t>(1.0F),
                   std::bit_cast<std::uint32_t>(2.0F)}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glStencilFunc",
                           {0x0202U, 2U, 0x7FU}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glStencilMask", {0x3FU}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glStencilOp",
                           {0x1E00U, 0x1E01U, 0x1E02U}) == 0U);
        CHECK(fixture.Call(
                  "libGLESv1_CM.so", "glTexParameterf",
                  {0x0DE1U, 0x2801U,
                   std::bit_cast<std::uint32_t>(9729.0F)}) == 0U);
        CHECK(fixture.Call(
                  "libGLESv1_CM.so", "glTexParameterf",
                  {0x0DE1U, ogplay::runtime::detail::kGles1GenerateMipmap,
                   std::bit_cast<std::uint32_t>(0.0F)}) == 0U);
        CHECK(fixture.Call(
                  "libGLESv1_CM.so", "glTexParameterf",
                  {0x0DE1U, ogplay::runtime::detail::kGles1GenerateMipmap,
                   std::bit_cast<std::uint32_t>(2.0F)}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glGetError") == 0x0500U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glGetError") == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glTexParameteri",
                           {0x0DE1U, 0x2800U, 0x2601U}) == 0U);
        const auto texture_pixels = fixture.output.Add(0x300U);
        const std::array<std::byte, 16> rgba_pixels{
            std::byte{0xff}, std::byte{}, std::byte{}, std::byte{0xff},
            std::byte{}, std::byte{0xff}, std::byte{}, std::byte{0xff},
            std::byte{}, std::byte{}, std::byte{0xff}, std::byte{0xff},
            std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}};
        fixture.memory.Write(texture_pixels, rgba_pixels, 1U);
        fixture.bus.Write32(fixture.stack, 2U, 1U);
        fixture.bus.Write32(fixture.stack.Add(4U), 0U, 1U);
        fixture.bus.Write32(fixture.stack.Add(8U), 0x1908U, 1U);
        fixture.bus.Write32(fixture.stack.Add(12U), 0x1401U, 1U);
        fixture.bus.Write32(fixture.stack.Add(16U), texture_pixels.Value(), 1U);
        CHECK(fixture.Call(
                  "libGLESv1_CM.so", "glTexParameteri",
                  {0x0DE1U, ogplay::runtime::detail::kGles1GenerateMipmap,
                   1U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glTexImage2D",
                           {0x0DE1U, 0U, 0x1908U, 2U}) == 0U);
        CHECK(fixture.Call(
                  "libGLESv1_CM.so", "glTexParameteri",
                  {0x0DE1U, ogplay::runtime::detail::kGles1GenerateMipmap,
                   0U}) == 0U);
        fixture.bus.Write32(fixture.stack.Add(16U), 0U, 1U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glTexImage2D",
                           {0x0DE1U, 0U, 0x1908U, 2U}) == 0U);
        fixture.bus.Write32(fixture.stack, 1U, 1U);
        fixture.bus.Write32(fixture.stack.Add(4U), 1U, 1U);
        fixture.bus.Write32(fixture.stack.Add(8U), 0x1908U, 1U);
        fixture.bus.Write32(fixture.stack.Add(12U), 0x1401U, 1U);
        fixture.bus.Write32(fixture.stack.Add(16U), texture_pixels.Value(), 1U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glTexSubImage2D",
                           {0x0DE1U, 0U, 0U, 0U}) == 0U);
        fixture.bus.Write32(fixture.stack.Add(16U), 0U, 1U);
        CHECK_THROWS_AS(
            fixture.Call("libGLESv1_CM.so", "glTexSubImage2D",
                         {0x0DE1U, 0U, 0U, 0U}),
            ogplay::gles::GuestTransferError);
        fixture.bus.Write32(fixture.stack, 0U, 1U);
        fixture.bus.Write32(fixture.stack.Add(4U), 1U, 1U);
        fixture.bus.Write32(fixture.stack.Add(8U), 1U, 1U);
        fixture.bus.Write32(fixture.stack.Add(12U), 0U, 1U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glCopyTexImage2D",
                           {0x0DE1U, 0U, 0x1908U, 0U}) == 0U);
        fixture.bus.Write32(fixture.stack, 4U, 1U);
        fixture.bus.Write32(fixture.stack.Add(4U), 0U, 1U);
        fixture.bus.Write32(fixture.stack.Add(8U), 8U, 1U);
        fixture.bus.Write32(fixture.stack.Add(12U), texture_pixels.Value(), 1U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glCompressedTexImage2D",
                           {0x0DE1U, 0U, 0x8D64U, 4U}) == 0U);
        fixture.bus.Write32(fixture.stack.Add(8U), 0xFFFFFFFFU, 1U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glCompressedTexImage2D",
                           {0x0DE1U, 0U, 0x8D64U, 4U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glGetError") == 0x0501U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glGetError") == 0U);
        const auto vertices = fixture.output.Add(0x400U);
        const std::array vertex_values{
            -0.75F, -0.75F, 0.0F, 0.75F, -0.75F, 0.0F,
            0.0F, 0.75F, 0.0F};
        for (std::size_t index = 0; index < vertex_values.size(); ++index) {
            fixture.bus.Write32(
                vertices.Add(index * sizeof(std::uint32_t)),
                std::bit_cast<std::uint32_t>(vertex_values[index]), 1U);
        }
        const auto colors = fixture.output.Add(0x440U);
        const std::array<std::byte, 12> color_values{
            std::byte{0xff}, std::byte{}, std::byte{}, std::byte{0xff},
            std::byte{}, std::byte{0xff}, std::byte{}, std::byte{0xff},
            std::byte{}, std::byte{}, std::byte{0xff}, std::byte{0xff}};
        fixture.memory.Write(colors, color_values, 1U);
        const auto normals = fixture.output.Add(0x480U);
        const auto texcoords = fixture.output.Add(0x4C0U);
        for (std::size_t vertex = 0; vertex < 3U; ++vertex) {
            for (std::size_t component = 0; component < 3U; ++component) {
                fixture.bus.Write32(
                    normals.Add((vertex * 3U + component) * 4U),
                    std::bit_cast<std::uint32_t>(component == 2U ? 1.0F : 0.0F), 1U);
            }
            fixture.bus.Write32(texcoords.Add(vertex * 8U), 0U, 1U);
            fixture.bus.Write32(texcoords.Add(vertex * 8U + 4U), 0U, 1U);
        }
        CHECK(fixture.Call("libGLESv1_CM.so", "glVertexPointer",
                           {3U, 0x1406U, 0U, vertices.Value()}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glColorPointer",
                           {4U, 0x1401U, 0U, colors.Value()}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glNormalPointer",
                           {0x1406U, 0U, normals.Value()}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glClientActiveTexture",
                           {0x84C0U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glTexCoordPointer",
                           {2U, 0x1406U, 0U, texcoords.Value()}) == 0U);
        for (const auto array : {
                 ogplay::runtime::detail::kGles1VertexArray,
                 ogplay::runtime::detail::kGles1ColorArray,
                 ogplay::runtime::detail::kGles1NormalArray,
                 ogplay::runtime::detail::kGles1TextureCoordArray}) {
            CHECK(fixture.Call("libGLESv1_CM.so", "glEnableClientState",
                               {array}) == 0U);
        }
        CHECK(fixture.Call("libGLESv1_CM.so", "glClientActiveTexture",
                           {0x84C1U}) == 0U);
        fixture.bus.Write32(fixture.stack, 1U, 1U);
        fixture.bus.Write32(fixture.stack.Add(4U), 0U, 1U);
        fixture.bus.Write32(fixture.stack.Add(8U), 0x1908U, 1U);
        fixture.bus.Write32(fixture.stack.Add(12U), 0x1401U, 1U);
        fixture.bus.Write32(fixture.stack.Add(16U), texture_pixels.Value(), 1U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glTexImage2D",
                           {0x0DE1U, 0U, 0x1908U, 1U}) == 0U);
        CHECK(fixture.Call(
                  "libGLESv1_CM.so", "glTexEnvi",
                  {ogplay::runtime::detail::kGles1TextureEnvironment,
                   ogplay::runtime::detail::kGles1TextureEnvironmentMode,
                   0x1E01U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glEnable",
                           {0x0DE1U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glDrawArrays",
                           {0x0004U, 0U, 3U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glActiveTexture",
                           {0x84C1U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glBindTexture",
                           {0x0DE1U, second_texture}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glTexImage2D",
                           {0x0DE1U, 0U, 0x1908U, 1U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glEnable",
                           {0x0DE1U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glTexCoordPointer",
                           {2U, 0x1406U, 0U, texcoords.Value()}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glEnableClientState",
                           {ogplay::runtime::detail::kGles1TextureCoordArray}) == 0U);
        CHECK(fixture.Call(
                  "libGLESv1_CM.so", "glClearColor",
                  {0U, 0U, 0U, std::bit_cast<std::uint32_t>(1.0F)}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glClear",
                           {0x00004000U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glDrawArrays",
                           {0x0004U, 0U, 3U}) == 0U);
        fixture.boundary.PresentManagedSurface();
        const auto multitexture_frame = fixture.boundary.TakeLatestFrame();
        REQUIRE(multitexture_frame.has_value());
        bool found_red_pixel = false;
        for (std::size_t pixel = 0; pixel < multitexture_frame->rgba8.size();
             pixel += 4U) {
            found_red_pixel |= multitexture_frame->rgba8[pixel] > 200U &&
                               multitexture_frame->rgba8[pixel + 1U] < 20U &&
                               multitexture_frame->rgba8[pixel + 2U] < 20U;
        }
        CHECK(found_red_pixel);
        CHECK(fixture.Call("libGLESv1_CM.so", "glDisableClientState",
                           {ogplay::runtime::detail::kGles1TextureCoordArray}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glDisable",
                           {0x0DE1U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glActiveTexture",
                           {0x84C0U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glClientActiveTexture",
                           {0x84C0U}) == 0U);
        CHECK(fixture.Call(
                  "libGLESv1_CM.so", "glTexEnvi",
                  {ogplay::runtime::detail::kGles1TextureEnvironment,
                   ogplay::runtime::detail::kGles1TextureEnvironmentMode,
                   0x0104U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glDrawArrays",
                           {0x0004U, 0U, 3U}) == 0U);
        using namespace ogplay::runtime::detail;
        for (const auto [pname, value] : std::array{
                 std::pair{kGles1TextureEnvironmentMode, 0x8570U},
                 std::pair{kGles1CombineRgb, 0x2100U},
                 std::pair{kGles1CombineAlpha, 0x2100U},
                 std::pair{kGles1Source0Rgb, 0x1702U},
                 std::pair{kGles1Source1Rgb, 0x8578U}}) {
            CHECK(fixture.Call(
                      "libGLESv1_CM.so", "glTexEnvi",
                      {kGles1TextureEnvironment, pname, value}) == 0U);
        }
        CHECK(fixture.Call("libGLESv1_CM.so", "glDrawArrays",
                           {0x0004U, 0U, 3U}) == 0U);
        for (const auto mode : {0x0BE2U, 0x2101U}) {
            CHECK(fixture.Call(
                      "libGLESv1_CM.so", "glTexEnvi",
                      {ogplay::runtime::detail::kGles1TextureEnvironment,
                       ogplay::runtime::detail::kGles1TextureEnvironmentMode,
                       mode}) == 0U);
            CHECK(fixture.Call("libGLESv1_CM.so", "glDrawArrays",
                               {0x0004U, 0U, 3U}) == 0U);
        }
        CHECK(fixture.Call("libGLESv1_CM.so", "glDisable",
                           {0x0DE1U}) == 0U);
        CHECK(fixture.Call(
                  "libGLESv1_CM.so", "glTexEnvi",
                  {ogplay::runtime::detail::kGles1TextureEnvironment,
                   ogplay::runtime::detail::kGles1TextureEnvironmentMode,
                   0x2100U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glDrawArrays",
                           {0x0004U, 0U, 3U}) == 0U);
        const auto indices = fixture.output.Add(0x500U);
        const std::array index_values{std::byte{}, std::byte{1}, std::byte{2}};
        fixture.memory.Write(indices, index_values, 1U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glDrawElements",
                           {0x0004U, 3U, 0x1401U, indices.Value()}) == 0U);
        CHECK_THROWS_AS(
            fixture.Call("libGLESv1_CM.so", "glDrawElements",
                         {0x0004U, 3U, 0x1401U, 0U}),
            ogplay::gles::GuestTransferError);
        CHECK(fixture.Call("libGLESv1_CM.so", "glDrawArrays",
                           {0x0004U, 0U, 0xFFFFFFFFU}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glGetError") == 0x0501U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glGetError") == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glDisableClientState",
                           {ogplay::runtime::detail::kGles1NormalArray}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glGetError") == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glViewport", {0, 0, 4, 3}) == 0);
        CHECK(fixture.Call("libGLESv1_CM.so", "glScissor", {0, 0, 2, 3}) == 0);
        CHECK(fixture.Call("libGLESv1_CM.so", "glEnable", {0x0C11U}) == 0);
        CHECK(fixture.Call(
                  "libGLESv1_CM.so", "glShadeModel",
                  {ogplay::runtime::detail::kGles1SmoothShadeModel}) == 0);
        CHECK(fixture.Call(
                  "libGLESv1_CM.so", "glShadeModel",
                  {ogplay::runtime::detail::kGles1FlatShadeModel}) == 0);
        CHECK(fixture.Call("libGLESv1_CM.so", "glShadeModel", {0U}) == 0);
        CHECK(fixture.Call("libGLESv1_CM.so", "glGetError") == 0x0500U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glGetError") == 0U);
        CHECK(fixture.Call(
                  "libGLESv1_CM.so", "glClearColor",
                  {std::bit_cast<std::uint32_t>(0.125F),
                   std::bit_cast<std::uint32_t>(0.25F),
                   std::bit_cast<std::uint32_t>(0.5F),
                   std::bit_cast<std::uint32_t>(1.0F)}) == 0);
        CHECK(fixture.Call("libGLESv1_CM.so", "glClearDepthf",
                           {std::bit_cast<std::uint32_t>(0.25F)}) == 0);
        CHECK(fixture.Call("libGLESv1_CM.so", "glClearColorx",
                           {8192U, 16384U, 32768U, 65536U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glClearDepthx",
                           {16384U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glClear",
                           {0x00004000U}) == 0);
        fixture.boundary.PresentManagedSurface();
        const auto frame = fixture.boundary.TakeLatestFrame();
        REQUIRE(frame.has_value());
        CHECK(frame->rgba8[0] == doctest::Approx(32).epsilon(0.04));
        CHECK(frame->rgba8[1] == doctest::Approx(64).epsilon(0.04));
        CHECK(frame->rgba8[2] == doctest::Approx(128).epsilon(0.04));
        CHECK(frame->rgba8[12] == 0U);
        CHECK(frame->rgba8[13] == 0U);
        CHECK(frame->rgba8[14] == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glDisable",
                           {0x0C11U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glFinish") == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glFlush") == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glDeleteTextures",
                           {2U, fixture.output.Value()}) == 0U);
        fixture.boundary.CloseManagedSurface();
    }
    CHECK_THROWS_WITH_AS(
        fixture.Call("libGLESv1_CM.so", "glClearDepthx", {0x00010000U}),
        "glClearDepthx has no current ANGLE frame", std::runtime_error);
}

TEST_CASE("GLES1 high sampled unit uses the unique unit-zero coordinate array") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
    fixture.boundary.OpenManagedSurface();
    CHECK(fixture.Call("libGLESv1_CM.so", "glViewport",
                       {0U, 0U, 4U, 3U}) == 0U);

    const auto texture_name = fixture.output.Add(0x300U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glGenTextures",
                       {1U, texture_name.Value()}) == 0U);
    const auto texture = fixture.bus.Read32(texture_name, 1U);
    REQUIRE(texture != 0U);
    CHECK(fixture.Call("libGLESv2.so", "glActiveTexture", {0x84DFU}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glBindTexture",
                       {0x0DE1U, texture}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glTexParameteri",
                       {0x0DE1U, 0x2800U, 0x2600U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glTexParameteri",
                       {0x0DE1U, 0x2801U, 0x2600U}) == 0U);
    const auto pixels = fixture.output.Add(0x320U);
    const std::array<std::byte, 8> texture_pixels{
        std::byte{}, std::byte{}, std::byte{}, std::byte{0xff},
        std::byte{0xff}, std::byte{}, std::byte{}, std::byte{0xff}};
    fixture.memory.Write(pixels, texture_pixels, 1U);
    fixture.bus.Write32(fixture.stack, 1U, 1U);
    fixture.bus.Write32(fixture.stack.Add(4U), 0U, 1U);
    fixture.bus.Write32(fixture.stack.Add(8U), 0x1908U, 1U);
    fixture.bus.Write32(fixture.stack.Add(12U), 0x1401U, 1U);
    fixture.bus.Write32(fixture.stack.Add(16U), pixels.Value(), 1U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glTexImage2D",
                       {0x0DE1U, 0U, 0x1908U, 2U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnable", {0x0DE1U}) == 0U);
    CHECK(fixture.Call(
              "libGLESv1_CM.so", "glTexEnvi",
              {ogplay::runtime::detail::kGles1TextureEnvironment,
               ogplay::runtime::detail::kGles1TextureEnvironmentMode,
               0x1E01U}) == 0U);

    const auto vertices = fixture.output.Add(0x380U);
    constexpr std::array vertex_values{
        -1.0F, -1.0F, 0.0F, 1.0F, -1.0F, 0.0F, 0.0F, 1.0F, 0.0F};
    for (std::size_t index = 0; index < vertex_values.size(); ++index) {
        fixture.bus.Write32(
            vertices.Add(index * sizeof(std::uint32_t)),
            std::bit_cast<std::uint32_t>(vertex_values[index]), 1U);
    }
    const auto texcoords = fixture.output.Add(0x3C0U);
    for (std::size_t vertex = 0; vertex < 3U; ++vertex) {
        fixture.bus.Write32(
            texcoords.Add(vertex * 2U * sizeof(std::uint32_t)),
            std::bit_cast<std::uint32_t>(0.75F), 1U);
        fixture.bus.Write32(
            texcoords.Add((vertex * 2U + 1U) * sizeof(std::uint32_t)),
            0U, 1U);
    }
    CHECK(fixture.Call("libGLESv1_CM.so", "glVertexPointer",
                       {3U, 0x1406U, 0U, vertices.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnableClientState",
                       {ogplay::runtime::detail::kGles1VertexArray}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glClientActiveTexture",
                       {0x84C0U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glTexCoordPointer",
                       {2U, 0x1406U, 0U, texcoords.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnableClientState",
                       {ogplay::runtime::detail::kGles1TextureCoordArray}) ==
          0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glClearColor",
                       {0U, 0U, 0U,
                        std::bit_cast<std::uint32_t>(1.0F)}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glClear", {0x00004000U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glDrawArrays",
                       {0x0004U, 0U, 3U}) == 0U);
    fixture.boundary.PresentManagedSurface();
    const auto frame = fixture.boundary.TakeLatestFrame();
    REQUIRE(frame.has_value());
    bool found_red_pixel{};
    for (std::size_t pixel = 0; pixel < frame->rgba8.size(); pixel += 4U) {
        found_red_pixel |= frame->rgba8[pixel] > 200U &&
                           frame->rgba8[pixel + 1U] < 20U &&
                           frame->rgba8[pixel + 2U] < 20U;
    }
    CHECK(found_red_pixel);
    fixture.boundary.CloseManagedSurface();
}

TEST_CASE("GLES1 cube map textures bind upload and sample the fixed pipeline") {
    BoundaryFixture fixture;
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    fixture.boundary.OpenManagedSurface();
    const auto renderer = fixture.Call(
        "libGLESv1_CM.so", "glGetString", {0x1F01U});
    REQUIRE(renderer != 0U);
    const auto renderer_length = fixture.memory.CStringLength(
        ogplay::memory::GuestAddress{renderer}, 256U, 1U);
    std::string renderer_text(renderer_length, '\0');
    fixture.memory.Read(ogplay::memory::GuestAddress{renderer},
                        std::as_writable_bytes(std::span(renderer_text)), 1U);
    CHECK(renderer_text != "OpenGL ES-CM 1.1");
    const auto version = fixture.Call(
        "libGLESv1_CM.so", "glGetString", {0x1F02U});
    REQUIRE(version != 0U);
    const auto version_length = fixture.memory.CStringLength(
        ogplay::memory::GuestAddress{version}, 64U, 1U);
    std::string version_text(version_length, '\0');
    fixture.memory.Read(ogplay::memory::GuestAddress{version},
                        std::as_writable_bytes(std::span(version_text)), 1U);
    CHECK(version_text == "OpenGL ES-CM 1.1");
    const auto extensions = fixture.Call(
        "libGLESv1_CM.so", "glGetString", {0x1F03U});
    REQUIRE(extensions != 0U);
    const auto extension_length = fixture.memory.CStringLength(
        ogplay::memory::GuestAddress{extensions}, 4096U, 1U);
    std::string extension_text(extension_length, '\0');
    fixture.memory.Read(ogplay::memory::GuestAddress{extensions},
                        std::as_writable_bytes(std::span(extension_text)), 1U);
    for (const auto* extension : {"GL_OES_texture_cube_map",
                                  "GL_OES_compressed_ETC1_RGB8_texture",
                                  "GL_IMG_texture_compression_pvrtc",
                                  "GL_OES_mapbuffer",
                                  "GL_OES_rgb8_rgba8"}) {
        CAPTURE(extension);
        CHECK(extension_text.find(std::string{extension} + ' ') !=
              std::string::npos);
    }
    CHECK(extension_text.ends_with(' '));

    const auto gles2_extensions = fixture.Call(
        "libGLESv2.so", "glGetString", {0x1F03U});
    REQUIRE(gles2_extensions != 0U);
    const auto gles2_extension_length = fixture.memory.CStringLength(
        ogplay::memory::GuestAddress{gles2_extensions}, 4096U, 1U);
    std::string gles2_extension_text(gles2_extension_length, '\0');
    fixture.memory.Read(ogplay::memory::GuestAddress{gles2_extensions},
                        std::as_writable_bytes(std::span(gles2_extension_text)), 1U);
    CHECK(gles2_extension_text.starts_with(
          "GL_OES_compressed_ETC1_RGB8_texture "
          "GL_IMG_texture_compression_pvrtc "
          "GL_OES_rgb8_rgba8 "));
    CHECK(gles2_extension_text.find("GL_OES_texture_3D") == std::string::npos);

    const auto names = fixture.output.Add(0x300U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glGenTextures",
                       {1U, names.Value()}) == 0U);
    const auto texture = fixture.bus.Read32(names, 1U);
    REQUIRE(texture != 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glBindTexture",
                       {0x8513U, texture}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glBindTexture",
                       {0x8515U, texture}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glGetError") == 0x0500U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glGetError") == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glTexParameteri",
                       {0x8513U, 0x2800U, 0x2600U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glTexParameteri",
                       {0x8513U, 0x2801U, 0x2600U}) == 0U);

    // +X uploads red, -X uploads blue and the remaining faces upload green;
    // the sampled direction (1, 0, 0) must select the red face.
    const auto faces = fixture.output.Add(0x320U);
    const std::array<std::array<std::byte, 4>, 3> face_pixels{
        std::array{std::byte{0xff}, std::byte{}, std::byte{}, std::byte{0xff}},
        std::array{std::byte{}, std::byte{}, std::byte{0xff}, std::byte{0xff}},
        std::array{std::byte{}, std::byte{0xff}, std::byte{}, std::byte{0xff}}};
    for (auto face = 0x8515U; face <= 0x851AU; ++face) {
        const auto& pixel = face == 0x8515U   ? face_pixels[0]
                            : face == 0x8516U ? face_pixels[1]
                                              : face_pixels[2];
        fixture.memory.Write(faces, pixel, 1U);
        fixture.bus.Write32(fixture.stack, 1U, 1U);
        fixture.bus.Write32(fixture.stack.Add(4U), 0U, 1U);
        fixture.bus.Write32(fixture.stack.Add(8U), 0x1908U, 1U);
        fixture.bus.Write32(fixture.stack.Add(12U), 0x1401U, 1U);
        fixture.bus.Write32(fixture.stack.Add(16U), faces.Value(), 1U);
        CAPTURE(face);
        CHECK(fixture.Call("libGLESv1_CM.so", "glTexImage2D",
                           {face, 0U, 0x1908U, 1U}) == 0U);
    }

    const auto vertices = fixture.output.Add(0x340U);
    const std::array vertex_values{
        -0.75F, -0.75F, 0.0F, 0.75F, -0.75F, 0.0F,
        0.0F, 0.75F, 0.0F};
    for (std::size_t index = 0; index < vertex_values.size(); ++index) {
        fixture.bus.Write32(
            vertices.Add(index * sizeof(std::uint32_t)),
            std::bit_cast<std::uint32_t>(vertex_values[index]), 1U);
    }
    const auto colors = fixture.output.Add(0x370U);
    const std::array<std::byte, 12> color_values{
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}};
    fixture.memory.Write(colors, color_values, 1U);
    const auto texcoords = fixture.output.Add(0x3A0U);
    for (std::size_t vertex = 0; vertex < 3U; ++vertex) {
        const std::array direction{1.0F, 0.0F, 0.0F};
        for (std::size_t component = 0; component < 3U; ++component) {
            fixture.bus.Write32(
                texcoords.Add((vertex * 3U + component) * 4U),
                std::bit_cast<std::uint32_t>(direction[component]), 1U);
        }
    }
    CHECK(fixture.Call("libGLESv1_CM.so", "glVertexPointer",
                       {3U, 0x1406U, 0U, vertices.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glColorPointer",
                       {4U, 0x1401U, 0U, colors.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glClientActiveTexture",
                       {0x84C0U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glTexCoordPointer",
                       {3U, 0x1406U, 0U, texcoords.Value()}) == 0U);
    for (const auto array : {
             ogplay::runtime::detail::kGles1VertexArray,
             ogplay::runtime::detail::kGles1ColorArray,
             ogplay::runtime::detail::kGles1TextureCoordArray}) {
        CHECK(fixture.Call("libGLESv1_CM.so", "glEnableClientState",
                           {array}) == 0U);
    }
    CHECK(fixture.Call(
              "libGLESv1_CM.so", "glTexEnvi",
              {ogplay::runtime::detail::kGles1TextureEnvironment,
               ogplay::runtime::detail::kGles1TextureEnvironmentMode,
               0x1E01U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnable", {0x8513U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glClearColor",
                       {0U, 0U, 0U, std::bit_cast<std::uint32_t>(1.0F)}) ==
          0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glClear", {0x00004000U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glDrawArrays",
                       {0x0004U, 0U, 3U}) == 0U);
    fixture.boundary.PresentManagedSurface();
    const auto cube_frame = fixture.boundary.TakeLatestFrame();
    REQUIRE(cube_frame.has_value());
    bool found_red_pixel = false;
    bool found_blue_pixel = false;
    for (std::size_t pixel = 0; pixel < cube_frame->rgba8.size();
         pixel += 4U) {
        const auto red = cube_frame->rgba8[pixel] > 200U;
        const auto green = cube_frame->rgba8[pixel + 1U] > 200U;
        const auto blue = cube_frame->rgba8[pixel + 2U] > 200U;
        found_red_pixel |= red && !green && !blue;
        found_blue_pixel |= blue && !red && !green;
    }
    CHECK(found_red_pixel);
    CHECK_FALSE(found_blue_pixel);
}

TEST_CASE("GLES1 lighting preserves diffuse material alpha for blending") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
    fixture.boundary.OpenManagedSurface();
    CHECK(fixture.Call("libGLESv1_CM.so", "glViewport", {0U, 0U, 4U, 3U}) == 0U);
    CHECK(fixture.Call(
              "libGLESv1_CM.so", "glClearColor",
              {std::bit_cast<std::uint32_t>(0.25F),
               std::bit_cast<std::uint32_t>(0.5F),
               std::bit_cast<std::uint32_t>(0.75F),
               std::bit_cast<std::uint32_t>(1.0F)}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glClear", {0x00004000U}) == 0U);

    const auto vertices = fixture.output.Add(0x600U);
    const auto normals = fixture.output.Add(0x680U);
    constexpr std::array vertex_values{
        -1.0F, -1.0F, 0.0F, 1.0F, -1.0F, 0.0F, 0.0F, 1.0F, 0.0F};
    for (std::size_t index = 0; index < vertex_values.size(); ++index) {
        fixture.bus.Write32(vertices.Add(index * 4U),
                            std::bit_cast<std::uint32_t>(vertex_values[index]), 1U);
        fixture.bus.Write32(
            normals.Add(index * 4U),
            std::bit_cast<std::uint32_t>(index % 3U == 2U ? 1.0F : 0.0F), 1U);
    }
    CHECK(fixture.Call("libGLESv1_CM.so", "glVertexPointer",
                       {3U, 0x1406U, 0U, vertices.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glNormalPointer",
                       {0x1406U, 0U, normals.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnableClientState",
                       {ogplay::runtime::detail::kGles1VertexArray}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnableClientState",
                       {ogplay::runtime::detail::kGles1NormalArray}) == 0U);

    const auto diffuse = fixture.output.Add(0x700U);
    for (std::size_t component = 0; component < 4U; ++component) {
        fixture.bus.Write32(diffuse.Add(component * 4U), 0U, 1U);
    }
    CHECK(fixture.Call("libGLESv1_CM.so", "glMaterialfv",
                       {0x0408U, 0x1201U, diffuse.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnable", {0x0B50U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnable", {0x0BE2U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glBlendFunc",
                       {0x0302U, 0x0303U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glDrawArrays",
                       {0x0004U, 0U, 3U}) == 0U);
    fixture.boundary.PresentManagedSurface();
    const auto frame = fixture.boundary.TakeLatestFrame();
    REQUIRE(frame.has_value());
    for (std::size_t pixel = 0; pixel < frame->rgba8.size(); pixel += 4U) {
        CHECK(frame->rgba8[pixel] == doctest::Approx(64).epsilon(0.04));
        CHECK(frame->rgba8[pixel + 1U] == doctest::Approx(128).epsilon(0.04));
        CHECK(frame->rgba8[pixel + 2U] == doctest::Approx(191).epsilon(0.04));
    }
    fixture.boundary.CloseManagedSurface();
}

TEST_CASE("GLES1 flat shading uses provoking colors for triangles strip and fan") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
    fixture.boundary.OpenManagedSurface();
    CHECK(fixture.Call("libGLESv1_CM.so", "glViewport",
                       {0U, 0U, 4U, 3U}) == 0U);
    const auto vertices = fixture.output.Add(0x600U);
    const auto colors = fixture.output.Add(0x700U);
    const auto indices = fixture.output.Add(0x780U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glVertexPointer",
                       {3U, 0x1406U, 0U, vertices.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glColorPointer",
                       {4U, 0x1401U, 0U, colors.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnableClientState",
                       {ogplay::runtime::detail::kGles1VertexArray}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnableClientState",
                       {ogplay::runtime::detail::kGles1ColorArray}) == 0U);
    constexpr std::array<std::array<std::byte, 4>, 4> vertex_colors{
        std::array{std::byte{0xff}, std::byte{}, std::byte{}, std::byte{0xff}},
        std::array{std::byte{}, std::byte{0xff}, std::byte{}, std::byte{0xff}},
        std::array{std::byte{}, std::byte{}, std::byte{0xff}, std::byte{0xff}},
        std::array{std::byte{0xff}, std::byte{0xff}, std::byte{}, std::byte{0xff}}};
    for (std::size_t index = 0; index < vertex_colors.size(); ++index) {
        fixture.memory.Write(colors.Add(index * 4U), vertex_colors[index], 1U);
        fixture.memory.Write(indices.Add(index),
                             std::array{static_cast<std::byte>(index)}, 1U);
    }
    const auto write_vertices = [&](const std::span<const float> values) {
        for (std::size_t index = 0; index < values.size(); ++index) {
            fixture.bus.Write32(vertices.Add(index * 4U),
                                std::bit_cast<std::uint32_t>(values[index]), 1U);
        }
    };
    const auto render = [&](const std::uint32_t mode,
                            const std::uint32_t count,
                            const bool elements,
                            const std::uint32_t shade) {
        CHECK(fixture.Call("libGLESv1_CM.so", "glShadeModel", {shade}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glClearColor",
                           {0U, 0U, 0U,
                            std::bit_cast<std::uint32_t>(1.0F)}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glClear", {0x00004000U}) == 0U);
        if (elements) {
            CHECK(fixture.Call("libGLESv1_CM.so", "glDrawElements",
                               {mode, count, 0x1401U, indices.Value()}) == 0U);
        } else {
            CHECK(fixture.Call("libGLESv1_CM.so", "glDrawArrays",
                               {mode, 0U, count}) == 0U);
        }
        fixture.boundary.PresentManagedSurface();
        const auto frame = fixture.boundary.TakeLatestFrame();
        REQUIRE(frame.has_value());
        return frame->rgba8;
    };
    const auto verify_difference = [&](const std::vector<std::uint8_t>& smooth,
                                       const std::vector<std::uint8_t>& flat) {
        REQUIRE(smooth.size() == flat.size());
        std::size_t different{};
        for (std::size_t index = 0; index < smooth.size(); index += 4U) {
            if (smooth[index] != flat[index] ||
                smooth[index + 1U] != flat[index + 1U] ||
                smooth[index + 2U] != flat[index + 2U]) {
                ++different;
            }
        }
        CHECK(different >= 2U);
    };

    constexpr std::array triangle{
        -1.0F, -1.0F, 0.0F, 1.0F, -1.0F, 0.0F, 0.0F, 1.0F, 0.0F};
    write_vertices(triangle);
    const auto triangle_smooth = render(0x0004U, 3U, false, 0x1D01U);
    const auto triangle_flat = render(0x0004U, 3U, false, 0x1D00U);
    verify_difference(triangle_smooth, triangle_flat);
    const auto center = (1U * 4U + 2U) * 4U;
    CHECK(triangle_flat[center] < 20U);
    CHECK(triangle_flat[center + 1U] < 20U);
    CHECK(triangle_flat[center + 2U] > 230U);

    constexpr std::array strip{
        -1.0F, -1.0F, 0.0F, 1.0F, -1.0F, 0.0F,
        -1.0F, 1.0F, 0.0F, 1.0F, 1.0F, 0.0F};
    write_vertices(strip);
    verify_difference(render(0x0005U, 4U, false, 0x1D01U),
                      render(0x0005U, 4U, false, 0x1D00U));

    constexpr std::array fan{
        -1.0F, -1.0F, 0.0F, 1.0F, -1.0F, 0.0F,
        1.0F, 1.0F, 0.0F, -1.0F, 1.0F, 0.0F};
    write_vertices(fan);
    verify_difference(render(0x0006U, 4U, true, 0x1D01U),
                      render(0x0006U, 4U, true, 0x1D00U));
    fixture.boundary.CloseManagedSurface();
}

TEST_CASE("GLES1 normalize changes lit pixels after inverse-transpose normal transform") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
    fixture.boundary.OpenManagedSurface();
    CHECK(fixture.Call("libGLESv1_CM.so", "glViewport", {0U, 0U, 4U, 3U}) == 0U);

    const auto vertices = fixture.output.Add(0x600U);
    constexpr std::array vertex_values{
        -1.0F, -1.0F, 0.0F, 1.0F, -1.0F, 0.0F, 0.0F, 1.0F, 0.0F};
    for (std::size_t index = 0; index < vertex_values.size(); ++index) {
        fixture.bus.Write32(vertices.Add(index * 4U),
                            std::bit_cast<std::uint32_t>(vertex_values[index]), 1U);
    }
    CHECK(fixture.Call("libGLESv1_CM.so", "glVertexPointer",
                       {3U, 0x1406U, 0U, vertices.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnableClientState",
                       {ogplay::runtime::detail::kGles1VertexArray}) == 0U);

    const auto value = fixture.output.Add(0x700U);
    const auto write4 = [&](const std::array<float, 4>& values) {
        for (std::size_t index = 0; index < values.size(); ++index) {
            fixture.bus.Write32(value.Add(index * 4U),
                                std::bit_cast<std::uint32_t>(values[index]), 1U);
        }
    };
    write4({0.0F, 0.0F, 0.0F, 1.0F});
    CHECK(fixture.Call("libGLESv1_CM.so", "glLightModelfv",
                       {0x0B53U, value.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glMaterialfv",
                       {0x0408U, 0x1200U, value.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glMaterialfv",
                       {0x0408U, 0x1600U, value.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glMaterialfv",
                       {0x0408U, 0x1202U, value.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glLightfv",
                       {0x4000U, 0x1200U, value.Value()}) == 0U);
    write4({1.0F, 1.0F, 1.0F, 1.0F});
    CHECK(fixture.Call("libGLESv1_CM.so", "glMaterialfv",
                       {0x0408U, 0x1201U, value.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glLightfv",
                       {0x4000U, 0x1201U, value.Value()}) == 0U);
    write4({0.0F, 0.0F, 1.0F, 0.0F});
    CHECK(fixture.Call("libGLESv1_CM.so", "glLightfv",
                       {0x4000U, 0x1203U, value.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glNormal3f",
                       {0U, 0U, std::bit_cast<std::uint32_t>(0.25F)}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnable", {0x4000U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnable", {0x0B50U}) == 0U);

    const auto render_red = [&]() {
        CHECK(fixture.Call("libGLESv1_CM.so", "glClearColor", {0U, 0U, 0U, 0U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glClear", {0x00004000U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glDrawArrays",
                           {0x0004U, 0U, 3U}) == 0U);
        fixture.boundary.PresentManagedSurface();
        const auto frame = fixture.boundary.TakeLatestFrame();
        REQUIRE(frame.has_value());
        return frame->rgba8[(1U * 4U + 2U) * 4U];
    };
    const auto unnormalized = render_red();
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnable", {0x0BA1U}) == 0U);
    const auto normalized = render_red();
    CHECK(unnormalized < 100U);
    CHECK(normalized > 200U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glDisable", {0x0BA1U}) == 0U);
    CHECK(fixture.Call(
              "libGLESv1_CM.so", "glScalef",
              {std::bit_cast<std::uint32_t>(4.0F),
               std::bit_cast<std::uint32_t>(4.0F),
               std::bit_cast<std::uint32_t>(4.0F)}) == 0U);
    const auto scaled_without_rescale = render_red();
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnable", {0x803AU}) == 0U);
    const auto scaled_with_rescale = render_red();
    CHECK(scaled_without_rescale < unnormalized);
    CHECK(scaled_with_rescale > scaled_without_rescale * 3U);
    CHECK(scaled_with_rescale == doctest::Approx(unnormalized).epsilon(0.12));
    CHECK(fixture.Call("libGLESv1_CM.so", "glDisable", {0x803AU}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnable", {0x0BA1U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glLoadIdentity") == 0U);
    CHECK(fixture.Call(
              "libGLESv1_CM.so", "glScalef",
              {std::bit_cast<std::uint32_t>(2.0F),
               std::bit_cast<std::uint32_t>(1.0F),
               std::bit_cast<std::uint32_t>(0.5F)}) == 0U);
    constexpr float inverse_sqrt_two = 0.70710678118F;
    CHECK(fixture.Call(
              "libGLESv1_CM.so", "glNormal3f",
              {std::bit_cast<std::uint32_t>(inverse_sqrt_two), 0U,
               std::bit_cast<std::uint32_t>(inverse_sqrt_two)}) == 0U);
    const auto non_uniform = render_red();
    const auto reference_z = 2.0F / std::sqrt(0.25F + 4.0F);
    CHECK(non_uniform == doctest::Approx(reference_z * 255.0F).epsilon(0.04));
    fixture.boundary.CloseManagedSurface();
}

TEST_CASE("GLES1 clip plane discards fixed pipeline fragments") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
    fixture.boundary.OpenManagedSurface();
    CHECK(fixture.Call("libGLESv1_CM.so", "glViewport",
                       {0U, 0U, 4U, 3U}) == 0U);
    CHECK(fixture.Call(
              "libGLESv1_CM.so", "glClearColor",
              {0U, 0U, std::bit_cast<std::uint32_t>(1.0F),
               std::bit_cast<std::uint32_t>(1.0F)}) == 0U);
    const auto vertices = fixture.output.Add(0x600U);
    constexpr std::array vertex_values{
        -1.0F, -1.0F, 0.0F, 1.0F, -1.0F, 0.0F, 0.0F, 1.0F, 0.0F};
    for (std::size_t index = 0; index < vertex_values.size(); ++index) {
        fixture.bus.Write32(
            vertices.Add(index * sizeof(std::uint32_t)),
            std::bit_cast<std::uint32_t>(vertex_values[index]), 1U);
    }
    CHECK(fixture.Call("libGLESv1_CM.so", "glVertexPointer",
                       {3U, 0x1406U, 0U, vertices.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnableClientState",
                       {ogplay::runtime::detail::kGles1VertexArray}) == 0U);
    CHECK(fixture.Call(
              "libGLESv1_CM.so", "glColor4f",
              {std::bit_cast<std::uint32_t>(1.0F), 0U, 0U,
               std::bit_cast<std::uint32_t>(1.0F)}) == 0U);
    CHECK(fixture.Call(
              "libGLESv1_CM.so", "glNormal3f",
              {0U, 0U, std::bit_cast<std::uint32_t>(1.0F)}) == 0U);
    const auto identity = fixture.output.Add(0x700U);
    for (std::size_t index = 0; index < 16U; ++index) {
        fixture.bus.Write32(
            identity.Add(index * sizeof(std::uint32_t)),
            std::bit_cast<std::uint32_t>(index % 5U == 0U ? 1.0F : 0.0F),
            1U);
    }
    CHECK(fixture.Call("libGLESv1_CM.so", "glMultMatrixf",
                       {identity.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glClear",
                       {0x00004000U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glDrawArrays",
                       {0x0004U, 0U, 3U}) == 0U);
    fixture.boundary.PresentManagedSurface();
    const auto visible = fixture.boundary.TakeLatestFrame();
    REQUIRE(visible.has_value());
    bool saw_red{};
    for (std::size_t pixel = 0; pixel < visible->rgba8.size(); pixel += 4U) {
        saw_red = saw_red || (visible->rgba8[pixel] > 200U &&
                              visible->rgba8[pixel + 2U] < 50U);
    }
    CHECK(saw_red);

    const auto equation = fixture.output.Add(0x780U);
    for (std::size_t index = 0; index < 4U; ++index) {
        fixture.bus.Write32(
            equation.Add(index * sizeof(std::uint32_t)),
            std::bit_cast<std::uint32_t>(index == 3U ? -1.0F : 0.0F), 1U);
    }
    CHECK(fixture.Call("libGLESv1_CM.so", "glClipPlanef",
                       {0x3000U, equation.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnable", {0x3000U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glClear",
                       {0x00004000U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glDrawArrays",
                       {0x0004U, 0U, 3U}) == 0U);
    fixture.boundary.PresentManagedSurface();
    const auto clipped = fixture.boundary.TakeLatestFrame();
    REQUIRE(clipped.has_value());
    for (std::size_t pixel = 0; pixel < clipped->rgba8.size(); pixel += 4U) {
        CHECK(clipped->rgba8[pixel] == 0U);
        CHECK(clipped->rgba8[pixel + 1U] == 0U);
        CHECK(clipped->rgba8[pixel + 2U] == 255U);
        CHECK(clipped->rgba8[pixel + 3U] == 255U);
    }
    fixture.boundary.CloseManagedSurface();
}

TEST_CASE("Android boundary publishes required GLES1 extensions separately") {
    BoundaryFixture fixture;
    CHECK(ogplay::gles::GlesFunctionCount(
              ogplay::gles::GlesApi::gles1_extensions) == 22);
    CHECK(ogplay::gles::FindGlesFunction(
              ogplay::gles::GlesApi::gles1_extensions,
              "glCurrentPaletteMatrixOES") == 3U);
    CHECK(ogplay::gles::FindGlesFunction(
              ogplay::gles::GlesApi::gles1_extensions,
              "glMatrixIndexPointerOES") == 18U);
    CHECK(ogplay::gles::FindGlesFunction(
              ogplay::gles::GlesApi::gles1_extensions,
              "glWeightPointerOES") == 21U);
    for (const auto name : {"glCurrentPaletteMatrixOES",
                            "glGetBufferPointervOES",
                            "glMapBufferOES",
                            "glMatrixIndexPointerOES",
                            "glUnmapBufferOES",
                            "glWeightPointerOES"}) {
        CAPTURE(name);
        CHECK(fixture.boundary.Symbols()
                  .Lookup("libGLESv1_CM.so", name)
                  .has_value());
    }
    CHECK_THROWS_WITH_AS(
        fixture.Call("libGLESv1_CM.so", "glCurrentPaletteMatrixOES", {0}),
        "glCurrentPaletteMatrixOES has no current ANGLE frame",
        std::runtime_error);
    fixture.boundary.OpenManagedSurface();
    for (const auto name : {
             "glBindFramebufferOES", "glBindRenderbufferOES",
             "glCheckFramebufferStatusOES", "glDeleteFramebuffersOES",
             "glDeleteRenderbuffersOES", "glFramebufferRenderbufferOES",
             "glFramebufferTexture2DOES", "glGenFramebuffersOES",
             "glGenRenderbuffersOES", "glGenerateMipmapOES",
             "glGetFramebufferAttachmentParameterivOES",
             "glGetRenderbufferParameterivOES", "glIsFramebufferOES",
             "glIsRenderbufferOES"}) {
        CAPTURE(name);
        CHECK(fixture.boundary.Symbols().Lookup("libGLESv1_CM.so", name).has_value());
    }
    const auto framebuffer_out = fixture.output.Add(0x40U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glGenFramebuffersOES",
                       {1U, framebuffer_out.Value()}) == 0U);
    const auto framebuffer = fixture.bus.Read32(framebuffer_out, 1U);
    REQUIRE(framebuffer != 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glBindFramebufferOES",
                       {0x8D40U, framebuffer}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glIsFramebufferOES",
                       {framebuffer}) == 1U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glDeleteFramebuffersOES",
                       {1U, framebuffer_out.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glCurrentPaletteMatrixOES",
                       {3U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glMatrixIndexPointerOES",
                       {4U, 0x1401U, 0U, fixture.output.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glWeightPointerOES",
                       {4U, 0x1406U, 0U, fixture.output.Add(32U).Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glCurrentPaletteMatrixOES",
                       {32U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glGetError") == 0x0500U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glMatrixIndexPointerOES",
                       {4U, 0x1406U, 0U, fixture.output.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glGetError") == 0x0500U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glGetError") == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glVertexPointer",
                           {3U, 0x1406U, 0U, fixture.output.Value()}) == 0U);
        const auto pointer_query = fixture.output.Add(0x240U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glGetPointerv",
                           {0x808EU, pointer_query.Value()}) == 0U);
        CHECK(fixture.bus.Read32(pointer_query, 1U) == fixture.output.Value());
        const auto pointer_integer_query = fixture.output.Add(0x244U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glGetIntegerv",
                           {0x807AU, pointer_integer_query.Value()}) == 0U);
        CHECK(fixture.bus.Read32(pointer_integer_query, 1U) == 3U);
        CHECK_THROWS_AS(
            fixture.Call("libGLESv1_CM.so", "glGetPointerv",
                         {0x808EU, 0U}),
            ogplay::gles::GuestTransferError);
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnableClientState",
                       {ogplay::runtime::detail::kGles1VertexArray}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnableClientState",
                       {ogplay::runtime::detail::kGles1MatrixIndexArray}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnable", {0x8840U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glDrawArrays", {0x0000U, 0U, 1U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glGetError") == 0x0502U);
    fixture.boundary.CloseManagedSurface();
}

TEST_CASE("GLES1 OES mapbuffer uses a guest arena and never leaks host pointers") {
    BoundaryFixture fixture;
    fixture.boundary.OpenManagedSurface();
    const auto name_output = fixture.output.Add(0x500U);
    const auto data = fixture.output.Add(0x520U);
    const auto pointer_output = fixture.output.Add(0x540U);
    constexpr std::uint32_t kArrayBuffer = 0x8892U;
    constexpr std::uint32_t kStaticDraw = 0x88E4U;
    constexpr std::uint32_t kWriteOnlyOes = 0x88B9U;
    constexpr std::uint32_t kBufferMapPointerOes = 0x88BDU;
    constexpr std::array<std::uint32_t, 4> initial{
        0x11111111U, 0x22222222U, 0x33333333U, 0x44444444U};
    for (std::size_t index = 0; index < initial.size(); ++index) {
        fixture.bus.Write32(data.Add(index * sizeof(std::uint32_t)),
                            initial[index], 1U);
    }
    CHECK(fixture.Call("libGLESv1_CM.so", "glGenBuffers",
                       {1U, name_output.Value()}) == 0U);
    const auto buffer = fixture.bus.Read32(name_output, 1U);
    REQUIRE(buffer != 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glBindBuffer",
                       {kArrayBuffer, buffer}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glBufferData",
                       {kArrayBuffer, sizeof(initial), data.Value(),
                        kStaticDraw}) == 0U);

    fixture.bus.Write32(pointer_output, 0xffffffffU, 1U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glGetBufferPointervOES",
                       {kArrayBuffer, kBufferMapPointerOes,
                        pointer_output.Value()}) == 0U);
    CHECK(fixture.bus.Read32(pointer_output, 1U) == 0U);

    const auto mapped = fixture.Call("libGLESv1_CM.so", "glMapBufferOES",
                                     {kArrayBuffer, kWriteOnlyOes});
    REQUIRE(mapped >= ogplay::runtime::detail::kGles1MapBufferArenaBegin);
    CHECK(mapped < ogplay::runtime::detail::kGles1MapBufferArenaBegin +
                       ogplay::runtime::detail::kGles1MapBufferArenaBytes);
    CHECK(fixture.Call("libGLESv1_CM.so", "glGetBufferPointervOES",
                       {kArrayBuffer, kBufferMapPointerOes,
                        pointer_output.Value()}) == 0U);
    CHECK(fixture.bus.Read32(pointer_output, 1U) == mapped);
    fixture.bus.Write32(ogplay::memory::GuestAddress{mapped}, 0xa5a5a5a5U, 1U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glUnmapBufferOES",
                       {kArrayBuffer}) == 1U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glGetBufferPointervOES",
                       {kArrayBuffer, kBufferMapPointerOes,
                        pointer_output.Value()}) == 0U);
    CHECK(fixture.bus.Read32(pointer_output, 1U) == 0U);

    CHECK(fixture.Call("libGLESv1_CM.so", "glMapBufferOES",
                       {kArrayBuffer, 0x88B8U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glGetError") == 0x0500U);
    fixture.boundary.CloseManagedSurface();
}

TEST_CASE("Android looper publishes command and input poll sources") {
    BoundaryFixture fixture;
    fixture.bus.Write32(fixture.stack.Add(4), 0x12345678U);
    CHECK(fixture.Call("libandroid.so", "ALooper_addFd", {1, 7, 1, 1}) == 1);
    fixture.boundary.NotifyFileWrite();
    CHECK(fixture.Call("libandroid.so", "ALooper_pollAll",
                       {0, fixture.output.Value(), fixture.output.Add(4).Value(),
                        fixture.output.Add(8).Value()}) == 1);
    CHECK(fixture.bus.Read32(fixture.output.Add(8)) == 0x12345678U);

    fixture.bus.Write32(fixture.stack, 0x87654321U);
    static_cast<void>(fixture.Call("libandroid.so", "AInputQueue_attachLooper",
                                   {2, 1, 2, 0}));
    fixture.boundary.PushInput({ogplay::runtime::AndroidBoundaryInputType::key,
                                29, 0, 0, true});
    CHECK(fixture.Call("libandroid.so", "ALooper_pollAll",
                       {0, 0, 0, fixture.output.Add(8).Value()}) == 2);
    CHECK(fixture.bus.Read32(fixture.output.Add(8)) == 0x87654321U);
    CHECK(fixture.Call("libandroid.so", "AInputQueue_getEvent",
                       {2, fixture.output.Value(), 0, 0}) == 0);
    CHECK(fixture.Call("libandroid.so", "AInputEvent_getType", {0x6e003200U}) == 1);
    CHECK(fixture.Call("libandroid.so", "AKeyEvent_getKeyCode", {0x6e003200U}) == 29);
}

TEST_CASE("Android EGL and GLES boundary produces a guest frame") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
    PrepareNativeEgl(fixture);
    CHECK(fixture.Call("libEGL.so", "eglMakeCurrent", {1, 3, 3, 4}) == 1);
    static_cast<void>(fixture.Call("libGLESv2.so", "glViewport", {0, 0, 4, 3}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glClearColor",
                 {std::bit_cast<std::uint32_t>(0.25F),
                  std::bit_cast<std::uint32_t>(0.5F),
                  std::bit_cast<std::uint32_t>(0.75F),
                  std::bit_cast<std::uint32_t>(1.0F)}));
    static_cast<void>(fixture.Call(
        "libGLESv2.so", "glBlendColor",
        {std::bit_cast<std::uint32_t>(0.1F),
         std::bit_cast<std::uint32_t>(0.2F),
         std::bit_cast<std::uint32_t>(0.3F),
         std::bit_cast<std::uint32_t>(0.4F)}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glBlendEquation",
                                   {0x8006U}));
    static_cast<void>(fixture.Call(
        "libGLESv2.so", "glSampleCoverage",
        {std::bit_cast<std::uint32_t>(1.0F), 0U}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glClear", {0x00004000U}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glFlush"));
    CHECK_FALSE(fixture.boundary.TakeLatestFrame().has_value());
    CHECK(fixture.CallProgress("libEGL.so", "eglSwapBuffers", {1, 3}) ==
          ogplay::runtime::SupervisorCallProgress::handled_advanced);
    CHECK(fixture.cpu.GetState().Register(ogplay::cpu::CoreRegister::r0) == 1);
    const auto frame = fixture.boundary.TakeLatestFrame();
    REQUIRE(frame.has_value());
    CHECK(frame->sequence == 1);
    REQUIRE(frame->rgba8.size() == 4U * 3U * 4U);
    CHECK(frame->rgba8[0] == doctest::Approx(64).epsilon(0.02));
    CHECK(frame->rgba8[1] == doctest::Approx(128).epsilon(0.02));
    CHECK(frame->rgba8[2] == doctest::Approx(191).epsilon(0.02));
    CHECK(fixture.Call("libEGL.so", "eglTerminate", {1}) == 1);
}

TEST_CASE("Java EGL bridge and native EGL share pbuffer context registry") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
    fixture.boundary.OpenManagedSurface();
    constexpr std::uint64_t thread_id = 0x33U;
    const auto attributes = fixture.output.Add(0x100U);
    fixture.bus.Write32(attributes, 0x3057U, 1U);
    fixture.bus.Write32(attributes.Add(4U), 2U, 1U);
    fixture.bus.Write32(attributes.Add(8U), 0x3056U, 1U);
    fixture.bus.Write32(attributes.Add(12U), 2U, 1U);
    fixture.bus.Write32(attributes.Add(16U), 0x3038U, 1U);
    const auto context_attributes = fixture.output.Add(0x140U);
    fixture.bus.Write32(context_attributes, 0x3098U, 1U);
    fixture.bus.Write32(context_attributes.Add(4U), 2U, 1U);
    fixture.bus.Write32(context_attributes.Add(8U), 0x3038U, 1U);

    CHECK(fixture.boundary.InvokeManagedEgl(
              "eglGetDisplay", std::array{0U}, thread_id) == 1U);
    CHECK(fixture.boundary.InvokeManagedEgl(
              "eglInitialize", std::array{1U, 0U, 0U}, thread_id) == 1U);
    const auto surface = fixture.boundary.InvokeManagedEgl(
        "eglCreatePbufferSurface",
        std::array{1U, 2U, attributes.Value()}, thread_id);
    REQUIRE(surface != 0U);
    const auto context = fixture.boundary.InvokeManagedEgl(
        "eglCreateContext",
        std::array{1U, 2U, 0U, context_attributes.Value()}, thread_id);
    REQUIRE(context != 0U);
    CHECK(fixture.boundary.InvokeManagedEgl(
              "eglMakeCurrent",
              std::array{1U, surface, surface, context}, thread_id) == 1U);

    CHECK(FastBoundaryCall(fixture, "libEGL.so", "eglGetCurrentContext",
                           {}, thread_id) == context);
    CHECK(fixture.boundary.InvokeManagedGles(
              ogplay::gles::GlesApi::gles2, "glClearColor",
              std::array{std::bit_cast<std::uint32_t>(0.25F),
                         std::bit_cast<std::uint32_t>(0.5F),
                         std::bit_cast<std::uint32_t>(0.75F),
                         std::bit_cast<std::uint32_t>(1.0F)},
              thread_id) == 0U);
    CHECK(fixture.boundary.InvokeManagedGles(
              ogplay::gles::GlesApi::gles2, "glClear",
              std::array{0x00004000U}, thread_id) == 0U);
    const auto pixels = fixture.output.Add(0x180U);
    CHECK(fixture.boundary.InvokeManagedGles(
              ogplay::gles::GlesApi::gles2, "glReadPixels",
              std::array{0U, 0U, 1U, 1U, 0x1908U, 0x1401U,
                         pixels.Value()}, thread_id) == 0U);
    std::array<std::byte, 4> rgba{};
    fixture.memory.Read(pixels, rgba, 1U);
    CHECK(std::to_integer<std::uint8_t>(rgba[0]) ==
          doctest::Approx(64).epsilon(0.04));
    CHECK(std::to_integer<std::uint8_t>(rgba[1]) ==
          doctest::Approx(128).epsilon(0.04));
    CHECK(std::to_integer<std::uint8_t>(rgba[2]) ==
          doctest::Approx(191).epsilon(0.04));

    const auto shared = FastBoundaryCall(
        fixture, "libEGL.so", "eglCreateContext",
        {1U, 2U, context, context_attributes.Value()}, thread_id);
    REQUIRE(shared != 0U);
    CHECK(fixture.boundary.InvokeManagedEgl(
              "eglMakeCurrent", std::array{1U, 0U, 0U, 0U},
              thread_id) == 1U);
    CHECK(FastBoundaryCall(fixture, "libEGL.so", "eglDestroyContext",
                           {1U, shared}, thread_id) == 1U);
    CHECK(fixture.boundary.InvokeManagedEgl(
              "eglDestroyContext", std::array{1U, context}, thread_id) == 1U);
    CHECK(fixture.boundary.InvokeManagedEgl(
              "eglDestroySurface", std::array{1U, surface}, thread_id) == 1U);
    CHECK(fixture.boundary.InvokeManagedEgl(
              "eglTerminate", std::array{1U}, thread_id) == 1U);
    fixture.boundary.CloseManagedSurface();
}

TEST_CASE("Android EGL registry isolates pbuffer content and honors share groups") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
    REQUIRE(fixture.Call("libEGL.so", "eglInitialize", {1U}) == 1U);
    const auto surface_attributes = fixture.output;
    const auto make_surface = [&](const std::uint32_t width,
                                  const std::uint32_t height) {
        fixture.bus.Write32(surface_attributes, 0x3057U, 1U);
        fixture.bus.Write32(surface_attributes.Add(4U), width, 1U);
        fixture.bus.Write32(surface_attributes.Add(8U), 0x3056U, 1U);
        fixture.bus.Write32(surface_attributes.Add(12U), height, 1U);
        fixture.bus.Write32(surface_attributes.Add(16U), 0x3038U, 1U);
        return fixture.Call("libEGL.so", "eglCreatePbufferSurface",
                            {1U, 2U, surface_attributes.Value()});
    };
    const auto first_surface = make_surface(4U, 3U);
    const auto second_surface = make_surface(2U, 2U);
    REQUIRE(first_surface == 3U);
    REQUIRE(second_surface == 4U);
    const auto context_attributes = fixture.output.Add(0x40U);
    fixture.bus.Write32(context_attributes, 0x3098U, 1U);
    fixture.bus.Write32(context_attributes.Add(4U), 2U, 1U);
    fixture.bus.Write32(context_attributes.Add(8U), 0x3038U, 1U);
    const auto first_context = fixture.Call(
        "libEGL.so", "eglCreateContext",
        {1U, 2U, 0U, context_attributes.Value()});
    REQUIRE(fixture.Call("libEGL.so", "eglMakeCurrent",
                         {1U, first_surface, first_surface, first_context}) == 1U);

    const auto clear = [&](const float red, const float green) {
        CHECK(fixture.Call(
                  "libGLESv2.so", "glClearColor",
                  {std::bit_cast<std::uint32_t>(red),
                   std::bit_cast<std::uint32_t>(green), 0U,
                   std::bit_cast<std::uint32_t>(1.0F)}) == 0U);
        CHECK(fixture.Call("libGLESv2.so", "glClear", {0x00004000U}) == 0U);
    };
    const auto pixel = fixture.output.Add(0x100U);
    const auto read_pixel = [&]() {
        fixture.bus.Write32(fixture.stack, 0x1908U, 1U);
        fixture.bus.Write32(fixture.stack.Add(4U), 0x1401U, 1U);
        fixture.bus.Write32(fixture.stack.Add(8U), pixel.Value(), 1U);
        CHECK(fixture.Call("libGLESv2.so", "glReadPixels",
                           {0U, 0U, 1U, 1U}) == 0U);
        std::array<std::byte, 4> rgba{};
        fixture.memory.Read(pixel, rgba, 1U);
        return rgba;
    };
    clear(1.0F, 0.0F);
    const auto texture_output = fixture.output.Add(0x120U);
    CHECK(fixture.Call("libGLESv2.so", "glGenTextures",
                       {1U, texture_output.Value()}) == 0U);
    const auto texture = fixture.bus.Read32(texture_output, 1U);
    REQUIRE(texture != 0U);
    CHECK(fixture.Call("libGLESv2.so", "glBindTexture",
                       {0x0DE1U, texture}) == 0U);
    const auto viewport_output = fixture.output.Add(0x140U);
    CHECK(fixture.Call("libGLESv2.so", "glViewport",
                       {1U, 0U, 3U, 3U}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetIntegerv",
                       {0x0BA2U, viewport_output.Value()}) == 0U);
    CHECK(fixture.bus.Read32(viewport_output, 1U) == 1U);
    CHECK(fixture.Call("libGLESv2.so", "glBindTexture",
                       {0xDEADU, 0U}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glEnable", {0x0BE2U}) == 0U);

    const auto shared_context = fixture.Call(
        "libEGL.so", "eglCreateContext",
        {1U, 2U, first_context, context_attributes.Value()});
    REQUIRE(fixture.Call("libEGL.so", "eglMakeCurrent",
                         {1U, second_surface, second_surface, shared_context}) == 1U);
    CHECK(fixture.Call("libGLESv2.so", "glIsTexture", {texture}) == 1U);
    CHECK(fixture.Call("libGLESv2.so", "glIsEnabled", {0x0BE2U}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetError") == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetIntegerv",
                       {0x0BA2U, viewport_output.Value()}) == 0U);
    CHECK(fixture.bus.Read32(viewport_output.Add(8U), 1U) == 2U);
    clear(0.0F, 1.0F);
    const auto green = read_pixel();
    CHECK(std::to_integer<std::uint8_t>(green[0]) == 0U);
    CHECK(std::to_integer<std::uint8_t>(green[1]) == 255U);

    const auto isolated_context = fixture.Call(
        "libEGL.so", "eglCreateContext",
        {1U, 2U, 0U, context_attributes.Value()});
    REQUIRE(fixture.Call("libEGL.so", "eglMakeCurrent",
                         {1U, second_surface, second_surface,
                          isolated_context}) == 1U);
    CHECK(fixture.Call("libGLESv2.so", "glIsTexture", {texture}) == 0U);

    REQUIRE(fixture.Call("libEGL.so", "eglMakeCurrent",
                         {1U, first_surface, first_surface, first_context}) == 1U);
    CHECK(fixture.Call("libGLESv2.so", "glGetError") == 0x0500U);
    CHECK(fixture.Call("libGLESv2.so", "glGetIntegerv",
                       {0x0BA2U, viewport_output.Value()}) == 0U);
    CHECK(fixture.bus.Read32(viewport_output, 1U) == 1U);
    CHECK(fixture.bus.Read32(viewport_output.Add(8U), 1U) == 3U);
    CHECK(fixture.Call("libGLESv2.so", "glIsEnabled", {0x0BE2U}) == 1U);
    const auto red = read_pixel();
    CHECK(std::to_integer<std::uint8_t>(red[0]) == 255U);
    CHECK(std::to_integer<std::uint8_t>(red[1]) == 0U);

    REQUIRE(fixture.Call("libEGL.so", "eglMakeCurrent",
                         {1U, first_surface, second_surface,
                          shared_context}) == 1U);
    clear(0.0F, 0.0F);
    const auto read_surface_green = read_pixel();
    CHECK(std::to_integer<std::uint8_t>(read_surface_green[1]) == 255U);
    REQUIRE(fixture.Call("libEGL.so", "eglMakeCurrent",
                         {1U, first_surface, first_surface,
                          shared_context}) == 1U);
    const auto draw_surface_black = read_pixel();
    CHECK(std::to_integer<std::uint8_t>(draw_surface_black[0]) == 0U);
    CHECK(std::to_integer<std::uint8_t>(draw_surface_black[1]) == 0U);

    REQUIRE(fixture.Call("libEGL.so", "eglMakeCurrent",
                         {1U, second_surface, second_surface,
                          shared_context}) == 1U);
    CHECK(fixture.Call("libEGL.so", "eglDestroyContext",
                       {1U, first_context}) == 1U);
    CHECK(fixture.Call("libGLESv2.so", "glIsTexture", {texture}) == 1U);
}

TEST_CASE("Android EGL restores GLES1 matrices with the current context") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
    REQUIRE(fixture.Call("libEGL.so", "eglInitialize", {1U}) == 1U);
    const auto attributes = fixture.output;
    fixture.bus.Write32(attributes, 0x3057U, 1U);
    fixture.bus.Write32(attributes.Add(4U), 4U, 1U);
    fixture.bus.Write32(attributes.Add(8U), 0x3056U, 1U);
    fixture.bus.Write32(attributes.Add(12U), 4U, 1U);
    fixture.bus.Write32(attributes.Add(16U), 0x3038U, 1U);
    const auto surface = fixture.Call("libEGL.so", "eglCreatePbufferSurface",
                                      {1U, 2U, attributes.Value()});
    const auto context_a = fixture.Call("libEGL.so", "eglCreateContext",
                                        {1U, 2U, 0U, 0U});
    const auto context_b = fixture.Call("libEGL.so", "eglCreateContext",
                                        {1U, 2U, 0U, 0U});
    REQUIRE(surface != 0U);
    REQUIRE(context_a != 0U);
    REQUIRE(context_b != 0U);
    const auto matrix = fixture.output.Add(64U);
    const auto translation_x = [&] {
        CHECK(fixture.Call("libGLESv1_CM.so", "glGetFloatv",
                           {0x0BA6U, matrix.Value()}) == 0U);
        return std::bit_cast<float>(fixture.bus.Read32(matrix.Add(48U), 1U));
    };

    REQUIRE(fixture.Call("libEGL.so", "eglMakeCurrent",
                         {1U, surface, surface, context_a}) == 1U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glMatrixMode", {0x1700U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glLoadIdentity") == 0U);
    CHECK(fixture.Call(
              "libGLESv1_CM.so", "glTranslatef",
              {std::bit_cast<std::uint32_t>(1.0F), 0U, 0U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glShadeModel", {0x1D00U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnable", {0x0BA1U}) == 0U);
    CHECK(translation_x() == doctest::Approx(1.0F));

    REQUIRE(fixture.Call("libEGL.so", "eglMakeCurrent",
                         {1U, surface, surface, context_b}) == 1U);
    CHECK(translation_x() == doctest::Approx(0.0F));
    CHECK(fixture.Call("libGLESv1_CM.so", "glGetIntegerv",
                       {0x0B54U, matrix.Value()}) == 0U);
    CHECK(fixture.bus.Read32(matrix, 1U) == 0x1D01U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glIsEnabled", {0x0BA1U}) == 0U);
    CHECK(fixture.Call(
              "libGLESv1_CM.so", "glTranslatef",
              {std::bit_cast<std::uint32_t>(4.0F), 0U, 0U}) == 0U);
    CHECK(translation_x() == doctest::Approx(4.0F));

    REQUIRE(fixture.Call("libEGL.so", "eglMakeCurrent",
                         {1U, surface, surface, context_a}) == 1U);
    CHECK(translation_x() == doctest::Approx(1.0F));
    CHECK(fixture.Call("libGLESv1_CM.so", "glGetIntegerv",
                       {0x0B54U, matrix.Value()}) == 0U);
    CHECK(fixture.bus.Read32(matrix, 1U) == 0x1D00U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glIsEnabled", {0x0BA1U}) == 1U);
}

TEST_CASE("Android boundary teardown retirement seals GLES and EGL swap") {
    BoundaryFixture fixture;
    WriteGuestString(fixture, fixture.output, "glGetString");
    const auto cached_proc = fixture.Call(
        "libEGL.so", "eglGetProcAddress", {fixture.output.Value()});
    REQUIRE(cached_proc != 0U);
    fixture.boundary.RetireGuestGraphics();
    fixture.boundary.RetireGuestGraphics();

    const std::array<std::uint32_t, 4> viewport{0U, 0U, 4U, 3U};
    CHECK(fixture.boundary.InvokeManagedGles(
              ogplay::gles::GlesApi::gles2, "glViewport", viewport) == 0U);
    CHECK(fixture.CallProgress("libGLESv2.so", "glViewport",
                               {0U, 0U, 4U, 3U}) ==
          ogplay::runtime::SupervisorCallProgress::handled_idle);
    CHECK(fixture.cpu.GetState().Register(ogplay::cpu::CoreRegister::r0) == 0U);
    const std::array version_args{0x1F02U};
    CHECK(BoundaryCallAddress(fixture, cached_proc, version_args) == 0U);
    CHECK(fixture.CallProgress("libEGL.so", "eglSwapBuffers", {1U, 3U}) ==
          ogplay::runtime::SupervisorCallProgress::handled_idle);
    CHECK(fixture.cpu.GetState().Register(ogplay::cpu::CoreRegister::r0) == 0U);
    CHECK(fixture.Call("libEGL.so", "eglGetError") == 0x300BU);
    CHECK(fixture.Call("libEGL.so", "eglGetError") == 0x3000U);
}

TEST_CASE("Android boundary owns a managed GLSurface frame lifecycle") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
    fixture.boundary.OpenManagedSurface();
    CHECK_THROWS_WITH_AS(
        fixture.boundary.OpenManagedSurface(),
        "Android boundary already has a current ANGLE frame",
        std::logic_error);
    static_cast<void>(fixture.Call(
        "libGLESv2.so", "glClearColor",
        {std::bit_cast<std::uint32_t>(0.125F),
         std::bit_cast<std::uint32_t>(0.25F),
         std::bit_cast<std::uint32_t>(0.5F),
         std::bit_cast<std::uint32_t>(1.0F)}));
    static_cast<void>(
        fixture.Call("libGLESv2.so", "glClear", {0x00004000U}));
    fixture.boundary.PresentManagedSurface();
    auto frame = fixture.boundary.TakeLatestFrame();
    REQUIRE(frame.has_value());
    CHECK(frame->sequence == 1);
    CHECK(frame->rgba8[0] == doctest::Approx(32).epsilon(0.04));
    CHECK(frame->rgba8[1] == doctest::Approx(64).epsilon(0.04));
    CHECK(frame->rgba8[2] == doctest::Approx(128).epsilon(0.04));
    CHECK_THROWS_WITH_AS(
        fixture.boundary.RecycleFrame({4U, 3U, 0U, {std::uint8_t{0}}}),
        "recycled Android boundary frame layout does not match",
        std::invalid_argument);
    const auto* const first_storage = frame->rgba8.data();
    fixture.boundary.RecycleFrame(std::move(*frame));
    fixture.boundary.PresentManagedSurface();
    const auto recycled = fixture.boundary.TakeLatestFrame();
    REQUIRE(recycled.has_value());
    CHECK(recycled->sequence == 2);
    CHECK(recycled->rgba8.data() == first_storage);
    CHECK(fixture.Call("libEGL.so", "eglMakeCurrent", {1, 3, 3, 4}) == 0U);
    CHECK(fixture.Call("libEGL.so", "eglGetError") == 0x3001U);
    fixture.boundary.CloseManagedSurface();
    CHECK(fixture.boundary.RenderTargets().empty());
    CHECK_THROWS_WITH_AS(
        fixture.boundary.PresentManagedSurface(),
        "Android boundary managed surface is not open",
        std::logic_error);
    CHECK_THROWS_WITH_AS(
        fixture.boundary.CloseManagedSurface(),
        "Android boundary managed surface is not open",
        std::logic_error);
}

TEST_CASE("managed GLSurface currency supports explicit cross-thread handoff") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
    fixture.boundary.OpenManagedSurface();
    fixture.boundary.BindManagedSurfaceOnCallingThread();

    std::string affinity_failure;
    std::thread contender([&] {
        try {
            fixture.boundary.BindManagedSurfaceOnCallingThread();
        } catch (const std::exception& error) {
            affinity_failure = error.what();
        }
    });
    contender.join();
    CHECK(affinity_failure ==
          "bind managed surface violates GL context thread affinity");

    fixture.boundary.ReleaseManagedSurfaceFromCallingThread();
    std::exception_ptr handoff_failure;
    std::thread successor([&] {
        try {
            fixture.boundary.BindManagedSurfaceOnCallingThread();
            fixture.boundary.PresentManagedSurface();
            fixture.boundary.ReleaseManagedSurfaceFromCallingThread();
        } catch (...) {
            handoff_failure = std::current_exception();
        }
    });
    successor.join();
    CHECK(handoff_failure == nullptr);
    const auto frame = fixture.boundary.TakeLatestFrame();
    REQUIRE(frame.has_value());
    CHECK(frame->sequence == 1);
    fixture.boundary.CloseManagedSurface();
}

TEST_CASE("Android GLES boundary compiles and links guest shader sources") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
    PrepareNativeEgl(fixture);
    CHECK(fixture.Call("libEGL.so", "eglMakeCurrent", {1, 3, 3, 4}) == 1);

    constexpr std::string_view vertex_source =
        "attribute vec2 aPosition;"
        "void main(){gl_Position=vec4(aPosition,0.0,1.0);}";
    constexpr std::string_view fragment_source =
        "precision mediump float;uniform vec4 uTint;"
        "void main(){gl_FragColor=uTint;}";
    const auto pointer_array = fixture.output;
    const auto length_array = fixture.output.Add(0x20);
    const auto query_output = fixture.output.Add(0x40);
    const auto vertex_address = fixture.output.Add(0x100);
    const auto fragment_address = fixture.output.Add(0x300);
    const auto attribute_name = fixture.output.Add(0x500);
    const auto uniform_name = fixture.output.Add(0x520);

    const auto vertex = fixture.Call("libGLESv2.so", "glCreateShader", {0x8b31U});
    REQUIRE(vertex != 0);
    WriteGuestString(fixture, vertex_address, vertex_source, false);
    fixture.bus.Write32(pointer_array, vertex_address.Value(), 1);
    fixture.bus.Write32(length_array, static_cast<std::uint32_t>(vertex_source.size()), 1);
    static_cast<void>(fixture.Call(
        "libGLESv2.so", "glShaderSource",
        {vertex, 1, pointer_array.Value(), length_array.Value()}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glCompileShader", {vertex}));
    static_cast<void>(fixture.Call(
        "libGLESv2.so", "glGetShaderiv",
        {vertex, 0x8b81U, query_output.Value()}));
    CHECK(fixture.bus.Read32(query_output, 1) == 1);

    const auto fragment = fixture.Call("libGLESv2.so", "glCreateShader", {0x8b30U});
    REQUIRE(fragment != 0);
    WriteGuestString(fixture, fragment_address, fragment_source);
    fixture.bus.Write32(pointer_array, fragment_address.Value(), 1);
    static_cast<void>(fixture.Call(
        "libGLESv2.so", "glShaderSource",
        {fragment, 1, pointer_array.Value(), 0}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glCompileShader", {fragment}));
    static_cast<void>(fixture.Call(
        "libGLESv2.so", "glGetShaderiv",
        {fragment, 0x8b81U, query_output.Value()}));
    CHECK(fixture.bus.Read32(query_output, 1) == 1);

    const auto program = fixture.Call("libGLESv2.so", "glCreateProgram");
    REQUIRE(program != 0);
    static_cast<void>(fixture.Call("libGLESv2.so", "glAttachShader", {program, vertex}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glAttachShader", {program, fragment}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glLinkProgram", {program}));
    static_cast<void>(fixture.Call(
        "libGLESv2.so", "glGetProgramiv",
        {program, 0x8b82U, query_output.Value()}));
    CHECK(fixture.bus.Read32(query_output, 1) == 1);

    const auto reflected_length = fixture.output.Add(0x700);
    const auto reflected_size = fixture.output.Add(0x704);
    const auto reflected_type = fixture.output.Add(0x708);
    const auto reflected_name = fixture.output.Add(0x720);
    fixture.bus.Write32(fixture.stack, reflected_size.Value(), 1);
    fixture.bus.Write32(fixture.stack.Add(4), reflected_type.Value(), 1);
    fixture.bus.Write32(fixture.stack.Add(8), reflected_name.Value(), 1);
    static_cast<void>(fixture.Call(
        "libGLESv2.so", "glGetActiveAttrib",
        {program, 0, 32, reflected_length.Value()}));
    CHECK(fixture.bus.Read32(reflected_length, 1) == 9U);
    CHECK(fixture.bus.Read32(reflected_size, 1) == 1U);
    CHECK(fixture.bus.Read32(reflected_type, 1) == 0x8B50U);
    CHECK(fixture.bus.Read8(reflected_name.Add(9), 1) == 0U);

    const auto uniform_reflected_name = fixture.output.Add(0x760);
    fixture.bus.Write32(fixture.stack.Add(8), uniform_reflected_name.Value(), 1);
    static_cast<void>(fixture.Call(
        "libGLESv2.so", "glGetActiveUniform",
        {program, 0, 32, reflected_length.Value()}));
    CHECK(fixture.bus.Read32(reflected_length, 1) == 5U);
    CHECK(fixture.bus.Read32(reflected_type, 1) == 0x8B52U);
    CHECK(fixture.bus.Read8(uniform_reflected_name.Add(5), 1) == 0U);

    const auto info_log = fixture.output.Add(0x7a0);
    static_cast<void>(fixture.Call(
        "libGLESv2.so", "glGetProgramInfoLog",
        {program, 32, reflected_length.Value(), info_log.Value()}));
    CHECK(fixture.bus.Read32(reflected_length, 1) == 0U);
    CHECK(fixture.bus.Read8(info_log, 1) == 0U);
    static_cast<void>(fixture.Call(
        "libGLESv2.so", "glGetShaderInfoLog",
        {vertex, 32, reflected_length.Value(), info_log.Value()}));
    CHECK(fixture.bus.Read32(reflected_length, 1) == 0U);

    WriteGuestString(fixture, attribute_name, "aPosition");
    WriteGuestString(fixture, uniform_name, "uTint");
    CHECK(fixture.Call("libGLESv2.so", "glGetAttribLocation",
                       {program, attribute_name.Value()}) != UINT32_MAX);
    const auto tint_location = fixture.Call("libGLESv2.so", "glGetUniformLocation",
                                            {program, uniform_name.Value()});
    CHECK(tint_location != UINT32_MAX);
    static_cast<void>(fixture.Call("libGLESv2.so", "glUseProgram", {program}));

    fixture.bus.Write32(fixture.stack, std::bit_cast<std::uint32_t>(1.0F), 1);
    static_cast<void>(fixture.Call(
        "libGLESv2.so", "glUniform4f",
        {tint_location, std::bit_cast<std::uint32_t>(0.25F),
         std::bit_cast<std::uint32_t>(0.5F), std::bit_cast<std::uint32_t>(0.75F)}));
    static_cast<void>(fixture.Call(
        "libGLESv2.so", "glUniform1f",
        {UINT32_MAX, std::bit_cast<std::uint32_t>(0.5F)}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glUniform1i",
                                   {UINT32_MAX, 2}));
    const auto vector_address = fixture.output.Add(0x800);
    constexpr std::array<float, 4> vector_values{0.1F, 0.2F, 0.3F, 0.4F};
    constexpr std::array<std::int32_t, 4> integer_values{1, 2, 3, 4};
    fixture.memory.Write(vector_address,
                         std::as_bytes(std::span(vector_values)), 1);
    fixture.memory.Write(vector_address.Add(0x20),
                         std::as_bytes(std::span(integer_values)), 1);
    for (const auto* symbol : {"glUniform1fv", "glUniform2fv",
                               "glUniform3fv", "glUniform4fv"}) {
        static_cast<void>(fixture.Call(
            "libGLESv2.so", symbol,
            {UINT32_MAX, 1, vector_address.Value()}));
    }
    for (const auto* symbol : {"glUniform1iv", "glUniform2iv",
                               "glUniform3iv", "glUniform4iv"}) {
        static_cast<void>(fixture.Call(
            "libGLESv2.so", symbol,
            {UINT32_MAX, 1, vector_address.Add(0x20).Value()}));
    }
    const auto matrix_address = fixture.output.Add(0x600);
    constexpr std::array<float, 9> identity{1.0F, 0.0F, 0.0F,
                                            0.0F, 1.0F, 0.0F,
                                            0.0F, 0.0F, 1.0F};
    fixture.memory.Write(matrix_address, std::as_bytes(std::span(identity)), 1);
    static_cast<void>(fixture.Call("libGLESv2.so", "glUniformMatrix3fv",
                                   {UINT32_MAX, 1, 0, matrix_address.Value()}));
    const auto matrix4_address = fixture.output.Add(0x880);
    constexpr std::array<float, 16> identity4{
        1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F};
    fixture.memory.Write(matrix4_address,
                         std::as_bytes(std::span(identity4)), 1);
    static_cast<void>(fixture.Call("libGLESv2.so", "glUniformMatrix4fv",
                                   {UINT32_MAX, 1, 0,
                                    matrix4_address.Value()}));
    fixture.bus.Write32(fixture.stack, std::bit_cast<std::uint32_t>(1.0F), 1);
    static_cast<void>(fixture.Call(
        "libGLESv2.so", "glVertexAttrib4f",
        {0, std::bit_cast<std::uint32_t>(0.0F),
         std::bit_cast<std::uint32_t>(0.0F),
         std::bit_cast<std::uint32_t>(0.0F)}));
    CHECK_THROWS_AS(
        fixture.Call("libGLESv2.so", "glUniformMatrix3fv",
                     {UINT32_MAX, 1, 0, 0x1000U}),
        ogplay::memory::MemoryFault);

    const auto client_vertices = fixture.output.Add(0x900);
    constexpr std::array<float, 6> client_triangle{
        -1.0F, -1.0F, 1.0F, -1.0F, 0.0F, 1.0F};
    fixture.memory.Write(client_vertices,
                         std::as_bytes(std::span(client_triangle)), 1);
    fixture.bus.Write32(fixture.stack, std::bit_cast<std::uint32_t>(1.0F), 1);
    static_cast<void>(fixture.Call(
        "libGLESv2.so", "glUniform4f",
        {tint_location, std::bit_cast<std::uint32_t>(1.0F),
         std::bit_cast<std::uint32_t>(0.0F),
         std::bit_cast<std::uint32_t>(0.0F)}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glBindBuffer", {0x8892U, 0}));
    fixture.bus.Write32(fixture.stack, 0, 1);
    fixture.bus.Write32(fixture.stack.Add(4), client_vertices.Value(), 1);
    static_cast<void>(fixture.Call("libGLESv2.so", "glVertexAttribPointer",
                                   {0, 2, 0x1406U, 0}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glEnableVertexAttribArray",
                                   {0}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glDrawArrays",
                                   {0x0004U, 0U, 3U}));
    static_cast<void>(fixture.Call("libGLESv1_CM.so", "glEnableClientState",
                                   {0x8074U}));
    static_cast<void>(fixture.Call("libGLESv1_CM.so", "glVertexPointer",
                                   {2U, 0x1406U, 0U, client_vertices.Value()}));
    static_cast<void>(fixture.Call("libGLESv1_CM.so", "glDrawArrays",
                                   {0x0004U, 0U, 3U}));
    static_cast<void>(fixture.Call(
        "libGLESv2.so", "glGetVertexAttribfv",
        {0U, 0x8626U, fixture.output.Add(0x9D0).Value()}));
    CHECK(std::bit_cast<float>(fixture.bus.Read32(
              fixture.output.Add(0x9DC), 1U)) == doctest::Approx(1.0F));
    static_cast<void>(fixture.Call("libGLESv1_CM.so", "glDisableClientState",
                                   {0x8074U}));
    static_cast<void>(fixture.Call("libGLESv1_CM.so", "glGetIntegerv",
                                   {0x8B8DU, fixture.output.Add(0x9C0).Value()}));
    CHECK(fixture.bus.Read32(fixture.output.Add(0x9C0), 1) == program);
    static_cast<void>(fixture.Call("libGLESv1_CM.so", "glGetIntegerv",
                                   {0x8894U, fixture.output.Add(0x9C4).Value()}));
    CHECK(fixture.bus.Read32(fixture.output.Add(0x9C4), 1) == 0U);
    CHECK_NOTHROW(static_cast<void>(fixture.Call(
        "libGLESv2.so", "glDrawArrays", {0x0004U, 0U, 3U})));
    const auto client_indices = fixture.output.Add(0x980);
    constexpr std::array<std::uint16_t, 3> index_values{0, 1, 2};
    fixture.memory.Write(client_indices, std::as_bytes(std::span(index_values)),
                         1);
    static_cast<void>(fixture.Call("libGLESv2.so", "glDrawElements",
                                   {0x0004U, 3U, 0x1403U,
                                    client_indices.Value()}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glGenBuffers",
                                   {1, pointer_array.Value()}));
    const auto element_buffer = fixture.bus.Read32(pointer_array, 1);
    REQUIRE(element_buffer != 0);
    static_cast<void>(fixture.Call("libGLESv2.so", "glBindBuffer",
                                   {0x8893U, element_buffer}));
    CHECK_THROWS_WITH_AS(
        fixture.Call("libGLESv2.so", "glDrawElements",
                     {0x0004U, 3U, 0x1403U, 0U}),
        "GLES2 cannot stage client arrays from an opaque element buffer",
        std::runtime_error);
    static_cast<void>(fixture.Call("libGLESv2.so", "glBindBuffer",
                                   {0x8893U, 0}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glDisableVertexAttribArray",
                                   {0}));

    static_cast<void>(fixture.Call("libGLESv2.so", "glEnableVertexAttribArray",
                                   {0}));
    static_cast<void>(fixture.Call("libGLESv1_CM.so", "glBindBuffer",
                                   {0x8892U, element_buffer}));
    fixture.bus.Write32(fixture.stack, 0, 1);
    fixture.bus.Write32(fixture.stack.Add(4), 0, 1);
    static_cast<void>(fixture.Call("libGLESv2.so", "glVertexAttribPointer",
                                   {0, 2, 0x1406U, 0}));
    CHECK_NOTHROW(static_cast<void>(fixture.Call(
        "libGLESv1_CM.so", "glDrawArrays", {0x0004U, 0U, 1U})));
    static_cast<void>(fixture.Call("libGLESv1_CM.so", "glBindBuffer",
                                   {0x8892U, 0}));
    fixture.bus.Write32(fixture.stack, 0, 1);
    fixture.bus.Write32(fixture.stack.Add(4), client_vertices.Value(), 1);
    static_cast<void>(fixture.Call("libGLESv2.so", "glVertexAttribPointer",
                                   {0, 2, 0x1406U, 0}));
    static_cast<void>(fixture.Call("libGLESv1_CM.so", "glDrawArrays",
                                   {0x0004U, 0U, 3U}));
    CHECK(fixture.boundary.Stats().draws == 5);
    static_cast<void>(fixture.Call("libGLESv2.so", "glDisableVertexAttribArray",
                                   {0}));

    CHECK_THROWS_AS(
        fixture.Call("libGLESv2.so", "glGetShaderiv", {vertex, 0x8b81U, 0}),
        std::invalid_argument);
    CHECK_THROWS_AS(
        fixture.Call("libGLESv2.so", "glShaderSource", {vertex, 1, 0x1000U, 0}),
        ogplay::memory::MemoryFault);

    static_cast<void>(fixture.Call("libGLESv2.so", "glUseProgram", {0}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glDeleteProgram", {program}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glDeleteShader", {vertex}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glDeleteShader", {fragment}));
    const auto stats = fixture.boundary.Stats();
    CHECK(stats.shader_compiles == 2);
    CHECK(stats.program_links == 1);
    CHECK(stats.draws == 5);
}

TEST_CASE("GLES2 completion covers shader uniform and vertex query lifecycle") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
    fixture.boundary.OpenManagedSurface();
    const auto pointer_array = fixture.output;
    const auto query = fixture.output.Add(0x40U);
    const auto vertex_source_address = fixture.output.Add(0x100U);
    const auto fragment_source_address = fixture.output.Add(0x300U);
    const auto name = fixture.output.Add(0x600U);
    const auto values = fixture.output.Add(0x700U);
    const auto address = [&](const std::string_view symbol) {
        const auto found = fixture.boundary.Symbols().Lookup(
            "libGLESv2.so", symbol);
        REQUIRE(found.has_value());
        return found->Value();
    };
    const auto call = [&](const std::string_view symbol,
                          const std::span<const std::uint32_t> arguments) {
        return BoundaryCallAddress(fixture, address(symbol), arguments);
    };
    constexpr std::string_view vertex_source =
        "attribute vec2 aPosition;"
        "void main(){gl_Position=vec4(aPosition,0.0,1.0);}";
    constexpr std::string_view fragment_source =
        "precision mediump float;"
        "uniform vec4 uTint;uniform ivec4 uInts;uniform mat2 uMatrix;"
        "void main(){gl_FragColor=uTint+vec4(uInts)*0.000001+"
        "vec4(uMatrix[0],uMatrix[1])*0.000001;}";
    WriteGuestString(fixture, vertex_source_address, vertex_source);
    WriteGuestString(fixture, fragment_source_address, fragment_source);

    const auto compile = [&](const std::uint32_t type,
                             const ogplay::memory::GuestAddress source) {
        const auto shader = fixture.Call(
            "libGLESv2.so", "glCreateShader", {type});
        REQUIRE(shader != 0U);
        fixture.bus.Write32(pointer_array, source.Value(), 1U);
        CHECK(fixture.Call("libGLESv2.so", "glShaderSource",
                           {shader, 1U, pointer_array.Value(), 0U}) == 0U);
        CHECK(fixture.Call("libGLESv2.so", "glCompileShader", {shader}) == 0U);
        CHECK(fixture.Call("libGLESv2.so", "glGetShaderiv",
                           {shader, 0x8B81U, query.Value()}) == 0U);
        REQUIRE(fixture.bus.Read32(query, 1U) == 1U);
        return shader;
    };
    const auto vertex = compile(0x8B31U, vertex_source_address);
    const auto fragment = compile(0x8B30U, fragment_source_address);
    const auto program = fixture.Call("libGLESv2.so", "glCreateProgram");
    REQUIRE(program != 0U);

    WriteGuestString(fixture, name, "aPosition");
    CHECK(fixture.Call("libGLESv2.so", "glBindAttribLocation",
                       {program, 0U, name.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glAttachShader",
                       {program, vertex}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glAttachShader",
                       {program, fragment}) == 0U);
    const auto attached = fixture.output.Add(0x900U);
    CHECK(fixture.Call("libGLESv2.so", "glGetAttachedShaders",
                       {program, 4U, query.Value(), attached.Value()}) == 0U);
    CHECK(fixture.bus.Read32(query, 1U) == 2U);
    CHECK(fixture.Call("libGLESv2.so", "glDetachShader",
                       {program, fragment}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetAttachedShaders",
                       {program, 4U, query.Value(), attached.Value()}) == 0U);
    CHECK(fixture.bus.Read32(query, 1U) == 1U);
    CHECK(fixture.Call("libGLESv2.so", "glAttachShader",
                       {program, fragment}) == 0U);

    const auto source_output = fixture.output.Add(0xA00U);
    CHECK(fixture.Call("libGLESv2.so", "glGetShaderSource",
                       {vertex, 256U, query.Value(), source_output.Value()}) == 0U);
    CHECK(fixture.bus.Read32(query, 1U) == vertex_source.size());
    CHECK(fixture.bus.Read8(source_output, 1U) ==
          static_cast<std::uint8_t>('a'));
    const auto precision = fixture.output.Add(0xB00U);
    CHECK(fixture.Call("libGLESv2.so", "glGetShaderPrecisionFormat",
                       {0x8B30U, 0x8DF2U, precision.Value(),
                        precision.Add(8U).Value()}) == 0U);
    CHECK(std::bit_cast<std::int32_t>(
              fixture.bus.Read32(precision.Add(4U), 1U)) > 0);
    CHECK(std::bit_cast<std::int32_t>(
              fixture.bus.Read32(precision.Add(8U), 1U)) > 0);

    CHECK(fixture.Call("libGLESv2.so", "glLinkProgram", {program}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetProgramiv",
                       {program, 0x8B82U, query.Value()}) == 0U);
    REQUIRE(fixture.bus.Read32(query, 1U) == 1U);
    CHECK(fixture.Call("libGLESv2.so", "glUseProgram", {program}) == 0U);
    const auto location = [&](const std::string_view uniform) {
        WriteGuestString(fixture, name, uniform);
        const auto result = fixture.Call(
            "libGLESv2.so", "glGetUniformLocation",
            {program, name.Value()});
        REQUIRE(result != UINT32_MAX);
        return result;
    };
    const auto tint = location("uTint");
    const auto integers = location("uInts");
    const auto matrix = location("uMatrix");

    fixture.bus.Write32(fixture.stack,
                        std::bit_cast<std::uint32_t>(0.4F), 1U);
    CHECK(fixture.Call("libGLESv2.so", "glUniform4f",
                       {tint, std::bit_cast<std::uint32_t>(0.1F),
                        std::bit_cast<std::uint32_t>(0.2F),
                        std::bit_cast<std::uint32_t>(0.3F)}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetUniformfv",
                       {program, tint, query.Value()}) == 0U);
    CHECK(std::bit_cast<float>(fixture.bus.Read32(query.Add(12U), 1U)) ==
          doctest::Approx(0.4F));
    fixture.bus.Write32(fixture.stack, 4U, 1U);
    CHECK(fixture.Call("libGLESv2.so", "glUniform4i",
                       {integers, 1U, 2U, 3U}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetUniformiv",
                       {program, integers, query.Value()}) == 0U);
    CHECK(fixture.bus.Read32(query.Add(12U), 1U) == 4U);

    constexpr std::array<float, 4> matrix_value{1.0F, 2.0F, 3.0F, 4.0F};
    fixture.memory.Write(values, std::as_bytes(std::span(matrix_value)), 1U);
    CHECK(fixture.Call("libGLESv2.so", "glUniformMatrix2fv",
                       {matrix, 1U, 0U, values.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetUniformfv",
                       {program, matrix, query.Value()}) == 0U);
    CHECK(std::bit_cast<float>(fixture.bus.Read32(query.Add(8U), 1U)) ==
          doctest::Approx(3.0F));
    CHECK(fixture.Call("libGLESv2.so", "glUniform2f",
                       {UINT32_MAX, 0U, 0U}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glUniform2i",
                       {UINT32_MAX, 0U, 0U}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glUniform3f",
                       {UINT32_MAX, 0U, 0U, 0U}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glUniform3i",
                       {UINT32_MAX, 0U, 0U, 0U}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glValidateProgram", {program}) == 0U);

    const auto f = [](const float value) {
        return std::bit_cast<std::uint32_t>(value);
    };
    CHECK(fixture.Call("libGLESv2.so", "glVertexAttrib1f",
                       {5U, f(0.1F)}) == 0U);
    const std::array<float, 1> one{0.2F};
    fixture.memory.Write(values, std::as_bytes(std::span(one)), 1U);
    CHECK(fixture.Call("libGLESv2.so", "glVertexAttrib1fv",
                       {5U, values.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glVertexAttrib2f",
                       {5U, f(0.3F), f(0.4F)}) == 0U);
    const std::array<float, 2> two{0.5F, 0.6F};
    fixture.memory.Write(values, std::as_bytes(std::span(two)), 1U);
    CHECK(fixture.Call("libGLESv2.so", "glVertexAttrib2fv",
                       {5U, values.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glVertexAttrib3f",
                       {5U, f(0.7F), f(0.8F), f(0.9F)}) == 0U);
    const std::array<float, 3> three{1.1F, 1.2F, 1.3F};
    fixture.memory.Write(values, std::as_bytes(std::span(three)), 1U);
    CHECK(fixture.Call("libGLESv2.so", "glVertexAttrib3fv",
                       {5U, values.Value()}) == 0U);
    const std::array<float, 4> four{1.0F, 2.0F, 3.0F, 4.0F};
    fixture.memory.Write(values, std::as_bytes(std::span(four)), 1U);
    CHECK(fixture.Call("libGLESv2.so", "glVertexAttrib4fv",
                       {5U, values.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetVertexAttribfv",
                       {5U, 0x8626U, query.Value()}) == 0U);
    CHECK(std::bit_cast<float>(fixture.bus.Read32(query.Add(12U), 1U)) ==
          doctest::Approx(4.0F));
    CHECK_THROWS_AS(
        fixture.Call("libGLESv2.so", "glVertexAttrib4fv",
                     {5U, 0x12345000U}),
        ogplay::memory::MemoryFault);
    CHECK(fixture.Call("libGLESv2.so", "glGetVertexAttribfv",
                       {5U, 0x8626U, query.Value()}) == 0U);
    CHECK(std::bit_cast<float>(fixture.bus.Read32(query.Add(12U), 1U)) ==
          doctest::Approx(4.0F));

    CHECK(fixture.Call("libGLESv2.so", "glBindBuffer",
                       {0x8892U, 0U}) == 0U);
    fixture.bus.Write32(fixture.stack, 0U, 1U);
    fixture.bus.Write32(fixture.stack.Add(4U), values.Value(), 1U);
    CHECK(fixture.Call("libGLESv2.so", "glVertexAttribPointer",
                       {6U, 2U, 0x1406U, 0U}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetVertexAttribPointerv",
                       {6U, 0x8645U, query.Value()}) == 0U);
    CHECK(fixture.bus.Read32(query, 1U) == values.Value());
    CHECK(fixture.Call("libGLESv2.so", "glGetVertexAttribiv",
                       {6U, 0x8623U, query.Value()}) == 0U);
    CHECK(fixture.bus.Read32(query, 1U) == 2U);
    CHECK(fixture.Call("libGLESv2.so", "glGetVertexAttribfv",
                       {6U, 0x8623U, query.Value()}) == 0U);
    CHECK(std::bit_cast<float>(fixture.bus.Read32(query, 1U)) ==
          doctest::Approx(2.0F));

    CHECK(fixture.Call("libGLESv2.so", "glReleaseShaderCompiler") == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetError") == 0U);
    fixture.bus.Write32(pointer_array, vertex, 1U);
    fixture.bus.Write32(values, 0x12345678U, 1U);
    const std::array<std::uint32_t, 5> binary{
        1U, pointer_array.Value(), UINT32_MAX, values.Value(), 4U};
    CHECK(call("glShaderBinary", binary) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetError") != 0U);

    CHECK(fixture.Call("libGLESv2.so", "glUseProgram", {0U}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glDeleteProgram", {program}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetUniformfv", {program, tint, query.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetError") == 0x0501U);
    CHECK(fixture.Call("libGLESv2.so", "glDeleteShader", {vertex}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glDeleteShader", {fragment}) == 0U);
    fixture.boundary.CloseManagedSurface();
}

TEST_CASE("Android GLES boundary transfers buffer and texture resources") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
    PrepareNativeEgl(fixture);
    CHECK(fixture.Call("libEGL.so", "eglMakeCurrent", {1, 3, 3, 4}) == 1);
    const auto names = fixture.output;
    const auto buffer_data = fixture.output.Add(0x100);
    const auto texture_data = fixture.output.Add(0x200);
    constexpr std::array<std::byte, 12> vertices{
        std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
        std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8},
        std::byte{9}, std::byte{10}, std::byte{11}, std::byte{12}};
    constexpr std::array<std::byte, 16> pixels{
        std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255},
        std::byte{0}, std::byte{255}, std::byte{0}, std::byte{255},
        std::byte{0}, std::byte{0}, std::byte{255}, std::byte{255},
        std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}};
    fixture.memory.Write(buffer_data, vertices, 1);
    fixture.memory.Write(texture_data, pixels, 1);

    static_cast<void>(fixture.Call("libGLESv2.so", "glGenFramebuffers",
                                   {1, names.Value()}));
    const auto framebuffer = fixture.bus.Read32(names, 1);
    REQUIRE(framebuffer != 0U);
    static_cast<void>(fixture.Call("libGLESv2.so", "glGenRenderbuffers",
                                   {1, names.Add(4).Value()}));
    const auto renderbuffer = fixture.bus.Read32(names.Add(4), 1);
    REQUIRE(renderbuffer != 0U);
    static_cast<void>(fixture.Call("libGLESv2.so", "glBindFramebuffer",
                                   {0x8D40U, framebuffer}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glBindRenderbuffer",
                                   {0x8D41U, renderbuffer}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glRenderbufferStorage",
                                   {0x8D41U, 0x8056U, 2U, 2U}));
    static_cast<void>(fixture.Call(
        "libGLESv2.so", "glFramebufferRenderbuffer",
        {0x8D40U, 0x8CE0U, 0x8D41U, renderbuffer}));
    CHECK(fixture.Call("libGLESv2.so", "glCheckFramebufferStatus",
                       {0x8D40U}) == 0x8CD5U);
    static_cast<void>(fixture.Call("libGLESv2.so", "glRenderbufferStorage",
                                   {0x8D41U, 0x8058U, 2U, 2U}));
    static_cast<void>(fixture.Call(
        "libGLESv2.so", "glGetRenderbufferParameteriv",
        {0x8D41U, 0x8D44U, names.Add(8).Value()}));
    CHECK(fixture.bus.Read32(names.Add(8), 1) == 0x8058U);
    CHECK(fixture.Call("libGLESv2.so", "glCheckFramebufferStatus",
                       {0x8D40U}) == 0x8CD5U);
    static_cast<void>(fixture.Call("libGLESv2.so", "glBindFramebuffer",
                                   {0x8D40U, 0U}));

    static_cast<void>(fixture.Call("libGLESv2.so", "glGenBuffers",
                                   {2, names.Value()}));
    const auto vertex_buffer = fixture.bus.Read32(names, 1);
    const auto index_buffer = fixture.bus.Read32(names.Add(4), 1);
    CHECK(vertex_buffer != 0); CHECK(index_buffer != 0);
    static_cast<void>(fixture.Call("libGLESv2.so", "glBindBuffer",
                                   {0x8892U, vertex_buffer}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glBufferData",
                                   {0x8892U, 12, buffer_data.Value(), 0x88e4U}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glBufferData",
                                   {0x8892U, 16, 0, 0x88e8U}));
    CHECK_THROWS_AS(
        fixture.Call("libGLESv2.so", "glBufferData",
                     {0x8892U, 12, 0x1000U, 0x88e4U}),
        ogplay::memory::MemoryFault);
    static_cast<void>(fixture.Call("libGLESv2.so", "glEnableVertexAttribArray", {0}));
    fixture.bus.Write32(fixture.stack, 0, 1);
    fixture.bus.Write32(fixture.stack.Add(4), 0, 1);
    static_cast<void>(fixture.Call("libGLESv2.so", "glVertexAttribPointer",
                                   {0, 3, 0x1406U, 0}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glDisableVertexAttribArray", {0}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glBindBuffer", {0x8892U, 0}));
    fixture.bus.Write32(fixture.stack, 0, 1);
    fixture.bus.Write32(fixture.stack.Add(4), buffer_data.Value(), 1);
    static_cast<void>(fixture.Call("libGLESv2.so", "glVertexAttribPointer",
                                   {0, 3, 0x1406U, 0}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glBindBuffer",
                                   {0x8892U, vertex_buffer}));

    static_cast<void>(fixture.Call("libGLESv2.so", "glGenTextures",
                                   {1, names.Value()}));
    const auto texture = fixture.bus.Read32(names, 1);
    REQUIRE(texture != 0);
    static_cast<void>(fixture.Call("libGLESv2.so", "glActiveTexture", {0x84c0U}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glBindTexture",
                                   {0x0de1U, texture}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glPixelStorei", {0x0cf5U, 1}));
    CHECK_THROWS_AS(fixture.Call("libGLESv2.so", "glPixelStorei", {0x0cf5U, 3}),
                    ogplay::gles::GlesTransferStateError);
    static_cast<void>(fixture.Call("libGLESv2.so", "glTexParameteri",
                                   {0x0de1U, 0x2801U, 0x2600U}));
    CHECK(fixture.Call("libGLESv2.so", "glTexParameteri",
                       {0x0de1U, 0x8191U, 0U}) == 0U);
    CHECK(fixture.Call("libGLESv2.so", "glGetError") == 0x0500U);
    CHECK(fixture.Call("libGLESv2.so", "glGetError") == 0U);
    fixture.bus.Write32(fixture.stack, 2, 1);
    fixture.bus.Write32(fixture.stack.Add(4), 0, 1);
    fixture.bus.Write32(fixture.stack.Add(8), 0x1908U, 1);
    fixture.bus.Write32(fixture.stack.Add(12), 0x1401U, 1);
    fixture.bus.Write32(fixture.stack.Add(16), texture_data.Value(), 1);
    static_cast<void>(fixture.Call("libGLESv2.so", "glTexImage2D",
                                   {0x0de1U, 0, 0x1908U, 2}));
    fixture.bus.Write32(fixture.stack, 1, 1);
    fixture.bus.Write32(fixture.stack.Add(4), 1, 1);
    fixture.bus.Write32(fixture.stack.Add(8), 0x1908U, 1);
    fixture.bus.Write32(fixture.stack.Add(12), 0x1401U, 1);
    fixture.bus.Write32(fixture.stack.Add(16), texture_data.Value(), 1);
    static_cast<void>(fixture.Call("libGLESv2.so", "glTexSubImage2D",
                                   {0x0de1U, 0, 0, 0}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glGenerateMipmap",
                                   {0x0de1U}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glBindFramebuffer",
                                   {0x8D40U, framebuffer}));
    fixture.bus.Write32(fixture.stack, 0U, 1U);
    static_cast<void>(fixture.Call(
        "libGLESv2.so", "glFramebufferTexture2D",
        {0x8D40U, 0x8CE0U, 0x0DE1U, texture}));
    CHECK(fixture.Call("libGLESv2.so", "glCheckFramebufferStatus",
                       {0x8D40U}) == 0x8CD5U);
    static_cast<void>(fixture.Call("libGLESv2.so", "glBindFramebuffer",
                                   {0x8D40U, 0U}));
    CHECK_THROWS_AS(fixture.Call("libGLESv2.so", "glGenBuffers", {1, 0}),
                    ogplay::gles::GuestTransferError);

    fixture.bus.Write32(names, texture, 1);
    static_cast<void>(fixture.Call("libGLESv2.so", "glDeleteTextures",
                                   {1, names.Value()}));
    fixture.bus.Write32(names, vertex_buffer, 1);
    fixture.bus.Write32(names.Add(4), index_buffer, 1);
    static_cast<void>(fixture.Call("libGLESv2.so", "glDeleteBuffers",
                                   {2, names.Value()}));
    fixture.bus.Write32(names, framebuffer, 1);
    fixture.bus.Write32(names.Add(4), renderbuffer, 1);
    static_cast<void>(fixture.Call("libGLESv2.so", "glDeleteFramebuffers",
                                   {1, names.Value()}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glDeleteRenderbuffers",
                                   {1, names.Add(4).Value()}));
}

TEST_CASE("Android boundary supersamples without changing guest surface size") {
    CHECK_THROWS_AS(BoundaryFixture(0), std::invalid_argument);
    CHECK_THROWS_AS(BoundaryFixture(5), std::invalid_argument);
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;

    BoundaryFixture fixture(2);
    PrepareNativeEgl(fixture);
    CHECK(fixture.Call("libEGL.so", "eglMakeCurrent", {1, 3, 3, 4}) == 1);
    CHECK(fixture.Call("libEGL.so", "eglQuerySurface",
                       {1, 3, 0x3057U, fixture.output.Value()}) == 1);
    CHECK(fixture.bus.Read32(fixture.output) == 4);
    CHECK(fixture.Call("libEGL.so", "eglQuerySurface",
                       {1, 3, 0x3056U, fixture.output.Value()}) == 1);
    CHECK(fixture.bus.Read32(fixture.output) == 3);

    const auto targets = fixture.boundary.RenderTargets();
    REQUIRE(targets.size() == 1);
    CHECK(targets[0].width == 8);
    CHECK(targets[0].height == 6);

    static_cast<void>(fixture.Call("libGLESv2.so", "glViewport", {0, 0, 4, 3}));
    CHECK_THROWS_AS(
        fixture.Call("libGLESv2.so", "glViewport", {0x40000000U, 0, 4, 3}),
        std::overflow_error);
    static_cast<void>(fixture.Call("libGLESv2.so", "glClearColor",
                 {std::bit_cast<std::uint32_t>(0.125F),
                  std::bit_cast<std::uint32_t>(0.25F),
                  std::bit_cast<std::uint32_t>(0.5F),
                  std::bit_cast<std::uint32_t>(1.0F)}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glClear", {0x00004000U}));
    CHECK(fixture.Call("libEGL.so", "eglSwapBuffers", {1, 3}) == 1);
    const auto frame = fixture.boundary.TakeLatestFrame();
    REQUIRE(frame.has_value());
    CHECK(frame->width == 4);
    CHECK(frame->height == 3);
    CHECK(frame->rgba8.size() == 4U * 3U * 4U);
    CHECK(frame->rgba8[0] == doctest::Approx(32).epsilon(0.02));
    CHECK(frame->rgba8[1] == doctest::Approx(64).epsilon(0.02));
    CHECK(frame->rgba8[2] == doctest::Approx(128).epsilon(0.02));
    CHECK(fixture.Call("libEGL.so", "eglTerminate", {1}) == 1);
}


namespace {
std::uint32_t AuditGl(BoundaryFixture& f, const std::string_view name,
                      const std::initializer_list<std::uint32_t> args = {}) {
    const auto entry = f.boundary.Symbols().Lookup("libGLESv2.so", name);
    REQUIRE(entry.has_value());
    return BoundaryCallAddress(f, entry->Value(), {args.begin(), args.size()});
}
std::uint32_t AuditSurface(BoundaryFixture& f) {
    f.bus.Write32(f.output, 0x3057U, 1U); f.bus.Write32(f.output.Add(4), 4U, 1U);
    f.bus.Write32(f.output.Add(8), 0x3056U, 1U); f.bus.Write32(f.output.Add(12), 4U, 1U);
    f.bus.Write32(f.output.Add(16), 0x3038U, 1U);
    const auto value = f.Call("libEGL.so", "eglCreatePbufferSurface", {1, 2, f.output.Value()});
    REQUIRE(value != 0U); return value;
}
std::uint32_t AuditContext(BoundaryFixture& f, const std::uint32_t version = 3U,
                           const std::uint32_t share = 0U) {
    f.bus.Write32(f.output, 0x3098U, 1U); f.bus.Write32(f.output.Add(4), version, 1U);
    f.bus.Write32(f.output.Add(8), 0x3038U, 1U);
    const auto value = f.Call("libEGL.so", "eglCreateContext", {1, 2, share, f.output.Value()});
    REQUIRE(value != 0U); return value;
}
void AuditBind(BoundaryFixture& f, const std::uint32_t c, const std::uint32_t d,
               const std::uint32_t r = 0U) {
    REQUIRE(f.Call("libEGL.so", "eglMakeCurrent", {1, d, r == 0U ? d : r, c}) == 1U);
}
std::uint32_t AuditName(BoundaryFixture& f, const std::string_view gen) {
    AuditGl(f, gen, {1U, f.output.Add(128).Value()});
    return f.bus.Read32(f.output.Add(128), 1U);
}
std::uint32_t AuditInteger(BoundaryFixture& f, const std::uint32_t pname) {
    AuditGl(f, "glGetIntegerv", {pname, f.output.Add(128).Value()});
    return f.bus.Read32(f.output.Add(128), 1U);
}
std::uint32_t AuditPixel(BoundaryFixture& f) {
    AuditGl(f, "glReadPixels", {0, 0, 1, 1, 0x1908, 0x1401, f.output.Add(160).Value()});
    REQUIRE(AuditGl(f, "glGetError") == 0U);
    return f.bus.Read32(f.output.Add(160), 1U);
}
}

TEST_CASE("BND34 GLES1 matrix palette performs weighted transforms and restores contexts") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture f;
    REQUIRE(f.Call("libEGL.so", "eglInitialize", {1, 0, 0}) == 1U);
    const auto surface = AuditSurface(f), context = AuditContext(f, 1U);
    AuditBind(f, context, surface);
    const auto gl = [&](std::string_view name, std::initializer_list<std::uint32_t> args = {}) {
        const auto entry = f.boundary.Symbols().Lookup("libGLESv1_CM.so", name);
        REQUIRE(entry.has_value());
        return BoundaryCallAddress(f, entry->Value(), {args.begin(), args.size()});
    };
    const auto word = [](float value) { return std::bit_cast<std::uint32_t>(value); };
    const auto vertices = f.output.Add(256), weights = f.output.Add(384), indices = f.output.Add(448);
    const std::array positions{-5.0F, -1.0F, -1.0F, -1.0F, -5.0F, 3.0F};
    for (std::size_t i = 0; i < positions.size(); ++i) f.bus.Write32(vertices.Add(i * 4), word(positions[i]), 1U);
    for (std::size_t i = 0; i < 6; ++i) f.bus.Write32(weights.Add(i * 4), word(0.5F), 1U);
    f.bus.Write32(indices, 0x01000100U, 1U); f.bus.Write32(indices.Add(4), 0x00000100U, 1U);
    gl("glVertexPointer", {2, 0x1406, 0, vertices.Value()});
    gl("glWeightPointerOES", {2, 0x1406, 0, weights.Value()});
    gl("glMatrixIndexPointerOES", {2, 0x1401, 0, indices.Value()});
    gl("glEnableClientState", {0x8074}); gl("glEnableClientState", {0x86AD}); gl("glEnableClientState", {0x8844});
    gl("glColor4f", {word(1), 0, 0, word(1)});
    gl("glMatrixMode", {0x8840}); gl("glTranslatef", {word(2), 0, 0});
    gl("glCurrentPaletteMatrixOES", {1});
    gl("glMatrixMode", {0x1700}); gl("glTranslatef", {word(6), 0, 0});
    gl("glLoadPaletteFromModelViewMatrixOES"); gl("glLoadIdentity");
    const auto draw = [&] {
        gl("glClearColor", {0, 0, 0, word(1)}); gl("glClear", {0x4000});
        gl("glDrawArrays", {4, 0, 3});
        gl("glReadPixels", {0, 0, 1, 1, 0x1908, 0x1401, f.output.Add(512).Value()});
        REQUIRE(gl("glGetError") == 0U);
        return f.bus.Read32(f.output.Add(512), 1U);
    };
    CHECK(draw() == 0xFF000000U);
    gl("glEnable", {0x8840}); CHECK(draw() == 0xFF0000FFU);
    const auto other = AuditContext(f, 1U);
    AuditBind(f, other, surface); gl("glCurrentPaletteMatrixOES", {7});
    AuditBind(f, context, surface);
    gl("glGetIntegerv", {0x8843, f.output.Add(520).Value()}); CHECK(f.bus.Read32(f.output.Add(520), 1U) == 1U);
    CHECK(draw() == 0xFF0000FFU);
    gl("glDisable", {0x8840}); CHECK(draw() == 0xFF000000U);
    gl("glPixelStorei", {0x0D05, 8});
    for (std::uint32_t i = 0; i < 16; i += 4) f.bus.Write32(f.output.Add(600 + i), 0xAABBCCDDU, 1U);
    gl("glReadPixels", {0, 0, 1, 2, 0x1908, 0x1401, f.output.Add(600).Value()});
    CHECK(f.bus.Read32(f.output.Add(604), 1U) == 0xAABBCCDDU);
    CHECK(f.bus.Read32(f.output.Add(608), 1U) == 0xFF000000U);
    CHECK(f.bus.Read32(f.output.Add(612), 1U) == 0xAABBCCDDU);
    CHECK(gl("glGetError") == 0U);
}

TEST_CASE("BND34 ES3 mapping flush ranges arena reuse and VAO deletion remain coherent") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture f;
    REQUIRE(f.Call("libEGL.so", "eglInitialize", {1, 0, 0}) == 1U);
    const auto surface = AuditSurface(f), context = AuditContext(f);
    AuditBind(f, context, surface);
    const auto data = f.output.Add(640);
    f.bus.Write32(data, 0x11223344U, 1U); f.bus.Write32(data.Add(4), 0x55667788U, 1U);
    const auto buffer = AuditName(f, "glGenBuffers");
    AuditGl(f, "glBindBuffer", {0x8892, buffer}); AuditGl(f, "glBufferData", {0x8892, 8, data.Value(), 0x88E8});
    const auto mapped = AuditGl(f, "glMapBufferRange", {0x8892, 0, 8, 0x12}); REQUIRE(mapped != 0U);
    f.bus.Write32(ogplay::memory::GuestAddress{mapped}, 0xABCDEF12U, 1U);
    AuditGl(f, "glFlushMappedBufferRange", {0x8892, 0, 4});
    f.bus.Write32(ogplay::memory::GuestAddress{mapped}, 0x98765432U, 1U);
    REQUIRE(AuditGl(f, "glUnmapBuffer", {0x8892}) == 1U);
    const auto read = AuditGl(f, "glMapBufferRange", {0x8892, 0, 8, 1}); REQUIRE(read != 0U);
    CHECK(f.bus.Read32(ogplay::memory::GuestAddress{read}, 1U) == 0xABCDEF12U);
    // Keep this mapping alive while repeatedly mapping a second buffer: the arena must reuse gaps.
    const auto second = AuditName(f, "glGenBuffers");
    AuditGl(f, "glBindBuffer", {0x8892, second}); AuditGl(f, "glBufferData", {0x8892, 2U * 1024U * 1024U, 0, 0x88E8});
    for (unsigned i = 0; i < 10; ++i) {
        REQUIRE(AuditGl(f, "glMapBufferRange", {0x8892, 0, 2U * 1024U * 1024U, 2}) != 0U);
        REQUIRE(AuditGl(f, "glUnmapBuffer", {0x8892}) == 1U);
    }
    AuditGl(f, "glBindBuffer", {0x8892, buffer}); REQUIRE(AuditGl(f, "glUnmapBuffer", {0x8892}) == 1U);
    const auto vao = AuditName(f, "glGenVertexArrays"); AuditGl(f, "glBindVertexArray", {vao});
    AuditGl(f, "glBindBuffer", {0x8893, buffer}); AuditGl(f, "glEnableVertexAttribArray", {0});
    AuditGl(f, "glVertexAttribI4ui", {3, 0xFFFFFFFFU, 0xABCDEF12U, 7, 9});
    AuditGl(f, "glVertexAttribDivisor", {3, 2});
    f.bus.Write32(data, 0U, 1U); f.bus.Write32(data.Add(4), 0U, 1U);
    f.Call("libGLESv1_CM.so", "glBindBuffer", {0x8892, 0});
    f.Call("libGLESv1_CM.so", "glVertexPointer", {2, 0x1406, 0, data.Value()});
    f.Call("libGLESv1_CM.so", "glEnableClientState", {0x8074});
    f.Call("libGLESv1_CM.so", "glDrawArrays", {0, 0, 1});
    CHECK(AuditInteger(f, 0x85B5) == vao);
    AuditGl(f, "glGetVertexAttribIuiv", {3, 0x8626, data.Value()});
    CHECK(f.bus.Read32(data, 1U) == 0xFFFFFFFFU); CHECK(f.bus.Read32(data.Add(4), 1U) == 0xABCDEF12U);
    AuditGl(f, "glGetVertexAttribiv", {3, 0x88FE, data.Value()}); CHECK(f.bus.Read32(data, 1U) == 2U);
    f.bus.Write32(data, vao, 1U); AuditGl(f, "glDeleteVertexArrays", {1, data.Value()});
    CHECK(AuditInteger(f, 0x85B5) == 0U); CHECK(AuditInteger(f, 0x8895) == 0U);
    AuditGl(f, "glGetVertexAttribiv", {0, 0x8622, data.Value()}); CHECK(f.bus.Read32(data, 1U) == 0U);
    AuditGl(f, "glViewport", {1, 2, 3, 4});
    f.bus.Write32(data.Add(32), 0xDEADBEEFU, 1U);
    AuditGl(f, "glGetInteger64v", {0x0BA2, data.Value()});
    for (std::uint32_t i = 0; i < 4; ++i) {
        CHECK(f.bus.Read32(data.Add(i * 8), 1U) == i + 1U);
        CHECK(f.bus.Read32(data.Add(i * 8 + 4), 1U) == 0U);
    }
    CHECK(f.bus.Read32(data.Add(32), 1U) == 0xDEADBEEFU);
    CHECK(AuditGl(f, "glGetError") == 0U);
}

TEST_CASE("BND34 EGL swap presents draw surface and preserves read framebuffer") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture f;
    REQUIRE(f.Call("libEGL.so", "eglInitialize", {1, 0, 0}) == 1U);
    const auto window = f.Call("libEGL.so", "eglCreateWindowSurface", {1, 2, 3, 0}); REQUIRE(window != 0U);
    const auto read_surface = AuditSurface(f), context = AuditContext(f);
    AuditBind(f, context, window, read_surface);
    AuditGl(f, "glClearColor", {0x3F800000U, 0, 0, 0x3F800000U}); AuditGl(f, "glClear", {0x4000});
    const auto texture = AuditName(f, "glGenTextures"), framebuffer = AuditName(f, "glGenFramebuffers");
    AuditGl(f, "glBindTexture", {0x0DE1, texture}); f.bus.Write32(f.output.Add(700), 0xFF00FF00U, 1U);
    AuditGl(f, "glTexImage2D", {0x0DE1, 0, 0x1908, 1, 1, 0, 0x1908, 0x1401, f.output.Add(700).Value()});
    AuditGl(f, "glBindFramebuffer", {0x8CA8, framebuffer});
    AuditGl(f, "glFramebufferTexture2D", {0x8CA8, 0x8CE0, 0x0DE1, texture, 0});
    CHECK(AuditPixel(f) == 0xFF00FF00U);
    REQUIRE(f.Call("libEGL.so", "eglSwapBuffers", {1, window}) == 1U);
    const auto frame = f.boundary.TakeLatestFrame(); REQUIRE(frame.has_value());
    REQUIRE(frame->rgba8.size() >= 4); CHECK(frame->rgba8[0] == 255U); CHECK(frame->rgba8[1] == 0U);
    CHECK(AuditInteger(f, 0x8CAA) == framebuffer); CHECK(AuditInteger(f, 0x8CA6) == 0U);
    CHECK(AuditPixel(f) == 0xFF00FF00U);
}

TEST_CASE("BND34 GLES1 mappings survive releasing a different context") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture f;
    REQUIRE(f.Call("libEGL.so", "eglInitialize", {1, 0, 0}) == 1U);
    const auto surface = AuditSurface(f), a = AuditContext(f, 1U), b = AuditContext(f, 1U);
    const auto map = [&] {
        f.Call("libGLESv1_CM.so", "glGenBuffers", {1, f.output.Add(256).Value()});
        const auto buffer = f.bus.Read32(f.output.Add(256), 1U);
        f.Call("libGLESv1_CM.so", "glBindBuffer", {0x8892, buffer});
        f.Call("libGLESv1_CM.so", "glBufferData", {0x8892, 16, 0, 0x88E8});
        const auto pointer = f.Call("libGLESv1_CM.so", "glMapBufferOES", {0x8892, 0x88B9});
        REQUIRE(pointer != 0U); return std::pair{buffer, pointer};
    };
    AuditBind(f, a, surface); const auto first = map();
    AuditBind(f, b, surface); const auto second = map();
    REQUIRE(first.first == second.first); CHECK(first.second != second.second);
    f.bus.Write32(ogplay::memory::GuestAddress{first.second}, 0x12345678U, 1U);
    f.bus.Write32(ogplay::memory::GuestAddress{second.second}, 0x90ABCDEFU, 1U);
    REQUIRE(f.Call("libEGL.so", "eglReleaseThread") == 1U);
    AuditBind(f, a, surface);
    f.Call("libGLESv1_CM.so", "glGetBufferPointervOES", {0x8892, 0x88BD, f.output.Add(256).Value()});
    CHECK(f.bus.Read32(f.output.Add(256), 1U) == first.second);
    REQUIRE(f.Call("libGLESv1_CM.so", "glUnmapBufferOES", {0x8892}) == 1U);
    AuditBind(f, b, surface);
    REQUIRE(f.Call("libGLESv1_CM.so", "glUnmapBufferOES", {0x8892}) == 1U);
    CHECK(f.Call("libGLESv1_CM.so", "glGetError") == 0U);
}

TEST_CASE("BND34 EGL separates context state surface content and read surface") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture f;
    REQUIRE(f.Call("libEGL.so", "eglInitialize", {1}) == 1U);
    const auto a = AuditContext(f), b = AuditContext(f);
    const auto x = AuditSurface(f), y = AuditSurface(f);
    AuditBind(f, a, x);
    AuditGl(f, "glEnable", {0x0BE2});
    AuditGl(f, "glClearColor", {0x3F800000, 0, 0, 0x3F800000});
    AuditGl(f, "glClear", {0x4000});
    AuditBind(f, a, y);
    CHECK(AuditGl(f, "glIsEnabled", {0x0BE2}) == 1U);
    AuditGl(f, "glClearColor", {0, 0x3F800000, 0, 0x3F800000});
    AuditGl(f, "glClear", {0x4000});
    AuditBind(f, b, x);
    CHECK(AuditGl(f, "glIsEnabled", {0x0BE2}) == 0U);
    CHECK(AuditPixel(f) == 0xFF0000FFU);
    AuditBind(f, a, y, x);
    CHECK(AuditPixel(f) == 0xFF0000FFU);
    const auto texture = AuditName(f, "glGenTextures");
    AuditGl(f, "glBindTexture", {0x0DE1, texture});
    AuditGl(f, "glCopyTexImage2D", {0x0DE1, 0, 0x1908, 0, 0, 1, 1, 0});
    const auto framebuffer = AuditName(f, "glGenFramebuffers");
    AuditGl(f, "glBindFramebuffer", {0x8D40, framebuffer});
    AuditGl(f, "glFramebufferTexture2D", {0x8D40, 0x8CE0, 0x0DE1, texture, 0});
    CHECK(AuditPixel(f) == 0xFF0000FFU);
}

TEST_CASE("BND34 EGL share group survives reverse bind order and source deletion") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture f;
    REQUIRE(f.Call("libEGL.so", "eglInitialize", {1}) == 1U);
    const auto a = AuditContext(f), b = AuditContext(f, 3U, a);
    const auto surface = AuditSurface(f);
    AuditBind(f, b, surface);
    const auto texture = AuditName(f, "glGenTextures");
    AuditGl(f, "glBindTexture", {0x0DE1, texture});
    AuditBind(f, a, surface);
    CHECK(AuditGl(f, "glIsTexture", {texture}) == 1U);
    REQUIRE(f.Call("libEGL.so", "eglMakeCurrent", {1, 0, 0, 0}) == 1U);
    REQUIRE(f.Call("libEGL.so", "eglDestroyContext", {1, a}) == 1U);
    AuditBind(f, b, surface);
    CHECK(AuditGl(f, "glIsTexture", {texture}) == 1U);
}

TEST_CASE("BND34 GLES1 isolates light fog and client arrays between contexts") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture f;
    REQUIRE(f.Call("libEGL.so", "eglInitialize", {1}) == 1U);
    const auto a = AuditContext(f, 1), b = AuditContext(f, 1), surface = AuditSurface(f);
    AuditBind(f, a, surface);
    f.Call("libGLESv1_CM.so", "glFogf", {0x0B62, 0x40000000});
    f.Call("libGLESv1_CM.so", "glLightf", {0x4000, 0x1205, 0x41000000});
    f.Call("libGLESv1_CM.so", "glEnableClientState", {0x8074});
    AuditBind(f, b, surface);
    f.Call("libGLESv1_CM.so", "glGetFloatv", {0x0B62, f.output.Value()});
    CHECK(f.bus.Read32(f.output, 1) == 0x3F800000U);
    CHECK(f.Call("libGLESv1_CM.so", "glIsEnabled", {0x8074}) == 0U);
    AuditBind(f, a, surface);
    f.Call("libGLESv1_CM.so", "glGetFloatv", {0x0B62, f.output.Value()});
    CHECK(f.bus.Read32(f.output, 1) == 0x40000000U);
    f.Call("libGLESv1_CM.so", "glGetLightfv", {0x4000, 0x1205, f.output.Value()});
    CHECK(f.bus.Read32(f.output, 1) == 0x41000000U);
    CHECK(f.Call("libGLESv1_CM.so", "glIsEnabled", {0x8074}) == 1U);
}

TEST_CASE("BND34 ES3 queries respect scalar and variable output extents") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture f;
    REQUIRE(f.Call("libEGL.so", "eglInitialize", {1}) == 1U);
    const auto context = AuditContext(f), surface = AuditSurface(f);
    AuditBind(f, context, surface);
    const auto sampler = AuditName(f, "glGenSamplers");
    const auto tail = f.output.Add(f.memory.PageSize() - 4);
    AuditGl(f, "glGetSamplerParameteriv", {sampler, 0x2800, tail.Value()});
    CHECK(f.bus.Read32(tail, 1) == 0x2601U);
    AuditGl(f, "glGetSamplerParameterfv", {sampler, 0x813B, tail.Value()});
    CHECK(AuditGl(f, "glGetError") == 0U);
    f.bus.Write32(tail, 0xDEADBEEFU, 1U);
    AuditGl(f, "glGetSamplerParameteriv", {sampler, 0x1004, tail.Value()});
    CHECK(AuditGl(f, "glGetError") == 0x0500U); CHECK(f.bus.Read32(tail, 1U) == 0xDEADBEEFU);
    AuditGl(f, "glSamplerParameterfv", {sampler, 0x1004, tail.Value()});
    CHECK(AuditGl(f, "glGetError") == 0x0500U);
    AuditGl(f, "glGetTexParameterfv", {0x0DE1, 0x1004, tail.Value()});
    CHECK(AuditGl(f, "glGetError") == 0x0500U); CHECK(f.bus.Read32(tail, 1U) == 0xDEADBEEFU);
    const auto tex = AuditName(f, "glGenTextures"); AuditGl(f, "glBindTexture", {0x0DE1, tex});
    f.bus.Write32(f.output.Add(256), 0x812FU, 1U);
    AuditGl(f, "glTexParameteriv", {0x0DE1, 0x8072, f.output.Add(256).Value()});
    AuditGl(f, "glGetTexParameteriv", {0x0DE1, 0x8072, tail.Value()}); CHECK(f.bus.Read32(tail, 1U) == 0x812FU);
    AuditGl(f, "glTexParameterf", {0x0DE1, 0x813A, 0x40600000U});
    AuditGl(f, "glGetTexParameterfv", {0x0DE1, 0x813A, tail.Value()}); CHECK(f.bus.Read32(tail, 1U) == 0x40600000U);
    CHECK(AuditGl(f, "glGetError") == 0U);
    const auto compile = [&](const std::uint32_t type, const std::string_view source) {
        const auto shader = AuditGl(f, "glCreateShader", {type});
        WriteGuestString(f, f.output.Add(512), source);
        f.bus.Write32(f.output.Add(256), f.output.Add(512).Value(), 1);
        AuditGl(f, "glShaderSource", {shader, 1, f.output.Add(256).Value(), 0});
        AuditGl(f, "glCompileShader", {shader});
        AuditGl(f, "glGetShaderiv", {shader, 0x8B81, f.output.Value()});
        REQUIRE(f.bus.Read32(f.output, 1) == 1U);
        return shader;
    };
    const auto vertex = compile(0x8B31, "#version 300 es\nlayout(std140) uniform U{vec4 a;vec4 b;};uniform mat4 m;void main(){gl_Position=m*(a+b);}");
    const auto fragment = compile(0x8B30, "#version 300 es\nprecision highp float;out vec4 c;void main(){c=vec4(1);}");
    const auto program = AuditGl(f, "glCreateProgram");
    AuditGl(f, "glAttachShader", {program, vertex});
    AuditGl(f, "glAttachShader", {program, fragment});
    AuditGl(f, "glLinkProgram", {program});
    AuditGl(f, "glGetProgramiv", {program, 0x8B82, f.output.Value()});
    REQUIRE(f.bus.Read32(f.output, 1) == 1U);
    AuditGl(f, "glGetProgramInfoLog", {program, 32, 0, f.output.Add(800).Value()});
    WriteGuestString(f, f.output.Add(256), "U");
    const auto block = AuditGl(f, "glGetUniformBlockIndex", {program, f.output.Add(256).Value()});
    AuditGl(f, "glGetActiveUniformBlockiv", {program, block, 0x8A42, f.output.Value()});
    REQUIRE(f.bus.Read32(f.output, 1) == 2U);
    f.bus.Write32(f.output.Add(8), 0xDEADBEEFU, 1);
    AuditGl(f, "glGetActiveUniformBlockiv", {program, block, 0x8A43, f.output.Value()});
    CHECK(f.bus.Read32(f.output, 1) != f.bus.Read32(f.output.Add(4), 1));
    CHECK(f.bus.Read32(f.output.Add(8), 1) == 0xDEADBEEFU);
    CHECK(AuditGl(f, "glGetError") == 0U);
    const auto count = AuditInteger(f, 0x821D);
    CHECK(count >= 3U);
    CHECK(AuditGl(f, "glGetStringi", {0x1F03, count}) == 0U);
    CHECK(AuditGl(f, "glGetError") == 0x0501U);
    WriteGuestString(f, f.output.Add(256), "m");
    const auto location = AuditGl(f, "glGetUniformLocation", {program, f.output.Add(256).Value()});
    REQUIRE(location != 0xFFFFFFFFU);
    const auto shared = AuditContext(f, 3U, context);
    AuditBind(f, shared, surface);
    f.bus.Write32(f.output.Add(64), 0xABCDEF12U, 1U);
    AuditGl(f, "glGetUniformfv", {program, location, f.output.Value()});
    for (std::uint32_t i = 0; i < 16; ++i) CHECK(f.bus.Read32(f.output.Add(i * 4), 1U) == 0U);
    CHECK(f.bus.Read32(f.output.Add(64), 1U) == 0xABCDEF12U);
    CHECK(AuditGl(f, "glGetError") == 0U);
}

TEST_CASE("BND34 ES3 buffers VAOs pixel stores and map identities are isolated") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture f;
    REQUIRE(f.Call("libEGL.so", "eglInitialize", {1}) == 1U);
    const auto a = AuditContext(f), b = AuditContext(f), surface = AuditSurface(f);
    AuditBind(f, a, surface);
    const auto buffer_a = AuditName(f, "glGenBuffers");
    AuditGl(f, "glBindBuffer", {0x8A11, buffer_a});
    AuditGl(f, "glBufferData", {0x8A11, 64, 0, 0x88E8});
    const auto mapped_a = AuditGl(f, "glMapBufferRange", {0x8A11, 0, 64, 2});
    REQUIRE(mapped_a != 0U);
    f.bus.Write32(ogplay::memory::GuestAddress{mapped_a}, 0x12345678U, 1);
    AuditBind(f, b, surface);
    const auto buffer_b = AuditName(f, "glGenBuffers");
    REQUIRE(buffer_a == buffer_b);
    AuditGl(f, "glBindBuffer", {0x8A11, buffer_b});
    AuditGl(f, "glBufferData", {0x8A11, 64, 0, 0x88E8});
    const auto mapped_b = AuditGl(f, "glMapBufferRange", {0x8A11, 0, 64, 2});
    REQUIRE(mapped_b != 0U); CHECK(mapped_a != mapped_b);
    CHECK(AuditGl(f, "glUnmapBuffer", {0x8A11}) == 1U);
    AuditBind(f, a, surface);
    CHECK(AuditGl(f, "glUnmapBuffer", {0x8A11}) == 1U);
    const auto vao_a = AuditName(f, "glGenVertexArrays"), vao_b = AuditName(f, "glGenVertexArrays");
    AuditGl(f, "glBindVertexArray", {vao_a});
    AuditGl(f, "glBindBuffer", {0x8893, buffer_a});
    AuditGl(f, "glEnableVertexAttribArray", {0});
    AuditGl(f, "glBindVertexArray", {vao_b});
    CHECK(AuditInteger(f, 0x8895) == 0U);
    AuditGl(f, "glBindVertexArray", {vao_a});
    CHECK(AuditInteger(f, 0x8895) == buffer_a);
    AuditGl(f, "glGetVertexAttribiv", {0, 0x8622, f.output.Value()});
    CHECK(f.bus.Read32(f.output, 1) == 1U);
    AuditGl(f, "glBindBuffer", {0x88EC, buffer_a});
    AuditGl(f, "glBufferData", {0x88EC, 64, 0, 0x88E8});
    const auto texture = AuditName(f, "glGenTextures");
    AuditGl(f, "glBindTexture", {0x806F, texture});
    AuditGl(f, "glTexImage3D", {0x806F, 0, 0x8058, 1, 1, 1, 0, 0x1908, 0x1401, 4});
    CHECK(AuditGl(f, "glGetError") == 0U);
    AuditGl(f, "glBindBuffer", {0x88EC, 0});
    AuditGl(f, "glPixelStorei", {0x0D02, 4});
    AuditGl(f, "glPixelStorei", {0x0D03, 1});
    AuditGl(f, "glPixelStorei", {0x0D04, 1});
    AuditGl(f, "glClearColor", {0x3F800000, 0, 0, 0x3F800000});
    AuditGl(f, "glClear", {0x4000});
    for (std::uint32_t i = 0; i < 64; i += 4) f.bus.Write32(f.output.Add(256 + i), 0xAABBCCDDU, 1);
    AuditGl(f, "glReadPixels", {0, 0, 1, 2, 0x1908, 0x1401, f.output.Add(256).Value()});
    CHECK(f.bus.Read32(f.output.Add(256), 1) == 0xAABBCCDDU);
    CHECK(f.bus.Read32(f.output.Add(276), 1) == 0xFF0000FFU);
    CHECK(f.bus.Read32(f.output.Add(280), 1) == 0xAABBCCDDU);
    CHECK(f.bus.Read32(f.output.Add(292), 1) == 0xFF0000FFU);
    CHECK(AuditGl(f, "glGetError") == 0U);
}


TEST_CASE("BND34 EGL fences images and texture pbuffers use real ANGLE objects") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture f;
    const auto egl = [&](const std::string_view name, const std::initializer_list<std::uint32_t> args) {
        const auto entry = f.boundary.Symbols().Lookup("libEGL.so", name);
        REQUIRE(entry.has_value());
        return BoundaryCallAddress(f, entry->Value(), {args.begin(), args.size()});
    };
    REQUIRE(egl("eglInitialize", {1}) == 1U);
    const auto context = AuditContext(f), surface = AuditSurface(f);
    AuditBind(f, context, surface);
    const auto extension_address = ogplay::memory::GuestAddress{egl("eglQueryString", {1, 0x3055})};
    std::string extensions;
    for (std::uint32_t i = 0; ; ++i) {
        const auto byte = f.bus.Read8(extension_address.Add(i), 1);
        if (byte == 0U) break;
        extensions.push_back(static_cast<char>(byte));
    }
    const bool reusable = extensions.find("EGL_KHR_reusable_sync") != std::string::npos;
    const auto sync_type = reusable ? 0x30FAU : 0x30F9U;
    const auto sync = egl("eglCreateSyncKHR", {1, sync_type, 0});
    INFO("EGL create sync error: ", egl("eglGetError", {}));
    REQUIRE(sync != 0U);
    if (reusable) {
        CHECK(egl("eglClientWaitSyncKHR", {1, sync, 0, 0, 0, 0}) == 0x30F5U);
        CHECK(egl("eglSignalSyncKHR", {1, sync, 0x30F2}) == 1U);
    }
    CHECK(egl("eglGetSyncAttribKHR", {1, sync, 0x30F7, f.output.Value()}) == 1U);
    CHECK(f.bus.Read32(f.output, 1) == sync_type);
    CHECK(egl("eglClientWaitSyncKHR", {1, sync, 1, 0, 0xFFFFFFFFU, 0xFFFFFFFFU}) == 0x30F6U);
    CHECK(egl("eglDestroySyncKHR", {1, sync}) == 1U);
    CHECK(egl("eglDestroySyncKHR", {1, sync}) == 0U);
    CHECK(egl("eglGetError", {}) == 0x300CU);
    const auto texture = AuditName(f, "glGenTextures");
    AuditGl(f, "glBindTexture", {0x0DE1, texture});
    f.bus.Write32(f.output.Add(256), 0xFF112233U, 1);
    AuditGl(f, "glTexImage2D", {0x0DE1, 0, 0x1908, 1, 1, 0, 0x1908, 0x1401, f.output.Add(256).Value()});
    f.bus.Write32(f.output, 0x30D2, 1); f.bus.Write32(f.output.Add(4), 1, 1);
    f.bus.Write32(f.output.Add(8), 0x3038, 1);
    const auto image = egl("eglCreateImageKHR", {1, context, 0x30B1, texture, f.output.Value()});
    REQUIRE(image != 0U);
    const auto sibling = AuditName(f, "glGenTextures");
    AuditGl(f, "glBindTexture", {0x0DE1, sibling});
    AuditGl(f, "glEGLImageTargetTexture2DOES", {0x0DE1, image});
    REQUIRE(AuditGl(f, "glGetError") == 0U);
    CHECK(egl("eglDestroyImageKHR", {1, image}) == 1U);
    const auto framebuffer = AuditName(f, "glGenFramebuffers");
    AuditGl(f, "glBindFramebuffer", {0x8D40, framebuffer});
    AuditGl(f, "glFramebufferTexture2D", {0x8D40, 0x8CE0, 0x0DE1, sibling, 0});
    CHECK(AuditPixel(f) == 0xFF112233U);
    AuditGl(f, "glBindFramebuffer", {0x8D40, 0});
    const std::array<std::uint32_t, 11> attributes{0x3057, 4, 0x3056, 4, 0x3080, 0x305E,
                                                0x3081, 0x305F, 0x3082, 0, 0x3038};
    for (std::size_t i = 0; i < attributes.size(); ++i) f.bus.Write32(f.output.Add(i * 4), attributes[i], 1);
    const auto textured = egl("eglCreatePbufferSurface", {1, 2, f.output.Value()});
    REQUIRE(textured != 0U);
    AuditBind(f, context, textured);
    AuditGl(f, "glClearColor", {0, 0x3F800000, 0, 0x3F800000});
    AuditGl(f, "glClear", {0x4000});
    AuditBind(f, context, surface);
    const auto bound_texture = AuditName(f, "glGenTextures");
    AuditGl(f, "glBindTexture", {0x0DE1, bound_texture});
    REQUIRE(egl("eglBindTexImage", {1, textured, 0x3084}) == 1U);
    AuditGl(f, "glBindFramebuffer", {0x8D40, framebuffer});
    AuditGl(f, "glFramebufferTexture2D", {0x8D40, 0x8CE0, 0x0DE1, bound_texture, 0});
    CHECK(AuditPixel(f) == 0xFF00FF00U);
    CHECK(egl("eglReleaseTexImage", {1, textured, 0x3084}) == 1U);
    CHECK(egl("eglSwapInterval", {1, 99}) == 1U);
    CHECK(egl("eglSwapInterval", {1, 0xFFFFFFFFU}) == 1U);
}
