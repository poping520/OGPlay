#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_util_Pair(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/util/Pair;");
    builder.Super("Ljava/lang/Object;");
    builder.Field("first", "Ljava/lang/Object;", false);
    builder.Field("second", "Ljava/lang/Object;", false);
    builder.Virtual("<init>", "(Ljava/lang/Object;Ljava/lang/Object;)V", handlers.handler_android_pair_init);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
