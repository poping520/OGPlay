#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_BroadcastReceiver(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/content/BroadcastReceiver;");
}

}  // namespace ogplay::runtime::android_intrinsics
