#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_javax_net_ssl_KeyManager(const Context& context) {
    return DeclareAndroidClass(context, "Ljavax/net/ssl/KeyManager;");
}

}  // namespace ogplay::runtime::android_intrinsics
