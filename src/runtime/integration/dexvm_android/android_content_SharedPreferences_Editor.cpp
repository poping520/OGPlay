#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_SharedPreferences_Editor(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/content/SharedPreferences$Editor;");
}

}  // namespace ogplay::runtime::android_intrinsics
