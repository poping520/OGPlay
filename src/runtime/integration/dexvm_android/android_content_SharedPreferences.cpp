#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_SharedPreferences(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/content/SharedPreferences;");
    builder.MarkInterface();
    builder.Virtual("edit", "()Landroid/content/SharedPreferences$Editor;", handlers.handler_android_prefs_edit);
    builder.Virtual("getBoolean", "(Ljava/lang/String;Z)Z", handlers.handler_android_prefs_get_boolean);
    builder.Virtual("getInt", "(Ljava/lang/String;I)I", handlers.handler_android_prefs_get_int);
    builder.Virtual("getLong", "(Ljava/lang/String;J)J", handlers.handler_android_prefs_get_long);
    builder.Virtual("getString", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", handlers.handler_android_prefs_get_string);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
