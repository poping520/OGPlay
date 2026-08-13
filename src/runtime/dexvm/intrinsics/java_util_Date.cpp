#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;

IntrinsicClassDecl Declare_java_util_Date() {
    IntrinsicClassBuilder builder("Ljava/util/Date;");
    builder.Super("Ljava/lang/Object;");
    builder.Field("millis", "J", false);
    builder.Virtual("<init>", "()V",
        {});
    builder.Virtual("getTime", "()J",
        {});
    builder.Virtual("getYear", "()I",
        {});
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics
