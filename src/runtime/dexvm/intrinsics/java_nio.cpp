#include "catalog.h"
#include "shared.h"
#include <array>
#include <bit>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/nio_runtime.h"

namespace ogplay::runtime::dexvm::intrinsics {
namespace {

JniObjectIdentity Id(Interpreter& vm, VmObjectRef ref) {
    return vm.Model().ToIdentity(ref);
}
VmValue Self(IntrinsicContext& call) { return VmValue::Ref(call.receiver); }

IntrinsicHandler Cursor(void (NioRuntime::*fn)(JniObjectIdentity)) {
    return [fn](IntrinsicContext& call) {
        (call.vm.NIO().*fn)(Id(call.vm, call.receiver));
        return Self(call);
    };
}

IntrinsicClassDecl DeclareBuffer() {
    auto b = IntrinsicClassBuilder::Class("Ljava/nio/Buffer;",
                                          "Ljava/lang/Object;", {},
                                          kAccPublic | kAccAbstract);
    b.FinalMethod("capacity", "()I", [](IntrinsicContext& c) {
        return VmValue::Int(c.vm.NIO().Snapshot(Id(c.vm, c.receiver)).capacity);
    });
    b.FinalMethod("position", "()I", [](IntrinsicContext& c) {
        return VmValue::Int(c.vm.NIO().Snapshot(Id(c.vm, c.receiver)).position);
    });
    b.FinalMethod("position", "(I)Ljava/nio/Buffer;", [](IntrinsicContext& c) {
        c.vm.NIO().SetPosition(Id(c.vm, c.receiver), c.arguments[0].AsInt());
        return Self(c);
    });
    b.FinalMethod("limit", "()I", [](IntrinsicContext& c) {
        return VmValue::Int(c.vm.NIO().Snapshot(Id(c.vm, c.receiver)).limit);
    });
    b.FinalMethod("limit", "(I)Ljava/nio/Buffer;", [](IntrinsicContext& c) {
        c.vm.NIO().SetLimit(Id(c.vm, c.receiver), c.arguments[0].AsInt());
        return Self(c);
    });
    b.FinalMethod("mark", "()Ljava/nio/Buffer;", Cursor(&NioRuntime::Mark));
    b.FinalMethod("reset", "()Ljava/nio/Buffer;", Cursor(&NioRuntime::Reset));
    b.FinalMethod("clear", "()Ljava/nio/Buffer;", Cursor(&NioRuntime::Clear));
    b.FinalMethod("flip", "()Ljava/nio/Buffer;", Cursor(&NioRuntime::Flip));
    b.FinalMethod("rewind", "()Ljava/nio/Buffer;", Cursor(&NioRuntime::Rewind));
    b.FinalMethod("remaining", "()I", [](IntrinsicContext& c) {
        const auto s = c.vm.NIO().Snapshot(Id(c.vm, c.receiver));
        return VmValue::Int(s.limit - s.position);
    });
    b.FinalMethod("hasRemaining", "()Z", [](IntrinsicContext& c) {
        const auto s = c.vm.NIO().Snapshot(Id(c.vm, c.receiver));
        return VmValue::Int(s.position < s.limit);
    });
    b.VirtualMethod("isReadOnly", "()Z", [](IntrinsicContext& c) {
        return VmValue::Int(c.vm.NIO().Snapshot(Id(c.vm, c.receiver)).read_only);
    });
    b.VirtualMethod("isDirect", "()Z", [](IntrinsicContext& c) {
        return VmValue::Int(c.vm.NIO().Snapshot(Id(c.vm, c.receiver)).direct);
    });
    b.VirtualMethod("hasArray", "()Z", [](IntrinsicContext& c) {
        const auto s = c.vm.NIO().Snapshot(Id(c.vm, c.receiver));
        return VmValue::Int(s.array.IsValid() && !s.read_only);
    });
    b.VirtualMethod("array", "()Ljava/lang/Object;", [](IntrinsicContext& c) {
        const auto s = c.vm.NIO().Snapshot(Id(c.vm, c.receiver));
        if (s.read_only) throw VmJavaThrow{"Ljava/nio/ReadOnlyBufferException;", "read-only buffer"};
        if (!s.array.IsValid()) throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "buffer has no array"};
        return VmValue::Ref(s.array);
    });
    b.VirtualMethod("arrayOffset", "()I", [](IntrinsicContext& c) {
        const auto s = c.vm.NIO().Snapshot(Id(c.vm, c.receiver));
        if (s.read_only) throw VmJavaThrow{"Ljava/nio/ReadOnlyBufferException;", "read-only buffer"};
        if (!s.array.IsValid()) throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "buffer has no array"};
        return VmValue::Int(s.array_offset);
    });
    return std::move(b).Build();
}

