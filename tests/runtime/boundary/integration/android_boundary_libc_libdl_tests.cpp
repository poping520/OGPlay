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
        bool fatal_open{};
        std::uint32_t unwind_pc{};
    } probe;
    const ogplay::runtime::BionicDynamicLinkHooks hooks{
        &probe,
        +[](void* owner, const std::string_view library,
            const std::uint32_t flags, const std::uint64_t thread) {
            auto& state = *static_cast<Probe*>(owner);
            if (state.fatal_open) throw std::runtime_error("constructor execution failed");
            if (state.fail_open) throw ogplay::loader::LinkError("library unavailable");
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
        },
        +[](void* owner, const std::uint32_t pc, const std::uint64_t thread) {
            auto& state = *static_cast<Probe*>(owner);
            state.unwind_pc = pc;
            state.thread = thread;
            return ogplay::runtime::BionicDynamicLinkHooks::ArmExidx{
                0x60012000U, 37U};
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

    const auto count = fixture.output.Add(128U);
    fixture.memory.Write32(count, 0xffffffffU, 1U);
    CHECK(fixture.Call("libdl.so", "dl_unwind_find_exidx",
                       {0x60001004U, count.Value(), 0U, 0U}) ==
          0x60012000U);
    CHECK(fixture.memory.Read32(count, 1U) == 37U);
    CHECK(probe.unwind_pc == 0x60001004U);
    CHECK(probe.thread == 1U);

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
    probe.fatal_open = true;
    CHECK_THROWS_WITH_AS(static_cast<void>(fixture.Call("libdl.so", "dlopen",
        {fixture.output.Value(), 2U, 0U, 0U})), "constructor execution failed", std::runtime_error);
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
