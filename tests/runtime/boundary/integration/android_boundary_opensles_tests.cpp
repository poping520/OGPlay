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

TEST_CASE("OpenSL ES rejects oversized enqueue before copying guest memory") {
    BoundaryFixture fixture;
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
    CHECK(call(create->Value(), {fixture.output.Value(), 0U, 0U, 0U, 0U, 0U}) ==
          0U);
    const auto engine_object = fixture.bus.Read32(fixture.output, 1U);
    const auto object_vtable = fixture.bus.Read32(
        ogplay::memory::GuestAddress{engine_object}, 1U);
    CHECK(call(fixture.bus.Read32(ogplay::memory::GuestAddress{object_vtable},
                                  1U),
               {engine_object, 0U}) == 0U);
    CHECK(call(fixture.bus.Read32(
                   ogplay::memory::GuestAddress{object_vtable + 12U}, 1U),
               {engine_object, iid("SL_IID_ENGINE"),
                fixture.output.Add(4U).Value()}) == 0U);
    const auto engine = fixture.bus.Read32(fixture.output.Add(4U), 1U);
    const auto engine_vtable = fixture.bus.Read32(
        ogplay::memory::GuestAddress{engine}, 1U);
    CHECK(call(fixture.bus.Read32(
                   ogplay::memory::GuestAddress{engine_vtable + 7U * 4U}, 1U),
               {engine, fixture.output.Add(8U).Value(), 0U, 0U, 0U}) == 0U);
    const auto mix_object = fixture.bus.Read32(fixture.output.Add(8U), 1U);
    const auto mix_object_vtable = fixture.bus.Read32(
        ogplay::memory::GuestAddress{mix_object}, 1U);
    CHECK(call(fixture.bus.Read32(
                   ogplay::memory::GuestAddress{mix_object_vtable}, 1U),
               {mix_object, 0U}) == 0U);
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
    fixture.bus.Write32(interface_required, 1U, 1U);
    CHECK(call(fixture.bus.Read32(
                   ogplay::memory::GuestAddress{engine_vtable + 2U * 4U}, 1U),
               {engine, fixture.output.Add(12U).Value(), source.Value(),
                sink.Value(), 1U, interface_ids.Value(),
                interface_required.Value()}) == 0U);
    const auto player_object = fixture.bus.Read32(fixture.output.Add(12U), 1U);
    const auto player_object_vtable = fixture.bus.Read32(
        ogplay::memory::GuestAddress{player_object}, 1U);
    CHECK(call(fixture.bus.Read32(
                   ogplay::memory::GuestAddress{player_object_vtable}, 1U),
               {player_object, 0U}) == 0U);
    CHECK(call(fixture.bus.Read32(
                   ogplay::memory::GuestAddress{player_object_vtable + 12U},
                   1U),
               {player_object, iid("SL_IID_ANDROIDSIMPLEBUFFERQUEUE"),
                fixture.output.Add(20U).Value()}) == 0U);
    const auto queue = fixture.bus.Read32(fixture.output.Add(20U), 1U);
    const auto queue_vtable =
        fixture.bus.Read32(ogplay::memory::GuestAddress{queue}, 1U);
    const auto enqueue = fixture.bus.Read32(
        ogplay::memory::GuestAddress{queue_vtable}, 1U);
    CHECK(call(enqueue,
               {queue, fixture.output.Add(0x200U).Value(),
                ogplay::audio::OpenSlesPcmMixer::kMaximumBufferBytes + 1U}) ==
          3U);
}

