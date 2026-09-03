// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from java_util_zip.cpp ----
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/io_runtime.h"
#include "ogplay/runtime/dexvm/zip_runtime.h"

#include "catalog.h"

namespace ogplay::runtime::dexvm::intrinsics {
namespace {

[[noreturn]] void ZipFailure(const ZipRuntimeError &error) {
  throw VmJavaThrow{"Ljava/io/IOException;", error.what()};
}

struct ZipEntryDeclaration final {
  IntrinsicClassDecl declaration;
  IntrinsicFieldHandle name;
};

ZipEntryDeclaration DeclareZipEntry() {
  auto builder = IntrinsicClassBuilder::Class("Ljava/util/zip/ZipEntry;");
  const auto name =
      builder.BoundInstanceField("name", "Ljava/lang/String;");
  builder.FinalMethod("getName", "()Ljava/lang/String;",
                      [name](IntrinsicContext &context) {
                        return VmValue::Ref(IntrinsicCall(context).GetRef(name));
                      });
  builder.FinalMethod("isDirectory", "()Z",
                      [name](IntrinsicContext &context) {
                        IntrinsicCall call(context);
                        const auto value = context.vm.StringUtf8(call.GetRef(name));
                        return VmValue::Int(!value.empty() && value.back() == '/');
                      });
  return {std::move(builder).Build(), name};
}

IntrinsicClassDecl
DeclareZipInputStream(const IntrinsicFieldHandle entry_name) {
  auto builder = IntrinsicClassBuilder::Class(
      "Ljava/util/zip/ZipInputStream;", "Ljava/io/FilterInputStream;");
  builder.Constructor("(Ljava/io/InputStream;)V", [](IntrinsicContext &context) {
    IntrinsicCall call(context);
    std::vector<std::byte> bytes;
    try {
      bytes = context.vm.IO().TakeRemainingInput(call.Ref(0));
      context.vm.ZIP().Open(context.receiver, std::move(bytes));
    } catch (const IoRuntimeError &error) {
      throw VmJavaThrow{"Ljava/io/IOException;", error.what()};
    } catch (const ZipRuntimeError &error) {
      ZipFailure(error);
    }
    return VmValue::Void();
  });
  builder.FinalMethod(
      "getNextEntry", "()Ljava/util/zip/ZipEntry;",
      [entry_name](IntrinsicContext &context) {
        std::optional<std::string> name;
        try {
          name = context.vm.ZIP().NextEntry(context.receiver);
        } catch (const ZipRuntimeError &error) {
          ZipFailure(error);
        }
        if (!name.has_value()) return VmValue::Ref(VmObjectRef{});
        const auto entry =
            context.vm.NewIntrinsicInstance("Ljava/util/zip/ZipEntry;");
        IntrinsicCall(context).SetRef(
            entry_name, entry, context.vm.NewStringUtf8(*name));
        return VmValue::Ref(entry);
      });
  builder.FinalOverrideMethod("read", "([BII)I", [](IntrinsicContext &context) {
    IntrinsicCall call(context);
    const auto array = call.Ref(0);
    const auto offset = call.Int(1);
    const auto length = call.Int(2);
    if (offset < 0 || length < 0 ||
        static_cast<std::int64_t>(offset) + length >
            context.vm.Model().ArrayLength(array)) {
      throw VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;",
                        "zip read range exceeds the array"};
    }
    if (length == 0) return VmValue::Int(0);
    std::optional<std::vector<std::byte>> bytes;
    try {
      bytes = context.vm.ZIP().Read(context.receiver,
                                    static_cast<std::size_t>(length));
    } catch (const ZipRuntimeError &error) {
      ZipFailure(error);
    }
    if (!bytes.has_value()) return VmValue::Int(-1);
    context.vm.Model().WriteByteRegion(array, offset, *bytes);
    return VmValue::Int(static_cast<std::int32_t>(bytes->size()));
  });
  builder.FinalMethod("closeEntry", "()V", [](IntrinsicContext &context) {
    try {
      context.vm.ZIP().CloseEntry(context.receiver);
    } catch (const ZipRuntimeError &error) {
      ZipFailure(error);
    }
    return VmValue::Void();
  });
  builder.FinalOverrideMethod("close", "()V", [](IntrinsicContext &context) {
    context.vm.ZIP().Close(context.receiver);
    return VmValue::Void();
  });
  return std::move(builder).Build();
}

} // namespace

void AppendJavaUtilZip(std::vector<IntrinsicClassDecl> &catalog) {
  auto entry = DeclareZipEntry();
  catalog.push_back(std::move(entry.declaration));
  catalog.push_back(DeclareZipInputStream(entry.name));
}

} // namespace ogplay::runtime::dexvm::intrinsics
