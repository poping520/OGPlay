#include "catalog.h"
#include "shared.h"
#include <array>
#include <bit>
#include <cstring>
#include <limits>
#include "ogplay/runtime/dexvm/nio_buffer_access.h"
#include <optional>
#include <string>
#include <string_view>
#include <tuple>

#include "ogplay/core/text.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/io_runtime.h"
#include "ogplay/runtime/dexvm/guest_memory.h"

namespace ogplay::runtime::dexvm::intrinsics {
namespace {

VmObjectRef FileChannelDescriptor(IntrinsicContext& c, VmObjectRef channel) {
    const auto field =
        c.vm.Linker().FindFieldRecursive(c.vm.Model().ObjectClass(channel), "fd", "Ljava/io/FileDescriptor;");
    if (!field) throw DexVmError{DexVmErrorReason::internal_invariant, "FileChannelImpl.fd is unavailable"};
    return VmObjectRef(c.vm.Model().InstanceSlots(channel)[c.vm.Linker().Field(*field).slot].bits);
}

IntrinsicClassDecl Declare_java_nio_FileChannelImpl_Overrides() {
    auto b = IntrinsicClassBuilder::Class("Ljava/nio/FileChannelImpl;");
    b.MergesBootClass();
    b.VirtualMethod(
        "transferTo", "(JJLjava/nio/channels/WritableByteChannel;)J",
        [](IntrinsicContext& c) {
            const auto position = c.arguments[0].AsLong();
            const auto count = c.arguments[1].AsLong();
            if (position < 0 || count < 0)
                throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "negative transfer range"};
            if (!c.arguments[2].ref.IsValid()) throw VmJavaThrow{"Ljava/lang/NullPointerException;", "target == null"};
            try {
                return VmValue::Long(static_cast<std::int64_t>(c.vm.IO().TransferFile(
                    FileChannelDescriptor(c, c.receiver), static_cast<std::uint64_t>(position),
                    static_cast<std::uint64_t>(count), FileChannelDescriptor(c, c.arguments[2].ref))));
            } catch (const IoRuntimeError& error) {
                throw VmJavaThrow{"Ljava/io/IOException;", error.what()};
            }
        },
        kAccPublic);
    return std::move(b).Build();
}

VmObjectRef PrimitiveArray(Interpreter& vm, NioElementKind kind, int capacity) {
    if (capacity < 0) throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "negative capacity"};
    const char* desc = "[B";
    auto primitive = JniPrimitiveKind::byte;
    switch (kind) {
    case NioElementKind::byte: break;
    case NioElementKind::character:
        desc = "[C";
        primitive = JniPrimitiveKind::character;
        break;
    case NioElementKind::short_value:
        desc = "[S";
        primitive = JniPrimitiveKind::short_integer;
        break;
    case NioElementKind::int_value:
        desc = "[I";
        primitive = JniPrimitiveKind::integer;
        break;
    case NioElementKind::float_value:
        desc = "[F";
        primitive = JniPrimitiveKind::float_value;
        break;
    case NioElementKind::long_value:
        desc = "[J";
        primitive = JniPrimitiveKind::long_integer;
        break;
    case NioElementKind::double_value:
        desc = "[D";
        primitive = JniPrimitiveKind::double_value;
        break;
    }
    return vm.Model().NewPrimitiveArray(vm.Linker().ResolveDescriptor(desc), primitive, capacity);
}

std::uint64_t CharsetGet(Interpreter& vm, VmObjectRef buffer, int index) {
    const auto chars = vm.Linker().IsAssignable(vm.Linker().ResolveDescriptor("Ljava/nio/CharBuffer;"),
                                                vm.Model().ObjectClass(buffer));
    const auto result = detail::InvokeGuest(vm, buffer, "get", chars ? "(I)C" : "(I)B", {VmValue::Int(index)}).AsInt();
    return chars ? static_cast<std::uint16_t>(result) : static_cast<std::uint8_t>(result);
}

void CharsetPut(Interpreter& vm, VmObjectRef buffer, std::uint64_t bits) {
    const auto chars = vm.Linker().IsAssignable(vm.Linker().ResolveDescriptor("Ljava/nio/CharBuffer;"),
                                                vm.Model().ObjectClass(buffer));
    static_cast<void>(detail::InvokeGuest(vm, buffer, "put",
                                          chars ? "(C)Ljava/nio/CharBuffer;" : "(B)Ljava/nio/ByteBuffer;",
                                          {VmValue::Int(static_cast<std::int32_t>(bits))}));
}

void CharsetPosition(Interpreter& vm, VmObjectRef buffer, int position) {
    static_cast<void>(detail::InvokeGuest(vm, buffer, "position", "(I)Ljava/nio/Buffer;", {VmValue::Int(position)}));
}

VmValue CharsetCallResult(Interpreter& vm, const VmCallOutcome& result) {
    if (result.exception.IsValid())
        throw VmJavaThrow{vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                          result.exception};
    return result.value;
}

