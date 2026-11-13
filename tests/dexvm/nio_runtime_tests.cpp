#include "ogplay/runtime/jni/host_copy_payload_provider.h"
#include "boot_dex.h"
#include "ogplay/runtime/integration/java_payload_provider.h"
#include <array>
#include <bit>
#include <cstddef>
#include <vector>
#include <fstream>
#include <iterator>
#include <tuple>
#include <cstring>

#include <doctest/doctest.h>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/guest_memory.h"
#include "ogplay/runtime/dexvm/nio_buffer_access.h"
#include "ogplay/runtime/dexvm/object_model.h"

namespace dx = ogplay::runtime::dexvm;
namespace memory = ogplay::memory;
using ogplay::runtime::JniObjectDomain;
using ogplay::runtime::JniObjectIdentity;

namespace {
struct NioVm final {
    memory::AddressSpace memory;
    ogplay::core::CapabilityLedger payload_ledger;
    ogplay::runtime::GuestJavaPayloadProvider arrays_payload{memory, payload_ledger};
    ogplay::runtime::JniPrimitiveArrayStore arrays{arrays_payload};
    dx::JavaObjectModel model{arrays};
    dx::DexClassLinker linker;
    ogplay::core::CapabilityLedger ledger;
    dx::Interpreter vm;

    explicit NioVm(dx::InterpreterBackend backend = dx::InterpreterBackend::switch_dispatch, bool disable_fast = false)
        : vm(
              [this]() -> dx::DexClassLinker& {
                  linker.RegisterIntrinsics(dx::CoreIntrinsicCatalog(ogplay::test::CoreBootFixtureServices()));
                  ogplay::test::RegisterBootDex(linker);
                  std::ifstream fixture(std::string(OGPLAY_DEXVM_FIXTURE_DIR) + "/nio_buffers.dex", std::ios::binary);
                  REQUIRE(fixture.good());
                  linker.RegisterDex(std::vector<std::uint8_t>(std::istreambuf_iterator<char>(fixture), {}));
                  linker.Link();
                  return linker;
              }(),
              model, nullptr, ledger, {.backend = backend, .disable_nio_fast_ops = disable_fast}) {
        vm.SetGuestMemoryAccess(
            {[](std::uint32_t) { return memory::GuestAddress(0x1000U); }, [](memory::GuestAddress, std::uint32_t) {},
             [](memory::GuestAddress address, std::uint32_t size) {
                 return address.Value() >= 0x1000U && address.Value() + size <= 0x1040U;
             },
             [this](memory::GuestAddress address, std::span<std::byte> out) { memory.Read(address, out); },
             [this](memory::GuestAddress address, std::span<const std::byte> in) { memory.Write(address, in); },
             [this](memory::GuestAddress address, std::uint32_t size, bool write) {
                 return memory.ResolveNativeRange({address, size},
                                                  write ? memory::AccessType::write : memory::AccessType::read);
             }});
    }

    dx::VmObjectRef Static(const char* name, const char* descriptor, std::vector<dx::VmValue> arguments) {
        const auto method =
            linker.FindDirectMethod(linker.ResolveDescriptor("Ljava/nio/ByteBuffer;"), name, descriptor);
        REQUIRE(method.has_value());
        const auto outcome = vm.Call(*method, arguments);
        REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
        return outcome.value.ref;
    }