TEST_CASE("OpenSL ES caps pending callbacks in one mix") {
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
    CHECK(call(create->Value(), {fixture.output.Value(), 0U, 0U, 0U, 0U, 0U}) ==
          0U);
    const auto engine_object = fixture.bus.Read32(fixture.output, 1U);
    const auto object_vtable = fixture.bus.Read32(
        ogplay::memory::GuestAddress{engine_object}, 1U);
    CHECK(call(fixture.bus.Read32(ogplay::memory::GuestAddress{object_vtable},
                                  1U),
               {engine_object, 0U}) == 0U);
    CHECK(call(fixture.bus.Read32(
                   ogplay::memory::GuestAddress{object_vtable + 12U}, 1U),
               {engine_object, iid("SL_IID_ENGINE"),
                fixture.output.Add(4U).Value()}) == 0U);
    const auto engine = fixture.bus.Read32(fixture.output.Add(4U), 1U);
    const auto engine_vtable = fixture.bus.Read32(
        ogplay::memory::GuestAddress{engine}, 1U);
    CHECK(call(fixture.bus.Read32(
                   ogplay::memory::GuestAddress{engine_vtable + 7U * 4U}, 1U),
               {engine, fixture.output.Add(8U).Value(), 0U, 0U, 0U}) == 0U);
    const auto mix_object = fixture.bus.Read32(fixture.output.Add(8U), 1U);
    const auto mix_object_vtable = fixture.bus.Read32(
        ogplay::memory::GuestAddress{mix_object}, 1U);
    CHECK(call(fixture.bus.Read32(
                   ogplay::memory::GuestAddress{mix_object_vtable}, 1U),
               {mix_object, 0U}) == 0U);
    const auto source = fixture.output.Add(0x100U);
    const auto source_locator = fixture.output.Add(0x120U);
    const auto source_format = fixture.output.Add(0x140U);
    const auto sink = fixture.output.Add(0x180U);
    const auto sink_locator = fixture.output.Add(0x1a0U);
    fixture.bus.Write32(source, source_locator.Value(), 1U);
    fixture.bus.Write32(source.Add(4U), source_format.Value(), 1U);
    fixture.bus.Write32(source_locator, 0x800007bdU, 1U);
    fixture.bus.Write32(source_locator.Add(4U), 255U, 1U);
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
    fixture.bus.Write32(interface_required, 1U, 1U);
    CHECK(call(fixture.bus.Read32(
                   ogplay::memory::GuestAddress{engine_vtable + 2U * 4U}, 1U),
               {engine, fixture.output.Add(12U).Value(), source.Value(),
                sink.Value(), 1U, interface_ids.Value(),
                interface_required.Value()}) == 0U);
    const auto player_object = fixture.bus.Read32(fixture.output.Add(12U), 1U);
    const auto player_object_vtable = fixture.bus.Read32(
        ogplay::memory::GuestAddress{player_object}, 1U);
    CHECK(call(fixture.bus.Read32(
                   ogplay::memory::GuestAddress{player_object_vtable}, 1U),
               {player_object, 0U}) == 0U);
    CHECK(call(fixture.bus.Read32(
                   ogplay::memory::GuestAddress{player_object_vtable + 12U},
                   1U),
               {player_object, iid("SL_IID_ANDROIDSIMPLEBUFFERQUEUE"),
                fixture.output.Add(20U).Value()}) == 0U);
    const auto queue = fixture.bus.Read32(fixture.output.Add(20U), 1U);
    const auto queue_vtable =
        fixture.bus.Read32(ogplay::memory::GuestAddress{queue}, 1U);
    CHECK(call(fixture.bus.Read32(
                   ogplay::memory::GuestAddress{queue_vtable + 3U * 4U}, 1U),
               {queue, 0x60000003U, 0U}) == 0U);
    CHECK(call(fixture.bus.Read32(
                   ogplay::memory::GuestAddress{player_object_vtable + 12U},
                   1U),
               {player_object, iid("SL_IID_PLAY"),
                fixture.output.Add(24U).Value()}) == 0U);
    const auto play = fixture.bus.Read32(fixture.output.Add(24U), 1U);
    const auto play_vtable =
        fixture.bus.Read32(ogplay::memory::GuestAddress{play}, 1U);
    CHECK(call(fixture.bus.Read32(
                   ogplay::memory::GuestAddress{play_vtable + 4U * 4U}, 1U),
               {play, 0x60000005U, 0U}) == 0U);
    CHECK(call(fixture.bus.Read32(
                   ogplay::memory::GuestAddress{play_vtable + 5U * 4U}, 1U),
               {play, 7U}) == 0U);
    const auto pcm = fixture.output.Add(0x200U);
    const std::array pcm_bytes{std::byte{0xe8}, std::byte{0x03}};
    fixture.memory.Write(pcm, pcm_bytes, 1U);
    const auto enqueue = fixture.bus.Read32(
        ogplay::memory::GuestAddress{queue_vtable}, 1U);
    for (std::uint32_t index = 0; index < 255U; ++index) {
        CHECK(call(enqueue, {queue, pcm.Value(), 2U}) == 0U);
    }
    CHECK(call(fixture.bus.Read32(ogplay::memory::GuestAddress{play_vtable},
                                  1U),
               {play, 3U}) == 0U);
    std::vector<std::int16_t> mixed(512U);
    static_cast<void>(fixture.boundary.MixOpenSlesPcm16(mixed, 48000U));
    CHECK(callbacks.size() ==
          ogplay::runtime::kMaximumOpenSlesPendingCallbacks);
}

