#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_util_Pair(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/util/Pair;");
}

}  // namespace ogplay::runtime::android_intrinsics