    dx::VmObjectRef On(dx::VmObjectRef receiver, const char* name) {
        const auto owner = model.ObjectClass(receiver);
        const auto index = linker.FindVtableIndex(owner, name, "()Ljava/nio/ByteBuffer;");
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

    dx::VmValue Success(dx::VmObjectRef buffer, const char* name, const char* descriptor,
                        std::vector<dx::VmValue> args = {}) {
        const auto result = Call(buffer, name, descriptor, std::move(args));
        REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
        return result.value;
    }

    void Position(dx::VmObjectRef buffer, int value) {
        Success(buffer, "position", "(I)Ljava/nio/Buffer;", {dx::VmValue::Int(value)});
    }

    void Put(dx::VmObjectRef buffer, int index, int value) {
        Success(buffer, "put", "(IB)Ljava/nio/ByteBuffer;", {dx::VmValue::Int(index), dx::VmValue::Int(value)});
    }

    int Byte(dx::VmObjectRef buffer, int index) {
        return static_cast<std::uint8_t>(Success(buffer, "get", "(I)B", {dx::VmValue::Int(index)}).AsInt());
    }

    std::string_view ClassOf(dx::VmObjectRef object) const {
        return linker.Class(model.ObjectClass(object)).descriptor;
    }
};
}  // namespace

TEST_CASE("DVM-82 NIO cursor and marks follow API 19 invariants") {
    NioVm f;
    const auto buffer = f.Static("allocate", "(I)Ljava/nio/ByteBuffer;", {dx::VmValue::Int(8)});
    f.Position(buffer, 4);
    f.Success(buffer, "mark", "()Ljava/nio/Buffer;");
    f.Position(buffer, 6);
    f.Success(buffer, "reset", "()Ljava/nio/Buffer;");
    CHECK(f.vm.Buffers().Read(buffer).position == 4);
    f.Success(buffer, "limit", "(I)Ljava/nio/Buffer;", {dx::VmValue::Int(3)});
    CHECK(f.vm.Buffers().Read(buffer).position == 3);
    CHECK_FALSE(f.vm.Buffers().Read(buffer).mark);
    const auto reset = f.Call(buffer, "reset", "()Ljava/nio/Buffer;");
    REQUIRE(reset.exception.IsValid());
    CHECK(f.ClassOf(reset.exception) == "Ljava/nio/InvalidMarkException;");
    f.Success(buffer, "flip", "()Ljava/nio/Buffer;");
    CHECK(f.vm.Buffers().Read(buffer).limit == 3);
    CHECK(f.vm.Buffers().Read(buffer).position == 0);
}

TEST_CASE("DVM-82 NIO views share storage and keep independent cursors") {
    NioVm f;
    const auto buffer = f.Static("allocate", "(I)Ljava/nio/ByteBuffer;", {dx::VmValue::Int(8)});
    f.Put(buffer, 2, 0x7a);
    f.Position(buffer, 2);
    const auto view = f.On(buffer, "slice");
    CHECK(f.Byte(view, 0) == 0x7a);
    f.Put(view, 1, 0x55);
    CHECK(f.Byte(buffer, 3) == 0x55);
    const auto readonly = f.On(view, "asReadOnlyBuffer");
    CHECK(f.vm.Buffers().Read(readonly).read_only);
    const auto put = f.Call(readonly, "put", "(IB)Ljava/nio/ByteBuffer;", {dx::VmValue::Int(0), dx::VmValue::Int(1)});
    REQUIRE(put.exception.IsValid());
    CHECK(f.ClassOf(put.exception) == "Ljava/nio/ReadOnlyBufferException;");
    const auto ints = f.Success(buffer, "asIntBuffer", "()Ljava/nio/IntBuffer;").ref;
    CHECK_FALSE(f.Success(ints, "hasArray", "()Z").AsInt());
}

TEST_CASE("DVM-82 ByteBuffer views preserve heap and direct concrete classes") {
    NioVm f;
    for (const auto allocation : {"allocate", "allocateDirect"}) {
        const auto buffer = f.Static(allocation, "(I)Ljava/nio/ByteBuffer;", {dx::VmValue::Int(8)});
        const bool direct = std::string_view(allocation) == "allocateDirect";
        for (const auto method : {"slice", "duplicate", "asReadOnlyBuffer"}) {
            const auto view = f.On(buffer, method);
            CHECK(f.ClassOf(view) == (direct ? "Ljava/nio/DirectByteBuffer;" : "Ljava/nio/ByteArrayBuffer;"));
            CHECK(f.vm.Buffers().Read(view).direct == direct);
        }
    }
}

TEST_CASE("DVM-82 ByteBuffer scalar access honors selected byte order") {
    NioVm f;
    const auto buffer = f.Static("allocate", "(I)Ljava/nio/ByteBuffer;", {dx::VmValue::Int(8)});
    f.Success(buffer, "putInt", "(II)Ljava/nio/ByteBuffer;", {dx::VmValue::Int(0), dx::VmValue::Int(0x11223344)});
    CHECK(f.Byte(buffer, 0) == 0x11);
    const auto type = f.linker.ResolveDescriptor("Ljava/nio/ByteOrder;");
    const auto field = f.linker.FindFieldRecursive(type, "LITTLE_ENDIAN", "Ljava/nio/ByteOrder;");
    REQUIRE(field);
    const auto order = dx::VmObjectRef(f.linker.Class(type).static_storage[f.linker.Field(*field).slot]);
    f.Success(buffer, "order", "(Ljava/nio/ByteOrder;)Ljava/nio/ByteBuffer;", {dx::VmValue::Ref(order)});
    CHECK(f.Success(buffer, "getInt", "(I)I", {dx::VmValue::Int(0)}).AsInt() == 0x44332211);
}

TEST_CASE("DVM-82 NIO ROM fields trace heap backing and collect owners") {
    NioVm f;
    const auto buffer = f.Static("allocate", "(I)Ljava/nio/ByteBuffer;", {dx::VmValue::Int(4)});
    const auto array = f.vm.Buffers().Read(buffer).array;
    f.Success(buffer, "capacity", "()I");
    {
        const auto roots = f.vm.ProtectReferences(std::array{buffer});
        static_cast<void>(f.vm.CollectGarbage());
        CHECK(f.model.IsValidRef(array));
    }
    static_cast<void>(f.vm.CollectGarbage());
    CHECK_FALSE(f.model.IsValidRef(buffer));
    CHECK_FALSE(f.model.IsValidRef(array));
}

TEST_CASE("DVM-82 direct buffers expose validated guest memory and GLES marshalling") {
    NioVm f;
    f.memory.Map({memory::GuestAddress(0x40000000), 4096},
                 memory::PageProtection::read | memory::PageProtection::write);
    const auto buffer = f.vm.Buffers().NewDirect(memory::GuestAddress(0x40000000), 16);
    f.Put(buffer, 4, 0x66);
    f.Position(buffer, 4);
    const auto slice = f.On(buffer, "slice");
    const auto view = f.vm.Buffers().Read(slice);
    CHECK(view.direct_address == memory::GuestAddress(0x40000004));
    CHECK(f.Byte(slice, 0) == 0x66);
    CHECK(f.vm.Buffers().Bytes(view, 0, 1, false)[0] == std::byte{0x66});
    // Dalvik JNI wrapping does not prevalidate the external address; access does.
    const auto invalid = f.vm.Buffers().NewDirect(memory::GuestAddress(0x40010000), 4);
    CHECK_THROWS(static_cast<void>(f.vm.Buffers().Bytes(f.vm.Buffers().Read(invalid), 0, 4, false)));
}

TEST_CASE("DVM-83 NIO marshals heap and direct buffers without moving cursors") {
    NioVm f;
    for (const auto allocation : {"allocate", "allocateDirect"}) {
        const auto buffer = f.Static(allocation, "(I)Ljava/nio/ByteBuffer;", {dx::VmValue::Int(8)});
        f.Position(buffer, 2);
        f.Success(buffer, "limit", "(I)Ljava/nio/Buffer;", {dx::VmValue::Int(6)});
        f.Put(buffer, 2, 0x11);
        const auto view = f.vm.Buffers().Read(buffer);
        REQUIRE(view.current_address);
        CHECK(f.memory.Read8(*view.current_address) == 0x11);
        f.memory.Write8(view.current_address->Add(1), 0x7a);
        CHECK(f.Byte(buffer, 3) == 0x7a);
        CHECK(f.vm.Buffers().Read(buffer).position == 2);
        CHECK(f.vm.Buffers().Read(buffer).limit == 6);
    }
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
            on(b, "put", "(IB)Ljava/nio/ByteBuffer;", {dx::VmValue::Int(1), dx::VmValue::Int(-5)});
            CHECK(on(b, "get", "(I)B", {dx::VmValue::Int(1)}).AsInt() == -5);
            on(b, "putShort", "(IS)Ljava/nio/ByteBuffer;", {dx::VmValue::Int(2), dx::VmValue::Int(-1234)});
            CHECK(on(b, "getShort", "(I)S", {dx::VmValue::Int(2)}).AsInt() == -1234);
            on(b, "putChar", "(IC)Ljava/nio/ByteBuffer;", {dx::VmValue::Int(4), dx::VmValue::Int(49200)});
            CHECK(on(b, "getChar", "(I)C", {dx::VmValue::Int(4)}).AsInt() == 49200);
            const auto view = on(b, "asShortBuffer", "()Ljava/nio/ShortBuffer;", {}).ref;
            CHECK(on(view, "get", "(I)S", {dx::VmValue::Int(1)}).AsInt() == -1234);
        }
    }
}

