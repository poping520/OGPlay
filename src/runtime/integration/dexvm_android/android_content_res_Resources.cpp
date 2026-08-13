#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_res_Resources(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/content/res/Resources;");
}

}  // namespace ogplay::runtime::android_intrinsics
