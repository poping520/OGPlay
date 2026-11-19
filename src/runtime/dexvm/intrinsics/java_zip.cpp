// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from java_util_zip.cpp ----
#include <utility>
#include <vector>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

#include "catalog.h"

namespace ogplay::runtime::dexvm::intrinsics {
namespace {

IntrinsicClassDecl DeclareZipNatives(const char* owner) {
    auto builder = IntrinsicClassBuilder::Class(owner);
    builder.AdmitBootNativeMethods();
    return std::move(builder).Build();
}

}  // namespace

void AppendJavaUtilZip(std::vector<IntrinsicClassDecl>& catalog) {
    for (const auto owner : {"Ljava/util/zip/CRC32;", "Ljava/util/zip/Adler32;", "Ljava/util/zip/Deflater;",
                             "Ljava/util/zip/Inflater;", "Lorg/ogplay/zip/NativeZip;"})
        catalog.push_back(DeclareZipNatives(owner));
}

}  // namespace ogplay::runtime::dexvm::intrinsics
