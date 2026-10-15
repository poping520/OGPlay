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
        CHECK(fixture.Call("libGLESv1_CM.so", "glMatrixMode",
                           {0x1700U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glLoadIdentity") == 0U);
        CHECK(fixture.Call(
                  "libGLESv1_CM.so", "glScalef",
                  {std::bit_cast<std::uint32_t>(1.0F),
                   std::bit_cast<std::uint32_t>(1.0F), 0U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glDrawArrays",
                           {0x0004U, 0U, 3U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glLoadIdentity") == 0U);
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
