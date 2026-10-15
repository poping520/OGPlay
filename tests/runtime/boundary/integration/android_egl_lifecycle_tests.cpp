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
        CHECK(fixture.bus.Read32(size, 1U) == (attribute == 0x302FU ? 0U : 0x3038U));
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
    REQUIRE(mapped >= ogplay::runtime::kBufferMapArenaBegin);
    fixture.bus.Write32(ogplay::memory::GuestAddress{mapped}, 0x12345678U, 1U);
    const auto pointer_out = fixture.output.Add(112U);
    CHECK(fixture.Call("libGLESv2.so", "glGetBufferPointerv",
                       {0x8892U, 0x88BDU, pointer_out.Value()}) == 0U);
    CHECK(fixture.bus.Read32(pointer_out, 1U) == mapped);
    CHECK(fixture.Call("libGLESv2.so", "glUnmapBuffer", {0x8892U}) == 1U);
    CHECK(fixture.Call("libGLESv2.so", "glGetError") == 0U);
    const auto remapped = fixture.Call("libGLESv2.so", "glMapBufferRange",
                                       {0x8892U, 0U, 16U, 0x0001U});
    REQUIRE(remapped >= ogplay::runtime::kBufferMapArenaBegin);
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

TEST_CASE("BND36 eglTerminate preserves current resources and permits reinitialize") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
    PrepareNativeEgl(fixture);
    constexpr std::uint64_t thread_id = 0x36U;
    CHECK(FastBoundaryCall(fixture, "libEGL.so", "eglMakeCurrent",
                           {1U, 3U, 3U, 4U}, thread_id) == 1U);

    CHECK(FastBoundaryCall(fixture, "libEGL.so", "eglTerminate",
                           {1U}, thread_id) == 1U);
    CHECK(FastBoundaryCall(fixture, "libEGL.so", "eglTerminate",
                           {1U}, thread_id) == 1U);
    CHECK(FastBoundaryCall(fixture, "libEGL.so", "eglGetCurrentContext",
                           {}, thread_id) == 4U);
    CHECK(FastBoundaryCall(fixture, "libGLESv2.so", "glClearColor",
                           {0U, 0U, 0U,
                            std::bit_cast<std::uint32_t>(1.0F)},
                           thread_id) == 0U);

    CHECK(FastBoundaryCall(fixture, "libEGL.so", "eglInitialize",
                           {1U, 0U, 0U}, thread_id) == 1U);
    CHECK(FastBoundaryCall(fixture, "libEGL.so", "eglMakeCurrent",
                           {1U, 0U, 0U, 0U}, thread_id) == 1U);
    CHECK(FastBoundaryCall(fixture, "libEGL.so", "eglGetCurrentContext",
                           {}, thread_id) == 0U);
    CHECK(FastBoundaryCall(fixture, "libEGL.so", "eglTerminate",
                           {1U}, thread_id) == 1U);
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

TEST_CASE("GLES string queries require calling-thread currency without acquiring it") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
    const std::array libraries{"libGLESv1_CM.so", "libGLESv2.so"};
    const std::array parameters{0x1F02U, 0x1F03U};
    const auto query = [&](bool bound, std::uint64_t tid) {
        for (const auto library : libraries) {
            for (const auto parameter : parameters) {
                CHECK((fixture.Call(library, "glGetString", {parameter}) != 0U) == bound);
                CHECK((FastBoundaryCall(fixture, library, "glGetString", {parameter}, tid) != 0U) == bound);
                const std::array arguments{parameter};
                const auto api = std::string_view(library) == "libGLESv2.so"
                    ? ogplay::gles::GlesApi::gles2 : ogplay::gles::GlesApi::gles1;
                CHECK((fixture.boundary.InvokeManagedGles(api, "glGetString", arguments, tid) != 0U) == bound);
            }
        }
    };
    query(false, 1U);
    fixture.boundary.OpenManagedSurface();
    fixture.boundary.BindManagedSurfaceOnCallingThread();
    query(true, 1U);
    std::exception_ptr worker_failure;
    std::thread worker([&] {
        try { query(false, 2U); }
        catch (...) { worker_failure = std::current_exception(); }
    });
    worker.join();
    REQUIRE(worker_failure == nullptr);
    query(true, 1U);
    fixture.boundary.ReleaseManagedSurfaceFromCallingThread();
    query(false, 1U);
    std::thread successor([&] {
        try {
            fixture.boundary.BindManagedSurfaceOnCallingThread();
            query(true, 3U);
            fixture.boundary.ReleaseManagedSurfaceFromCallingThread();
            query(false, 3U);
        } catch (...) { worker_failure = std::current_exception(); }
    });
    successor.join();
    REQUIRE(worker_failure == nullptr);
    fixture.boundary.CloseManagedSurface();

    PrepareNativeEgl(fixture);
    query(false, 1U);
    REQUIRE(fixture.Call("libEGL.so", "eglMakeCurrent", {1U, 3U, 3U, 4U}) == 1U);
    query(true, 1U);
    std::thread unbound([&] {
        try { query(false, 4U); }
        catch (...) { worker_failure = std::current_exception(); }
    });
    unbound.join();
    REQUIRE(worker_failure == nullptr);
    // A backend context stolen outside the registry is a contract failure, not no-context.
    auto other = ogplay::gles::AngleFrame::CreatePbuffer(
        {kNativeRenderer, ogplay::gles::AngleDevice::hardware}, 4U, 3U);
    CHECK_THROWS_WITH(fixture.Call("libGLESv2.so", "glGetString", {0x1F03U}),
                      "glGetString: EGL current binding mismatch");
    other.ReleaseCurrent();
    REQUIRE(fixture.Call("libEGL.so", "eglMakeCurrent", {1U, 0U, 0U, 0U}) == 1U);
    query(false, 1U);
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
