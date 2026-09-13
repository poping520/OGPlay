// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from java_io_EOFException.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
    using namespace detail;

    IntrinsicClassDecl Declare_java_io_EOFException() {
        return DeclareSimpleThrowable("Ljava/io/EOFException;", "Ljava/io/IOException;");
    }
} // namespace ogplay::runtime::dexvm::intrinsics


// ---- migrated from java_io_FileNotFoundException.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
    using namespace detail;

    IntrinsicClassDecl Declare_java_io_FileNotFoundException() {
        return DeclareSimpleThrowable("Ljava/io/FileNotFoundException;", "Ljava/io/IOException;");
    }
} // namespace ogplay::runtime::dexvm::intrinsics


// ---- migrated from java_io_files.cpp ----
#include "catalog.h"
#include "shared.h"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/io_runtime.h"
#include "ogplay/runtime/dexvm/reflection.h"

namespace ogplay::runtime::dexvm::intrinsics {

    void AppendJavaIoFiles(std::vector<IntrinsicClassDecl>& catalog) {
        auto block_guard = IntrinsicClassBuilder::Class(
            "Ldalvik/system/BlockGuard;");
        block_guard.StaticMethod(
            "getThreadPolicy", "()Ldalvik/system/BlockGuard$Policy;",
            [](IntrinsicContext& call) {
                const auto type = call.vm.Linker().ResolveDescriptor(
                    "Ldalvik/system/BlockGuard;");
                const auto field = call.vm.Linker().FindFieldRecursive(
                    type, "LAX_POLICY", "Ldalvik/system/BlockGuard$Policy;");
                if (!field) throw DexVmError{
                    DexVmErrorReason::internal_invariant,
                    "BlockGuard.LAX_POLICY is unavailable"};
                const auto& linked = call.vm.Linker().Field(*field);
                return VmValue::Ref(VmObjectRef(
                    call.vm.Linker().Class(linked.owner)
                        .static_storage[linked.slot]));
            }, kAccPublic | kAccStatic);
        catalog.push_back(std::move(block_guard).Build());
        auto file = IntrinsicClassBuilder::Class("Ljava/io/File;");
        file.StaticMethod("listImpl", "(Ljava/lang/String;)[Ljava/lang/String;",
            [](IntrinsicContext& call) {
                const auto entries = call.vm.IO().List(
                    call.vm.StringUtf8(call.arguments[0].ref));
                if (!entries) return VmValue::Ref(VmObjectRef{});
                const auto array = call.vm.Model().NewObjectArray(
                    call.vm.Linker().ResolveDescriptor("[Ljava/lang/String;"),
                    call.vm.Linker().ResolveDescriptor("Ljava/lang/String;"),
                    static_cast<JniSize>(entries->size()));
                const auto root = call.vm.ProtectReferences(std::array{array});
                for (std::size_t i = 0; i < entries->size(); ++i)
                    call.vm.Model().SetObjectElement(
                        array, static_cast<JniSize>(i),
                        call.vm.NewStringUtf8((*entries)[i]));
                return VmValue::Ref(array);
            }, kAccPrivate | kAccStatic | kAccNative);
        for (const auto* name : {"readlink", "realpath"}) {
            file.StaticMethod(name, "(Ljava/lang/String;)Ljava/lang/String;",
                [](IntrinsicContext& call) {
                    return VmValue::Ref(call.arguments[0].ref);
                }, kAccPrivate | kAccStatic | kAccNative);
        }
        file.StaticMethod("setLastModifiedImpl", "(Ljava/lang/String;J)Z",
            [](IntrinsicContext&) { return VmValue::Int(0); },
            kAccPrivate | kAccStatic | kAccNative);
        catalog.push_back(std::move(file).Build());
    }
} // namespace ogplay::runtime::dexvm::intrinsics


// ---- migrated from java_io_IOException.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
    using namespace detail;

} // namespace ogplay::runtime::dexvm::intrinsics


