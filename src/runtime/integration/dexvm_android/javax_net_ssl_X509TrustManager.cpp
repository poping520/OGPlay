#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_javax_net_ssl_X509TrustManager(const Context& context) {
    return DeclareAndroidClass(context, "Ljavax/net/ssl/X509TrustManager;");
}

}  // namespace ogplay::runtime::android_intrinsics
