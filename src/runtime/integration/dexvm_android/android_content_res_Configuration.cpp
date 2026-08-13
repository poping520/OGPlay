#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_res_Configuration(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/content/res/Configuration;");
}

}  // namespace ogplay::runtime::android_intrinsics
