#include "ogplay/runtime/jni/host_copy_payload_provider.h"
#include "boot_dex.h"
#include "ogplay/runtime/integration/java_payload_provider.h"
#include <array>
#include <bit>
#include <cstddef>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/nio_runtime.h"
#include "ogplay/runtime/dexvm/object_model.h"

namespace dx = ogplay::runtime::dexvm;
namespace memory = ogplay::memory;
using ogplay::runtime::JniObjectDomain;
using ogplay::runtime::JniObjectIdentity;

namespace {
JniObjectIdentity Object(std::uint64_t value) {
    return {JniObjectDomain::dex_vm, value};
}

struct NioVm final {
    ogplay::runtime::HostCopyJavaPayloadProvider strings_payload;
    ogplay::runtime::JniStringStore strings{strings_payload};
    memory::AddressSpace memory;
    ogplay::core::CapabilityLedger payload_ledger;
    ogplay::runtime::GuestJavaPayloadProvider arrays_payload{memory, payload_ledger};
    ogplay::runtime::JniPrimitiveArrayStore arrays{arrays_payload};
    dx::JavaObjectModel model{strings, arrays};
    dx::DexClassLinker linker;
    ogplay::core::CapabilityLedger ledger;
    dx::Interpreter vm;

    explicit NioVm(dx::InterpreterBackend backend = dx::InterpreterBackend::switch_dispatch)
        : vm([this]() -> dx::DexClassLinker& {
              linker.RegisterIntrinsics(dx::CoreIntrinsicCatalog());
              ogplay::test::RegisterBootDex(linker);
              linker.Link();
              return linker;
          }(), model, nullptr, ledger, {.backend = backend}) {
        vm.NIO().SetDirectMemoryAccess(
            {[](std::uint32_t) { return memory::GuestAddress(0x1000U); }, [](memory::GuestAddress, std::uint32_t) {},
             [](memory::GuestAddress address, std::uint32_t size) {
                 return address.Value() >= 0x1000U && address.Value() + size <= 0x1040U;
             },
             [this](memory::GuestAddress address, std::span<std::byte> out) { memory.Read(address, out); },
             [this](memory::GuestAddress address, std::span<const std::byte> in) { memory.Write(address, in); }});
    }

    dx::VmObjectRef Static(const char* name, const char* descriptor,
                           std::vector<dx::VmValue> arguments) {
        const auto method = linker.FindDirectMethod(
            linker.ResolveDescriptor("Ljava/nio/ByteBuffer;"), name,
            descriptor);
        REQUIRE(method.has_value());
        const auto outcome = vm.Call(*method, arguments);
        REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
        return outcome.value.ref;
    }

    dx::VmObjectRef On(dx::VmObjectRef receiver, const char* name) {
        const auto owner = model.ObjectClass(receiver);
        const auto index = linker.FindVtableIndex(
            owner, name, "()Ljava/nio/ByteBuffer;");
        REQUIRE(index.has_value());
        const std::array arguments{dx::VmValue::Ref(receiver)};
        const auto outcome = vm.Call(linker.Class(owner).vtable[*index], arguments);
        REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
        return outcome.value.ref;
    }

    dx::VmCallOutcome Call(dx::VmObjectRef receiver, const char* name, const char* descriptor,
                           std::vector<dx::VmValue> arguments = {}) {
        const auto owner = model.ObjectClass(receiver);
        const auto slot = linker.FindVtableIndex(owner, name, descriptor);
        REQUIRE(slot);
        arguments.insert(arguments.begin(), dx::VmValue::Ref(receiver));
        return vm.Call(linker.Class(owner).vtable[*slot], arguments);
    }

    std::string_view ClassOf(dx::VmObjectRef object) const {
        return linker.Class(model.ObjectClass(object)).descriptor;
    }
};
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

TEST_CASE("DVM-82 ByteBuffer views preserve heap and direct concrete classes") {
    NioVm fixture;
    const auto heap = fixture.Static("allocate", "(I)Ljava/nio/ByteBuffer;",
                                     {dx::VmValue::Int(8)});
    const auto direct = fixture.Static(
        "allocateDirect", "(I)Ljava/nio/ByteBuffer;", {dx::VmValue::Int(8)});

    CHECK(fixture.ClassOf(fixture.On(heap, "slice")) ==
          "Ljava/nio/HeapByteBuffer;");
    for (const auto method : {"slice", "duplicate", "asReadOnlyBuffer"}) {
        const auto view = fixture.On(direct, method);
        CHECK(fixture.ClassOf(view) == "Ljava/nio/DirectByteBuffer;");
        CHECK(fixture.vm.NIO().Snapshot(fixture.model.ToIdentity(view)).direct);
    }
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
    nio.WrapDirect(Object(1), memory::GuestAddress(0x1000U), 16);
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
    CHECK_FALSE(released);
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

TEST_CASE("WU7-6 NIO byte and short reads sign extend on both backends") {
    for (auto backend : {dx::InterpreterBackend::switch_dispatch, dx::InterpreterBackend::threaded}) {
        NioVm f(backend);
        const auto on = [&](dx::VmObjectRef receiver, const char* name, const char* descriptor,
                            std::vector<dx::VmValue> args) {
            const auto type = f.model.ObjectClass(receiver);
            const auto slot = f.linker.FindVtableIndex(type, name, descriptor);
            REQUIRE(slot);
            args.insert(args.begin(), dx::VmValue::Ref(receiver));
            const auto result = f.vm.Call(f.linker.Class(type).vtable[*slot], args);
            REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
            return result.value;
        };
        for (const char* allocation : {"allocate", "allocateDirect"}) {
            const auto b = f.Static(allocation, "(I)Ljava/nio/ByteBuffer;", {dx::VmValue::Int(8)});
            const auto root = f.vm.ProtectReferences(std::array{b});
            on(b,"put","(IB)Ljava/nio/ByteBuffer;",{dx::VmValue::Int(1),dx::VmValue::Int(-5)});
            CHECK(on(b,"get","(I)B",{dx::VmValue::Int(1)}).AsInt() == -5);
            on(b,"putShort","(IS)Ljava/nio/ByteBuffer;",{dx::VmValue::Int(2),dx::VmValue::Int(-1234)});
            CHECK(on(b,"getShort","(I)S",{dx::VmValue::Int(2)}).AsInt() == -1234);
            on(b,"putChar","(IC)Ljava/nio/ByteBuffer;",{dx::VmValue::Int(4),dx::VmValue::Int(49200)});
            CHECK(on(b,"getChar","(I)C",{dx::VmValue::Int(4)}).AsInt() == 49200);
            const auto view = on(b,"asShortBuffer","()Ljava/nio/ShortBuffer;",{}).ref;
            CHECK(on(view,"get","(I)S",{dx::VmValue::Int(1)}).AsInt() == -1234);
        }
    }
}

TEST_CASE("DVM-260 direct buffers share managed byte payload across views and GC") {
    for (const auto backend : {dx::InterpreterBackend::switch_dispatch, dx::InterpreterBackend::threaded}) {
        NioVm f(backend);
        const auto buffer = f.Static("allocateDirect", "(I)Ljava/nio/ByteBuffer;", {dx::VmValue::Int(16)});
        const auto id = f.model.ToIdentity(buffer);
        const auto state = f.vm.NIO().Snapshot(id);
        REQUIRE(state.array.IsValid());
        REQUIRE(state.direct_address);
        const auto array = state.array;
        const auto address = *state.direct_address;
        CHECK(f.Call(buffer, "hasArray", "()Z").value.AsInt() == 1);
        CHECK(f.Call(buffer, "arrayOffset", "()I").value.AsInt() == 0);
        CHECK(f.Call(buffer, "array", "()Ljava/lang/Object;").value.ref == array);
        CHECK(f.arrays.PinCount(f.model.ToIdentity(array)) == 0);
        CHECK(f.model.PrimitiveArrayAddress(array) == address);
        f.model.SetPrimitiveElement(array, 2, 0xfe);
        CHECK(f.vm.NIO().Get(id, 2) == 0xfe);
        CHECK(f.memory.Read8(address.Add(2)) == 0xfe);
        f.vm.NIO().Put(id, 3, 0x7a);
        CHECK(f.model.GetPrimitiveElement(array, 3) == 0x7a);
        CHECK(f.memory.Read8(address.Add(3)) == 0x7a);
        f.memory.Write8(address.Add(4), 0xab);
        CHECK(f.vm.NIO().Get(id, 4) == 0xab);
        CHECK(f.model.GetPrimitiveElement(array, 4) == 0xab);
        f.vm.NIO().SetPosition(id, 4);
        const auto slice = f.On(buffer, "slice");
        const auto slice_id = f.model.ToIdentity(slice);
        CHECK(f.Call(slice, "arrayOffset", "()I").value.AsInt() == 4);
        CHECK(f.vm.NIO().Snapshot(slice_id).direct_address == address.Add(4));
        const auto duplicate = f.On(slice, "duplicate");
        CHECK(f.vm.NIO().Snapshot(f.model.ToIdentity(duplicate)).array_offset == 4);
        CHECK(f.vm.NIO().Snapshot(f.model.ToIdentity(duplicate)).direct_address == address.Add(4));
        const auto read_only = f.On(slice, "asReadOnlyBuffer");
        CHECK(f.Call(read_only, "hasArray", "()Z").value.AsInt() == 0);
        for (const auto [name, signature] :
             {std::pair{"array", "()Ljava/lang/Object;"}, std::pair{"arrayOffset", "()I"}}) {
            const auto result = f.Call(read_only, name, signature);
            REQUIRE(result.exception.IsValid());
            CHECK(f.ClassOf(result.exception) == "Ljava/nio/ReadOnlyBufferException;");
        }
        const auto ints_result = f.Call(slice, "asIntBuffer", "()Ljava/nio/IntBuffer;");
        REQUIRE_FALSE(ints_result.exception.IsValid());
        const auto ints = ints_result.value.ref;
        const auto ints_id = f.model.ToIdentity(ints);
        CHECK(f.vm.NIO().Snapshot(ints_id).direct_address == address.Add(4));
        CHECK(f.Call(ints, "hasArray", "()Z").value.AsInt() == 0);
        f.vm.NIO().Put(ints_id, 1, 0x11223344);
        CHECK(f.model.GetPrimitiveElement(array, 8) == 0x11);
        CHECK(f.memory.Read8(address.Add(11)) == 0x44);
        const auto readonly_ints = f.Call(read_only, "asIntBuffer", "()Ljava/nio/IntBuffer;").value.ref;
        CHECK_THROWS_AS(f.vm.NIO().Put(f.model.ToIdentity(readonly_ints), 0, 1), dx::VmJavaThrow);
        {
            const auto roots = f.vm.ProtectReferences(std::array{readonly_ints});
            static_cast<void>(f.vm.CollectGarbage());
            CHECK(f.model.IsValidRef(array));
            CHECK_FALSE(f.vm.NIO().Contains(id));
            CHECK(f.vm.NIO().Get(f.model.ToIdentity(readonly_ints), 1) == 0x11223344);
        }
        {
            const auto roots = f.vm.ProtectReferences(std::array{array});
            static_cast<void>(f.vm.CollectGarbage());
            CHECK_FALSE(f.model.IsValidRef(readonly_ints));
            CHECK(f.model.IsValidRef(array));
            f.memory.Write8(address, 0x42);
            CHECK(f.model.GetPrimitiveElement(array, 0) == 0x42);
        }
        const auto before = f.arrays_payload.Statistics().allocated_bytes;
        static_cast<void>(f.vm.CollectGarbage());
        CHECK_FALSE(f.model.IsValidRef(array));
        const auto after = f.arrays_payload.Statistics().allocated_bytes;
        CHECK(before - after == 16);
        static_cast<void>(f.vm.CollectGarbage());
        CHECK(f.arrays_payload.Statistics().allocated_bytes == after);
    }
}

TEST_CASE("DVM-260 VMRuntime array allocation and addressOf follow Dalvik boundaries") {
    for (const auto backend : {dx::InterpreterBackend::switch_dispatch, dx::InterpreterBackend::threaded}) {
        NioVm f(backend);
        const auto getter = f.linker.FindDirectMethod(f.linker.ResolveDescriptor("Ldalvik/system/VMRuntime;"),
                                                      "getRuntime", "()Ldalvik/system/VMRuntime;");
        REQUIRE(getter);
        const auto runtime = f.vm.Call(*getter, {}).value.ref;
        const auto roots = f.vm.ProtectReferences(std::array{runtime});
        const auto make = [&](dx::VmObjectRef type, int length) {
            return f.Call(runtime, "newNonMovableArray", "(Ljava/lang/Class;I)Ljava/lang/Object;",
                          {dx::VmValue::Ref(type), dx::VmValue::Int(length)});
        };
        const auto addr = [&](dx::VmObjectRef array) {
            return f.Call(runtime, "addressOf", "(Ljava/lang/Object;)J", {dx::VmValue::Ref(array)});
        };
        const std::array kinds{
            ogplay::runtime::JniPrimitiveKind::boolean,     ogplay::runtime::JniPrimitiveKind::byte,
            ogplay::runtime::JniPrimitiveKind::character,   ogplay::runtime::JniPrimitiveKind::short_integer,
            ogplay::runtime::JniPrimitiveKind::integer,     ogplay::runtime::JniPrimitiveKind::long_integer,
            ogplay::runtime::JniPrimitiveKind::float_value, ogplay::runtime::JniPrimitiveKind::double_value};
        for (std::size_t i = 0; i < kinds.size(); ++i) {
            const std::string descriptor(1, "ZBCSIJFD"[i]);
            const auto type = f.model.ClassObject(f.linker.ResolveDescriptor(descriptor));
            for (const int length : {0, 3}) {
                const auto result = make(type, length);
                REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
                const auto array = result.value.ref;
                CHECK(f.model.PrimitiveArrayKind(array) == kinds[i]);
                CHECK(f.model.ArrayLength(array) == length);
                const auto address = addr(array);
                REQUIRE_FALSE(address.exception.IsValid());
                CHECK(address.value.AsLong() ==
                      static_cast<std::int64_t>(f.model.PrimitiveArrayAddress(array).Value()));
                CHECK(address.value.AsLong() > 0);
            }
        }
        const auto byte_class = f.model.ClassObject(f.linker.ResolveDescriptor("B"));
        CHECK(f.ClassOf(make(dx::VmObjectRef(0), -1).exception) == "Ljava/lang/NullPointerException;");
        CHECK(f.ClassOf(make(byte_class, -1).exception) == "Ljava/lang/NegativeArraySizeException;");
        CHECK(f.ClassOf(addr(dx::VmObjectRef(0)).exception) == "Ljava/lang/IllegalArgumentException;");
        CHECK(f.ClassOf(addr(runtime).exception) == "Ljava/lang/IllegalArgumentException;");
        for (const char* descriptor : {"Ljava/lang/Object;", "[B"}) {
            const auto type = f.model.ClassObject(f.linker.ResolveDescriptor(descriptor));
            const auto result = make(type, 2);
            REQUIRE_FALSE(result.exception.IsValid());
            CHECK(f.model.Kind(result.value.ref) == dx::VmObjectKind::object_array);
            CHECK(f.model.ObjectArrayElementClass(result.value.ref) == f.linker.ResolveDescriptor(descriptor));
            CHECK(f.model.ArrayLength(result.value.ref) == 2);
            CHECK(f.ClassOf(addr(result.value.ref).exception) == "Ljava/lang/UnsupportedOperationException;");
        }
        const auto hits = f.ledger.Unimplemented();
        CHECK(std::any_of(hits.begin(), hits.end(), [](const auto& hit) {
            return hit.id == "dexvm.runtime.address_of_object_array" && hit.count == 2;
        }));
        const auto ordinary =
            f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[B"), ogplay::runtime::JniPrimitiveKind::byte, 4);
        CHECK_FALSE(addr(ordinary).exception.IsValid());
        const auto allocate = f.linker.FindDirectMethod(f.linker.ResolveDescriptor("Ljava/nio/ByteBuffer;"),
                                                        "allocateDirect", "(I)Ljava/nio/ByteBuffer;");
        REQUIRE(allocate);
        const auto huge = f.vm.Call(*allocate, std::array{dx::VmValue::Int(INT32_MAX)});
        REQUIRE(huge.exception.IsValid());
        CHECK(f.ClassOf(huge.exception) == "Ljava/lang/OutOfMemoryError;");
    }
}
