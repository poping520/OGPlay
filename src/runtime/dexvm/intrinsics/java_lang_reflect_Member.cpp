#include "catalog.h"

#include <utility>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {

IntrinsicClassDecl Declare_java_lang_reflect_Member() {
    auto builder = IntrinsicClassBuilder::Interface(
        "Ljava/lang/reflect/Member;");
    builder.UnimplementedVirtual("getDeclaringClass", "()Ljava/lang/Class;");
    builder.UnimplementedVirtual("getName", "()Ljava/lang/String;");
    builder.UnimplementedVirtual("getModifiers", "()I");
    builder.UnimplementedVirtual("isSynthetic", "()Z");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::dexvm::intrinsics