VmObjectRef PrimitiveArray(Interpreter& vm, NioElementKind kind, int capacity) {
    if (capacity < 0) throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "negative capacity"};
    const char* desc = "[B";
    auto primitive = JniPrimitiveKind::byte;
    switch (kind) {
        case NioElementKind::byte: break;
        case NioElementKind::character: desc = "[C"; primitive = JniPrimitiveKind::character; break;
        case NioElementKind::short_value: desc = "[S"; primitive = JniPrimitiveKind::short_integer; break;
        case NioElementKind::int_value: desc = "[I"; primitive = JniPrimitiveKind::integer; break;
        case NioElementKind::float_value: desc = "[F"; primitive = JniPrimitiveKind::float_value; break;
        case NioElementKind::long_value: desc = "[J"; primitive = JniPrimitiveKind::long_integer; break;
        case NioElementKind::double_value: desc = "[D"; primitive = JniPrimitiveKind::double_value; break;
    }
    return vm.Model().NewPrimitiveArray(vm.Linker().ResolveDescriptor(desc),
                                        primitive, capacity);
}

VmObjectRef HeapBuffer(Interpreter& vm, const std::string& descriptor,
                       NioElementKind kind, VmObjectRef array, int capacity) {
    const auto object = vm.NewIntrinsicInstance(descriptor);
    vm.NIO().CreateHeap(Id(vm, object), array, 0, capacity, kind);
    return object;
}

IntrinsicHandler Allocate(std::string concrete, NioElementKind kind) {
    return [concrete = std::move(concrete), kind](IntrinsicContext& c) {
        const auto capacity = c.arguments[0].AsInt();
        return VmValue::Ref(HeapBuffer(c.vm, concrete, kind,
                                      PrimitiveArray(c.vm, kind, capacity), capacity));
    };
}
IntrinsicHandler Wrap(std::string concrete, NioElementKind kind, bool range) {
    return [concrete = std::move(concrete), kind, range](IntrinsicContext& c) {
        const auto array = c.arguments[0].ref;
        const auto length = c.vm.Model().ArrayLength(array);
        const auto offset = range ? c.arguments[1].AsInt() : 0;
        const auto count = range ? c.arguments[2].AsInt() : length;
        if (offset < 0 || count < 0 || offset > length - count)
            throw VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;", "invalid wrap range"};
        const auto object = HeapBuffer(c.vm, concrete, kind, array, length);
        c.vm.NIO().SetPosition(Id(c.vm, object), offset);
        c.vm.NIO().SetLimit(Id(c.vm, object), offset + count);
        return VmValue::Ref(object);
    };
}

