#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_SharedPreferences(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/content/SharedPreferences;");
    builder.FinalMethod("edit", "()Landroid/content/SharedPreferences$Editor;", PrefsEditHandler(context));
    builder.FinalMethod("getBoolean", "(Ljava/lang/String;Z)Z", PrefsGetBooleanHandler(context));
    builder.FinalMethod("getInt", "(Ljava/lang/String;I)I", PrefsGetIntHandler(context));
    builder.FinalMethod("getLong", "(Ljava/lang/String;J)J", PrefsGetLongHandler(context));
    builder.FinalMethod("getString", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", PrefsGetStringHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
