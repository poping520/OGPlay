#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "ogplay/gles/gles_call_preparation.h"
#include "ogplay/gles/gles_dispatch.h"
#include "ogplay/gles/gles_transfer_state.h"
#include "ogplay/memory/address_space.h"

namespace {

constexpr ogplay::memory::GuestAddress kStart{0x10000};

[[nodiscard]] ogplay::gles::GlesThunkId Thunk(const std::string_view name) {
    const auto id = ogplay::gles::GlesDispatchTable::Find(name);
    if (!id) throw std::runtime_error("test GLES thunk is missing");
    return *id;
}

void MapTransferPage(ogplay::memory::AddressSpace& memory) {
    memory.Map({kStart, memory.PageSize()},
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write);
}

}  // namespace

TEST_CASE("GLES pixel resolver honors pack and unpack row alignment") {
    ogplay::memory::AddressSpace memory;
    MapTransferPage(memory);
    ogplay::gles::GlesTransferState state;
    const std::array read{UINT32_C(0), UINT32_C(0), UINT32_C(3), UINT32_C(2),
                          UINT32_C(0x1907), UINT32_C(0x1401), kStart.Value()};
    auto packed = ogplay::gles::PrepareGles2Call(
        memory, Thunk("glReadPixels"), read, 0, &state);
    REQUIRE(packed.pointers.size() == 1);
    CHECK(packed.pointers[0].byte_size == 21);

    state.PixelStore(0x0D05, 1);
    auto tightly_packed = ogplay::gles::PrepareGles2Call(
        memory, Thunk("glReadPixels"), read, 0, &state);
    CHECK(tightly_packed.pointers[0].byte_size == 18);

    state.PixelStore(0x0CF5, 8);
    const std::array image{UINT32_C(0x0DE1), UINT32_C(0), UINT32_C(0x1907),
                           UINT32_C(3), UINT32_C(2), UINT32_C(0),
                           UINT32_C(0x1907), UINT32_C(0x1401), kStart.Value()};
    auto unpacked = ogplay::gles::PrepareGles2Call(
        memory, Thunk("glTexImage2D"), image, 0, &state);
    CHECK(unpacked.pointers[0].byte_size == 25);

    const std::array sub_image{
        UINT32_C(0x0DE1), UINT32_C(0), UINT32_C(5), UINT32_C(7), UINT32_C(3),
        UINT32_C(2), UINT32_C(0x1907), UINT32_C(0x1401), kStart.Value()};
    auto sub_image_unpacked = ogplay::gles::PrepareGles2Call(
        memory, Thunk("glTexSubImage2D"), sub_image, 0, &state);
    CHECK(sub_image_unpacked.pointers[0].byte_size == 25);
}

TEST_CASE("GLES index resolver distinguishes guest arrays and buffer offsets") {
    ogplay::memory::AddressSpace memory;
    MapTransferPage(memory);
    ogplay::gles::GlesTransferState state;
    const std::array guest_indices{UINT32_C(4), UINT32_C(6), UINT32_C(0x1403),
                                   kStart.Value()};
    auto guest = ogplay::gles::PrepareGles2Call(
        memory, Thunk("glDrawElements"), guest_indices, 0, &state);
    REQUIRE(guest.pointers.size() == 1);
    CHECK(guest.pointers[0].byte_size == 12);
    REQUIRE(guest.pointers[0].transfer);

    state.BindBuffer(0x8893, 7);
    const std::array buffer_indices{UINT32_C(4), UINT32_C(6),
                                    UINT32_C(0x1403), UINT32_C(24)};
    auto buffer = ogplay::gles::PrepareGles2Call(
        memory, Thunk("glDrawElements"), buffer_indices, 0, &state);
    CHECK(buffer.pointers[0].deferred);
    CHECK_FALSE(buffer.pointers[0].transfer);
    CHECK(buffer.pointers[0].guest_address.Value() == 24);
}

