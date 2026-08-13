#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_SharedPreferences(const Context& context) {
    dx::IntrinsicClassBuilder builder("Landroid/content/SharedPreferences;");
    builder.MarkInterface();
    builder.Virtual("edit", "()Landroid/content/SharedPreferences$Editor;", PrefsEditHandler(context));
    builder.Virtual("getBoolean", "(Ljava/lang/String;Z)Z", PrefsGetBooleanHandler(context));
    builder.Virtual("getInt", "(Ljava/lang/String;I)I", PrefsGetIntHandler(context));
    builder.Virtual("getLong", "(Ljava/lang/String;J)J", PrefsGetLongHandler(context));
    builder.Virtual("getString", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", PrefsGetStringHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
