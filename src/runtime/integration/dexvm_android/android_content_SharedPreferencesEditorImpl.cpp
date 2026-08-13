#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_SharedPreferencesEditorImpl(const Context& context) {
    dx::IntrinsicClassBuilder builder("Landroid/content/SharedPreferencesEditorImpl;");
    builder.Super("Ljava/lang/Object;");
    builder.Implements("Landroid/content/SharedPreferences$Editor;");
    builder.Virtual("putBoolean", "(Ljava/lang/String;Z)Landroid/content/SharedPreferences$Editor;", PrefsEditorPutBooleanHandler(context));
    builder.Virtual("putInt", "(Ljava/lang/String;I)Landroid/content/SharedPreferences$Editor;", PrefsEditorPutIntHandler(context));
    builder.Virtual("putLong", "(Ljava/lang/String;J)Landroid/content/SharedPreferences$Editor;", PrefsEditorPutLongHandler(context));
    builder.Virtual("putString", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;", PrefsEditorPutStringHandler(context));
    builder.Virtual("commit", "()Z", PrefsEditorCommitHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
