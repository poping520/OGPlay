#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_javax_net_ssl_SSLContext(const Context& context) {
    return DeclareAndroidClass(context, "Ljavax/net/ssl/SSLContext;");
}

}  // namespace ogplay::runtime::android_intrinsics
