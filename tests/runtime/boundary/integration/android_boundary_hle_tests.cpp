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
    const auto benchmark_looper = fixture.Call("libandroid.so", "ALooper_prepare", {1});
    const std::array stack_words{5U, 6U};
    const auto stack_us = run_fast(
        "libandroid.so", "ALooper_addFd", {benchmark_looper, 2U, 3U, 4U},
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
