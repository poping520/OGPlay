#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_SharedPreferencesEditorImpl(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/content/SharedPreferencesEditorImpl;", "Ljava/lang/Object;", {"Landroid/content/SharedPreferences$Editor;"});
    builder.FinalMethod("putBoolean", "(Ljava/lang/String;Z)Landroid/content/SharedPreferences$Editor;", PrefsEditorPutBooleanHandler(context));
    builder.FinalMethod("putInt", "(Ljava/lang/String;I)Landroid/content/SharedPreferences$Editor;", PrefsEditorPutIntHandler(context));
    builder.FinalMethod("putLong", "(Ljava/lang/String;J)Landroid/content/SharedPreferences$Editor;", PrefsEditorPutLongHandler(context));
    builder.FinalMethod("putString", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;", PrefsEditorPutStringHandler(context));
    builder.FinalMethod("commit", "()Z", PrefsEditorCommitHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