VmValue BitsValue(std::uint64_t bits, NioElementKind kind) {
    if (kind == NioElementKind::long_value) return VmValue::Long(static_cast<std::int64_t>(bits));
    if (kind == NioElementKind::double_value) return VmValue::Double(std::bit_cast<double>(bits));
    if (kind == NioElementKind::float_value) return VmValue::Float(std::bit_cast<float>(static_cast<std::uint32_t>(bits)));
    return VmValue::Int(static_cast<std::int32_t>(bits));
}
std::uint64_t ValueBits(const VmValue& value, NioElementKind kind) {
    return kind == NioElementKind::long_value || kind == NioElementKind::double_value
               ? value.wide : value.cat1;
}
IntrinsicHandler Get(NioElementKind kind, bool absolute) {
    return [kind, absolute](IntrinsicContext& c) {
        return BitsValue(c.vm.NIO().Get(Id(c.vm, c.receiver),
            absolute ? std::optional(c.arguments[0].AsInt()) : std::nullopt), kind);
    };
}
IntrinsicHandler Put(NioElementKind kind, bool absolute) {
    return [kind, absolute](IntrinsicContext& c) {
        const auto value = absolute ? 1U : 0U;
        c.vm.NIO().Put(Id(c.vm, c.receiver),
            absolute ? std::optional(c.arguments[0].AsInt()) : std::nullopt,
            ValueBits(c.arguments[value], kind));
        return Self(c);
    };
}
struct BulkTransfer final {
    VmObjectRef array;
    std::int32_t offset{};
    std::int32_t count{};
};
BulkTransfer PrepareBulkTransfer(IntrinsicContext& c, const bool range,
                                 const std::string_view remaining_exception) {
    const auto array = c.arguments[0].ref;
    const auto array_length = c.vm.Model().ArrayLength(array);
    const auto offset = range ? c.arguments[1].AsInt() : 0;
    const auto count = range ? c.arguments[2].AsInt() : array_length;
    const auto state = c.vm.NIO().Snapshot(Id(c.vm, c.receiver));
    if (offset < 0 || count < 0 || offset > array_length - count) {
        throw VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;",
                          "invalid bulk range"};
    }
    if (count > state.limit - state.position) {
        throw VmJavaThrow{std::string(remaining_exception),
                          "insufficient remaining"};
    }
    return {array, offset, count};
}
IntrinsicHandler BulkGet(NioElementKind kind, bool range) {
    return [kind, range](IntrinsicContext& c) {
        static_cast<void>(kind);
        const auto transfer = PrepareBulkTransfer(
            c, range, "Ljava/nio/BufferUnderflowException;");
        for (int index = 0; index < transfer.count; ++index)
            c.vm.Model().SetPrimitiveElement(
                transfer.array, transfer.offset + index,
                c.vm.NIO().Get(Id(c.vm, c.receiver), {}));
        return Self(c);
    };
}
IntrinsicHandler BulkPut(NioElementKind kind, bool range) {
    return [kind, range](IntrinsicContext& c) {
        static_cast<void>(kind);
        const auto transfer = PrepareBulkTransfer(
            c, range, "Ljava/nio/BufferOverflowException;");
        for (int index = 0; index < transfer.count; ++index)
            c.vm.NIO().Put(Id(c.vm, c.receiver), {},
                c.vm.Model().GetPrimitiveElement(
                    transfer.array, transfer.offset + index));
        return Self(c);
    };
}
IntrinsicHandler View(std::string concrete, NioElementKind kind,
                      bool slice, bool read_only) {
    return [concrete = std::move(concrete), kind, slice, read_only](IntrinsicContext& c) {
        const auto source = Id(c.vm, c.receiver);
        const auto state = c.vm.NIO().Snapshot(source);
        const auto target = concrete.empty()
            ? c.vm.Linker().Class(c.vm.Model().ObjectClass(c.receiver)).descriptor
            : concrete;
        const auto object = c.vm.NewIntrinsicInstance(target);
        if (slice) {
            const auto source_size = NioRuntime::ElementSize(state.element);
            const auto target_size = NioRuntime::ElementSize(kind);
            c.vm.NIO().CreateView(Id(c.vm, object), source, kind,
                state.position * static_cast<int>(source_size),
                (state.limit - state.position) * static_cast<int>(source_size) /
                    static_cast<int>(target_size), read_only);
        } else {
            c.vm.NIO().Duplicate(Id(c.vm, object), source, read_only);
        }
        return VmValue::Ref(object);
    };
}
void Views(IntrinsicClassBuilder& b, const std::string& type,
           NioElementKind kind) {
    b.VirtualMethod("slice", "()" + type, View({}, kind, true, false));
    b.VirtualMethod("duplicate", "()" + type, View({}, kind, false, false));
    b.VirtualMethod("asReadOnlyBuffer", "()" + type, View({}, kind, false, true));
    b.VirtualMethod("compact", "()" + type, [](IntrinsicContext& c) {
        c.vm.NIO().Compact(Id(c.vm, c.receiver)); return Self(c);
    });
}

IntrinsicClassDecl DeclareByteOrder() {
    auto b = IntrinsicClassBuilder::Class(
        "Ljava/nio/ByteOrder;", "Ljava/lang/Object;", {},
        kAccPublic | kAccFinal);
    b.StaticField("BIG_ENDIAN", "Ljava/nio/ByteOrder;",
                  kAccPublic | kAccStatic | kAccFinal);
    b.StaticField("LITTLE_ENDIAN", "Ljava/nio/ByteOrder;",
                  kAccPublic | kAccStatic | kAccFinal);
    b.ClassInitializer([](IntrinsicContext& c) {
        c.vm.SetIntrinsicStaticRef("Ljava/nio/ByteOrder;", "BIG_ENDIAN", "Ljava/nio/ByteOrder;", c.vm.NewIntrinsicInstance("Ljava/nio/ByteOrder;"));
        c.vm.SetIntrinsicStaticRef("Ljava/nio/ByteOrder;", "LITTLE_ENDIAN", "Ljava/nio/ByteOrder;", c.vm.NewIntrinsicInstance("Ljava/nio/ByteOrder;"));
        return VmValue::Void();
    });
    b.StaticMethod("nativeOrder", "()Ljava/nio/ByteOrder;", [](IntrinsicContext& c) {
        const auto& cls = c.vm.Linker().Class(c.vm.Linker().ResolveDescriptor("Ljava/nio/ByteOrder;"));
        const auto name = std::endian::native == std::endian::little ? "LITTLE_ENDIAN" : "BIG_ENDIAN";
        for (const auto id : cls.own_static_fields) {
            const auto& field = c.vm.Linker().Field(id);
            if (field.name == name) return VmValue::Ref(VmObjectRef(cls.static_storage[field.slot]));
        }
        return VmValue::Ref(VmObjectRef(0));
    });
    return std::move(b).Build();
}

