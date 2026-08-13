#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_util_Locale(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Ljava/util/Locale;");
    builder.Super("Ljava/lang/Object;");
    builder.Static("getDefault", "()Ljava/util/Locale;", handlers.handler_android_locale_get_default);
    builder.Virtual("getISO3Language", "()Ljava/lang/String;", handlers.handler_android_locale_get_iso3_language);
    builder.Virtual("getISO3Country", "()Ljava/lang/String;", handlers.handler_android_locale_get_iso3_country);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
