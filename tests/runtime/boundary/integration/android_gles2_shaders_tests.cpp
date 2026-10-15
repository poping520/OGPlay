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
    static_cast<void>(fixture.Call("libGLESv2.so", "glDrawElements",
                                    {0x0004U, 3U, 0x1403U, 0U}));
    CHECK(fixture.Call("libGLESv2.so", "glGetError") == 0x0502U);
    // Empty allocation followed by a partial upload, with a nonzero index
    // offset. The offset is not a readable guest address.
    static_cast<void>(fixture.Call("libGLESv2.so", "glBufferData",
                                    {0x8893U, 8U, 0U, 0x88E4U}));
    static_cast<void>(fixture.Call("libGLESv2.so", "glBufferSubData",
                                    {0x8893U, 2U, 6U, client_indices.Value()}));
    CHECK_NOTHROW(static_cast<void>(fixture.Call("libGLESv2.so", "glDrawElements",
                                    {0x0004U, 3U, 0x1403U, 2U})));
    CHECK(fixture.Call("libGLESv2.so", "glGetError") == 0U);
    static_cast<void>(fixture.Call("libGLESv2.so", "glGetIntegerv",
                                    {0x8895U, query_output.Value()}));
    CHECK(fixture.bus.Read32(query_output, 1) == element_buffer);
    static_cast<void>(fixture.Call("libGLESv2.so", "glGetIntegerv",
                                    {0x8894U, query_output.Value()}));
    CHECK(fixture.bus.Read32(query_output, 1) == 0U);
    const std::array<std::uint8_t, 3> sparse_indices{0, 1, 4};
    fixture.memory.Write(client_indices, std::as_bytes(std::span(sparse_indices)), 1);
    static_cast<void>(fixture.Call("libGLESv2.so", "glBufferSubData",
                                    {0x8893U, 2U, 3U, client_indices.Value()}));
    fixture.bus.Write32(fixture.stack, 0, 1);
    fixture.bus.Write32(fixture.stack.Add(4), fixture.output.Add(fixture.memory.PageSize() - 24).Value(), 1);
    static_cast<void>(fixture.Call("libGLESv2.so", "glVertexAttribPointer", {0, 2, 0x1406U, 0}));
    // count is 3, but the largest index is 4: 5 vertices must be validated.
    CHECK_THROWS_AS(fixture.Call("libGLESv2.so", "glDrawElements",
                                 {0x0004U, 3U, 0x1401U, 2U}), ogplay::memory::MemoryFault);
    static_cast<void>(fixture.Call("libGLESv2.so", "glGetIntegerv", {0x8894U, query_output.Value()}));
    CHECK(fixture.bus.Read32(query_output, 1) == 0U);
    static_cast<void>(fixture.Call("libGLESv2.so", "glGetIntegerv", {0x8895U, query_output.Value()}));
    CHECK(fixture.bus.Read32(query_output, 1) == element_buffer);
    fixture.bus.Write32(fixture.stack.Add(4), client_vertices.Value(), 1);
    static_cast<void>(fixture.Call("libGLESv2.so", "glVertexAttribPointer", {0, 2, 0x1406U, 0}));
    fixture.bus.Write8(client_indices.Add(2), 2, 1);
    static_cast<void>(fixture.Call("libGLESv2.so", "glBufferSubData", {0x8893U, 2U, 3U, client_indices.Value()}));
    CHECK_NOTHROW(static_cast<void>(fixture.Call("libGLESv2.so", "glDrawElements", {0x0004U, 3U, 0x1401U, 2U})));
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
    CHECK(fixture.boundary.Stats().draws == 7);
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
    CHECK(stats.draws == 7);
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
