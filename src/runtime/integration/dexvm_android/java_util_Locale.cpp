#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_util_Locale(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Ljava/util/Locale;", "Ljava/lang/Object;");
    builder.StaticMethod("getDefault", "()Ljava/util/Locale;", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(Singleton(call, context, "locale", "Ljava/util/Locale;"));
    });
    builder.FinalMethod("getISO3Language", "()Ljava/lang/String;", [context](dx::IntrinsicContext& call) {
        return MakeString(call, context->iso3_language);
    });
    builder.FinalMethod("getISO3Country", "()Ljava/lang/String;", [context](dx::IntrinsicContext& call) {
        return MakeString(call, context->iso3_country);
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
