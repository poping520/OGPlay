#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_SharedPreferencesEditorImpl(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/content/SharedPreferencesEditorImpl;");
}

}  // namespace ogplay::runtime::android_intrinsics
