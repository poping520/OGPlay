#include "catalog.h"

#include <utility>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {

IntrinsicClassDecl Declare_java_lang_reflect_GenericDeclaration() {
    auto builder = IntrinsicClassBuilder::Interface(
        "Ljava/lang/reflect/GenericDeclaration;",
        {"Ljava/lang/reflect/AnnotatedElement;"});
    builder.UnimplementedVirtual(
        "getTypeParameters", "()[Ljava/lang/reflect/TypeVariable;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::dexvm::intrinsics
