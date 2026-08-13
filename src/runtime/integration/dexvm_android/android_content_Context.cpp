#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_Context(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/content/Context;");
}

}  // namespace ogplay::runtime::android_intrinsics
