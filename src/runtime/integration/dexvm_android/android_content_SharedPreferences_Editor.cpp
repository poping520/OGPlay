#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_SharedPreferences_Editor(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/content/SharedPreferences$Editor;");
    builder.MarkInterface();
    builder.Virtual("putBoolean", "(Ljava/lang/String;Z)Landroid/content/SharedPreferences$Editor;", handlers.handler_android_prefs_editor_put_boolean);
    builder.Virtual("putInt", "(Ljava/lang/String;I)Landroid/content/SharedPreferences$Editor;", handlers.handler_android_prefs_editor_put_int);
    builder.Virtual("putLong", "(Ljava/lang/String;J)Landroid/content/SharedPreferences$Editor;", handlers.handler_android_prefs_editor_put_long);
    builder.Virtual("putString", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;", handlers.handler_android_prefs_editor_put_string);
    builder.Virtual("commit", "()Z", handlers.handler_android_prefs_editor_commit);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