TEST_CASE("DVM-260 direct buffers share managed byte payload across views and GC") {
    for (const auto backend : {dx::InterpreterBackend::switch_dispatch, dx::InterpreterBackend::threaded}) {
        NioVm f(backend);
        const auto buffer = f.Static("allocateDirect", "(I)Ljava/nio/ByteBuffer;", {dx::VmValue::Int(16)});
        const auto state = f.vm.Buffers().Read(buffer);
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
        CHECK(f.Byte(buffer, 2) == 0xfe);
        CHECK(f.memory.Read8(address.Add(2)) == 0xfe);
        f.Put(buffer, 3, 0x7a);
        CHECK(f.model.GetPrimitiveElement(array, 3) == 0x7a);
        CHECK(f.memory.Read8(address.Add(3)) == 0x7a);
        f.memory.Write8(address.Add(4), 0xab);
        CHECK(f.Byte(buffer, 4) == 0xab);
        CHECK(f.model.GetPrimitiveElement(array, 4) == 0xab);
        f.Position(buffer, 4);
        const auto slice = f.On(buffer, "slice");
        CHECK(f.Call(slice, "arrayOffset", "()I").value.AsInt() == 4);
        CHECK(f.vm.Buffers().Read(slice).direct_address == address.Add(4));
        const auto duplicate = f.On(slice, "duplicate");
        CHECK(f.vm.Buffers().Read(duplicate).array_byte_offset == 4);
        CHECK(f.vm.Buffers().Read(duplicate).direct_address == address.Add(4));
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
        CHECK(f.vm.Buffers().Read(ints).direct_address == address.Add(4));
        CHECK(f.Call(ints, "hasArray", "()Z").value.AsInt() == 0);
        f.Success(ints, "put", "(II)Ljava/nio/IntBuffer;", {dx::VmValue::Int(1), dx::VmValue::Int(0x11223344)});
        CHECK(f.model.GetPrimitiveElement(array, 8) == 0x11);
        CHECK(f.memory.Read8(address.Add(11)) == 0x44);
        const auto readonly_ints = f.Call(read_only, "asIntBuffer", "()Ljava/nio/IntBuffer;").value.ref;
        const auto denied =
            f.Call(readonly_ints, "put", "(II)Ljava/nio/IntBuffer;", {dx::VmValue::Int(0), dx::VmValue::Int(1)});
        REQUIRE(denied.exception.IsValid());
        CHECK(f.ClassOf(denied.exception) == "Ljava/nio/ReadOnlyBufferException;");
        {
            const auto roots = f.vm.ProtectReferences(std::array{readonly_ints});
            static_cast<void>(f.vm.CollectGarbage());
            CHECK(f.model.IsValidRef(array));
            CHECK_FALSE(f.model.IsValidRef(buffer));
            CHECK(f.Success(readonly_ints, "get", "(I)I", {dx::VmValue::Int(1)}).AsInt() == 0x11223344);
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

TEST_CASE("UTF8 streaming encoder preserves sliced input and overflow cursors") {
    for (const auto backend : {dx::InterpreterBackend::switch_dispatch, dx::InterpreterBackend::threaded}) {
        NioVm f(backend);
        const auto invoke = [&](const char* owner, const char* name, const char* signature,
                                std::vector<dx::VmValue> arguments) {
            const auto method = f.linker.FindDirectMethod(f.linker.ResolveDescriptor(owner), name, signature);
            REQUIRE(method);
            const auto result = f.vm.Call(*method, arguments);
            REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
            return result.value.ref;
        };
        const auto charset =
            invoke("Ljava/nio/charset/Charset;", "forName", "(Ljava/lang/String;)Ljava/nio/charset/Charset;",
                   {dx::VmValue::Ref(f.vm.NewStringUtf8("UTF-8"))});
        const auto encoder_result = f.Call(charset, "newEncoder", "()Ljava/nio/charset/CharsetEncoder;");
        REQUIRE_FALSE(encoder_result.exception.IsValid());
        const auto encoder = encoder_result.value.ref;
        const auto encoder_root = f.vm.ProtectReferences(std::array{encoder});
        const auto chars = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[C"),
                                                     ogplay::runtime::JniPrimitiveKind::character, 6);
        const auto chars_root = f.vm.ProtectReferences(std::array{chars});
        const std::array<std::uint32_t, 6> units{'x', 'A', 0xe9, 0xd83d, 0xde00, 'y'};
        for (std::size_t i = 0; i < units.size(); ++i)
            f.model.SetPrimitiveElement(chars, static_cast<ogplay::runtime::JniSize>(i), units[i]);
        const auto input = invoke("Ljava/nio/CharBuffer;", "wrap", "([CII)Ljava/nio/CharBuffer;",
                                  {dx::VmValue::Ref(chars), dx::VmValue::Int(1), dx::VmValue::Int(4)});
        const auto input_root = f.vm.ProtectReferences(std::array{input});
        const auto first = f.Static("allocate", "(I)Ljava/nio/ByteBuffer;", {dx::VmValue::Int(2)});
        const auto first_root = f.vm.ProtectReferences(std::array{first});
        const auto encode = [&](dx::VmObjectRef output) {
            const auto result =
                f.Call(encoder, "encode", "(Ljava/nio/CharBuffer;Ljava/nio/ByteBuffer;Z)Ljava/nio/charset/CoderResult;",
                       {dx::VmValue::Ref(input), dx::VmValue::Ref(output), dx::VmValue::Int(1)});
            REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
            return result.value.ref;
        };
        CHECK(f.Call(encode(first), "isOverflow", "()Z").value.AsInt() == 1);
        CHECK(f.vm.Buffers().Read(input).position == 2);
        CHECK(f.vm.Buffers().Read(first).position == 1);
        CHECK(f.Byte(first, 0) == 'A');
        const auto second = f.Static("allocate", "(I)Ljava/nio/ByteBuffer;", {dx::VmValue::Int(6)});
        const auto second_root = f.vm.ProtectReferences(std::array{second});
        CHECK(f.Call(encode(second), "isUnderflow", "()Z").value.AsInt() == 1);
        CHECK(f.vm.Buffers().Read(input).position == 5);
        CHECK(f.vm.Buffers().Read(input).limit == 5);
        const std::array<std::uint8_t, 6> expected{0xc3, 0xa9, 0xf0, 0x9f, 0x98, 0x80};
        for (std::size_t i = 0; i < expected.size(); ++i)
            CHECK(f.Byte(second, static_cast<std::int32_t>(i)) == expected[i]);
    }
}

TEST_CASE("NIO ROM Memory bulk bindings preserve swap, bounds and overlapping bytes") {
    for (const auto backend : {dx::InterpreterBackend::switch_dispatch, dx::InterpreterBackend::threaded}) {
        using V = dx::VmValue;
        NioVm f(backend);
        const auto memory_class = f.linker.ResolveDescriptor("Llibcore/io/Memory;");
        const auto call = [&](const char* name, const char* descriptor, std::initializer_list<V> args) {
            const auto method = f.linker.FindDirectMethod(memory_class, name, descriptor);
            REQUIRE(method);
            return f.vm.Call(*method, args);
        };
        const auto direct = f.Static("allocateDirect", "(I)Ljava/nio/ByteBuffer;", {V::Int(16)});
        const auto bytes = f.vm.Buffers().Read(direct);
        REQUIRE(bytes.direct_address);
        const auto array = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[S"),
                                                     ogplay::runtime::JniPrimitiveKind::short_integer, 2);
        const auto roots = f.vm.ProtectReferences(std::array{direct, array});
        f.model.SetPrimitiveElement(array, 0, 0x1234);
        f.model.SetPrimitiveElement(array, 1, 0xabcd);
        const auto address = V::Long(bytes.direct_address->Value());
        CHECK_FALSE(call("pokeShortArray", "(J[SIIZ)V", {address, V::Ref(array), V::Int(0), V::Int(2), V::Int(1)})
                        .exception.IsValid());
        CHECK(f.memory.Read8(*bytes.direct_address) == 0x12);
        CHECK(f.memory.Read8(bytes.direct_address->Add(3)) == 0xcd);
        CHECK_FALSE(call("peekShortArray", "(J[SIIZ)V", {address, V::Ref(array), V::Int(0), V::Int(2), V::Int(0)})
                        .exception.IsValid());
        CHECK(f.model.GetPrimitiveElement(array, 0) == 0x3412);
        const auto invalid =
            call("pokeShortArray", "(J[SIIZ)V", {address, V::Ref(array), V::Int(1), V::Int(2), V::Int(0)});
        REQUIRE(invalid.exception.IsValid());
        CHECK(f.ClassOf(invalid.exception) == "Ljava/lang/ArrayIndexOutOfBoundsException;");
        const auto null_array =
            call("peekByteArray", "(J[BII)V", {address, V::Ref(dx::VmObjectRef{}), V::Int(0), V::Int(1)});
        REQUIRE(null_array.exception.IsValid());
        CHECK(f.ClassOf(null_array.exception) == "Ljava/lang/NullPointerException;");
        CHECK_FALSE(call("memmove", "(Ljava/lang/Object;ILjava/lang/Object;IJ)V",
                         {V::Ref(direct), V::Int(1), V::Ref(direct), V::Int(0), V::Long(4)})
                        .exception.IsValid());
        const std::array expected{0x12U, 0x12U, 0x34U, 0xabU, 0xcdU};
        for (std::uint32_t offset = 0; offset < expected.size(); ++offset)
            CHECK(f.memory.Read8(bytes.direct_address->Add(offset)) == expected[offset]);
    }
}

TEST_CASE("NIO ROM scalar calls and exceptions are bit exact with fast calls disabled and switch") {
    using V = dx::VmValue;
    std::vector<std::uint64_t> expected;
    for (const auto [backend, disabled] :
         {std::pair{dx::InterpreterBackend::threaded, false}, std::pair{dx::InterpreterBackend::threaded, true},
          std::pair{dx::InterpreterBackend::switch_dispatch, false}}) {
        CAPTURE(backend);
        CAPTURE(disabled);
        NioVm f(backend, disabled);
        std::vector<std::uint64_t> actual;
        const auto invoke = [&](const std::string& owner, const std::string& name, const std::string& signature,
                                std::vector<V> args) {
            const auto method = f.linker.FindDirectMethod(f.linker.ResolveDescriptor(owner), name, signature);
            REQUIRE(method);
            return f.vm.Call(*method, args);
        };
        const auto record = [&](const dx::VmCallOutcome& result, dx::VmObjectRef buffer) {
            actual.push_back(result.exception.IsValid() ? 1U : 0U);
            if (result.exception.IsValid()) {
                const auto descriptor = f.ClassOf(result.exception);
                for (const char ch : descriptor)
                    actual.push_back(static_cast<unsigned char>(ch));
                actual.push_back(0);
                for (const char ch : result.exception_message)
                    actual.push_back(static_cast<unsigned char>(ch));
                actual.push_back(0);
            } else if (result.value.kind != V::Kind::ref)
                actual.push_back(result.value.kind == V::Kind::wide ? result.value.wide : result.value.cat1);
            const auto view = f.vm.Buffers().Read(buffer);
            actual.push_back(static_cast<std::uint32_t>(view.position));
            actual.push_back(static_cast<std::uint32_t>(view.limit));
            actual.push_back(static_cast<std::uint32_t>(view.mark.value_or(-1)));
            const auto bytes = f.vm.Buffers().Bytes(
                view, 0, static_cast<std::uint32_t>(view.capacity) << static_cast<unsigned>(view.element_size_shift),
                false);
            for (auto byte : bytes)
                actual.push_back(std::to_integer<unsigned>(byte));
        };
        for (const auto [label, type] :
             {std::pair{"Byte", 'B'}, std::pair{"Char", 'C'}, std::pair{"Short", 'S'}, std::pair{"Int", 'I'},
              std::pair{"Float", 'F'}, std::pair{"Long", 'J'}, std::pair{"Double", 'D'}}) {
            const std::string typed = "Ljava/nio/" + std::string(label) + "Buffer;";
            for (int storage = 0; storage < 3; ++storage) {
                dx::VmObjectRef buffer;
                if (storage == 0) {
                    const auto allocated = invoke(typed, "allocate", "(I)" + typed, {V::Int(4)});
                    REQUIRE_FALSE(allocated.exception.IsValid());
                    buffer = allocated.value.ref;
                } else {
                    const auto bb = f.Static(storage == 1 ? "allocate" : "allocateDirect", "(I)Ljava/nio/ByteBuffer;",
                                             {V::Int(32)});
                    buffer =
                        type == 'B'
                            ? bb
                            : f.Success(bb, ("as" + std::string(label) + "Buffer").c_str(), ("()" + typed).c_str()).ref;
                }
                for (const auto bits :
                     {UINT64_C(0x8000000080000000), UINT64_C(0x7ff800017fc00001), UINT64_C(0xfedcba9876543210)}) {
                    const auto value = type == 'J' || type == 'D'
                                           ? V::Long(std::bit_cast<std::int64_t>(bits))
                                           : V::Int(std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(bits)));
                    for (const int index : {0, 1, -1, 99}) {
                        record(invoke("LNioCalls;", std::string(label) + "PutAbs",
                                      "(" + typed + "I" + type + ")" + typed, {V::Ref(buffer), V::Int(index), value}),
                               buffer);
                        record(invoke("LNioCalls;", std::string(label) + "GetAbs", "(" + typed + "I)" + type,
                                      {V::Ref(buffer), V::Int(index)}),
                               buffer);
                    }
                    f.Position(buffer, 0);
                    record(invoke("LNioCalls;", std::string(label) + "Put", "(" + typed + type + ")" + typed,
                                  {V::Ref(buffer), value}),
                           buffer);
                    f.Position(buffer, 0);
                    record(invoke("LNioCalls;", std::string(label) + "Get", "(" + typed + ")" + type, {V::Ref(buffer)}),
                           buffer);
                    const auto readonly = f.Success(buffer, "asReadOnlyBuffer", ("()" + typed).c_str()).ref;
                    record(invoke("LNioCalls;", std::string(label) + "Put", "(" + typed + type + ")" + typed,
                                  {V::Ref(readonly), value}),
                           readonly);
                }
                for (const int index : {0, 2, 99, -1})
                    record(invoke("LNioCalls;", "cursorposition", "(Ljava/nio/Buffer;I)Ljava/nio/Buffer;",
                                  {V::Ref(buffer), V::Int(index)}),
                           buffer);
                record(invoke("LNioCalls;", "cursorflip", "(Ljava/nio/Buffer;)Ljava/nio/Buffer;", {V::Ref(buffer)}),
                       buffer);
                record(invoke("LNioCalls;", "cursorremaining", "(Ljava/nio/Buffer;)I", {V::Ref(buffer)}), buffer);
                record(invoke("LNioCalls;", "cursorreset", "(Ljava/nio/Buffer;)Ljava/nio/Buffer;", {V::Ref(buffer)}),
                       buffer);
            }
        }
        // Exercise every ByteBuffer scalar width, relative/absolute, both byte orders.
        for (const auto allocation : {"allocate", "allocateDirect"}) {
            const auto bb = f.Static(allocation, "(I)Ljava/nio/ByteBuffer;", {V::Int(32)});
            for (const auto order_name : {"BIG_ENDIAN", "LITTLE_ENDIAN"}) {
                const auto order_type = f.linker.ResolveDescriptor("Ljava/nio/ByteOrder;");
                const auto field = f.linker.FindFieldRecursive(order_type, order_name, "Ljava/nio/ByteOrder;");
                REQUIRE(field);
                const auto order =
                    dx::VmObjectRef(f.linker.Class(order_type).static_storage[f.linker.Field(*field).slot]);
                f.Success(bb, "order", "(Ljava/nio/ByteOrder;)Ljava/nio/ByteBuffer;", {V::Ref(order)});
                for (const auto [label, type] :
                     {std::pair{"Char", 'C'}, std::pair{"Short", 'S'}, std::pair{"Int", 'I'}, std::pair{"Float", 'F'},
                      std::pair{"Long", 'J'}, std::pair{"Double", 'D'}}) {
                    const auto value = type == 'J' || type == 'D'
                                           ? V::Long(std::bit_cast<std::int64_t>(UINT64_C(0xfff800017fc00001)))
                                           : V::Int(std::bit_cast<std::int32_t>(0x7fc00001U));
                    for (const bool absolute : {false, true}) {
                        f.Position(bb, 0);
                        std::vector<V> args{V::Ref(bb)};
                        if (absolute) args.push_back(V::Int(1));
                        args.push_back(value);
                        record(invoke("LNioCalls;", "byte" + std::string(label) + "Put" + (absolute ? "Abs" : ""),
                                      std::string("(Ljava/nio/ByteBuffer;") + (absolute ? "I" : "") + type +
                                          ")Ljava/nio/ByteBuffer;",
                                      args),
                               bb);
                        f.Position(bb, 0);
                        args = {V::Ref(bb)};
                        if (absolute) args.push_back(V::Int(1));
                        record(invoke("LNioCalls;", "byte" + std::string(label) + "Get" + (absolute ? "Abs" : ""),
                                      std::string("(Ljava/nio/ByteBuffer;") + (absolute ? "I" : "") + ")" + type, args),
                               bb);
                    }
                }
            }
            const auto address = f.vm.Buffers().Read(bb).current_address;
            REQUIRE(address);
            for (const auto [label, type] :
                 {std::pair{"Byte", 'B'}, std::pair{"Short", 'S'}, std::pair{"Int", 'I'}, std::pair{"Long", 'J'}})
                for (int swap = 0; swap < 2; ++swap) {
                    const auto value = type == 'J' ? V::Long(std::bit_cast<std::int64_t>(UINT64_C(0xfedcba9876543210)))
                                                   : V::Int(std::bit_cast<std::int32_t>(0x87654321U));
                    std::vector<V> args{V::Long(address->Value()), value};
                    if (type != 'B') args.push_back(V::Int(swap));
                    record(invoke("LNioCalls;", std::string("poke") + label,
                                  std::string("(J") + type + (type == 'B' ? "" : "Z") + ")V", args),
                           bb);
                    args = {V::Long(address->Value())};
                    if (type != 'B') args.push_back(V::Int(swap));
                    record(invoke("LNioCalls;", std::string("peek") + label,
                                  std::string("(J") + (type == 'B' ? "" : "Z") + ")" + type, args),
                           bb);
                }
        }
        if (expected.empty())
            expected = actual;
        else
            CHECK(actual == expected);
    }
}
