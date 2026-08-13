#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_util_Timer(const Context& context) {
    return DeclareAndroidClass(context, "Ljava/util/Timer;");
}

}  // namespace ogplay::runtime::android_intrinsics
