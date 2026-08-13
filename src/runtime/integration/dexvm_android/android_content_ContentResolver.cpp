#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_ContentResolver(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/content/ContentResolver;");
}

}  // namespace ogplay::runtime::android_intrinsics
