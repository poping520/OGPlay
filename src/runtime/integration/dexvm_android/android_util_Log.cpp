#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_util_Log(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/util/Log;");
}

}  // namespace ogplay::runtime::android_intrinsics
