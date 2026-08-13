#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_net_URL(const Context& context) {
    return DeclareAndroidClass(context, "Ljava/net/URL;");
}

}  // namespace ogplay::runtime::android_intrinsics
