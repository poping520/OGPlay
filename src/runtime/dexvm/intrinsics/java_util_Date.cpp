#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;

IntrinsicClassDecl Declare_java_util_Date() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/util/Date;", "Ljava/lang/Object;");
    builder.InstanceField("millis", "J");
    builder.UnimplementedConstructor("()V");
    builder.UnimplementedFinal("getTime", "()J");
    builder.UnimplementedFinal("getYear", "()I");
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics
