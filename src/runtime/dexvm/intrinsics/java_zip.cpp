// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from java_util_zip.cpp ----
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/access_flags.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/io_runtime.h"

#include "catalog.h"
#include "shared.h"

namespace ogplay::runtime::dexvm::intrinsics {
namespace {

IntrinsicClassDecl DeclareZipNative(const char* owner) {
    auto builder = IntrinsicClassBuilder::Class(owner);
    builder.AdmitBootNativeMethods();
    return std::move(builder).Build();
}

}  // namespace

void AppendJavaUtilZip(std::vector<IntrinsicClassDecl>& catalog) {
    for (const auto owner : {"Ljava/util/zip/CRC32;", "Ljava/util/zip/Deflater;", "Lorg/ogplay/zip/NativeZip;"})
        catalog.push_back(DeclareZipNative(owner));
    auto inflater = IntrinsicClassBuilder::Class("Ljava/util/zip/Inflater;");
    inflater.AdmitBootNativeMethods();
    inflater.DirectMethod(
        "setFileInputImpl", "(Ljava/io/FileDescriptor;JIJ)I",
        [](IntrinsicContext& context) -> VmValue {
            IntrinsicCall call(context);
            const auto fd = call.NonNullRef(0, "fd");
            const auto offset = call.Long(1);
            const auto count = call.Int(2);
            if (count < 0) throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "byteCount < 0"};
            // The byte-array snapshot uses the same jint and VM heap bounds as setInputImpl.
            const auto array = context.vm.Model().NewPrimitiveArray(context.vm.Linker().ResolveDescriptor("[B"),
                                                                    JniPrimitiveKind::byte, count);
            const auto root = context.vm.ProtectReferences(std::array{array});
            std::size_t total = 0;
            try {
                const auto field = *context.vm.Linker().FindFieldRecursive(
                    context.vm.Linker().ResolveDescriptor("Ljava/io/FileDescriptor;"), "descriptor", "I");
                const auto handle = static_cast<std::int32_t>(
                    context.vm.Model().InstanceSlots(fd)[context.vm.Linker().Field(field).slot].bits);
                const auto& descriptor = context.vm.IO().ResolveDescriptor(fd, handle);
                if (descriptor.kind != IoRuntime::DescriptorKind::vfs_path)
                    throw IoRuntimeError("not a file descriptor", 9);
                if (offset < 0) throw IoRuntimeError("negative seek", 22);
                // Follow Posix read/lseek's synchronous IoRuntime convention.
                context.vm.IO().SetFileOffset(fd, static_cast<std::uint64_t>(offset));
                std::vector<std::byte> bytes(static_cast<std::size_t>(count));
                while (total < bytes.size()) {
                    const auto amount = context.vm.IO().ReadFileStream(fd, std::span(bytes).subspan(total));
                    if (amount == 0) break;
                    total += amount;
                }
                context.vm.Model().WriteByteRegion(array, 0, std::span(bytes).first(total));
            } catch (const IoRuntimeError& error) {
                throw VmJavaThrow{"Ljava/io/IOException;",
                                  std::string(error.what()) + " (errno=" + std::to_string(error.ErrorNumber()) + ")"};
            }
            const auto method = *context.vm.Linker().FindDirectMethod(
                context.vm.Linker().ResolveDescriptor("Ljava/util/zip/Inflater;"), "setInputImpl", "([BIIJ)V");
            const auto result = context.vm.Call(
                method, std::array{VmValue::Ref(context.receiver), VmValue::Ref(array), VmValue::Int(0),
                                   VmValue::Int(static_cast<std::int32_t>(total)), VmValue::Long(call.Long(3))});
            if (result.exception.IsValid())
                throw VmJavaThrow{context.vm.Linker().Class(result.exception_class).descriptor,
                                  result.exception_message, result.exception};
            return VmValue::Int(static_cast<std::int32_t>(total));
        },
        kAccPrivate | kAccNative);
    catalog.push_back(std::move(inflater).Build());
}

}  // namespace ogplay::runtime::dexvm::intrinsics
