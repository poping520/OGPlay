#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_javax_net_ssl_KeyManager(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Ljavax/net/ssl/KeyManager;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