VmObjectRef OrderObject(IntrinsicContext& c, bool little) {
    const auto& cls = c.vm.Linker().Class(c.vm.Linker().ResolveDescriptor("Ljava/nio/ByteOrder;"));
    const auto name = little ? "LITTLE_ENDIAN" : "BIG_ENDIAN";
    for (const auto id : cls.own_static_fields) {
        const auto& f = c.vm.Linker().Field(id);
        if (f.name == name) return VmObjectRef(cls.static_storage[f.slot]);
    }
    return VmObjectRef(0);
}

IntrinsicClassDecl DeclareByteBuffer() {
    auto b = IntrinsicClassBuilder::Class("Ljava/nio/ByteBuffer;", "Ljava/nio/Buffer;",
                                          {"Ljava/lang/Comparable;"},
                                          kAccPublic | kAccAbstract);
    b.StaticMethod("allocate", "(I)Ljava/nio/ByteBuffer;", Allocate("Ljava/nio/HeapByteBuffer;", NioElementKind::byte));
    b.StaticMethod("allocateDirect", "(I)Ljava/nio/ByteBuffer;", [](IntrinsicContext& c) {
        const auto object = c.vm.NewIntrinsicInstance("Ljava/nio/DirectByteBuffer;");
        c.vm.NIO().CreateDirect(Id(c.vm, object), c.arguments[0].AsInt());
        return VmValue::Ref(object);
    });
    b.StaticMethod("wrap", "([B)Ljava/nio/ByteBuffer;", Wrap("Ljava/nio/HeapByteBuffer;", NioElementKind::byte, false));
    b.StaticMethod("wrap", "([BII)Ljava/nio/ByteBuffer;", Wrap("Ljava/nio/HeapByteBuffer;", NioElementKind::byte, true));
    b.VirtualMethod("get", "()B", Get(NioElementKind::byte, false));
    b.VirtualMethod("get", "(I)B", Get(NioElementKind::byte, true));
    b.VirtualMethod("put", "(B)Ljava/nio/ByteBuffer;", Put(NioElementKind::byte, false));
    b.VirtualMethod("put", "(IB)Ljava/nio/ByteBuffer;", Put(NioElementKind::byte, true));
    b.VirtualMethod("get", "([B)Ljava/nio/ByteBuffer;", BulkGet(NioElementKind::byte, false));
    b.VirtualMethod("get", "([BII)Ljava/nio/ByteBuffer;", BulkGet(NioElementKind::byte, true));
    b.VirtualMethod("put", "([B)Ljava/nio/ByteBuffer;", BulkPut(NioElementKind::byte, false));
    b.VirtualMethod("put", "([BII)Ljava/nio/ByteBuffer;", BulkPut(NioElementKind::byte, true));
    b.VirtualMethod("array", "()[B", [](IntrinsicContext& c) {
        const auto s = c.vm.NIO().Snapshot(Id(c.vm, c.receiver));
        if (s.read_only) throw VmJavaThrow{"Ljava/nio/ReadOnlyBufferException;", "read-only buffer"};
        if (!s.array.IsValid()) throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "buffer has no array"};
        return VmValue::Ref(s.array);
    });
    b.VirtualMethod("put", "(Ljava/nio/ByteBuffer;)Ljava/nio/ByteBuffer;", [](IntrinsicContext& c) {
        c.vm.NIO().Copy(Id(c.vm, c.receiver), Id(c.vm, c.arguments[0].ref)); return Self(c);
    });
    b.VirtualMethod("order", "()Ljava/nio/ByteOrder;", [](IntrinsicContext& c) {
        return VmValue::Ref(OrderObject(c, c.vm.NIO().Snapshot(Id(c.vm, c.receiver)).order == NioByteOrder::little_endian));
    });
    b.VirtualMethod("order", "(Ljava/nio/ByteOrder;)Ljava/nio/ByteBuffer;", [](IntrinsicContext& c) {
        c.vm.NIO().SetOrder(Id(c.vm, c.receiver), c.arguments[0].ref == OrderObject(c, true)
            ? NioByteOrder::little_endian : NioByteOrder::big_endian); return Self(c);
    });
    Views(b, "Ljava/nio/ByteBuffer;", NioElementKind::byte);
    struct Scalar { const char* name; const char* sig; NioElementKind kind; };
    constexpr std::array scalars{
        Scalar{"Char", "C", NioElementKind::character}, Scalar{"Short", "S", NioElementKind::short_value},
        Scalar{"Int", "I", NioElementKind::int_value}, Scalar{"Long", "J", NioElementKind::long_value},
        Scalar{"Float", "F", NioElementKind::float_value}, Scalar{"Double", "D", NioElementKind::double_value}};
    for (const auto& s : scalars) {
        b.VirtualMethod(std::string("get") + s.name, std::string("()") + s.sig,
            [kind = s.kind](IntrinsicContext& c) { return BitsValue(c.vm.NIO().GetScalar(Id(c.vm, c.receiver), kind, {}), kind); });
        b.VirtualMethod(std::string("get") + s.name, std::string("(I)") + s.sig,
            [kind = s.kind](IntrinsicContext& c) { return BitsValue(c.vm.NIO().GetScalar(Id(c.vm, c.receiver), kind, c.arguments[0].AsInt()), kind); });
        b.VirtualMethod(std::string("put") + s.name, std::string("(") + s.sig + ")Ljava/nio/ByteBuffer;",
            [kind = s.kind](IntrinsicContext& c) { c.vm.NIO().PutScalar(Id(c.vm, c.receiver), kind, {}, ValueBits(c.arguments[0], kind)); return Self(c); });
        b.VirtualMethod(std::string("put") + s.name, std::string("(I") + s.sig + ")Ljava/nio/ByteBuffer;",
            [kind = s.kind](IntrinsicContext& c) { c.vm.NIO().PutScalar(Id(c.vm, c.receiver), kind, c.arguments[0].AsInt(), ValueBits(c.arguments[1], kind)); return Self(c); });
    }
    struct TV { const char* name; NioElementKind kind; };
    constexpr std::array views{TV{"Short", NioElementKind::short_value}, TV{"Int", NioElementKind::int_value},
        TV{"Float", NioElementKind::float_value}, TV{"Char", NioElementKind::character},
        TV{"Long", NioElementKind::long_value}, TV{"Double", NioElementKind::double_value}};
    for (const auto& v : views) {
        b.VirtualMethod(std::string("as") + v.name + "Buffer",
            std::string("()Ljava/nio/") + v.name + "Buffer;",
            View(std::string("Ljava/nio/ByteBufferAs") + v.name + "Buffer;", v.kind, true, false));
    }
    return std::move(b).Build();
}

