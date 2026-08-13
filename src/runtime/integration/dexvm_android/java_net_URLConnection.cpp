#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_net_URLConnection(const Context& context) {
    return DeclareAndroidClass(context, "Ljava/net/URLConnection;");
}

}  // namespace ogplay::runtime::android_intrinsics
