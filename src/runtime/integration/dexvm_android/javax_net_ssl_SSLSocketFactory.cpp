#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_javax_net_ssl_SSLSocketFactory(const Context& context) {
    return DeclareAndroidClass(context, "Ljavax/net/ssl/SSLSocketFactory;");
}

}  // namespace ogplay::runtime::android_intrinsics
