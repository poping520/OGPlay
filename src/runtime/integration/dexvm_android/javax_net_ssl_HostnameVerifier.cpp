#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_javax_net_ssl_HostnameVerifier(const Context& context) {
    return DeclareAndroidClass(context, "Ljavax/net/ssl/HostnameVerifier;");
}

}  // namespace ogplay::runtime::android_intrinsics
