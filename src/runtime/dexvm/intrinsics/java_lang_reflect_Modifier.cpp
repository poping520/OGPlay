#include "catalog.h"

#include <utility>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {

IntrinsicClassDecl Declare_java_lang_reflect_Modifier() {
    return std::move(IntrinsicClassBuilder::Class(
                         "Ljava/lang/reflect/Modifier;",
                         "Ljava/lang/Object;"))
        .Build();
}

}  // namespace ogplay::runtime::dexvm::intrinsics
