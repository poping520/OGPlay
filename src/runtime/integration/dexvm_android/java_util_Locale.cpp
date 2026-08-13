#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_util_Locale(const Context& context) {
    return DeclareAndroidClass(context, "Ljava/util/Locale;");
}

}  // namespace ogplay::runtime::android_intrinsics
