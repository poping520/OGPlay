#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_net_URLEncoder(const Context& context) {
    return DeclareAndroidClass(context, "Ljava/net/URLEncoder;");
}

}  // namespace ogplay::runtime::android_intrinsics
