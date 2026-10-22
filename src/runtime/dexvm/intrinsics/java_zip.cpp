// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from java_util_zip.cpp ----
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/access_flags.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/io_runtime.h"
#include "ogplay/runtime/dexvm/zip_runtime.h"

#include "catalog.h"
#include "shared.h"

namespace ogplay::runtime::dexvm::intrinsics {
namespace {

IntrinsicClassDecl DeclareZipNative(const char* owner) {
  auto builder = IntrinsicClassBuilder::Class(owner);
  builder.AdmitBootNativeMethods();
  return std::move(builder).Build();
}

[[noreturn]] void ZipFailure(const ZipRuntimeError &error) {
  throw VmJavaThrow{"Ljava/io/IOException;", error.what()};
}

struct ZipEntryDeclaration final {
  IntrinsicClassDecl declaration;
  IntrinsicFieldHandle name;
};

ZipEntryDeclaration DeclareZipEntry() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/util/zip/ZipEntry;", "Ljava/lang/Object;",
                                                {"Ljava/util/zip/ZipConstants;", "Ljava/lang/Cloneable;"});
    const auto name = builder.BoundInstanceField("name", "Ljava/lang/String;");
    builder.VirtualMethod("getName", "()Ljava/lang/String;", [name](IntrinsicContext& context) {
        return VmValue::Ref(IntrinsicCall(context).GetRef(name));
    });
    builder.VirtualMethod("isDirectory", "()Z", [name](IntrinsicContext& context) {
        IntrinsicCall call(context);
        const auto value = context.vm.StringUtf8(call.GetRef(name));
        return VmValue::Int(!value.empty() && value.back() == '/');
    });
    const auto wu_comment = builder.BoundInstanceField("comment", "Ljava/lang/String;", kAccNone);
    const auto wu_time = builder.BoundInstanceField("time", "I", kAccNone);
    const auto wu_size = builder.BoundInstanceField("size", "J", kAccNone);
    const auto wu_compressedSize = builder.BoundInstanceField("compressedSize", "J", kAccNone);
    const auto wu_crc = builder.BoundInstanceField("crc", "J", kAccNone);
    const auto wu_compressionMethod = builder.BoundInstanceField("compressionMethod", "I", kAccNone);
    const auto wu_modDate = builder.BoundInstanceField("modDate", "I", kAccNone);
    const auto wu_extra = builder.BoundInstanceField("extra", "[B", kAccNone);
    const auto wu_nameLength = builder.BoundInstanceField("nameLength", "I", kAccNone);
    const auto wu_localHeaderRelOffset = builder.BoundInstanceField("localHeaderRelOffset", "J", kAccNone);
    // AOSP 4.4 ROM line 307.
    builder.Constructor(
        "(Ljava/util/zip/ZipEntry;)V",
        [name, wu_comment, wu_time, wu_size, wu_compressedSize, wu_crc, wu_compressionMethod, wu_modDate, wu_extra,
         wu_nameLength, wu_localHeaderRelOffset](IntrinsicContext& c) {
            IntrinsicCall api(c);
            const auto source = c.arguments[0].ref;
            api.SetRef(name, api.GetRef(name, source));
            api.SetRef(wu_comment, api.GetRef(wu_comment, source));
            api.SetInt(wu_time, api.GetInt(wu_time, source));
            api.SetLong(wu_size, api.GetLong(wu_size, source));
            api.SetLong(wu_compressedSize, api.GetLong(wu_compressedSize, source));
            api.SetLong(wu_crc, api.GetLong(wu_crc, source));
            api.SetInt(wu_compressionMethod, api.GetInt(wu_compressionMethod, source));
            api.SetInt(wu_modDate, api.GetInt(wu_modDate, source));
            api.SetRef(wu_extra, api.GetRef(wu_extra, source));
            api.SetInt(wu_nameLength, api.GetInt(wu_nameLength, source));
            api.SetLong(wu_localHeaderRelOffset, api.GetLong(wu_localHeaderRelOffset, source));
            return VmValue::Void();
        },
        kAccPublic);

    // AOSP 4.4 ROM line 74.
    builder.Constructor(
        "(Ljava/lang/String;)V",
        [wu_crc, wu_compressedSize, wu_size, wu_compressionMethod, wu_time, wu_modDate, wu_nameLength,
         wu_localHeaderRelOffset, name](IntrinsicContext& c) {
            IntrinsicCall api(c);
            api.SetLong(wu_crc, -1);
            api.SetLong(wu_compressedSize, -1);
            api.SetLong(wu_size, -1);
            api.SetInt(wu_compressionMethod, -1);
            api.SetInt(wu_time, -1);
            api.SetInt(wu_modDate, -1);
            api.SetInt(wu_nameLength, -1);
            api.SetLong(wu_localHeaderRelOffset, -1);
            const auto value = api.NonNullRef(0, "name");
            if (c.vm.Model().StringValue(value).size() > 0xffffU)
                throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "Name too long"};
            api.SetRef(name, value);
            return VmValue::Void();
        },
        kAccPublic);

    return {std::move(builder).Build(), name};
}

// Explicit superclass calls avoid dispatching back into the ZIP override.
VmValue CallIoParent(IntrinsicContext& context, const char* owner,
                     const char* name, const char* signature,
                     std::vector<VmValue> arguments = {}) {
  auto& linker = context.vm.Linker();
  const auto type = linker.ResolveDescriptor(owner);
  const auto method = std::string_view(name) == "<init>"
      ? *linker.FindDirectMethod(type, name, signature)
      : linker.Class(type).vtable[*linker.FindVtableIndex(type, name, signature)];
  arguments.insert(arguments.begin(), VmValue::Ref(context.receiver));
  const auto result = context.vm.Call(method, arguments);
  if (result.exception.IsValid()) throw VmJavaThrow{
      linker.Class(result.exception_class).descriptor,
      result.exception_message, result.exception};
  return result.value;
}

IntrinsicClassDecl
DeclareZipInputStream(const IntrinsicFieldHandle entry_name) {
    auto builder = IntrinsicClassBuilder::Class("Ljava/util/zip/ZipInputStream;", "Ljava/util/zip/InflaterInputStream;",
                                                {"Ljava/util/zip/ZipConstants;"});
    const auto closed = builder.BoundInstanceField("closed", "Z", kAccPrivate);
    builder.Constructor("(Ljava/io/InputStream;)V", [](IntrinsicContext& context) {
        IntrinsicCall call(context);
        static_cast<void>(call.NonNullRef(0, "input"));
        const auto pushback = context.vm.NewIntrinsicInstance("Ljava/io/PushbackInputStream;");
        const auto pushback_root = context.vm.ProtectReferences(std::array{pushback});
        const auto inflater = context.vm.NewIntrinsicInstance("Ljava/util/zip/Inflater;");
        const auto inflater_root = context.vm.ProtectReferences(std::array{inflater});
        const auto construct = [&](VmObjectRef object, const char* signature, std::vector<VmValue> arguments) {
            const auto target =
                context.vm.Linker().FindDirectMethod(context.vm.Model().ObjectClass(object), "<init>", signature);
            arguments.insert(arguments.begin(), VmValue::Ref(object));
            const auto result = context.vm.Call(*target, arguments);
            if (result.exception.IsValid())
                throw VmJavaThrow{context.vm.Linker().Class(result.exception_class).descriptor,
                                  result.exception_message, result.exception};
        };
        construct(pushback, "(Ljava/io/InputStream;I)V", {VmValue::Ref(call.Ref(0)), VmValue::Int(512)});
        construct(inflater, "(Z)V", {VmValue::Int(1)});
        CallIoParent(context, "Ljava/util/zip/InflaterInputStream;", "<init>",
                     "(Ljava/io/InputStream;Ljava/util/zip/Inflater;)V",
                     {VmValue::Ref(pushback), VmValue::Ref(inflater)});
        std::vector<std::byte> bytes;
        try {
            while (true) {
                const auto value = detail::InvokeGuest(context.vm, call.Ref(0), "read", "()I").AsInt();
                if (value < 0) break;
                bytes.push_back(static_cast<std::byte>(value));
            }
            context.vm.ZIP().Open(context.receiver, std::move(bytes));
        } catch (const IoRuntimeError& error) {
            throw VmJavaThrow{"Ljava/io/IOException;", error.what()};
        } catch (const ZipRuntimeError& error) {
            ZipFailure(error);
        }
        return VmValue::Void();
    });
    builder.VirtualMethod("getNextEntry", "()Ljava/util/zip/ZipEntry;", [entry_name](IntrinsicContext& context) {
        detail::InvokeGuest(context.vm, context.receiver, "closeEntry", "()V");
        std::optional<std::string> name;
        try {
            name = context.vm.ZIP().NextEntry(context.receiver);
        } catch (const ZipRuntimeError& error) {
            ZipFailure(error);
        }
        if (!name.has_value()) return VmValue::Ref(VmObjectRef{});
        const auto entry = context.vm.NewIntrinsicInstance("Ljava/util/zip/ZipEntry;");
        IntrinsicCall(context).SetRef(entry_name, entry, context.vm.NewStringUtf8(*name));
        return VmValue::Ref(entry);
    });
    builder.FinalOverrideMethod("read", "()I", [](IntrinsicContext& context) {
        try {
            const auto bytes = context.vm.ZIP().Read(context.receiver, 1);
            return VmValue::Int(bytes ? static_cast<std::uint8_t>((*bytes)[0]) : -1);
        } catch (const ZipRuntimeError& error) {
            ZipFailure(error);
        }
    });
    builder.FinalOverrideMethod("skip", "(J)J", [](IntrinsicContext& context) {
        if (context.arguments[0].AsLong() < 0)
            throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "byteCount < 0"};
        return CallIoParent(context, "Ljava/io/InputStream;", "skip", "(J)J", {context.arguments[0]});
    });
    builder.OverrideMethod("available", "()I", [](IntrinsicContext& context) {
        try {
            return VmValue::Int(context.vm.ZIP().Available(context.receiver) ? 1 : 0);
        } catch (const ZipRuntimeError& error) {
            ZipFailure(error);
        }
    });
    builder.FinalOverrideMethod("markSupported", "()Z", [](IntrinsicContext&) { return VmValue::Int(0); });
    builder.FinalOverrideMethod("mark", "(I)V", [](IntrinsicContext&) { return VmValue::Void(); });
    builder.FinalOverrideMethod("reset", "()V", [](IntrinsicContext&) -> VmValue {
        throw VmJavaThrow{"Ljava/io/IOException;", "mark/reset not supported"};
    });
    builder.OverrideMethod("read", "([BII)I", [](IntrinsicContext& context) {
        IntrinsicCall call(context);
        const auto array = call.Ref(0);
        const auto offset = call.Int(1);
        const auto length = call.Int(2);
        if (offset < 0 || length < 0 ||
            static_cast<std::int64_t>(offset) + length > context.vm.Model().ArrayLength(array)) {
            throw VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;", "zip read range exceeds the array"};
        }
        if (length == 0) return VmValue::Int(0);
        std::optional<std::vector<std::byte>> bytes;
        try {
            bytes = context.vm.ZIP().Read(context.receiver, static_cast<std::size_t>(length));
        } catch (const ZipRuntimeError& error) {
            ZipFailure(error);
        }
        if (!bytes.has_value()) return VmValue::Int(-1);
        context.vm.Model().WriteByteRegion(array, offset, *bytes);
        return VmValue::Int(static_cast<std::int32_t>(bytes->size()));
    });
    builder.VirtualMethod("closeEntry", "()V", [](IntrinsicContext& context) {
        try {
            context.vm.ZIP().CloseEntry(context.receiver);
        } catch (const ZipRuntimeError& error) {
            ZipFailure(error);
        }
        return VmValue::Void();
    });
    builder.OverrideMethod("close", "()V", [closed](IntrinsicContext& context) {
        IntrinsicCall call(context);
        if (!call.GetInt(closed)) {
            detail::InvokeGuest(context.vm, context.receiver, "closeEntry", "()V");
            CallIoParent(context, "Ljava/util/zip/InflaterInputStream;", "close", "()V");
            context.vm.ZIP().Close(context.receiver);
            call.SetInt(closed, 1);
        }
        return VmValue::Void();
    });
    return std::move(builder).Build();
}

} // namespace

void AppendJavaUtilZip(std::vector<IntrinsicClassDecl> &catalog) {
  for (const auto owner : {"Ljava/util/zip/CRC32;", "Ljava/util/zip/Deflater;",
                           "Lorg/ogplay/zip/NativeZip;"})
    catalog.push_back(DeclareZipNative(owner));
  auto inflater = IntrinsicClassBuilder::Class("Ljava/util/zip/Inflater;");
  inflater.AdmitBootNativeMethods();
  inflater.DirectMethod("setFileInputImpl", "(Ljava/io/FileDescriptor;JIJ)I",
      [](IntrinsicContext& context) -> VmValue {
        if (auto* ledger = context.vm.Ledger())
          ledger->RecordUnimplemented("dexvm.zip.file_input", 0);
        throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                          "Inflater file descriptor input is not supported"};
      }, kAccPrivate | kAccNative);
  catalog.push_back(std::move(inflater).Build());
  auto entry = DeclareZipEntry();
  catalog.push_back(std::move(entry.declaration));
  catalog.push_back(DeclareZipInputStream(entry.name));
}

} // namespace ogplay::runtime::dexvm::intrinsics
