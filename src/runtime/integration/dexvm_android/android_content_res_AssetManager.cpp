#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_res_AssetManager(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/content/res/AssetManager;");
}

}  // namespace ogplay::runtime::android_intrinsics