IntrinsicClassDecl TypedBuffer(const std::string& name, const std::string& array,
                               const std::string& value, NioElementKind kind) {
    const auto type = "Ljava/nio/" + name + "Buffer;";
    const auto concrete = "Ljava/nio/" + name + "ArrayBuffer;";
    auto b = IntrinsicClassBuilder::Class(type, "Ljava/nio/Buffer;",
                                          {"Ljava/lang/Comparable;"},
                                          kAccPublic | kAccAbstract);
    b.StaticMethod("allocate", "(I)" + type, Allocate(concrete, kind));
    b.StaticMethod("wrap", "(" + array + ")" + type, Wrap(concrete, kind, false));
    b.StaticMethod("wrap", "(" + array + "II)" + type, Wrap(concrete, kind, true));
    b.VirtualMethod("get", "()" + value, Get(kind, false));
    b.VirtualMethod("get", "(I)" + value, Get(kind, true));
    b.VirtualMethod("put", "(" + value + ")" + type, Put(kind, false));
    b.VirtualMethod("put", "(I" + value + ")" + type, Put(kind, true));
    b.VirtualMethod("get", "(" + array + ")" + type, BulkGet(kind, false));
    b.VirtualMethod("get", "(" + array + "II)" + type, BulkGet(kind, true));
    b.VirtualMethod("put", "(" + array + ")" + type, BulkPut(kind, false));
    b.VirtualMethod("put", "(" + array + "II)" + type, BulkPut(kind, true));
    b.VirtualMethod("array", "()" + array, [](IntrinsicContext& c) {
        const auto s = c.vm.NIO().Snapshot(Id(c.vm, c.receiver));
        if (s.read_only) throw VmJavaThrow{"Ljava/nio/ReadOnlyBufferException;", "read-only buffer"};
        if (!s.array.IsValid()) throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "buffer has no array"};
        return VmValue::Ref(s.array);
    });
    b.VirtualMethod("order", "()Ljava/nio/ByteOrder;", [](IntrinsicContext& c) {
        return VmValue::Ref(OrderObject(c, c.vm.NIO().Snapshot(Id(c.vm, c.receiver)).order == NioByteOrder::little_endian));
    });
    b.VirtualMethod("put", "(" + type + ")" + type, [](IntrinsicContext& c) {
        c.vm.NIO().Copy(Id(c.vm, c.receiver), Id(c.vm, c.arguments[0].ref)); return Self(c);
    });
    Views(b, type, kind);
    return std::move(b).Build();
}

