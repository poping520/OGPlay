#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_javax_net_ssl_HttpsURLConnection(const Context& context) {
    return DeclareAndroidClass(context, "Ljavax/net/ssl/HttpsURLConnection;");
}

}  // namespace ogplay::runtime::android_intrinsics