VmValue CharsetLibraryCall(Interpreter& vm, const char* owner, const char* name, const char* signature,
                           std::vector<VmValue> arguments = {}) {
    const auto type = vm.Linker().ResolveDescriptor(owner);
    const auto method = vm.Linker().FindDirectMethod(type, name, signature);
    if (!method) throw DexVmError(DexVmErrorReason::unresolved_reference, std::string(owner) + "->" + name);
    return CharsetCallResult(vm, vm.Call(*method, arguments));
}

constexpr auto kBuiltinCharset = "Lorg/ogplay/runtime/BuiltinCharset;";
constexpr auto kCharsetEncoder = "Lorg/ogplay/runtime/BuiltinCharsetEncoder;";
constexpr auto kUtf16Decoder = "Lorg/ogplay/runtime/Utf16Decoder;";
constexpr auto kCoderResult = "Ljava/nio/charset/CoderResult;";

VmValue CoderResultConstant(Interpreter& vm, const char* name) {
    const auto type = vm.Linker().ResolveDescriptor(kCoderResult);
    static_cast<void>(CharsetCallResult(vm, vm.EnsureClassInitialized(type)));
    const auto field = vm.Linker().FindFieldRecursive(type, name, kCoderResult);
    if (!field) throw DexVmError(DexVmErrorReason::unresolved_reference, name);
    return VmValue::Ref(VmObjectRef{vm.Linker().Class(type).static_storage[vm.Linker().Field(*field).slot]});
}

