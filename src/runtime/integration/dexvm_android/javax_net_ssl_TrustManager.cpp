#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_javax_net_ssl_TrustManager(const Context& context) {
    return DeclareAndroidClass(context, "Ljavax/net/ssl/TrustManager;");
}

}  // namespace ogplay::runtime::android_intrinsics
