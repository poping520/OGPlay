#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "ogplay/gles/gles_call_preparation.h"
#include "ogplay/memory/address_space.h"

namespace {

constexpr ogplay::memory::GuestAddress kStart{0x10000};

class TestResolver final : public ogplay::gles::GlesLengthResolver {
public:
    std::optional<ogplay::gles::GlesLengthResolution> Resolve(
        const ogplay::gles::GlesLengthRequest& request) const override {
        if (request.expression == "pixel_bytes(width,height,format,type)") {
            return ogplay::gles::GlesLengthResolution{
                ogplay::gles::GlesLengthDisposition::transfer, 16};
        }
        if (request.expression == "client_array(index,size,type,stride)") {
            return ogplay::gles::GlesLengthResolution{
                ogplay::gles::GlesLengthDisposition::deferred, 0};
        }
        return std::nullopt;
    }
};

[[nodiscard]] ogplay::gles::GlesThunkId Thunk(const std::string_view name) {
    const auto id = ogplay::gles::GlesDispatchTable::Find(name);
    if (!id) {
        throw std::runtime_error("test GLES thunk is missing");
    }
    return *id;
}

void MapTransferPage(ogplay::memory::AddressSpace& memory) {
    memory.Map({kStart, memory.PageSize()},
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write);
}

}  // namespace

TEST_CASE("GLES call preparation evaluates scalar multiplication") {
    ogplay::memory::AddressSpace memory;
    MapTransferPage(memory);
    std::array<std::byte, 32> values{};
    values[31] = std::byte{0x7f};
    memory.Write(kStart, values);
    const std::array arguments{UINT32_C(3), UINT32_C(2), kStart.Value()};

    auto prepared = ogplay::gles::PrepareGles2Call(
        memory, Thunk("glUniform4fv"), arguments, 17);
    REQUIRE(prepared.pointers.size() == 1);
    const auto& pointer = prepared.pointers[0];
    CHECK(pointer.parameter_index == 2);
    CHECK(pointer.byte_size == 32);
    REQUIRE(pointer.transfer);
    CHECK(pointer.transfer->Bytes()[31] == std::byte{0x7f});
}

TEST_CASE("GLES call preparation sizes outputs and pointer arrays") {
    ogplay::memory::AddressSpace memory;
    MapTransferPage(memory);
    const std::array gen_arguments{UINT32_C(3), kStart.Value()};
    auto generated = ogplay::gles::PrepareGles2Call(
        memory, Thunk("glGenTextures"), gen_arguments);
    REQUIRE(generated.pointers.size() == 1);
    CHECK(generated.pointers[0].byte_size == 12);
    REQUIRE(generated.pointers[0].transfer);
    CHECK(generated.pointers[0].transfer->Direction() ==
          ogplay::gles::GuestTransferDirection::output);

    const std::array shader_arguments{UINT32_C(4), UINT32_C(2),
                                      kStart.Value(), kStart.Add(8).Value()};
    auto shader = ogplay::gles::PrepareGles2Call(
        memory, Thunk("glShaderSource"), shader_arguments);
    REQUIRE(shader.pointers.size() == 2);
    CHECK(shader.pointers[0].byte_size == 8);
    CHECK(shader.pointers[1].byte_size == 8);
}

TEST_CASE("GLES call preparation copies bounded terminated strings") {
    ogplay::memory::AddressSpace memory;
    MapTransferPage(memory);
    constexpr std::array name{std::byte{0x75}, std::byte{0x43}, std::byte{0}};
    memory.Write(kStart, name);
    const std::array arguments{UINT32_C(9), kStart.Value()};
    auto prepared = ogplay::gles::PrepareGles2Call(
        memory, Thunk("glGetUniformLocation"), arguments, 0, nullptr, 16);
    REQUIRE(prepared.pointers.size() == 1);
    CHECK(prepared.pointers[0].byte_size == 3);

    const std::array unterminated{std::byte{0x78}, std::byte{0x78},
                                  std::byte{0x78}, std::byte{0x78}};
    memory.Write(kStart, unterminated);
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::gles::PrepareGles2Call(
            memory, Thunk("glGetUniformLocation"), arguments, 0, nullptr, 4)),
        ogplay::gles::GlesCallPreparationError);
}

TEST_CASE("GLES call preparation requires explicit complex resolution") {
    ogplay::memory::AddressSpace memory;
    MapTransferPage(memory);
    const std::array read_arguments{UINT32_C(0), UINT32_C(0), UINT32_C(2),
                                    UINT32_C(2), UINT32_C(0x1908),
                                    UINT32_C(0x1401), kStart.Value()};
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::gles::PrepareGles2Call(
            memory, Thunk("glReadPixels"), read_arguments)),
        ogplay::gles::GlesCallPreparationError);

    const TestResolver resolver;
    auto prepared = ogplay::gles::PrepareGles2Call(
        memory, Thunk("glReadPixels"), read_arguments, 0, &resolver);
    REQUIRE(prepared.pointers.size() == 1);
    CHECK(prepared.pointers[0].byte_size == 16);
    REQUIRE(prepared.pointers[0].transfer);

    const std::array vertex_arguments{
        UINT32_C(1), UINT32_C(2), UINT32_C(0x1406), UINT32_C(0),
        UINT32_C(8), UINT32_C(24)};
    auto deferred = ogplay::gles::PrepareGles2Call(
        memory, Thunk("glVertexAttribPointer"), vertex_arguments, 0,
        &resolver);
    REQUIRE(deferred.pointers.size() == 1);
    CHECK(deferred.pointers[0].deferred);
    CHECK_FALSE(deferred.pointers[0].transfer);
    CHECK(deferred.pointers[0].guest_address.Value() == 24);
}

TEST_CASE("GLES call preparation rejects negative and oversized lengths") {
    ogplay::memory::AddressSpace memory;
    MapTransferPage(memory);
    const std::array negative{UINT32_C(0), UINT32_MAX, kStart.Value()};
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::gles::PrepareGles2Call(
            memory, Thunk("glUniform1fv"), negative)),
        ogplay::gles::GlesCallPreparationError);
    const std::array oversized{UINT32_C(0), UINT32_C(3), kStart.Value()};
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::gles::PrepareGles2Call(
            memory, Thunk("glUniform4fv"), oversized, 0, nullptr, 32)),
        ogplay::gles::GlesCallPreparationError);

    const std::array null_data{UINT32_C(0x8892), UINT32_C(0x08000000),
                               UINT32_C(0), UINT32_C(0x88e4)};
    auto allocation_only = ogplay::gles::PrepareGles2Call(
        memory, Thunk("glBufferData"), null_data);
    REQUIRE(allocation_only.pointers.size() == 1);
    REQUIRE(allocation_only.pointers[0].transfer);
    CHECK(allocation_only.pointers[0].transfer->IsNull());
    CHECK(allocation_only.pointers[0].byte_size == UINT32_C(0x08000000));
}
