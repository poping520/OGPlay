#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_net_HttpURLConnection(const Context& context) {
    return DeclareAndroidClass(context, "Ljava/net/HttpURLConnection;");
}

}  // namespace ogplay::runtime::android_intrinsics