IntrinsicClassDecl Plain(const std::string& descriptor, const std::string& parent) {
    return std::move(
               IntrinsicClassBuilder::Class(descriptor, parent, {}, kAccNone))
        .Build();
}
IntrinsicClassDecl Exception(const std::string& descriptor, const std::string& parent) {
    auto b = IntrinsicClassBuilder::Class(descriptor, parent);
    b.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); });
    return std::move(b).Build();
}
IntrinsicClassDecl Charset() {
    auto b = IntrinsicClassBuilder::Class("Ljava/nio/charset/Charset;", "Ljava/lang/Object;", {"Ljava/lang/Comparable;"});
    const auto name = b.BoundInstanceField("canonicalName", "Ljava/lang/String;", kAccPrivate | kAccFinal);
    const auto make = [name](IntrinsicContext& c, std::string text) {
        text = CanonicalCharset(std::move(text));
        const auto object = c.vm.NewIntrinsicInstance("Ljava/nio/charset/Charset;");
        const std::array refs{object}; const auto roots = c.vm.ProtectReferences(refs);
        IntrinsicCall(c).SetRef(name, object, c.vm.NewStringUtf8(text));
        return VmValue::Ref(object);
    };
    b.StaticMethod("forName", "(Ljava/lang/String;)Ljava/nio/charset/Charset;", [make](IntrinsicContext& c) {
        if (!c.arguments[0].ref.IsValid()) throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "charsetName == null"};
        return make(c, c.vm.StringUtf8(c.arguments[0].ref));
    });
    b.StaticMethod("defaultCharset", "()Ljava/nio/charset/Charset;", [make](IntrinsicContext& c) { return make(c, "UTF-8"); });
    b.StaticMethod("isSupported", "(Ljava/lang/String;)Z", [](IntrinsicContext& c) {
        if (!c.arguments[0].ref.IsValid()) throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "charsetName == null"};
        try { static_cast<void>(CanonicalCharset(c.vm.StringUtf8(c.arguments[0].ref))); return VmValue::Int(1); }
        catch (const VmJavaThrow& e) { if (e.descriptor == "Ljava/nio/charset/UnsupportedCharsetException;") return VmValue::Int(0); throw; }
    });
    const auto compare = [name](IntrinsicContext& c) {
        const auto other = c.arguments[0].ref;
        if (!other.IsValid()) throw VmJavaThrow{"Ljava/lang/NullPointerException;", "charset == null"};
        if (c.vm.Model().ObjectClass(other) != c.vm.Model().ObjectClass(c.receiver))
            throw VmJavaThrow{"Ljava/lang/ClassCastException;", "expected Charset"};
        return detail::InvokeGuest(c.vm, IntrinsicCall(c).GetRef(name), "compareToIgnoreCase",
            "(Ljava/lang/String;)I", {VmValue::Ref(IntrinsicCall(c).GetRef(name, other))});
    };
    b.FinalMethod("compareTo", "(Ljava/nio/charset/Charset;)I", compare);
    b.VirtualMethod("compareTo", "(Ljava/lang/Object;)I", compare, kAccPublic | kAccBridge | kAccSynthetic);
    b.FinalMethod("name", "()Ljava/lang/String;", [name](IntrinsicContext& c) { return VmValue::Ref(IntrinsicCall(c).GetRef(name)); });
    b.FinalMethod("displayName", "()Ljava/lang/String;", [name](IntrinsicContext& c) {
        return VmValue::Ref(IntrinsicCall(c).GetRef(name));
    });
    b.OverrideMethod("toString", "()Ljava/lang/String;", [name](IntrinsicContext& c) { return VmValue::Ref(IntrinsicCall(c).GetRef(name)); });
    b.OverrideMethod("hashCode", "()I", [name](IntrinsicContext& c) { return detail::InvokeGuest(c.vm, IntrinsicCall(c).GetRef(name), "hashCode", "()I"); });
    b.OverrideMethod("equals", "(Ljava/lang/Object;)Z", [name](IntrinsicContext& c) {
        const auto other = c.arguments[0].ref;
        if (!other.IsValid() || c.vm.Model().ObjectClass(other) != c.vm.Model().ObjectClass(c.receiver)) return VmValue::Int(0);
        return VmValue::Int(c.vm.StringUtf8(IntrinsicCall(c).GetRef(name)) == c.vm.StringUtf8(IntrinsicCall(c).GetRef(name, other)));
    });
    return std::move(b).Build();
}

}  // namespace