IntrinsicClassDecl DeclareBuiltinCharsetEncoder() {
    auto builder = IntrinsicClassBuilder::Class(kCharsetEncoder, "Ljava/nio/charset/CharsetEncoder;");
    const auto encoding = builder.BoundInstanceField("encoding", "Ljava/lang/String;");
    const auto bom_written = builder.BoundInstanceField("bomWritten", "Z");
    builder.Constructor("(Ljava/nio/charset/Charset;Ljava/lang/String;)V", [encoding](IntrinsicContext& c) {
        const auto name = c.vm.StringUtf8(c.arguments[1].ref);
        const bool utf16 = name.starts_with("UTF-16");
        const auto replacement = EncodeCharset(utf16 ? u"\ufffd" : u"?", name == "UTF-16" ? "UTF-16BE" : name);
        const auto array = PrimitiveArray(c.vm, NioElementKind::byte, static_cast<JniSize>(replacement.size()));
        const auto roots = c.vm.ProtectReferences(std::array{array});
        c.vm.Model().WriteByteRegion(array, 0, replacement);
        IntrinsicCall(c).SetRef(encoding, c.arguments[1].ref);
        return CharsetLibraryCall(c.vm, "Ljava/nio/charset/CharsetEncoder;", "<init>",
                                  "(Ljava/nio/charset/Charset;FF[BZ)V",
                                  {VmValue::Ref(c.receiver), c.arguments[0], VmValue::Float(utf16 ? 2 : 1),
                                   VmValue::Float(utf16             ? 4
                                                  : name == "UTF-8" ? 3
                                                                    : 1),
                                   VmValue::Ref(array), VmValue::Int(1)});
    });
    builder.OverrideMethod(
        "implReset", "()V",
        [bom_written](IntrinsicContext& c) {
            IntrinsicCall(c).SetInt(bom_written, 0);
            return VmValue::Void();
        },
        kAccProtected);
    builder.OverrideMethod("isLegalReplacement", "([B)Z", [encoding](IntrinsicContext& call) {
        const auto array = IntrinsicCall(call).NonNullRef(0, "replacement");
        const auto bytes = call.vm.Model().ReadByteRegion(array, 0, call.vm.Model().ArrayLength(array));
        const auto name = call.vm.StringUtf8(IntrinsicCall(call).GetRef(encoding));
        if (name.starts_with("UTF-16")) {
            if ((bytes.size() & 1U) != 0U) return VmValue::Int(0);
            const auto decoded = DecodeCharset(bytes, name);
            return VmValue::Int(core::Utf16ToUtf8(decoded, core::InvalidUtf16Policy::reject).has_value());
        }
        if (name == "US-ASCII")
            return VmValue::Int(std::ranges::all_of(bytes, [](std::byte byte) { return byte <= std::byte{0x7f}; }));
        const std::string_view replacement(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        return VmValue::Int(name == "ISO-8859-1" || core::IsValidUtf8(replacement));
    });
    builder.OverrideMethod(
        "encodeLoop", "(Ljava/nio/CharBuffer;Ljava/nio/ByteBuffer;)Ljava/nio/charset/CoderResult;",
        [encoding, bom_written](IntrinsicContext& c) {
            auto& vm = c.vm;
            const auto input = c.arguments[0].ref;
            const auto output = c.arguments[1].ref;
            const auto name = vm.StringUtf8(IntrinsicCall(c).GetRef(encoding));
            const auto in = vm.Buffers().Read(input, false);
            const auto out = vm.Buffers().Read(output, false);
            const bool utf16 = name.starts_with("UTF-16");
            auto available = out.limit - out.position;
            if (name == "UTF-16" && in.position < in.limit && !IntrinsicCall(c).GetInt(bom_written)) {
                if (available < 2) return CoderResultConstant(vm, "OVERFLOW");
                CharsetPut(vm, output, 0xfe);
                CharsetPut(vm, output, 0xff);
                available -= 2;
                IntrinsicCall(c).SetInt(bom_written, 1);
            }
            const auto malformed = [&] {
                return CharsetLibraryCall(vm, kCoderResult, "malformedForLength", "(I)Ljava/nio/charset/CoderResult;",
                                          {VmValue::Int(1)});
            };
            for (auto position = in.position; position < in.limit;) {
                const auto lead = static_cast<char16_t>(CharsetGet(vm, input, position));
                char16_t units[]{lead, 0};
                auto width = 1;
                if (lead >= 0xd800 && lead <= 0xdbff) {
                    if (position + 1 == in.limit) break;
                    units[1] = static_cast<char16_t>(CharsetGet(vm, input, position + 1));
                    if (units[1] < 0xdc00 || units[1] > 0xdfff) return malformed();
                    width = 2;
                } else if (lead >= 0xdc00 && lead <= 0xdfff) {
                    return malformed();
                }
                if ((name == "US-ASCII" && lead > 0x7f) || (name == "ISO-8859-1" && lead > 0xff))
                    return CharsetLibraryCall(vm, kCoderResult, "unmappableForLength",
                                              "(I)Ljava/nio/charset/CoderResult;", {VmValue::Int(width)});
                std::string bytes;
                if (utf16) {
                    const bool little = name == "UTF-16LE";
                    for (int i = 0; i < width; ++i) {
                        const auto unit = units[i];
                        bytes.push_back(static_cast<char>(little ? unit & 0xff : unit >> 8));
                        bytes.push_back(static_cast<char>(little ? unit >> 8 : unit & 0xff));
                    }
                } else if (name == "UTF-8") {
                    const auto code_point =
                        width == 1
                            ? static_cast<std::uint32_t>(lead)
                            : 0x10000U + ((static_cast<std::uint32_t>(lead) - 0xd800U) << 10U) + units[1] - 0xdc00U;
                    static_cast<void>(core::AppendUtf8(bytes, code_point));
                } else {
                    bytes.push_back(static_cast<char>(lead));
                }
                if (static_cast<JniSize>(bytes.size()) > available) return CoderResultConstant(vm, "OVERFLOW");
                for (const auto byte : bytes)
                    CharsetPut(vm, output, static_cast<std::uint8_t>(byte));
                available -= static_cast<JniSize>(bytes.size());
                position += width;
                CharsetPosition(vm, input, position);
            }
            return CoderResultConstant(vm, "UNDERFLOW");
        },
        kAccProtected);
    return std::move(builder).Build();
}

IntrinsicClassDecl DeclareUtf16Decoder() {
    auto builder = IntrinsicClassBuilder::Class(kUtf16Decoder, "Ljava/nio/charset/CharsetDecoder;");
    const auto encoding = builder.BoundInstanceField("encoding", "Ljava/lang/String;");
    const auto byte_order = builder.BoundInstanceField("byteOrder", "I");
    builder.Constructor("(Ljava/nio/charset/Charset;Ljava/lang/String;)V", [encoding](IntrinsicContext& c) {
        IntrinsicCall(c).SetRef(encoding, c.arguments[1].ref);
        return CharsetLibraryCall(c.vm, "Ljava/nio/charset/CharsetDecoder;", "<init>",
                                  "(Ljava/nio/charset/Charset;FF)V",
                                  {VmValue::Ref(c.receiver), c.arguments[0], VmValue::Float(0.5F), VmValue::Float(1)});
    });
    builder.OverrideMethod(
        "implReset", "()V",
        [byte_order](IntrinsicContext& c) {
            IntrinsicCall(c).SetInt(byte_order, 0);
            return VmValue::Void();
        },
        kAccProtected);
    builder.OverrideMethod(
        "decodeLoop", "(Ljava/nio/ByteBuffer;Ljava/nio/CharBuffer;)Ljava/nio/charset/CoderResult;",
        [encoding, byte_order](IntrinsicContext& c) {
            auto& vm = c.vm;
            const auto input = c.arguments[0].ref;
            const auto output = c.arguments[1].ref;
            const auto in = vm.Buffers().Read(input, false);
            const auto out = vm.Buffers().Read(output, false);
            auto position = in.position;
            auto available = out.limit - out.position;
            if (in.limit - position < 2) return CoderResultConstant(vm, "UNDERFLOW");
            const auto unit = [&](JniSize index, bool little) {
                const auto first = CharsetGet(vm, input, index);
                const auto second = CharsetGet(vm, input, index + 1);
                return static_cast<char16_t>(little ? first | (second << 8U) : (first << 8U) | second);
            };
            auto order = IntrinsicCall(c).GetInt(byte_order);
            if (order == 0) {
                const auto name = vm.StringUtf8(IntrinsicCall(c).GetRef(encoding));
                order = name == "UTF-16LE" ? 2 : 1;
                if (name == "UTF-16") {
                    const auto mark = unit(position, false);
                    if (mark == 0xfeff || mark == 0xfffe) {
                        order = mark == 0xfffe ? 2 : 1;
                        position += 2;
                        CharsetPosition(vm, input, position);
                    }
                }
                IntrinsicCall(c).SetInt(byte_order, order);
            }
            const auto malformed = [&] {
                return CharsetLibraryCall(vm, kCoderResult, "malformedForLength", "(I)Ljava/nio/charset/CoderResult;",
                                          {VmValue::Int(2)});
            };
            while (in.limit - position >= 2) {
                const auto lead = unit(position, order == 2);
                char16_t tail = 0;
                auto width = 1;
                if (lead >= 0xd800 && lead <= 0xdbff) {
                    if (in.limit - position < 4) break;
                    tail = unit(position + 2, order == 2);
                    if (tail < 0xdc00 || tail > 0xdfff) return malformed();
                    width = 2;
                } else if (lead >= 0xdc00 && lead <= 0xdfff) {
                    return malformed();
                }
                if (width > available) return CoderResultConstant(vm, "OVERFLOW");
                CharsetPut(vm, output, lead);
                if (width == 2) CharsetPut(vm, output, tail);
                available -= width;
                position += width * 2;
                CharsetPosition(vm, input, position);
            }
            return CoderResultConstant(vm, "UNDERFLOW");
        },
        kAccProtected);
    return std::move(builder).Build();
}

IntrinsicClassDecl DeclareBuiltinCharset() {
    constexpr auto charset = "Ljava/nio/charset/Charset;";
    auto builder = IntrinsicClassBuilder::Class(kBuiltinCharset, charset);
    builder.VirtualMethod("newEncoder", "()Ljava/nio/charset/CharsetEncoder;", [](IntrinsicContext& c) {
        auto& vm = c.vm;
        const auto encoding = detail::InvokeGuest(vm, c.receiver, "name", "()Ljava/lang/String;").ref;
        const auto encoder = vm.NewIntrinsicInstance(kCharsetEncoder);
        const auto roots = vm.ProtectReferences(std::array{encoder, encoding});
        static_cast<void>(
            CharsetLibraryCall(vm, kCharsetEncoder, "<init>", "(Ljava/nio/charset/Charset;Ljava/lang/String;)V",
                               {VmValue::Ref(encoder), VmValue::Ref(c.receiver), VmValue::Ref(encoding)}));
        return VmValue::Ref(encoder);
    });
    builder.VirtualMethod("newDecoder", "()Ljava/nio/charset/CharsetDecoder;", [](IntrinsicContext& c) {
        auto& vm = c.vm;
        const auto encoding = detail::InvokeGuest(vm, c.receiver, "name", "()Ljava/lang/String;").ref;
        if (!vm.StringUtf8(encoding).starts_with("UTF-16")) {
            if (auto* ledger = vm.Ledger()) ledger->RecordUnimplemented("dexvm.charset.new_decoder", 0);
            throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "streaming decoder is unavailable"};
        }
        const auto decoder = vm.NewIntrinsicInstance(kUtf16Decoder);
        const auto roots = vm.ProtectReferences(std::array{decoder, encoding});
        static_cast<void>(
            CharsetLibraryCall(vm, kUtf16Decoder, "<init>", "(Ljava/nio/charset/Charset;Ljava/lang/String;)V",
                               {VmValue::Ref(decoder), VmValue::Ref(c.receiver), VmValue::Ref(encoding)}));
        return VmValue::Ref(decoder);
    });
    builder.VirtualMethod("contains", "(Ljava/nio/charset/Charset;)Z", [](IntrinsicContext& c) {
        if (!c.arguments[0].ref.IsValid()) return VmValue::Int(0);
        const auto self = CharsetName(c.vm, c.receiver);
        const auto other =
            c.vm.StringUtf8(detail::InvokeGuest(c.vm, c.arguments[0].ref, "name", "()Ljava/lang/String;").ref);
        if (self == other || other == "US-ASCII") return VmValue::Int(1);
        if (self == "US-ASCII" || self == "ISO-8859-1") return VmValue::Int(0);
        return VmValue::Int(other == "ISO-8859-1" || other == "UTF-8" || other.starts_with("UTF-16"));
    });
    return std::move(builder).Build();
}

