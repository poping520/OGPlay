#include <doctest/doctest.h>

#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ogplay/cpu/interpreter.h"
#include "ogplay/gles/egl_lifecycle.h"
#include "ogplay/gles/gles_dispatch.h"
#include "ogplay/gles/gles_transfer_state.h"
#include "ogplay/memory/bus.h"
#include "ogplay/runtime/integration/android_boundary_hle.h"
#include "../../src/runtime/integration/android_boundary_gles1.h"
#include "../../src/runtime/integration/android_boundary_gles1_draw.h"
#include "../../src/runtime/integration/android_boundary_gles1_query.h"
#include "../../src/runtime/integration/android_boundary_gles1_support.h"

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
        fixture.Call("libGLESv2.so", "glValidateProgram"),
        "Android boundary HLE is not implemented: glValidateProgram",
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
        "GLES1 texture target must be GL_TEXTURE_2D", std::invalid_argument);
    CHECK_THROWS_WITH_AS(
        state.SetCapability(0U, true), "GLES1 capability is invalid",
        std::invalid_argument);
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
    CHECK(fixture.Call("libEGL.so", "eglTerminate") == 1U);
}

TEST_CASE("Android boundary shares framebuffer raster and capability state") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
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

TEST_CASE("GLES1 matrix state composes and bounds stacks") {
    ogplay::runtime::detail::AndroidBoundaryGles1MatrixState matrices;
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
    matrices.SetActiveTexture(0x84C0U);
    matrices.Translate(0.25F, 0.0F, 0.0F);
    matrices.SetActiveTexture(0x84C1U);
    CHECK(matrices.Current()[12] == 0.0F);
    matrices.Translate(0.5F, 0.0F, 0.0F);
    CHECK(matrices.Current(ogplay::runtime::detail::kGles1Texture,
                           0x84C0U)[12] == 0.25F);
    CHECK(matrices.Current(ogplay::runtime::detail::kGles1Texture,
                           0x84C1U)[12] == 0.5F);
    CHECK_THROWS_WITH_AS(
        matrices.SetActiveTexture(0U),
        "GLES1 texture unit is outside GL_TEXTURE0..31",
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
        CHECK_THROWS_WITH_AS(
            fixture.Call("libGLESv1_CM.so", "glGenTextures",
                         {std::bit_cast<std::uint32_t>(-1),
                          fixture.output.Value()}),
            "glGenTextures count cannot be negative", std::invalid_argument);
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
        CHECK_THROWS_WITH_AS(
            fixture.Call("libGLESv1_CM.so", "glBufferData",
                         {0x8892U, 0xFFFFFFFFU, 0U, 0x88E4U}),
            "glBufferData count cannot be negative", std::invalid_argument);
        CHECK(fixture.Call("libGLESv1_CM.so", "glDeleteBuffers",
                           {1U, buffer_names.Value()}) == 0U);
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
        CHECK_THROWS_WITH_AS(
            fixture.Call("libGLESv1_CM.so", "glGetIntegerv",
                         {0xDEADBEEFU, integer_query.Value()}),
            "GLES1 integer query is unsupported: 3735928559",
            std::invalid_argument);
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
        CHECK_THROWS_WITH_AS(
            fixture.Call(
                "libGLESv1_CM.so", "glTexParameterf",
                {0x0DE1U, ogplay::runtime::detail::kGles1GenerateMipmap,
                 std::bit_cast<std::uint32_t>(2.0F)}),
            "GLES1 GL_GENERATE_MIPMAP must be GL_FALSE or GL_TRUE",
            std::invalid_argument);
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
        CHECK_THROWS_AS(
            fixture.Call("libGLESv1_CM.so", "glCompressedTexImage2D",
                         {0x0DE1U, 0U, 0x8D64U, 4U}),
            std::invalid_argument);
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
        CHECK_THROWS_AS(
            fixture.Call("libGLESv1_CM.so", "glDrawArrays",
                         {0x0004U, 0U, 0xFFFFFFFFU}),
            std::invalid_argument);
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
        CHECK_THROWS_WITH_AS(
            fixture.Call("libGLESv1_CM.so", "glShadeModel", {0U}),
            "glShadeModel mode must be GL_FLAT or GL_SMOOTH",
            std::invalid_argument);
        CHECK(fixture.Call(
                  "libGLESv1_CM.so", "glClearColor",
                  {std::bit_cast<std::uint32_t>(0.125F),
                   std::bit_cast<std::uint32_t>(0.25F),
                   std::bit_cast<std::uint32_t>(0.5F),
                   std::bit_cast<std::uint32_t>(1.0F)}) == 0);
        CHECK(fixture.Call("libGLESv1_CM.so", "glClearDepthf",
                           {std::bit_cast<std::uint32_t>(0.25F)}) == 0);
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
        CHECK(fixture.Call("libGLESv1_CM.so", "glDeleteTextures",
                           {2U, fixture.output.Value()}) == 0U);
        fixture.boundary.CloseManagedSurface();
    }
    CHECK_THROWS_WITH_AS(
        fixture.Call("libGLESv1_CM.so", "glClearDepthx", {0x00010000U}),
        "unimplemented GLES1 call glClearDepthx (thunk 12, guest thread 1)",
        ogplay::gles::GlesDispatchError);
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
        "glCurrentPaletteMatrixOES has no current ANGLE frame",
        std::runtime_error);
    fixture.boundary.OpenManagedSurface();
    CHECK(fixture.Call("libGLESv1_CM.so", "glCurrentPaletteMatrixOES",
                       {3U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glMatrixIndexPointerOES",
                       {4U, 0x1401U, 0U, fixture.output.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glWeightPointerOES",
                       {4U, 0x1406U, 0U, fixture.output.Add(32U).Value()}) == 0U);
    CHECK_THROWS_AS(
        fixture.Call("libGLESv1_CM.so", "glCurrentPaletteMatrixOES",
                     {32U}),
        std::invalid_argument);
    CHECK_THROWS_AS(
        fixture.Call("libGLESv1_CM.so", "glMatrixIndexPointerOES",
                     {4U, 0x1406U, 0U, fixture.output.Value()}),
        std::invalid_argument);
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
    CHECK_THROWS_WITH_AS(
        fixture.Call("libGLESv1_CM.so", "glDrawArrays",
                     {0x0000U, 0U, 1U}),
        "GLES1 matrix-palette skinning draw conversion is not implemented",
        std::runtime_error);
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
