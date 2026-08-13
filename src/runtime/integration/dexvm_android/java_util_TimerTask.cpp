#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_util_TimerTask(const Context& context) {
    return DeclareAndroidClass(context, "Ljava/util/TimerTask;");
}

}  // namespace ogplay::runtime::android_intrinsics