std::string CanonicalCharset(std::string name) {
    const auto original = name;
    if (name.empty()) throw VmJavaThrow{"Ljava/nio/charset/IllegalCharsetNameException;", name};
    for (std::size_t i = 0; i < name.size(); ++i) {
        auto& c = name[i];
        const bool alnum = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (!alnum && (i == 0 || (c != '-' && c != '+' && c != ':' && c != '_' && c != '.')))
            throw VmJavaThrow{"Ljava/nio/charset/IllegalCharsetNameException;", original};
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    if (name == "UTF8" || name == "UTF-8") return "UTF-8";
    if (name == "UTF16" || name == "UTF-16") return "UTF-16";
    if (name == "UTF16BE" || name == "UTF-16BE") return "UTF-16BE";
    if (name == "UTF16LE" || name == "UTF-16LE") return "UTF-16LE";
    if (name == "ASCII" || name == "US-ASCII" || name == "ISO646-US") return "US-ASCII";
    if (name == "ISO-8859-1" || name == "ISO_8859-1" || name == "ISO8859_1" || name == "LATIN1") return "ISO-8859-1";
    throw VmJavaThrow{"Ljava/nio/charset/UnsupportedCharsetException;", original};
}
std::string CharsetName(Interpreter& vm, VmObjectRef charset) {
    return CanonicalCharset(vm.StringUtf8(detail::InvokeGuest(vm, charset, "name", "()Ljava/lang/String;").ref));
}
std::u16string DecodeCharset(std::span<const std::byte> bytes, const std::string& charset) {
    if (charset == "UTF-8") return detail::Utf8DecodeReplace(bytes);
    std::u16string result;
    if (charset == "US-ASCII" || charset == "ISO-8859-1") {
        result.reserve(bytes.size());
        for (const auto byte : bytes) {
            const auto value = static_cast<std::uint8_t>(byte);
            result.push_back(charset == "US-ASCII" && value > 0x7fU
                                 ? u'\ufffd' : static_cast<char16_t>(value));
        }
        return result;
    }
    bool big = charset != "UTF-16LE";
    std::size_t offset{};
    if (charset == "UTF-16" && bytes.size() >= 2U) {
        const auto first = static_cast<std::uint8_t>(bytes[0]);
        const auto second = static_cast<std::uint8_t>(bytes[1]);
        if (first == 0xffU && second == 0xfeU) { big = false; offset = 2; }
        else if (first == 0xfeU && second == 0xffU) { big = true; offset = 2; }
    }
    for (; offset + 1U < bytes.size(); offset += 2U) {
        const auto a = static_cast<std::uint8_t>(bytes[offset]);
        const auto b = static_cast<std::uint8_t>(bytes[offset + 1U]);
        result.push_back(static_cast<char16_t>(big ? (a << 8U) | b : (b << 8U) | a));
    }
    if (offset != bytes.size()) result.push_back(u'\ufffd');
    return result;
}
std::vector<std::byte> EncodeCharset(std::u16string_view text, const std::string& charset) {
    if (charset == "UTF-8") {
        std::u16string sanitized(text);
        for (std::size_t i = 0; i < sanitized.size(); ++i) {
            const auto unit = sanitized[i];
            if (unit >= 0xd800U && unit <= 0xdbffU &&
                i + 1U < sanitized.size() && sanitized[i + 1U] >= 0xdc00U &&
                sanitized[i + 1U] <= 0xdfffU) {
                ++i;
            } else if (unit >= 0xd800U && unit <= 0xdfffU) {
                sanitized[i] = u'?';
            }
        }
        return detail::Utf8Encode(sanitized);
    }
    std::vector<std::byte> result;
    if (charset == "US-ASCII" || charset == "ISO-8859-1") {
        result.reserve(text.size());
        for (const auto unit : text)
            result.push_back(static_cast<std::byte>(
                unit <= (charset == "US-ASCII" ? 0x7fU : 0xffU) ? unit : '?'));
        return result;
    }
    const bool big = charset != "UTF-16LE";
    if (charset == "UTF-16") { result.push_back(std::byte{0xfe}); result.push_back(std::byte{0xff}); }
    result.reserve(result.size() + text.size() * 2U);
    for (std::size_t i = 0; i < text.size(); ++i) {
        auto unit = text[i];
        const bool pair = unit >= 0xd800U && unit <= 0xdbffU &&
            i + 1U < text.size() && text[i + 1U] >= 0xdc00U &&
            text[i + 1U] <= 0xdfffU;
        if (unit >= 0xd800U && unit <= 0xdfffU && !pair) unit = u'\ufffd';
        result.push_back(static_cast<std::byte>(big ? unit >> 8U : unit & 0xffU));
        result.push_back(static_cast<std::byte>(big ? unit & 0xffU : unit >> 8U));
        if (pair) {
            unit = text[++i];
            result.push_back(static_cast<std::byte>(big ? unit >> 8U : unit & 0xffU));
            result.push_back(static_cast<std::byte>(big ? unit & 0xffU : unit >> 8U));
        }
    }
    return result;
}

IntrinsicClassDecl DeclareMemoryArray() {
    auto b = IntrinsicClassBuilder::Class("Llibcore/io/Memory;");
    for (const auto& [name, type, width] : std::vector<std::tuple<std::string,std::string,int>>{{"Short","S",2},{"Int","I",4},{"Long","J",8}}) {
        const auto range = [](IntrinsicContext& c, int count) {
            if (!c.arguments[0].ref.IsValid()) throw VmJavaThrow{"Ljava/lang/NullPointerException;", "array == null"};
            const auto offset=c.arguments[1].AsInt();
            if (offset<0 || static_cast<std::int64_t>(offset)+count>c.vm.Model().ArrayLength(c.arguments[0].ref)) throw VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;", "memory range"};
            return offset;
        };
        b.StaticMethod("poke"+name,"([BI"+type+"Ljava/nio/ByteOrder;)V",[range,width](IntrinsicContext& c) {
            const auto offset=range(c,width);
            if (!c.arguments[3].ref.IsValid()) throw VmJavaThrow{"Ljava/lang/NullPointerException;", "byte order == null"};
            const bool big=c.arguments[3].ref==OrderObject(c,false);
            const auto bits=width==8?static_cast<std::uint64_t>(c.arguments[2].AsLong()):static_cast<std::uint64_t>(static_cast<std::uint32_t>(c.arguments[2].AsInt()));
            std::vector<std::byte> bytes(static_cast<std::size_t>(width));
            for(int i=0;i<width;++i)bytes[static_cast<std::size_t>(i)]=static_cast<std::byte>(bits>>(static_cast<unsigned>(big?width-i-1:i)*8U));
            c.vm.Model().WriteByteRegion(c.arguments[0].ref,offset,bytes);return VmValue::Void();
        });
        b.StaticMethod("peek"+name,"([BILjava/nio/ByteOrder;)"+type,[range,width](IntrinsicContext& c) {
            const auto offset=range(c,width);
            if (!c.arguments[2].ref.IsValid()) throw VmJavaThrow{"Ljava/lang/NullPointerException;", "byte order == null"};
            const bool big=c.arguments[2].ref==OrderObject(c,false);
            const auto bytes=c.vm.Model().ReadByteRegion(c.arguments[0].ref,offset,width);std::uint64_t bits{};
            for(int i=0;i<width;++i)bits|=static_cast<std::uint64_t>(bytes[static_cast<std::size_t>(i)])<<(static_cast<unsigned>(big?width-i-1:i)*8U);
            if(width==8)return VmValue::Long(std::bit_cast<std::int64_t>(bits));
            if(width==2)return VmValue::Int(std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(bits)));
            return VmValue::Int(std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(bits)));
        });
    }
    return std::move(b).Build();
}