IntrinsicClassDecl Declare_java_nio_charset_Charsets_Natives() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/nio/charset/Charsets;");
    builder.MergesBootClass();
    const auto bounds = [](JavaObjectModel& model, VmObjectRef array, JniSize offset, JniSize length) {
        if (!array.IsValid()) throw VmJavaThrow{"Ljava/lang/NullPointerException;", "array == null"};
        if (offset < 0 || length < 0 || offset > model.ArrayLength(array) - length)
            throw VmJavaThrow{"Ljava/lang/ArrayIndexOutOfBoundsException;", "charset array range"};
    };
    // API19 libcore native java_nio_charset_Charsets.cpp. ASCII decoding
    // substitutes U+FFFD, while single-byte encoders substitute per code unit.
    for (const bool ascii : {false, true}) {
        builder.StaticMethod(
            ascii ? "asciiBytesToChars" : "isoLatin1BytesToChars", "([BII[C)V",
            [bounds, ascii](IntrinsicContext& c) {
                auto& model = c.vm.Model();
                const auto source = c.arguments[0].ref;
                const auto offset = c.arguments[1].AsInt();
                const auto length = c.arguments[2].AsInt();
                const auto target = c.arguments[3].ref;
                bounds(model, source, offset, length);
                bounds(model, target, 0, length);
                for (JniSize i = 0; i < length; ++i) {
                    const auto unit = model.GetPrimitiveElement(source, offset + i);
                    model.SetPrimitiveElement(target, i, ascii && unit > 0x7fU ? 0xfffdU : unit);
                }
                return VmValue::Void();
            },
            kAccPublic | kAccNative);
    }
    for (const auto& [name, encoding] :
         {std::pair{"toAsciiBytes", "US-ASCII"}, std::pair{"toIsoLatin1Bytes", "ISO-8859-1"},
          std::pair{"toUtf8Bytes", "UTF-8"}}) {
        builder.StaticMethod(
            name, "([CII)[B",
            [bounds, encoding](IntrinsicContext& c) {
                auto& model = c.vm.Model();
                const auto source = c.arguments[0].ref;
                const auto offset = c.arguments[1].AsInt();
                const auto length = c.arguments[2].AsInt();
                bounds(model, source, offset, length);
                const auto chars = model.CharArrayChars(source).substr(static_cast<std::size_t>(offset),
                                                                       static_cast<std::size_t>(length));
                const auto bytes = EncodeCharset(chars, encoding);
                if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<JniSize>::max()))
                    throw VmJavaThrow{"Ljava/lang/OutOfMemoryError;", ""};
                const auto target = model.NewPrimitiveArray(c.vm.Linker().ResolveDescriptor("[B"),
                                                            JniPrimitiveKind::byte, static_cast<JniSize>(bytes.size()));
                model.WriteByteRegion(target, 0, bytes);
                return VmValue::Ref(target);
            },
            kAccPublic | kAccNative);
    }
    return std::move(builder).Build();
}

