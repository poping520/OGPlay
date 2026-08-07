#include <doctest/doctest.h>

#include <array>
#include <bit>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/cpu/interpreter.h"
#include "ogplay/gles/egl_lifecycle.h"
#include "ogplay/gles/gles_dispatch.h"
#include "ogplay/gles/gles_transfer_state.h"
#include "ogplay/memory/bus.h"
#include "ogplay/runtime/integration/android_boundary_hle.h"
#include "../../src/runtime/integration/android_boundary_gles1.h"

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
    explicit BoundaryFixture(const std::uint32_t supersample_factor = 1)
        : bus(memory), cpu(bus), boundary(memory,
              {kNativeRenderer,
               ogplay::gles::AngleDevice::hardware}, 4, 3,
              supersample_factor) {
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
        if (!boundary.Handle(cpu, stopped)) {
            throw std::runtime_error("Android boundary fixture call was not handled");
        }
        return cpu.GetState().Register(ogplay::cpu::CoreRegister::r0);
    }

    ogplay::memory::AddressSpace memory;
    ogplay::memory::CheckedMemoryBus bus;
    ogplay::cpu::InterpreterCpu cpu;
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

TEST_CASE("Android boundary publishes the complete generated GLES2 namespace") {
    BoundaryFixture fixture;
    CHECK(ogplay::gles::GlesDispatchTable::FunctionCount() == 142);
    const auto legacy_viewport =
        fixture.boundary.Symbols().Lookup("libGLESv2.so", "glViewport");
    REQUIRE(legacy_viewport.has_value());
    CHECK(legacy_viewport->Value() == 0x70000095U);
    for (std::size_t index = 0;
         index < ogplay::gles::GlesDispatchTable::FunctionCount(); ++index) {
        const auto function = ogplay::gles::GlesDispatchTable::Describe(
            static_cast<ogplay::gles::GlesThunkId>(index));
        CAPTURE(function.name);
        CHECK(fixture.boundary.Symbols().Lookup("libGLESv2.so", function.name).has_value());
    }

    CHECK_THROWS_WITH_AS(
        fixture.Call("libGLESv2.so", "glBlendColor"),
        "Android boundary HLE is not implemented: glBlendColor",
        std::runtime_error);
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
    state.Reset();
    CHECK(state.ShadeModel() ==
          ogplay::runtime::detail::kGles1SmoothShadeModel);
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
    if (ogplay::gles::IsNativeAngleEglAvailable()) {
        fixture.boundary.OpenManagedSurface();
        CHECK(fixture.Call("libGLESv1_CM.so", "glViewport", {0, 0, 4, 3}) == 0);
        CHECK(fixture.Call("libGLESv1_CM.so", "glScissor", {0, 0, 4, 3}) == 0);
        CHECK(fixture.Call(
                  "libGLESv1_CM.so", "glShadeModel",
                  {ogplay::runtime::detail::kGles1SmoothShadeModel}) == 0);
        CHECK(fixture.Call(
                  "libGLESv1_CM.so", "glShadeModel",
                  {ogplay::runtime::detail::kGles1FlatShadeModel}) == 0);
        CHECK_THROWS_WITH_AS(
            fixture.Call("libGLESv1_CM.so", "glShadeModel", {0U}),
            "glShadeModel mode must be GL_FLAT or GL_SMOOTH",
            std::invalid_argument);
        fixture.boundary.CloseManagedSurface();
    }
    CHECK_THROWS_WITH_AS(
        fixture.Call("libGLESv1_CM.so", "glAlphaFunc", {0x0201U, 0}),
        "unimplemented GLES1 call glAlphaFunc (thunk 1, guest thread 1)",
        ogplay::gles::GlesDispatchError);
}