TEST_CASE("GLES resolver provides query uniform and vertex output shapes") {
    ogplay::memory::AddressSpace memory;
    MapTransferPage(memory);
    ogplay::gles::GlesTransferState state;
    const std::array viewport{UINT32_C(0x0BA2), kStart.Value()};
    auto query = ogplay::gles::PrepareGles2Call(
        memory, Thunk("glGetIntegerv"), viewport, 0, &state);
    CHECK(query.pointers[0].byte_size == 16);

    const std::array depth_range{UINT32_C(0x0B70), kStart.Value()};
    const auto float_range = ogplay::gles::PrepareGles2Call(
        memory, Thunk("glGetFloatv"), depth_range, 0, &state);
    CHECK(float_range.pointers[0].byte_size == 8);

    for (const auto pname : {UINT32_C(0x8800), UINT32_C(0x8801),
                             UINT32_C(0x8802), UINT32_C(0x8803),
                             UINT32_C(0x8CA3), UINT32_C(0x8CA4),
                             UINT32_C(0x8CA5)}) {
        const std::array back_stencil{pname, kStart.Value()};
        const auto scalar_query = ogplay::gles::PrepareGles2Call(
            memory, Thunk("glGetIntegerv"), back_stencil, 0, &state);
        CHECK(scalar_query.pointers[0].byte_size == 4);
    }

    state.SetQueryElementCount(0x86A3, 5);
    const std::array formats{UINT32_C(0x86A3), kStart.Value()};
    auto dynamic_query = ogplay::gles::PrepareGles2Call(
        memory, Thunk("glGetIntegerv"), formats, 0, &state);
    CHECK(dynamic_query.pointers[0].byte_size == 20);

    state.SetUniformElementCount(9, 3, 16);
    const std::array uniform{UINT32_C(9), UINT32_C(3), kStart.Value()};
    auto uniform_query = ogplay::gles::PrepareGles2Call(
        memory, Thunk("glGetUniformfv"), uniform, 0, &state);
    CHECK(uniform_query.pointers[0].byte_size == 64);

    const std::array vertex{UINT32_C(2), UINT32_C(0x8626), kStart.Value()};
    auto vertex_query = ogplay::gles::PrepareGles2Call(
        memory, Thunk("glGetVertexAttribfv"), vertex, 0, &state);
    CHECK(vertex_query.pointers[0].byte_size == 16);

    const std::array texture_parameter{UINT32_C(0x0DE1), UINT32_C(0x2801),
                                       kStart.Value()};
    auto texture_query = ogplay::gles::PrepareGles2Call(
        memory, Thunk("glGetTexParameteriv"), texture_parameter, 0, &state);
    CHECK(texture_query.pointers[0].byte_size == 4);

    const std::array unknown_texture_parameter{
        UINT32_C(0x0DE1), UINT32_C(0x8BAD), kStart.Value()};
    CHECK_THROWS_AS(static_cast<void>(ogplay::gles::PrepareGles2Call(
                        memory, Thunk("glGetTexParameteriv"),
                        unknown_texture_parameter, 0, &state)),
                    ogplay::gles::GlesTransferStateError);
}

TEST_CASE("GLES client vertex pointers remain deferred state") {
    ogplay::memory::AddressSpace memory;
    ogplay::gles::GlesTransferState state;
    const std::array pointer{UINT32_C(1), UINT32_C(3), UINT32_C(0x1406),
                             UINT32_C(0), UINT32_C(12), UINT32_C(32)};
    auto prepared = ogplay::gles::PrepareGles2Call(
        memory, Thunk("glVertexAttribPointer"), pointer, 0, &state);
    REQUIRE(prepared.pointers.size() == 1);
    CHECK(prepared.pointers[0].deferred);
    CHECK_FALSE(prepared.pointers[0].transfer);

    state.BindBuffer(0x8892, 11);
    const auto snapshot = state.Snapshot();
    CHECK(snapshot.array_buffer == 11);
    CHECK(snapshot.element_array_buffer == 0);
}

TEST_CASE("GLES transfer state rejects invalid mutations and shapes") {
    ogplay::gles::GlesTransferState state;
    const auto baseline = state.Snapshot();
    CHECK_THROWS_AS(state.PixelStore(0x0D05, 3),
                    ogplay::gles::GlesTransferStateError);
    CHECK_THROWS_AS(state.PixelStore(0x1234, 4),
                    ogplay::gles::GlesTransferStateError);
    CHECK_THROWS_AS(state.BindBuffer(0x1234, 1),
                    ogplay::gles::GlesTransferStateError);
    CHECK_THROWS_AS(state.SetQueryElementCount(1, 0),
                    ogplay::gles::GlesTransferStateError);
    CHECK(state.Snapshot() == baseline);

    state.SetUniformElementCount(4, 2, 4);
    CHECK(state.Snapshot().uniform_shapes == 1);
    state.ClearUniformElementCounts(4);
    CHECK(state.Snapshot().uniform_shapes == 0);
}
