#include "catalog.h"

#include <array>
#include <bit>
#include <optional>
#include <string>

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
                                          "Ljava/lang/Object;", {}, 0x0401U);
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
IntrinsicHandler BulkGet(NioElementKind kind, bool range) {
    return [kind, range](IntrinsicContext& c) {
        const auto array = c.arguments[0].ref;
        const auto array_length = c.vm.Model().ArrayLength(array);
        const auto offset = range ? c.arguments[1].AsInt() : 0;
        const auto count = range ? c.arguments[2].AsInt() : array_length;
        const auto state = c.vm.NIO().Snapshot(Id(c.vm, c.receiver));
        if (offset < 0 || count < 0 || offset > array_length - count)
            throw VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;", "invalid bulk range"};
        if (count > state.limit - state.position)
            throw VmJavaThrow{"Ljava/nio/BufferUnderflowException;", "insufficient remaining"};
        for (int index = 0; index < count; ++index)
            c.vm.Model().SetPrimitiveElement(array, offset + index,
                c.vm.NIO().Get(Id(c.vm, c.receiver), {}));
        return Self(c);
    };
}
IntrinsicHandler BulkPut(NioElementKind kind, bool range) {
    return [kind, range](IntrinsicContext& c) {
        const auto array = c.arguments[0].ref;
        const auto array_length = c.vm.Model().ArrayLength(array);
        const auto offset = range ? c.arguments[1].AsInt() : 0;
        const auto count = range ? c.arguments[2].AsInt() : array_length;
        const auto state = c.vm.NIO().Snapshot(Id(c.vm, c.receiver));
        if (offset < 0 || count < 0 || offset > array_length - count)
            throw VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;", "invalid bulk range"};
        if (count > state.limit - state.position)
            throw VmJavaThrow{"Ljava/nio/BufferOverflowException;", "insufficient remaining"};
        for (int index = 0; index < count; ++index)
            c.vm.NIO().Put(Id(c.vm, c.receiver), {},
                c.vm.Model().GetPrimitiveElement(array, offset + index));
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
    auto b = IntrinsicClassBuilder::Class("Ljava/nio/ByteOrder;", "Ljava/lang/Object;", {}, 0x0011U);
    b.StaticField("BIG_ENDIAN", "Ljava/nio/ByteOrder;", 0x0019U);
    b.StaticField("LITTLE_ENDIAN", "Ljava/nio/ByteOrder;", 0x0019U);
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
                                          {"Ljava/lang/Comparable;"}, 0x0401U);
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
                                          {"Ljava/lang/Comparable;"}, 0x0401U);
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
    return std::move(IntrinsicClassBuilder::Class(descriptor, parent, {}, 0x0000U)).Build();
}
IntrinsicClassDecl Exception(const std::string& descriptor, const std::string& parent) {
    auto b = IntrinsicClassBuilder::Class(descriptor, parent);
    b.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); });
    return std::move(b).Build();
}
IntrinsicClassDecl Charset() {
    auto b = IntrinsicClassBuilder::Class("Ljava/nio/charset/Charset;", "Ljava/lang/Object;");
    b.StaticMethod("forName", "(Ljava/lang/String;)Ljava/nio/charset/Charset;", [](IntrinsicContext& c) {
        return VmValue::Ref(c.vm.NewIntrinsicInstance("Ljava/nio/charset/Charset;"));
    });
    return std::move(b).Build();
}

}  // namespace

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
    auto direct = IntrinsicClassBuilder::Class("Ljava/nio/DirectByteBuffer;", "Ljava/nio/MappedByteBuffer;", {}, 0x0000U);
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
}

}  // namespace ogplay::runtime::dexvm::intrinsics
