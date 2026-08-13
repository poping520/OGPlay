#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_javax_net_ssl_TrustManager(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Ljavax/net/ssl/TrustManager;");
    builder.MarkInterface();
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