TEST_CASE("OpenSL ES recycles destroyed object slots and drops stale callbacks") {
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
    CHECK(call(create->Value(), {fixture.output.Value(), 0U, 0U, 0U, 0U, 0U}) ==
          0U);
    const auto first_engine = fixture.bus.Read32(fixture.output, 1U);
    const auto object_vtable = fixture.bus.Read32(
        ogplay::memory::GuestAddress{first_engine}, 1U);
    const auto destroy = fixture.bus.Read32(
        ogplay::memory::GuestAddress{object_vtable + 6U * 4U}, 1U);
    CHECK(call(destroy, {first_engine}) == 0U);
    CHECK(call(create->Value(),
               {fixture.output.Add(4U).Value(), 0U, 0U, 0U, 0U, 0U}) == 0U);
    const auto reused_engine = fixture.bus.Read32(fixture.output.Add(4U), 1U);
    CHECK(reused_engine == first_engine);

    CHECK(call(fixture.bus.Read32(ogplay::memory::GuestAddress{object_vtable},
                                  1U),
               {reused_engine, 0U}) == 0U);
    CHECK(call(fixture.bus.Read32(
                   ogplay::memory::GuestAddress{object_vtable + 12U}, 1U),
               {reused_engine, iid("SL_IID_ENGINE"),
                fixture.output.Add(8U).Value()}) == 0U);
    const auto engine = fixture.bus.Read32(fixture.output.Add(8U), 1U);
    const auto engine_vtable = fixture.bus.Read32(
        ogplay::memory::GuestAddress{engine}, 1U);
    CHECK(call(fixture.bus.Read32(
                   ogplay::memory::GuestAddress{engine_vtable + 7U * 4U}, 1U),
               {engine, fixture.output.Add(12U).Value(), 0U, 0U, 0U}) == 0U);
    const auto mix_object = fixture.bus.Read32(fixture.output.Add(12U), 1U);
    const auto mix_object_vtable = fixture.bus.Read32(
        ogplay::memory::GuestAddress{mix_object}, 1U);
    CHECK(call(fixture.bus.Read32(
                   ogplay::memory::GuestAddress{mix_object_vtable}, 1U),
               {mix_object, 0U}) == 0U);
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
    fixture.bus.Write32(interface_required, 1U, 1U);
    CHECK(call(fixture.bus.Read32(
                   ogplay::memory::GuestAddress{engine_vtable + 2U * 4U}, 1U),
               {engine, fixture.output.Add(16U).Value(), source.Value(),
                sink.Value(), 1U, interface_ids.Value(),
                interface_required.Value()}) == 0U);
    const auto player_object = fixture.bus.Read32(fixture.output.Add(16U), 1U);
    const auto player_object_vtable = fixture.bus.Read32(
        ogplay::memory::GuestAddress{player_object}, 1U);
    CHECK(call(fixture.bus.Read32(
                   ogplay::memory::GuestAddress{player_object_vtable}, 1U),
               {player_object, 0U}) == 0U);
    CHECK(call(fixture.bus.Read32(
                   ogplay::memory::GuestAddress{player_object_vtable + 12U},
                   1U),
               {player_object, iid("SL_IID_ANDROIDSIMPLEBUFFERQUEUE"),
                fixture.output.Add(20U).Value()}) == 0U);
    const auto queue = fixture.bus.Read32(fixture.output.Add(20U), 1U);
    const auto queue_vtable =
        fixture.bus.Read32(ogplay::memory::GuestAddress{queue}, 1U);
    CHECK(call(fixture.bus.Read32(
                   ogplay::memory::GuestAddress{queue_vtable + 3U * 4U}, 1U),
               {queue, 0x60000003U, 0x5678U}) == 0U);
    const auto pcm = fixture.output.Add(0x200U);
    const std::array pcm_bytes{
        std::byte{0xe8}, std::byte{0x03}, std::byte{0xd0}, std::byte{0x07}};
    fixture.memory.Write(pcm, pcm_bytes, 1U);
    CHECK(call(fixture.bus.Read32(ogplay::memory::GuestAddress{queue_vtable},
                                  1U),
               {queue, pcm.Value(),
                static_cast<std::uint32_t>(pcm_bytes.size())}) == 0U);
    CHECK(call(fixture.bus.Read32(
                   ogplay::memory::GuestAddress{player_object_vtable + 12U},
                   1U),
               {player_object, iid("SL_IID_PLAY"),
                fixture.output.Add(24U).Value()}) == 0U);
    const auto play = fixture.bus.Read32(fixture.output.Add(24U), 1U);
    const auto play_vtable =
        fixture.bus.Read32(ogplay::memory::GuestAddress{play}, 1U);
    CHECK(call(fixture.bus.Read32(ogplay::memory::GuestAddress{play_vtable},
                                  1U),
               {play, 3U}) == 0U);
    std::array<std::int16_t, 4> mixed{};
    CHECK(fixture.boundary.MixOpenSlesPcm16(mixed, 48000U).size() == 1U);
    REQUIRE(callbacks.size() == 1U);
    const auto generation = callbacks[0].generation;
    CHECK(fixture.boundary.OpenSlesCallbackCurrent(callbacks[0].object_key,
                                                   generation));
    CHECK(call(fixture.bus.Read32(
                   ogplay::memory::GuestAddress{queue_vtable + 4U}, 1U),
               {queue}) == 0U);
    CHECK_FALSE(fixture.boundary.OpenSlesCallbackCurrent(
        callbacks[0].object_key, generation));
    CHECK(call(destroy, {reused_engine}) == 0U);
    CHECK_THROWS_AS(static_cast<void>(call(destroy, {player_object})),
                    std::runtime_error);
}