IntrinsicClassDecl Declare_java_nio_charset_Charset_Class() {
    auto b =
        IntrinsicClassBuilder::Class("Ljava/nio/charset/Charset;", "Ljava/lang/Object;", {"Ljava/lang/Comparable;"});
    b.MergesBootClass();
    const auto name = b.BoundInstanceField("canonicalName", "Ljava/lang/String;", kAccPrivate | kAccFinal);
    const auto make = [](IntrinsicContext& c, std::string text) {
        text = CanonicalCharset(std::move(text));
        const auto object = c.vm.NewIntrinsicInstance(kBuiltinCharset);
        const auto roots = c.vm.ProtectReferences(std::array{object});
        const auto canonical = c.vm.NewStringUtf8(text);
        static_cast<void>(
            CharsetLibraryCall(c.vm, "Ljava/nio/charset/Charset;", "<init>", "(Ljava/lang/String;[Ljava/lang/String;)V",
                               {VmValue::Ref(object), VmValue::Ref(canonical), VmValue::Ref(VmObjectRef{})}));
        return VmValue::Ref(object);
    };
    b.StaticMethod("forName", "(Ljava/lang/String;)Ljava/nio/charset/Charset;", [make](IntrinsicContext& c) {
        if (!c.arguments[0].ref.IsValid())
            throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "charsetName == null"};
        return make(c, c.vm.StringUtf8(c.arguments[0].ref));
    });
    b.StaticMethod("availableCharsets", "()Ljava/util/SortedMap;", [make](IntrinsicContext& c) {
        auto& vm = c.vm;
        const auto names =
            CharsetLibraryCall(vm, "Llibcore/icu/NativeConverter;", "getAvailableCharsetNames", "()[Ljava/lang/String;")
                .ref;
        const auto names_root = vm.ProtectReferences(std::array{names});
        const auto string_type = vm.Linker().ResolveDescriptor("Ljava/lang/String;");
        static_cast<void>(CharsetCallResult(vm, vm.EnsureClassInitialized(string_type)));
        const auto comparator_field =
            vm.Linker().FindFieldRecursive(string_type, "CASE_INSENSITIVE_ORDER", "Ljava/util/Comparator;");
        if (!comparator_field) throw DexVmError(DexVmErrorReason::unresolved_reference, "String comparator field");
        const auto comparator =
            VmObjectRef{vm.Linker().Class(string_type).static_storage[vm.Linker().Field(*comparator_field).slot]};
        const auto map = vm.NewIntrinsicInstance("Ljava/util/TreeMap;");
        const auto map_root = vm.ProtectReferences(std::array{map});
        const auto map_ctor =
            vm.Linker().FindDirectMethod(vm.Model().ObjectClass(map), "<init>", "(Ljava/util/Comparator;)V");
        if (!map_ctor) throw DexVmError(DexVmErrorReason::unresolved_reference, "charset map constructor");
        CharsetCallResult(vm, vm.Call(*map_ctor, std::array{VmValue::Ref(map), VmValue::Ref(comparator)}));
        for (JniSize i = 0; i < vm.Model().ArrayLength(names); ++i) {
            const auto raw = vm.Model().GetObjectElement(names, i);
            std::string canonical;
            try {
                canonical = CanonicalCharset(vm.StringUtf8(raw));
            } catch (const VmJavaThrow& error) {
                if (error.descriptor == "Ljava/nio/charset/UnsupportedCharsetException;" ||
                    error.descriptor == "Ljava/nio/charset/IllegalCharsetNameException;")
                    continue;
                throw;
            }
            // Publish only codecs actually supported by this runtime, not
            // every ICU provider whose conversion protocol is not yet wired.
            const auto charset = make(c, canonical).ref;
            const auto charset_root = vm.ProtectReferences(std::array{charset});
            const auto key = vm.NewStringUtf8(canonical);
            const auto key_root = vm.ProtectReferences(std::array{key});
            static_cast<void>(detail::InvokeGuest(vm, map, "put",
                                                  "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                                                  {VmValue::Ref(key), VmValue::Ref(charset)}));
        }
        return CharsetLibraryCall(vm, "Ljava/util/Collections;", "unmodifiableSortedMap",
                                  "(Ljava/util/SortedMap;)Ljava/util/SortedMap;", {VmValue::Ref(map)});
    });
    b.StaticMethod("defaultCharset", "()Ljava/nio/charset/Charset;",
                   [make](IntrinsicContext& c) { return make(c, "UTF-8"); });
    b.StaticMethod("isSupported", "(Ljava/lang/String;)Z", [](IntrinsicContext& c) {
        if (!c.arguments[0].ref.IsValid())
            throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "charsetName == null"};
        try {
            static_cast<void>(CanonicalCharset(c.vm.StringUtf8(c.arguments[0].ref)));
            return VmValue::Int(1);
        } catch (const VmJavaThrow& e) {
            if (e.descriptor == "Ljava/nio/charset/UnsupportedCharsetException;") return VmValue::Int(0);
            throw;
        }
    });
    const auto compare = [name](IntrinsicContext& c) {
        const auto other = c.arguments[0].ref;
        if (!other.IsValid()) throw VmJavaThrow{"Ljava/lang/NullPointerException;", "charset == null"};
        if (c.vm.Model().ObjectClass(other) != c.vm.Model().ObjectClass(c.receiver))
            throw VmJavaThrow{"Ljava/lang/ClassCastException;", "expected Charset"};
        return detail::InvokeGuest(c.vm, IntrinsicCall(c).GetRef(name), "compareToIgnoreCase", "(Ljava/lang/String;)I",
                                   {VmValue::Ref(IntrinsicCall(c).GetRef(name, other))});
    };
    b.FinalMethod("compareTo", "(Ljava/nio/charset/Charset;)I", compare);
    b.VirtualMethod("compareTo", "(Ljava/lang/Object;)I", compare, kAccPublic | kAccBridge | kAccSynthetic);
    b.FinalMethod("name", "()Ljava/lang/String;",
                  [name](IntrinsicContext& c) { return VmValue::Ref(IntrinsicCall(c).GetRef(name)); });
    b.VirtualMethod("displayName", "()Ljava/lang/String;",
                    [name](IntrinsicContext& c) { return VmValue::Ref(IntrinsicCall(c).GetRef(name)); });
    b.OverrideMethod("toString", "()Ljava/lang/String;",
                     [name](IntrinsicContext& c) { return VmValue::Ref(IntrinsicCall(c).GetRef(name)); });
    b.OverrideMethod("hashCode", "()I", [name](IntrinsicContext& c) {
        return detail::InvokeGuest(c.vm, IntrinsicCall(c).GetRef(name), "hashCode", "()I");
    });
    b.OverrideMethod("equals", "(Ljava/lang/Object;)Z", [name](IntrinsicContext& c) {
        const auto other = c.arguments[0].ref;
        if (!other.IsValid() || c.vm.Model().ObjectClass(other) != c.vm.Model().ObjectClass(c.receiver))
            return VmValue::Int(0);
        return VmValue::Int(c.vm.StringUtf8(IntrinsicCall(c).GetRef(name)) ==
                            c.vm.StringUtf8(IntrinsicCall(c).GetRef(name, other)));
    });
    // AOSP 4.4 ROM line 410.
    b.VirtualMethod(
        "displayName", "(Ljava/util/Locale;)Ljava/lang/String;",
        [name](IntrinsicContext& c) { return VmValue::Ref(IntrinsicCall(c).GetRef(name)); }, kAccPublic);

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
            result.push_back(charset == "US-ASCII" && value > 0x7fU ? u'\ufffd' : static_cast<char16_t>(value));
        }
        return result;
    }
    bool big = charset != "UTF-16LE";
    std::size_t offset{};
    if (charset == "UTF-16" && bytes.size() >= 2U) {
        const auto first = static_cast<std::uint8_t>(bytes[0]);
        const auto second = static_cast<std::uint8_t>(bytes[1]);
        if (first == 0xffU && second == 0xfeU) {
            big = false;
            offset = 2;
        } else if (first == 0xfeU && second == 0xffU) {
            big = true;
            offset = 2;
        }
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
    if (charset == "UTF-8") return detail::Utf8Encode(text);
    std::vector<std::byte> result;
    if (charset == "US-ASCII" || charset == "ISO-8859-1") {
        result.reserve(text.size());
        for (const auto unit : text)
            result.push_back(static_cast<std::byte>(unit <= (charset == "US-ASCII" ? 0x7fU : 0xffU) ? unit : '?'));
        return result;
    }
    const bool big = charset != "UTF-16LE";
    if (charset == "UTF-16") {
        result.push_back(std::byte{0xfe});
        result.push_back(std::byte{0xff});
    }
    result.reserve(result.size() + text.size() * 2U);
    for (std::size_t i = 0; i < text.size(); ++i) {
        auto unit = text[i];
        const bool pair = unit >= 0xd800U && unit <= 0xdbffU && i + 1U < text.size() && text[i + 1U] >= 0xdc00U &&
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

namespace {
std::uint64_t MemorySwap(std::uint64_t value, unsigned width) {
    std::uint64_t result{};
    for (unsigned i = 0; i < width; ++i) {
        result = (result << 8U) | (value & 255U);
        value >>= 8U;
    }
    return result;
}

std::span<std::byte> MemoryArray(Interpreter& vm, VmObjectRef array, std::int64_t offset, std::int64_t count) {
    if (!array.IsValid()) throw VmJavaThrow{"Ljava/lang/NullPointerException;", "array == null"};
    auto bytes = vm.Model().PrimitiveArrayBytes(array);
    if (offset < 0 || count < 0 || static_cast<std::uint64_t>(offset) > bytes.size() ||
        static_cast<std::uint64_t>(count) > bytes.size() - static_cast<std::uint64_t>(offset))
        throw VmJavaThrow{"Ljava/lang/ArrayIndexOutOfBoundsException;", "memory array range"};
    return bytes.subspan(static_cast<std::size_t>(offset), static_cast<std::size_t>(count));
}

std::span<std::byte> MemoryAddress(Interpreter& vm, std::int64_t address, std::int64_t count, bool write) {
    if (address < 0 || address > UINT32_MAX || count < 0 || count > UINT32_MAX ||
        static_cast<std::uint64_t>(count) > UINT64_C(0x100000000) - static_cast<std::uint64_t>(address))
        throw VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;", "guest memory range"};
    if (count == 0) return {};
    return {vm.GuestMemory().Resolve(memory::GuestAddress(static_cast<std::uint32_t>(address)),
                                     static_cast<std::uint32_t>(count), write),
            static_cast<std::size_t>(count)};
}

void MemoryCopy(std::span<std::byte> destination, std::span<const std::byte> source, unsigned width, bool swap) {
    if (!swap || width == 1) {
        if (!source.empty()) std::memmove(destination.data(), source.data(), source.size());
        return;
    }
    for (std::size_t offset = 0; offset + width <= source.size(); offset += width) {
        std::array<std::byte, 8> scalar{};
        std::memcpy(scalar.data(), source.data() + offset, width);
        for (unsigned i = 0; i < width; ++i)
            destination[offset + i] = scalar[width - i - 1];
    }
}

std::span<std::byte> MemoryObject(Interpreter& vm, VmObjectRef object, int offset, std::int64_t count, bool write) {
    if (!object.IsValid()) throw VmJavaThrow{"Ljava/lang/NullPointerException;", "memory object == null"};
    if (vm.Buffers().IsBuffer(object)) {
        const auto address = vm.Buffers().DirectAddress(object);
        return MemoryAddress(vm, static_cast<std::int64_t>(address.Value()) + offset, count, write);
    }
    return MemoryArray(vm, object, offset, count);
}
}  // namespace

IntrinsicClassDecl Declare_libcore_io_Memory_Natives() {
    auto b = IntrinsicClassBuilder::Class("Llibcore/io/Memory;");
    b.MergesBootClass();
    for (const auto& [name, type, width] : std::vector<std::tuple<std::string, std::string, unsigned>>{
             {"Byte", "B", 1}, {"Short", "S", 2}, {"Int", "I", 4}, {"Long", "J", 8}}) {
        b.StaticMethod(
            "peek" + name, "(J" + std::string(width == 1 ? "" : "Z") + ")" + type,
            [width](IntrinsicContext& c) {
                const auto bytes = MemoryAddress(c.vm, c.arguments[0].AsLong(), width, false);
                std::uint64_t bits{};
                std::memcpy(&bits, bytes.data(), width);
                if (width > 1 && c.arguments[1].AsInt()) {
                    bits = MemorySwap(bits, width);
                }
                if (width == 8) return VmValue::Long(std::bit_cast<std::int64_t>(bits));
                if (width == 1) return VmValue::Int(std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(bits)));
                if (width == 2) return VmValue::Int(std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(bits)));
                return VmValue::Int(std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(bits)));
            },
            kAccPublic | kAccNative);
        b.StaticMethod(
            "poke" + name, "(J" + type + (width == 1 ? "" : "Z") + ")V",
            [width](IntrinsicContext& c) {
                auto bytes = MemoryAddress(c.vm, c.arguments[0].AsLong(), width, true);
                auto bits = width == 8 ? static_cast<std::uint64_t>(c.arguments[1].AsLong())
                                       : static_cast<std::uint64_t>(static_cast<std::uint32_t>(c.arguments[1].AsInt()));
                if (width > 1 && c.arguments[2].AsInt()) {
                    bits = MemorySwap(bits, width);
                }
                std::memcpy(bytes.data(), &bits, width);
                return VmValue::Void();
            },
            kAccPublic | kAccNative);
    }
    for (const auto& [name, type, width] :
         std::vector<std::tuple<std::string, std::string, unsigned>>{{"Byte", "B", 1},
                                                                     {"Char", "C", 2},
                                                                     {"Short", "S", 2},
                                                                     {"Int", "I", 4},
                                                                     {"Float", "F", 4},
                                                                     {"Long", "J", 8},
                                                                     {"Double", "D", 8}}) {
        for (const bool poke : {false, true}) {
            b.StaticMethod(
                std::string(poke ? "poke" : "peek") + name + "Array",
                "(J[" + type + "II" + (width == 1 ? "" : "Z") + ")V",
                [width, poke](IntrinsicContext& c) {
                    const auto offset = static_cast<std::int64_t>(c.arguments[2].AsInt()) * width;
                    const auto count = static_cast<std::int64_t>(c.arguments[3].AsInt()) * width;
                    auto array = MemoryArray(c.vm, c.arguments[1].ref, offset, count);
                    auto memory = MemoryAddress(c.vm, c.arguments[0].AsLong(), count, !poke ? false : true);
                    const bool swap = width > 1 && c.arguments[4].AsInt();
                    if (poke)
                        MemoryCopy(memory, array, width, swap);
                    else
                        MemoryCopy(array, memory, width, swap);
                    return VmValue::Void();
                },
                kAccPublic | kAccNative);
        }
    }
    b.StaticMethod(
        "memmove", "(Ljava/lang/Object;ILjava/lang/Object;IJ)V",
        [](IntrinsicContext& c) {
            const auto count = c.arguments[4].AsLong();
            auto dst = MemoryObject(c.vm, c.arguments[0].ref, c.arguments[1].AsInt(), count, true);
            auto src = MemoryObject(c.vm, c.arguments[2].ref, c.arguments[3].AsInt(), count, false);
            MemoryCopy(dst, src, 1, false);
            return VmValue::Void();
        },
        kAccPublic | kAccNative);
    for (const bool put : {false, true}) {
        b.StaticMethod(
            put ? "unsafeBulkPut" : "unsafeBulkGet",
            put ? "([BIILjava/lang/Object;IIZ)V" : "(Ljava/lang/Object;II[BIIZ)V",
            [put](IntrinsicContext& c) {
                const auto width = c.arguments[5].AsInt();
                const auto count = c.arguments[2].AsInt();
                const bool swap = c.arguments[6].AsInt() != 0;
                auto dst = MemoryArray(c.vm, c.arguments[0].ref,
                                       static_cast<std::int64_t>(c.arguments[1].AsInt()) * (put ? 1 : width), count);
                auto src = MemoryArray(c.vm, c.arguments[3].ref,
                                       static_cast<std::int64_t>(c.arguments[4].AsInt()) * (put ? width : 1), count);
                if (!swap || width == 2 || width == 4 || width == 8)
                    MemoryCopy(dst, src, static_cast<unsigned>(width), swap);
                return VmValue::Void();
            },
            kAccPublic | kAccNative);
    }
    return std::move(b).Build();
}

void AppendJavaNio(std::vector<IntrinsicClassDecl>& catalog, bool guest_icu_converters) {
    auto order = IntrinsicClassBuilder::Class("Ljava/nio/ByteOrder;");
    order.MergesBootClass();
    order.StaticMethod(
        "isLittleEndian", "()Z", [](IntrinsicContext&) { return VmValue::Int(1); }, kAccPrivate | kAccNative);
    catalog.push_back(std::move(order).Build());
    if (!guest_icu_converters) {
        catalog.push_back(Declare_java_nio_charset_Charset_Class());
        catalog.push_back(DeclareBuiltinCharset());
        catalog.push_back(DeclareBuiltinCharsetEncoder());
        catalog.push_back(DeclareUtf16Decoder());
    }
    catalog.push_back(Declare_java_nio_charset_Charsets_Natives());
    catalog.push_back(Declare_libcore_io_Memory_Natives());
    catalog.push_back(Declare_java_nio_FileChannelImpl_Overrides());
}
}  // namespace ogplay::runtime::dexvm::intrinsics