TEST_CASE("Android boundary publishes required GLES1 extensions separately") {
    BoundaryFixture fixture;
    CHECK(ogplay::gles::GlesFunctionCount(
              ogplay::gles::GlesApi::gles1_extensions) == 3);
    for (const auto name : {"glCurrentPaletteMatrixOES",
                            "glMatrixIndexPointerOES",
                            "glWeightPointerOES"}) {
        CAPTURE(name);
        CHECK(fixture.boundary.Symbols()
                  .Lookup("libGLESv1_CM.so", name)
                  .has_value());
    }
    CHECK_THROWS_WITH_AS(
        fixture.Call("libGLESv1_CM.so", "glCurrentPaletteMatrixOES", {0}),
        "unimplemented GLES1 call glCurrentPaletteMatrixOES "
        "(thunk 0, guest thread 1)",
        ogplay::gles::GlesDispatchError);
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
    const auto frame = fixture.boundary.TakeLatestFrame();
    REQUIRE(frame.has_value());
    CHECK(frame->sequence == 1);
    CHECK(frame->rgba8[0] == doctest::Approx(32).epsilon(0.04));
    CHECK(frame->rgba8[1] == doctest::Approx(64).epsilon(0.04));
    CHECK(frame->rgba8[2] == doctest::Approx(128).epsilon(0.04));
    CHECK_THROWS_WITH_AS(
        fixture.Call("libEGL.so", "eglMakeCurrent", {1, 3, 3, 4}),
        "guest EGL cannot replace a host-managed ANGLE surface",
        std::runtime_error);
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

TEST_CASE("Android GLES boundary compiles and links guest shader sources") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
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
    const auto matrix_address = fixture.output.Add(0x600);
    constexpr std::array<float, 9> identity{1.0F, 0.0F, 0.0F,
                                            0.0F, 1.0F, 0.0F,
                                            0.0F, 0.0F, 1.0F};
    fixture.memory.Write(matrix_address, std::as_bytes(std::span(identity)), 1);
    static_cast<void>(fixture.Call("libGLESv2.so", "glUniformMatrix3fv",
                                   {UINT32_MAX, 1, 0, matrix_address.Value()}));
    CHECK_THROWS_AS(
        fixture.Call("libGLESv2.so", "glUniformMatrix3fv",
                     {UINT32_MAX, 1, 0, 0x1000U}),
        ogplay::memory::MemoryFault);

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
}

TEST_CASE("Android GLES boundary transfers buffer and texture resources") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
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
    CHECK_THROWS_WITH_AS(
        fixture.Call("libGLESv2.so", "glVertexAttribPointer", {0, 3, 0x1406U, 0}),
        "glVertexAttribPointer client arrays are not implemented", std::runtime_error);
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
    fixture.bus.Write32(fixture.stack, 2, 1);
    fixture.bus.Write32(fixture.stack.Add(4), 0, 1);
    fixture.bus.Write32(fixture.stack.Add(8), 0x1908U, 1);
    fixture.bus.Write32(fixture.stack.Add(12), 0x1401U, 1);
    fixture.bus.Write32(fixture.stack.Add(16), texture_data.Value(), 1);
    static_cast<void>(fixture.Call("libGLESv2.so", "glTexImage2D",
                                   {0x0de1U, 0, 0x1908U, 2}));
    CHECK_THROWS_AS(fixture.Call("libGLESv2.so", "glGenBuffers", {1, 0}),
                    ogplay::gles::GuestTransferError);

    fixture.bus.Write32(names, texture, 1);
    static_cast<void>(fixture.Call("libGLESv2.so", "glDeleteTextures",
                                   {1, names.Value()}));
    fixture.bus.Write32(names, vertex_buffer, 1);
    fixture.bus.Write32(names.Add(4), index_buffer, 1);
    static_cast<void>(fixture.Call("libGLESv2.so", "glDeleteBuffers",
                                   {2, names.Value()}));
}

TEST_CASE("Android boundary supersamples without changing guest surface size") {
    CHECK_THROWS_AS(BoundaryFixture(0), std::invalid_argument);
    CHECK_THROWS_AS(BoundaryFixture(5), std::invalid_argument);
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;

    BoundaryFixture fixture(2);
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
