#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_IntentFilter(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/content/IntentFilter;");
}

}  // namespace ogplay::runtime::android_intrinsics
