#include "catalog.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
IntrinsicClassDecl Declare_java_util_regex_Pattern_Natives() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/util/regex/Pattern;");
    builder.AdmitBootNativeMethods();
    return std::move(builder).Build();
}

IntrinsicClassDecl Declare_java_util_regex_Matcher_Natives() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/util/regex/Matcher;");
    builder.AdmitBootNativeMethods();
    return std::move(builder).Build();
}
}  // namespace ogplay::runtime::dexvm::intrinsics
