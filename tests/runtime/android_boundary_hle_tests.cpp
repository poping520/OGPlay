#include <doctest/doctest.h>

#include <array>
#include <bit>
#include <cstdint>
#include <string_view>

#include "ogplay/cpu/interpreter.h"
#include "ogplay/gles/egl_lifecycle.h"
#include "ogplay/memory/bus.h"
#include "ogplay/runtime/integration/android_boundary_hle.h"

namespace {

class BoundaryFixture final {
public:
    BoundaryFixture()
        : bus(memory), cpu(bus), boundary(memory,
              {ogplay::gles::AngleRenderer::d3d11,
               ogplay::gles::AngleDevice::hardware}, 4, 3) {
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

    std::uint32_t Call(const std::string_view library, const std::string_view symbol,
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
        REQUIRE(boundary.Handle(cpu, stopped));
        return cpu.GetState().Register(ogplay::cpu::CoreRegister::r0);
    }

    ogplay::memory::AddressSpace memory;
    ogplay::memory::CheckedMemoryBus bus;
    ogplay::cpu::InterpreterCpu cpu;
    ogplay::runtime::AndroidBoundaryHle boundary;
    const ogplay::memory::GuestAddress stack{0x6e100000U};
    const ogplay::memory::GuestAddress output{0x6e101000U};
};

}  // namespace

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
    CHECK(fixture.Call("libEGL.so", "eglGetDisplay") == 1);
    const ogplay::cpu::RunResult unknown{
        1, ogplay::cpu::RunStopReason::supervisor_call,
        ogplay::memory::GuestAddress{0x70000f00U}, 0xdf02U, 2, std::nullopt};
    CHECK_FALSE(fixture.boundary.Handle(fixture.cpu, unknown));
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
    CHECK(fixture.Call("libEGL.so", "eglMakeCurrent", {1, 3, 3, 4}) == 1);
    static_cast<void>(fixture.Call("libGLESv2.so", "glViewport", {0, 0, 4, 3}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glClearColor",
                 {std::bit_cast<std::uint32_t>(0.25F),
                  std::bit_cast<std::uint32_t>(0.5F),
                  std::bit_cast<std::uint32_t>(0.75F),
                  std::bit_cast<std::uint32_t>(1.0F)}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glClear", {0x00004000U}));
    CHECK(fixture.Call("libEGL.so", "eglSwapBuffers", {1, 3}) == 1);
    const auto frame = fixture.boundary.TakeLatestFrame();
    REQUIRE(frame.has_value());
    CHECK(frame->sequence == 1);
    REQUIRE(frame->rgba8.size() == 4U * 3U * 4U);
    CHECK(frame->rgba8[0] == doctest::Approx(64).epsilon(0.02));
    CHECK(frame->rgba8[1] == doctest::Approx(128).epsilon(0.02));
    CHECK(frame->rgba8[2] == doctest::Approx(191).epsilon(0.02));
    CHECK(fixture.Call("libEGL.so", "eglTerminate", {1}) == 1);
}