// ---- migrated from java_io_PrintStream.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
    using namespace detail;

    IntrinsicClassDecl Declare_java_io_PrintStream() {
        auto builder = IntrinsicClassBuilder::Class("Ljava/io/PrintStream;", "Ljava/io/OutputStream;", {"Ljava/lang/Appendable;"});
        builder.OverrideMethod("write", "(I)V", [](IntrinsicContext& c) {
            const auto value = static_cast<char>(c.arguments[0].AsInt() & 0xff);
            GuestLine(c, std::string(1, value));
            return VmValue::Void();
        });
        builder.OverrideMethod("write", "([BII)V", [](IntrinsicContext& c) {
            const auto array = c.arguments[0].ref;
            if (!array.IsValid())
                throw VmJavaThrow{"Ljava/lang/NullPointerException;", "buffer == null"};
            const auto offset = c.arguments[1].AsInt();
            const auto count = c.arguments[2].AsInt();
            if (offset < 0 || count < 0 ||
                static_cast<std::int64_t>(offset) + count >
                    c.vm.Model().ArrayLength(array))
                throw VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;", "write range"};
            const auto bytes = c.vm.Model().ReadByteRegion(array, offset, count);
            if (!bytes.empty())
                GuestLine(c, std::string(
                    reinterpret_cast<const char*>(bytes.data()), bytes.size()));
            return VmValue::Void();
        });
        for (const auto* result : {"Ljava/io/PrintStream;", "Ljava/lang/Appendable;"}) {
            builder.VirtualMethod("append", std::string("(Ljava/lang/CharSequence;)") + result,
                [](IntrinsicContext& c) {
                    const auto ref = c.arguments[0].ref;
                    const auto text = ref.IsValid() ? InvokeGuest(c.vm, ref, "toString", "()Ljava/lang/String;").ref : c.vm.NewStringUtf8("null");
                    const auto bytes = c.vm.StringUtf8(text);
                    if (!bytes.empty()) GuestLine(c, bytes);
                    return VmValue::Ref(c.receiver);
                });
            builder.VirtualMethod("append", std::string("(C)") + result,
                [](IntrinsicContext& c) {
                    const auto text = c.vm.Model().NewString(std::u16string(1, static_cast<char16_t>(c.arguments[0].AsInt())));
                    const auto bytes = c.vm.StringUtf8(text);
                    if (!bytes.empty()) GuestLine(c, bytes);
                    return VmValue::Ref(c.receiver);
                });
            builder.VirtualMethod("append", std::string("(Ljava/lang/CharSequence;II)") + result,
                [](IntrinsicContext& c) {
                    const auto ref = c.arguments[0].ref.IsValid() ? c.arguments[0].ref : c.vm.NewStringUtf8("null");
                    const auto part = InvokeGuest(c.vm, ref, "subSequence", "(II)Ljava/lang/CharSequence;",
                        {c.arguments[1], c.arguments[2]}).ref;
                    const auto text = InvokeGuest(c.vm, part, "toString", "()Ljava/lang/String;").ref;
                    const auto bytes = c.vm.StringUtf8(text);
                    if (!bytes.empty()) GuestLine(c, bytes);
                    return VmValue::Ref(c.receiver);
                });
        }
        builder.FinalMethod("println", "(Ljava/lang/String;)V",
                            [](IntrinsicContext& context) {
                                const auto argument = context.arguments[0].ref;
                                GuestLine(context, argument.IsValid()
                                                       ? Narrow(Value(context, argument))
                                                       : std::string("null"));
                                return VmValue::Void();
                            });
        builder.FinalMethod("println", "(I)V",
                            [](IntrinsicContext& context) {
                                GuestLine(context, std::to_string(context.arguments[0].AsInt()));
                                return VmValue::Void();
                            });
        builder.FinalMethod("println", "()V",
                            [](IntrinsicContext& context) {
                                GuestLine(context, "");
                                return VmValue::Void();
                            });
        builder.FinalMethod("print", "(Ljava/lang/String;)V",
                            [](IntrinsicContext& context) {
                                const auto argument = context.arguments[0].ref;
                                GuestLine(context, argument.IsValid()
                                                       ? Narrow(Value(context, argument))
                                                       : std::string("null"));
                                return VmValue::Void();
                            });
        auto result = std::move(builder).Build();
        return result;
    }
} // namespace ogplay::runtime::dexvm::intrinsics


// ---- migrated from java_io_Serializable.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
    using namespace detail;


} // namespace ogplay::runtime::dexvm::intrinsics


// ---- migrated from java_io_streams.cpp ----
#include "catalog.h"
#include "shared.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ogplay/runtime/dexvm/access_flags.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/io_runtime.h"
#include "ogplay/runtime/dexvm/reflection.h"
#include "ogplay/runtime/jni/jni_utf.h"