void AppendJavaNio(std::vector<IntrinsicClassDecl>& catalog) {
    catalog.push_back(Exception("Ljava/nio/BufferOverflowException;", "Ljava/lang/RuntimeException;"));
    catalog.push_back(Exception("Ljava/nio/BufferUnderflowException;", "Ljava/lang/RuntimeException;"));
    catalog.push_back(Exception("Ljava/nio/InvalidMarkException;", "Ljava/lang/IllegalStateException;"));
    catalog.push_back(Exception("Ljava/nio/ReadOnlyBufferException;", "Ljava/lang/UnsupportedOperationException;"));
    catalog.push_back(DeclareBuffer());
    catalog.push_back(DeclareByteOrder());
    catalog.push_back(DeclareByteBuffer());
    catalog.push_back(Plain("Ljava/nio/MappedByteBuffer;", "Ljava/nio/ByteBuffer;"));
    catalog.push_back(Plain("Ljava/nio/HeapByteBuffer;", "Ljava/nio/ByteBuffer;"));
    auto direct = IntrinsicClassBuilder::Class(
        "Ljava/nio/DirectByteBuffer;", "Ljava/nio/MappedByteBuffer;", {},
        kAccNone);
    direct.Constructor("(JI)V", [](IntrinsicContext& c) {
        c.vm.NIO().WrapDirect(Id(c.vm, c.receiver), memory::GuestAddress(static_cast<std::uint32_t>(c.arguments[0].AsLong())), c.arguments[1].AsInt());
        return VmValue::Void();
    });
    catalog.push_back(std::move(direct).Build());
    const struct T { const char* name; const char* array; const char* value; NioElementKind kind; } types[] = {
        {"Short", "[S", "S", NioElementKind::short_value}, {"Int", "[I", "I", NioElementKind::int_value},
        {"Float", "[F", "F", NioElementKind::float_value}, {"Char", "[C", "C", NioElementKind::character},
        {"Long", "[J", "J", NioElementKind::long_value}, {"Double", "[D", "D", NioElementKind::double_value}};
    for (const auto& t : types) {
        catalog.push_back(TypedBuffer(t.name, t.array, t.value, t.kind));
        catalog.push_back(Plain(std::string("Ljava/nio/") + t.name + "ArrayBuffer;", std::string("Ljava/nio/") + t.name + "Buffer;"));
        catalog.push_back(Plain(std::string("Ljava/nio/ByteBufferAs") + t.name + "Buffer;", std::string("Ljava/nio/") + t.name + "Buffer;"));
    }
    catalog.push_back(Charset());
    catalog.push_back(DeclareMemoryArray());
}

}  // namespace ogplay::runtime::dexvm::intrinsics
