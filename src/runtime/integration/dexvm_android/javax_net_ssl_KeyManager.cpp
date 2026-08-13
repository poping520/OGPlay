#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_javax_net_ssl_KeyManager(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Ljavax/net/ssl/KeyManager;");
    builder.MarkInterface();
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
