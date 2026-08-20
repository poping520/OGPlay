#include "catalog.h"

#include <utility>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {

IntrinsicClassDecl Declare_java_lang_reflect_Type() {
    return std::move(IntrinsicClassBuilder::Interface(
                         "Ljava/lang/reflect/Type;"))
        .Build();
}

}  // namespace ogplay::runtime::dexvm::intrinsics
