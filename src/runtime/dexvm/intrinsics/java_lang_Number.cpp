#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;

IntrinsicClassDecl Declare_java_lang_Number() {
    IntrinsicClassBuilder builder("Ljava/lang/Number;");
    builder.Super("Ljava/lang/Object;");
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics
