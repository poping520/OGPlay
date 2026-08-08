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
#include "../../src/runtime/integration/android_boundary_gles1_query.h"

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
    CHECK_FALSE(state.GenerateMipmapEnabled(0x0DE1U));
    state.SetGenerateMipmap(0x0DE1U, true);
    CHECK(state.GenerateMipmapEnabled(0x0DE1U));
    state.SetActiveTexture(0x84C1U);
    CHECK_FALSE(state.Capability(0x0DE1U));
    state.BindTexture(0x0DE1U, 8U);
    CHECK_FALSE(state.GenerateMipmapEnabled(0x0DE1U));
    state.SetCapability(0x0DE1U, true);
    CHECK(state.Capability(0x0DE1U));
    const std::array deleted_textures{7U};
    state.DeleteTextures(deleted_textures);
    state.SetActiveTexture(0x84C0U);
    CHECK_FALSE(state.GenerateMipmapEnabled(0x0DE1U));
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
    CHECK_FALSE(state.GenerateMipmapEnabled(0x0DE1U));
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
    state.SetAlphaFunction(0x0201U, 2.0F);
    CHECK(state.AlphaFunction() == 0x0201U);
    CHECK(state.AlphaReference() == 1.0F);
    state.SetClientActiveTexture(0x84C1U);
    CHECK(state.ClientActiveTexture() == 0x84C1U);
    const std::array color{-1.0F, 0.25F, 0.5F, 2.0F};
    state.SetColor(color);
    CHECK(state.Color()[0] == 0.0F);
    CHECK(state.Color()[3] == 1.0F);
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
    CHECK(state.TextureEnvironment(
              0x84C0U,
              ogplay::runtime::detail::kGles1TextureEnvironmentMode)[0] ==
          8448.0F);
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
            "glCullFace", {0x0405U}},
        std::pair<std::string_view, std::array<std::uint32_t, 4>>{
            "glDepthFunc", {0x0201U}},
        std::pair<std::string_view, std::array<std::uint32_t, 4>>{
            "glDepthMask", {1U}},
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
            "glPixelStorei", {0x0CF5U, 4U}},
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
        const auto vendor = fixture.Call(
            "libGLESv1_CM.so", "glGetString", {0x1F00U});
        REQUIRE(vendor != 0U);
        CHECK(fixture.bus.Read8(ogplay::memory::GuestAddress{vendor}, 1U) != 0U);
        CHECK_THROWS_AS(
            fixture.memory.Write(ogplay::memory::GuestAddress{vendor},
                                 std::array{std::byte{'X'}}, 1U),
            ogplay::memory::MemoryFault);
        CHECK_THROWS_WITH_AS(
            fixture.Call("libGLESv1_CM.so", "glGetString", {0x8B8CU}),
            "GLES1 string query is unsupported", std::invalid_argument);
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
        CHECK_THROWS_AS(
            fixture.Call("libGLESv1_CM.so", "glGenTextures", {1U, 0U}),
            ogplay::gles::GuestTransferError);
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
        CHECK(fixture.Call("libGLESv1_CM.so", "glActiveTexture",
                           {0x84C0U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glEnable",
                           {0x0DE1U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glDisable",
                           {0x0DE1U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glBindBuffer",
                           {0x8892U, 0U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glBindTexture",
                           {0x0DE1U, texture}) == 0U);
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
        CHECK(fixture.Call("libGLESv1_CM.so", "glBlendFunc",
                           {1U, 0U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glColorMask",
                           {1U, 1U, 1U, 1U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glCullFace",
                           {0x0405U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glDepthFunc",
                           {0x0201U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glDepthMask", {1U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glFrontFace",
                           {0x0901U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glHint",
                           {0x8192U, 0x1100U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glHint",
                           {0x0C50U, 0x1102U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glPixelStorei",
                           {0x0CF5U, 4U}) == 0U);
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
    CHECK_THROWS_WITH_AS(
        fixture.Call("libGLESv1_CM.so", "glClearStencil", {0U}),
        "unimplemented GLES1 call glClearStencil (thunk 13, guest thread 1)",
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
