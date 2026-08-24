#include <array>
#include <bit>
#include <cstddef>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/nio_runtime.h"

namespace dx = ogplay::runtime::dexvm;
namespace memory = ogplay::memory;
using ogplay::runtime::JniObjectDomain;
using ogplay::runtime::JniObjectIdentity;

namespace {
JniObjectIdentity Object(std::uint64_t value) {
    return {JniObjectDomain::dex_vm, value};
}
}

TEST_CASE("DVM-82 NIO cursor and marks follow API 19 invariants") {
    dx::NioRuntime nio;
    nio.CreateHeap(Object(1), dx::VmObjectRef(0), 0, 8,
                   dx::NioElementKind::byte);
    nio.SetPosition(Object(1), 4);
    nio.Mark(Object(1));
    nio.SetPosition(Object(1), 6);
    nio.Reset(Object(1));
    CHECK(nio.Snapshot(Object(1)).position == 4);
    nio.SetLimit(Object(1), 3);
    CHECK(nio.Snapshot(Object(1)).position == 3);
    CHECK_FALSE(nio.Snapshot(Object(1)).mark.has_value());
    CHECK_THROWS_AS(nio.Reset(Object(1)), dx::VmJavaThrow);
    nio.Flip(Object(1));
    CHECK(nio.Snapshot(Object(1)).limit == 3);
    CHECK(nio.Snapshot(Object(1)).position == 0);
}

TEST_CASE("DVM-82 NIO views share storage and keep independent cursors") {
    dx::NioRuntime nio;
    nio.CreateHeap(Object(1), dx::VmObjectRef(0), 0, 8,
                   dx::NioElementKind::byte);
    nio.Put(Object(1), 2, 0x7a);
    nio.CreateView(Object(2), Object(1), dx::NioElementKind::byte, 2, 4,
                   false);
    CHECK(nio.Get(Object(2), 0) == 0x7a);
    nio.Put(Object(2), 1, 0x55);
    CHECK(nio.Get(Object(1), 3) == 0x55);
    nio.Duplicate(Object(3), Object(2), true);
    CHECK(nio.Snapshot(Object(3)).read_only);
    CHECK_THROWS_AS(nio.Put(Object(3), 0, 1), dx::VmJavaThrow);
    nio.CreateView(Object(4), Object(1), dx::NioElementKind::int_value, 0, 2,
                   false);
    CHECK_FALSE(nio.Snapshot(Object(4)).array.IsValid());
}

TEST_CASE("DVM-82 ByteBuffer scalar access honors selected byte order") {
    dx::NioRuntime nio;
    nio.CreateHeap(Object(1), dx::VmObjectRef(0), 0, 8,
                   dx::NioElementKind::byte);
    nio.PutScalar(Object(1), dx::NioElementKind::int_value, 0, 0x11223344U);
    CHECK(nio.Get(Object(1), 0) == 0x11);
    nio.SetOrder(Object(1), dx::NioByteOrder::little_endian);
    CHECK(nio.GetScalar(Object(1), dx::NioElementKind::int_value, 0) ==
          0x44332211U);
}

TEST_CASE("DVM-82 NIO side table traces heap backing and sweeps owners") {
    dx::NioRuntime nio;
    nio.CreateHeap(Object(1), dx::VmObjectRef(9), 0, 4,
                   dx::NioElementKind::byte);
    dx::VmObjectRef traced(0);
    nio.Trace(Object(1), [&traced](const dx::VmObjectRef ref) { traced = ref; });
    CHECK(traced == dx::VmObjectRef(9));
    nio.Sweep(Object(1));
    CHECK_FALSE(nio.Contains(Object(1)));
}

TEST_CASE("DVM-82 direct buffers expose validated guest memory and GLES marshalling") {
    dx::NioRuntime nio;
    std::array<std::byte, 16> bytes{};
    bool released{};
    nio.SetDirectMemoryAccess({
        [](std::uint32_t) { return memory::GuestAddress(0x1000U); },
        [&released](memory::GuestAddress, std::uint32_t) { released = true; },
        [](memory::GuestAddress address, std::uint32_t size) {
            return address.Value() >= 0x1000U &&
                   address.Value() + size <= 0x1010U;
        },
        [&bytes](memory::GuestAddress address, std::span<std::byte> out) {
            std::copy_n(bytes.begin() + (address.Value() - 0x1000U), out.size(),
                        out.begin());
        },
        [&bytes](memory::GuestAddress address, std::span<const std::byte> in) {
            std::copy(in.begin(), in.end(),
                      bytes.begin() + (address.Value() - 0x1000U));
        }});
    nio.CreateDirect(Object(1), 16);
    nio.Put(Object(1), 4, 0x66);
    nio.CreateView(Object(2), Object(1), dx::NioElementKind::byte, 4, 8,
                   false);
    CHECK(nio.Snapshot(Object(2)).direct_address->Value() == 0x1004U);
    CHECK(nio.Get(Object(2), 0) == 0x66);
    const auto marshalled = nio.ReadRemainingBytes(Object(2));
    REQUIRE(marshalled.size() == 8U);
    CHECK(marshalled[0] == std::byte{0x66});
    CHECK_THROWS_AS(nio.WrapDirect(Object(3), memory::GuestAddress(0x2000U), 4),
                    dx::VmJavaThrow);
    nio.Sweep(Object(1));
    CHECK_FALSE(released);
    nio.Sweep(Object(2));
    CHECK(released);
}

TEST_CASE("DVM-83 NIO marshals heap and direct buffers without moving cursors") {
    dx::NioRuntime nio;
    std::array<std::byte, 32> guest{};
    nio.SetDirectMemoryAccess({
        [](std::uint32_t) { return memory::GuestAddress(0x1000U); },
        [](memory::GuestAddress, std::uint32_t) {},
        [](memory::GuestAddress address, std::uint32_t size) {
            return address.Value() >= 0x1000U &&
                   address.Value() + size <= 0x1020U;
        },
        [&guest](memory::GuestAddress address, std::span<std::byte> out) {
            std::copy_n(guest.begin() + (address.Value() - 0x1000U), out.size(), out.begin());
        },
        [&guest](memory::GuestAddress address, std::span<const std::byte> in) {
            std::copy(in.begin(), in.end(), guest.begin() + (address.Value() - 0x1000U));
        }});
    nio.CreateHeap(Object(1), dx::VmObjectRef{}, 0, 8, dx::NioElementKind::byte);
    nio.SetPosition(Object(1), 2);
    nio.SetLimit(Object(1), 6);
    nio.Put(Object(1), 2, 0x11U);
    CHECK(nio.WithBufferGuestMemory(Object(1), true,
        [&guest](memory::GuestAddress address) {
            CHECK(address.Value() == 0x1000U);
            CHECK(guest[0] == std::byte{0x11});
            guest[1] = std::byte{0x7a};
            return 9U;
        }) == 9U);
    CHECK(nio.Get(Object(1), 3) == 0x7aU);
    CHECK(nio.Snapshot(Object(1)).position == 2);
    CHECK(nio.Snapshot(Object(1)).limit == 6);

    nio.WrapDirect(Object(2), memory::GuestAddress(0x1010U), 8);
    nio.SetPosition(Object(2), 3);
    CHECK(nio.WithBufferGuestMemory(Object(2), false,
        [](memory::GuestAddress address) {
            CHECK(address.Value() == 0x1013U);
            return 0U;
        }) == 0U);
    CHECK(nio.Snapshot(Object(2)).position == 3);
}