namespace ogplay::runtime::dexvm::intrinsics {
    namespace {
        class ReaderMonitor final {
        public:
            ReaderMonitor(Interpreter& vm, VmObjectRef lock)
                : vm_(vm), lock_(lock), token_(vm.CurrentContextToken()) {
                vm_.Monitors().Enter(lock_, token_);
            }
            ~ReaderMonitor() { vm_.Monitors().Exit(lock_, token_); }
        private:
            Interpreter& vm_;
            VmObjectRef lock_;
            std::uint64_t token_;
        };

        IntrinsicClassDecl DeclareInputStreamReader() {
            auto b = IntrinsicClassBuilder::Class("Ljava/io/InputStreamReader;", "Ljava/io/Reader;");
            const auto source = b.BoundInstanceField("source", "Ljava/io/InputStream;", kAccPrivate);
            const auto encoding = b.BoundInstanceField("encoding", "Ljava/lang/String;", kAccPrivate);
            const auto closed = b.BoundInstanceField("closed", "Z", kAccPrivate);
            const auto construct = [source, encoding](IntrinsicContext& c, std::string name) {
                if (!c.arguments[0].ref.IsValid()) throw VmJavaThrow{"Ljava/lang/NullPointerException;", "input == null"};
                const auto parent = c.vm.Linker().ResolveDescriptor("Ljava/io/Reader;");
                const auto ctor = c.vm.Linker().FindDirectMethod(parent, "<init>", "(Ljava/lang/Object;)V");
                const std::array args{VmValue::Ref(c.receiver), VmValue::Ref(c.arguments[0].ref)};
                const auto result = c.vm.Call(*ctor, args);
                if (result.exception.IsValid()) throw VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message, result.exception};
                IntrinsicCall(c).SetRef(source, c.arguments[0].ref);
                IntrinsicCall(c).SetRef(encoding, c.vm.NewStringUtf8(name));
                auto& decoder = c.vm.IO().Decoder(c.receiver);
                decoder.encoding = std::move(name);
                decoder.encoded.clear();
                decoder.pending.clear();
                decoder.ended = false;
                return VmValue::Void();
            };
            b.Constructor("(Ljava/io/InputStream;)V", [construct](IntrinsicContext& c) { return construct(c, "UTF-8"); });
            b.Constructor("(Ljava/io/InputStream;Ljava/nio/charset/Charset;)V", [construct](IntrinsicContext& c) { return construct(c, CharsetName(c.vm, c.arguments[1].ref)); });
            b.Constructor("(Ljava/io/InputStream;Ljava/lang/String;)V", [construct](IntrinsicContext& c) {
                if (!c.arguments[1].ref.IsValid()) throw VmJavaThrow{"Ljava/lang/NullPointerException;", "charset == null"};
                try { return construct(c, CanonicalCharset(c.vm.StringUtf8(c.arguments[1].ref))); }
                catch (const VmJavaThrow& e) { if (e.descriptor == "Ljava/nio/charset/UnsupportedCharsetException;" || e.descriptor == "Ljava/nio/charset/IllegalCharsetNameException;") throw VmJavaThrow{"Ljava/io/UnsupportedEncodingException;", e.message}; throw; }
            });
            const auto read = [source, closed](IntrinsicContext& c) -> std::int32_t {
                if (IntrinsicCall(c).GetInt(closed)) throw VmJavaThrow{"Ljava/io/IOException;", "reader is closed"};
                auto& state = c.vm.IO().Decoder(c.receiver);
                if (state.encoding.empty()) throw VmJavaThrow{"Ljava/io/IOException;", "reader is uninitialized"};
                while (state.pending.empty() && !state.ended) {
                    const auto value = detail::InvokeGuest(c.vm, IntrinsicCall(c).GetRef(source), "read", "()I").AsInt();
                    state.ended = value < 0;
                    if (!state.ended) state.encoded.push_back(static_cast<std::byte>(value));
                    std::size_t required = 1U;
                    if (state.encoding.starts_with("UTF-16")) required = 2U;
                    else if (state.encoding == "UTF-8" && !state.encoded.empty()) {
                        const auto lead = static_cast<std::uint8_t>(state.encoded.front());
                        required = lead < 0x80U ? 1U : (lead & 0xe0U) == 0xc0U ? 2U :
                                   (lead & 0xf0U) == 0xe0U ? 3U : (lead & 0xf8U) == 0xf0U ? 4U : 1U;
                    }
                    if (state.encoded.size() >= required || (state.ended && !state.encoded.empty())) {
                        const auto decoded = DecodeCharset(state.encoded, state.encoding);
                        state.encoded.clear();
                        for (const auto unit : decoded) state.pending.push_back(unit);
                    }
                }
                if (state.pending.empty()) return -1;
                const auto unit = state.pending.front(); state.pending.pop_front(); return unit;
            };
            b.OverrideMethod("read", "()I", [read,source](IntrinsicContext& c) {
                const ReaderMonitor lock(c.vm, IntrinsicCall(c).GetRef(source));
                return VmValue::Int(read(c));
            });
            b.OverrideMethod("read", "([CII)I", [read,source,closed](IntrinsicContext& c) {
                const ReaderMonitor lock(c.vm, IntrinsicCall(c).GetRef(source));
                if (IntrinsicCall(c).GetInt(closed)) throw VmJavaThrow{"Ljava/io/IOException;", "reader is closed"};
                const auto array = c.arguments[0].ref;
                if (!array.IsValid()) throw VmJavaThrow{"Ljava/lang/NullPointerException;", "buffer == null"};
                const auto offset = c.arguments[1].AsInt(), count = c.arguments[2].AsInt();
                if (offset < 0 || count < 0 || static_cast<std::int64_t>(offset) + count > c.vm.Model().ArrayLength(array)) throw VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;", "read range"};
                std::int32_t done = 0;
                while (done < count) {
                    if (done && c.vm.IO().Decoder(c.receiver).pending.empty() && detail::InvokeGuest(c.vm, IntrinsicCall(c).GetRef(source), "available", "()I").AsInt() == 0) break;
                    const auto unit = read(c); if (unit < 0) return VmValue::Int(done ? done : -1);
                    c.vm.Model().SetPrimitiveElement(array, offset + done++, static_cast<std::uint64_t>(unit));
                }
                return VmValue::Int(done);
            });
            b.OverrideMethod("ready", "()Z", [source, closed](IntrinsicContext& c) {
                const ReaderMonitor lock(c.vm, IntrinsicCall(c).GetRef(source));
                if (IntrinsicCall(c).GetInt(closed)) throw VmJavaThrow{"Ljava/io/IOException;", "reader is closed"};
                return VmValue::Int(!c.vm.IO().Decoder(c.receiver).pending.empty() || detail::InvokeGuest(c.vm, IntrinsicCall(c).GetRef(source), "available", "()I").AsInt() > 0);
            });
            b.OverrideMethod("close", "()V", [source, closed](IntrinsicContext& c) {
                const ReaderMonitor lock(c.vm, IntrinsicCall(c).GetRef(source));
                if (!IntrinsicCall(c).GetInt(closed)) {
                    detail::InvokeGuest(c.vm, IntrinsicCall(c).GetRef(source), "close", "()V");
                    c.vm.IO().Decoder(c.receiver) = {};
                    IntrinsicCall(c).SetInt(closed, 1);
                }
                return VmValue::Void();
            });
            b.VirtualMethod("getEncoding", "()Ljava/lang/String;", [encoding,closed](IntrinsicContext& c) { return VmValue::Ref(IntrinsicCall(c).GetInt(closed) ? VmObjectRef{} : IntrinsicCall(c).GetRef(encoding)); });
            return std::move(b).Build();
        }

