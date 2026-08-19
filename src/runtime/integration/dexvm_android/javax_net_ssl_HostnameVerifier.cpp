#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_javax_net_ssl_HostnameVerifier(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Ljavax/net/ssl/HostnameVerifier;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
