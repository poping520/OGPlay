#include <doctest/doctest.h>

#include <array>
#include <cstdint>

#include "ogplay/gles/gles_dispatch.h"

TEST_CASE("generated GLES2 catalog has stable complete lookup") {
    CHECK(ogplay::gles::GlesDispatchTable::FunctionCount() == 142);
    const auto clear = ogplay::gles::GlesDispatchTable::Find("glClear");
    REQUIRE(clear);
    const auto info = ogplay::gles::GlesDispatchTable::Describe(*clear);
    CHECK(info.name.compare("glClear") == 0);
    CHECK(info.return_type.compare("void") == 0);
    CHECK(info.parameter_count == 1);
    CHECK(info.pointer_parameter_count == 0);

    const auto shader_source =
        ogplay::gles::GlesDispatchTable::Find("glShaderSource");
    REQUIRE(shader_source);
    CHECK(ogplay::gles::GlesDispatchTable::Describe(*shader_source)
              .pointer_parameter_count == 2);
    CHECK_FALSE(ogplay::gles::GlesDispatchTable::Find("glMissing"));
}

TEST_CASE("GLES2 dispatch invokes only an explicitly bound exact handler") {
    ogplay::gles::GlesDispatchTable dispatch;
    const auto clear = ogplay::gles::GlesDispatchTable::Find("glClear");
    REQUIRE(clear);
    dispatch.Bind("glClear", [](const auto arguments, const auto thread_id) {
        CHECK(arguments[0] == 0x4000U);
        CHECK(thread_id == 7);
        return UINT32_C(0x12345678);
    });
    CHECK(dispatch.IsBound(*clear));
    const std::array arguments{UINT32_C(0x4000)};
    CHECK(dispatch.Invoke(*clear, arguments, 7) == UINT32_C(0x12345678));
    CHECK_THROWS_AS(dispatch.Bind("glClear", [](const auto, const auto) {
                        return std::uint32_t{};
                    }),
                    ogplay::gles::GlesDispatchError);
    CHECK_THROWS_AS(dispatch.Bind("glMissing", [](const auto, const auto) {
                        return std::uint32_t{};
                    }),
                    ogplay::gles::GlesDispatchError);
}

TEST_CASE("GLES2 dispatch validates thunk and argument shape") {
    ogplay::gles::GlesDispatchTable dispatch;
    const auto clear = ogplay::gles::GlesDispatchTable::Find("glClear");
    REQUIRE(clear);
    const std::array<std::uint32_t, 0> no_arguments{};
    CHECK_THROWS_AS(static_cast<void>(dispatch.Invoke(*clear, no_arguments)),
                    ogplay::gles::GlesDispatchError);
    CHECK_THROWS_AS(static_cast<void>(dispatch.IsBound(
                        static_cast<ogplay::gles::GlesThunkId>(999))),
                    ogplay::gles::GlesDispatchError);
}

TEST_CASE("unbound GLES2 calls fail and remain queryable") {
    ogplay::gles::GlesDispatchTable dispatch;
    const auto get_error =
        ogplay::gles::GlesDispatchTable::Find("glGetError");
    REQUIRE(get_error);
    const std::array<std::uint32_t, 0> arguments{};
    for (const auto thread : {41U, 99U}) {
        try {
            static_cast<void>(dispatch.Invoke(*get_error, arguments, thread));
            FAIL("unbound GLES2 call returned success");
        } catch (const ogplay::gles::GlesUnimplementedError& error) {
            CHECK(error.Id() == *get_error);
            CHECK(error.Name().compare("glGetError") == 0);
            CHECK(error.ThreadId() == thread);
        }
    }
    const auto calls = dispatch.UnimplementedCalls();
    REQUIRE(calls.size() == 1);
    CHECK(calls[0].name.compare("glGetError") == 0);
    CHECK(calls[0].hits == 2);
    CHECK(calls[0].first_thread == 41);
    CHECK(calls[0].last_thread == 99);
}