        IntrinsicClassDecl DeclareOutputStreamWriter() {
            auto b = IntrinsicClassBuilder::Class(
                "Ljava/io/OutputStreamWriter;", "Ljava/io/Writer;");
            const auto target = b.BoundInstanceField(
                "target", "Ljava/io/OutputStream;", kAccPrivate);
            const auto encoding = b.BoundInstanceField(
                "encoding", "Ljava/lang/String;", kAccPrivate);
            const auto closed = b.BoundInstanceField("closed", "Z", kAccPrivate);
            const auto construct = [target, encoding](IntrinsicContext& c,
                                                       std::string name) {
                if (!c.arguments[0].ref.IsValid())
                    throw VmJavaThrow{"Ljava/lang/NullPointerException;", "output == null"};
                const auto parent = c.vm.Linker().ResolveDescriptor("Ljava/io/Writer;");
                const auto ctor = c.vm.Linker().FindDirectMethod(
                    parent, "<init>", "(Ljava/lang/Object;)V");
                const std::array args{VmValue::Ref(c.receiver), c.arguments[0]};
                const auto result = c.vm.Call(*ctor, args);
                if (result.exception.IsValid())
                    throw VmJavaThrow{
                        c.vm.Linker().Class(result.exception_class).descriptor,
                        result.exception_message, result.exception};
                IntrinsicCall(c).SetRef(target, c.arguments[0].ref);
                IntrinsicCall(c).SetRef(encoding, c.vm.NewStringUtf8(std::move(name)));
                return VmValue::Void();
            };
            b.Constructor("(Ljava/io/OutputStream;)V", [construct](IntrinsicContext& c) {
                return construct(c, "UTF-8");
            });
            b.Constructor("(Ljava/io/OutputStream;Ljava/nio/charset/Charset;)V",
                          [construct](IntrinsicContext& c) {
                              return construct(c, CharsetName(c.vm, c.arguments[1].ref));
                          });
            b.Constructor("(Ljava/io/OutputStream;Ljava/lang/String;)V",
                          [construct](IntrinsicContext& c) {
                              if (!c.arguments[1].ref.IsValid())
                                  throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                                                    "charset == null"};
                              try {
                                  return construct(c, CanonicalCharset(
                                      c.vm.StringUtf8(c.arguments[1].ref)));
                              } catch (const VmJavaThrow& e) {
                                  if (e.descriptor ==
                                          "Ljava/nio/charset/UnsupportedCharsetException;" ||
                                      e.descriptor ==
                                          "Ljava/nio/charset/IllegalCharsetNameException;")
                                      throw VmJavaThrow{
                                          "Ljava/io/UnsupportedEncodingException;", e.message};
                                  throw;
                              }
                          });
            const auto write = [target, encoding, closed](
                                   IntrinsicContext& c, std::u16string_view text) {
                if (IntrinsicCall(c).GetInt(closed))
                    throw VmJavaThrow{"Ljava/io/IOException;", "writer is closed"};
                const auto bytes = EncodeCharset(
                    text, c.vm.StringUtf8(IntrinsicCall(c).GetRef(encoding)));
                const auto array = c.vm.Model().NewPrimitiveArray(
                    c.vm.Linker().ResolveDescriptor("[B"), JniPrimitiveKind::byte,
                    static_cast<JniSize>(bytes.size()));
                if (!bytes.empty())
                    c.vm.Model().WriteByteRegion(array, 0, bytes);
                detail::InvokeGuest(
                    c.vm, IntrinsicCall(c).GetRef(target), "write", "([BII)V",
                    {VmValue::Ref(array), VmValue::Int(0),
                     VmValue::Int(static_cast<std::int32_t>(bytes.size()))});
                return VmValue::Void();
            };
            b.OverrideMethod("write", "([CII)V", [write](IntrinsicContext& c) {
                const auto array = c.arguments[0].ref;
                if (!array.IsValid())
                    throw VmJavaThrow{"Ljava/lang/NullPointerException;", "buffer == null"};
                const auto offset = c.arguments[1].AsInt();
                const auto count = c.arguments[2].AsInt();
                if (offset < 0 || count < 0 ||
                    static_cast<std::int64_t>(offset) + count >
                        c.vm.Model().ArrayLength(array))
                    throw VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;", "write range"};
                std::u16string text;
                text.reserve(static_cast<std::size_t>(count));
                for (std::int32_t i = 0; i < count; ++i)
                    text.push_back(static_cast<char16_t>(
                        c.vm.Model().GetPrimitiveElement(array, offset + i)));
                return write(c, text);
            });
            b.OverrideMethod("write", "(Ljava/lang/String;II)V",
                             [write](IntrinsicContext& c) {
                if (!c.arguments[0].ref.IsValid())
                    throw VmJavaThrow{"Ljava/lang/NullPointerException;", "string == null"};
                const auto& text = Value(c, c.arguments[0].ref);
                const auto offset = c.arguments[1].AsInt();
                const auto count = c.arguments[2].AsInt();
                if (offset < 0 || count < 0 ||
                    static_cast<std::int64_t>(offset) + count >
                        static_cast<std::int64_t>(text.size()))
                    throw VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;", "write range"};
                return write(c, std::u16string_view(text).substr(offset, count));
            });
            b.OverrideMethod("flush", "()V", [target, closed](IntrinsicContext& c) {
                if (IntrinsicCall(c).GetInt(closed))
                    throw VmJavaThrow{"Ljava/io/IOException;", "writer is closed"};
                detail::InvokeGuest(c.vm, IntrinsicCall(c).GetRef(target), "flush", "()V");
                return VmValue::Void();
            });
            b.OverrideMethod("close", "()V", [target, closed](IntrinsicContext& c) {
                if (!IntrinsicCall(c).GetInt(closed)) {
                    detail::InvokeGuest(c.vm, IntrinsicCall(c).GetRef(target), "close", "()V");
                    IntrinsicCall(c).SetInt(closed, 1);
                }
                return VmValue::Void();
            });
            b.VirtualMethod("getEncoding", "()Ljava/lang/String;",
                            [encoding, closed](IntrinsicContext& c) {
                return VmValue::Ref(IntrinsicCall(c).GetInt(closed)
                                        ? VmObjectRef{}
                                        : IntrinsicCall(c).GetRef(encoding));
            });
            return std::move(b).Build();
        }

    } // namespace

    IntrinsicClassDecl Declare_java_io_ObjectStreamClass() {
        auto b = IntrinsicClassBuilder::Class("Ljava/io/ObjectStreamClass;");
        b.StaticMethod("getConstructorId", "(Ljava/lang/Class;)J", [](IntrinsicContext& c) {
            const auto type = c.vm.Model().ClassOfClassObject(IntrinsicCall(c).NonNullRef(0, "class"));
            return VmValue::Long(c.vm.Reflection().SerializationConstructor(type));
        }, kAccPrivate | kAccNative);
        b.StaticMethod("newInstance", "(Ljava/lang/Class;J)Ljava/lang/Object;", [](IntrinsicContext& c) {
            const auto type = c.vm.Model().ClassOfClassObject(IntrinsicCall(c).NonNullRef(0, "class"));
            return VmValue::Ref(c.vm.Reflection().NewSerializationInstance(type, c.arguments[1].AsLong()));
        }, kAccPrivate | kAccNative);
        for (const auto& [name, signature] : std::array{
                 std::pair{"getConstructorSignature", "(Ljava/lang/reflect/Constructor;)Ljava/lang/String;"},
                 std::pair{"getMethodSignature", "(Ljava/lang/reflect/Method;)Ljava/lang/String;"},
                 std::pair{"getFieldSignature", "(Ljava/lang/reflect/Field;)Ljava/lang/String;"}}) {
            b.StaticMethod(name, signature, [](IntrinsicContext& c) {
                const auto descriptor = c.vm.Reflection().MemberDescriptor(IntrinsicCall(c).NonNullRef(0, "member"));
                return VmValue::Ref(c.vm.NewStringUtf8(descriptor));
            }, kAccNative);
        }
        b.StaticMethod("hasClinit", "(Ljava/lang/Class;)Z", [](IntrinsicContext& c) {
            const auto type = c.vm.Model().ClassOfClassObject(IntrinsicCall(c).NonNullRef(0, "class"));
            c.vm.Linker().EnsureClassLinked(type);
            const auto initialized = c.vm.EnsureClassInitialized(type); // JNI GetStaticMethodID initializes the class.
            if (initialized.exception.IsValid()) {
                return VmValue::Int(0); // AOSP clears lookup/init exceptions.
            }
            return VmValue::Int(c.vm.Linker().FindDirectMethod(type, "<clinit>", "()V").has_value());
        }, kAccPrivate | kAccNative);
        return std::move(b).Build();
    }

    IntrinsicClassDecl Declare_java_io_ObjectOutputStream() {
        auto b = IntrinsicClassBuilder::Class("Ljava/io/ObjectOutputStream;");
        b.UnimplementedStatic("getFieldL", "(Ljava/lang/Object;Ljava/lang/Class;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/Object;", kAccPrivate | kAccNative);
        return std::move(b).Build();
    }

    void AppendJavaIoStreams(std::vector<IntrinsicClassDecl>& catalog) {
        catalog.push_back(DeclareInputStreamReader());
        catalog.push_back(DeclareOutputStreamWriter());
        catalog.push_back(Declare_java_io_ObjectOutputStream());
        catalog.push_back(Declare_java_io_ObjectStreamClass());
    }
} // namespace ogplay::runtime::dexvm::intrinsics


// ---- migrated from java_io_UnsupportedEncodingException.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
    using namespace detail;

    IntrinsicClassDecl Declare_java_io_UnsupportedEncodingException() {
        return DeclareSimpleThrowable("Ljava/io/UnsupportedEncodingException;", "Ljava/io/IOException;");
    }
} // namespace ogplay::runtime::dexvm::intrinsics
